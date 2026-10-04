"""Author the boss/reward foundation using a current Editor target; no game packaging.

New assets are seeded once. Reruns preserve designer-edited profiles and tables.
Only tagged foundation actors and the two original decorative chest actors change.
"""
import json
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/ZombieBoss'
BOSS = '/Game/Gameplay/Boss'
ROGUE = '/Game/Gameplay/Roguelike'
MAP = '/Game/Maps/L_Mouth'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
level = u.get_editor_subsystem(u.LevelEditorSubsystem)
report = {'assets': [], 'zones': []}

def save(obj):
    assert lib.save_loaded_asset(obj, only_if_is_dirty=False), obj.get_path_name()
    report['assets'].append(obj.get_path_name())
    return obj

def create(name, directory, cls, factory):
    existing = lib.load_asset(directory + '/' + name)
    return existing or assets.create_asset(name, directory, cls, factory)

table = lib.load_asset(ROGUE + '/DT_Perks')
if not table:
    factory = u.DataTableFactory()
    factory.set_editor_property('struct', u.load_object(None, '/Script/MessControl.MCPerkDefinition'))
    table = assets.create_asset('DT_Perks', ROGUE, u.DataTable, factory)
    rows = json.loads((ROOT / 'Tools/Unreal/roguelike_perks.json').read_text(encoding='utf-8-sig'))
    for row in rows:
        row.update(EffectClass='None', Icon='None')
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)), 'Perk table import failed'
    save(table)
assert len(u.DataTableFunctionLibrary.get_data_table_row_names(table)) >= 6

# Legacy FBX makes skeleton ownership explicit and avoids sharing the player's reference pose.
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
mesh = lib.load_asset(BOSS + '/Zombie/SK_ZombieBoss')
if not mesh or not mesh.get_editor_property('skeleton'):
    task = u.AssetImportTask()
    task.filename = str(SOURCE / 'SK_ZombieBoss.fbx')
    task.destination_path = BOSS + '/Zombie'
    task.destination_name = 'SK_ZombieBoss'
    task.automated = task.save = True
    task.replace_existing = task.replace_existing_settings = True
    task.factory = u.FbxFactory()
    options = u.FbxImportUI()
    options.import_mesh = options.import_as_skeletal = True
    options.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_materials = options.import_textures = options.import_animations = False
    options.create_physics_asset = False
    options.skeletal_mesh_import_data.set_editor_property('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task.options = options
    assets.import_asset_tasks([task])
    mesh = lib.load_asset(BOSS + '/Zombie/SK_ZombieBoss')
assert isinstance(mesh, u.SkeletalMesh)
assert str(mesh.get_editor_property('skeleton').get_path_name()).startswith(BOSS + '/Zombie/'), 'Zombie must own its skeleton'
save(mesh.get_editor_property('skeleton'))

texture_map = {}
source_report = json.loads((SOURCE / 'ZombieBossReport.json').read_text(encoding='utf8'))
for entry in source_report['textures']:
    path = BOSS + '/Zombie/Textures/' + Path(entry['file']).stem
    texture = lib.load_asset(path)
    if not texture:
        task = u.AssetImportTask()
        task.filename = str(SOURCE / entry['file'])
        task.destination_path = BOSS + '/Zombie/Textures'
        task.destination_name = Path(entry['file']).stem
        task.automated = task.save = True
        assets.import_asset_tasks([task])
        texture = lib.load_asset(path)
        kind = entry['kind']
        texture.set_editor_property('srgb', kind == 'BaseColor')
        texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP if kind == 'Normal' else u.TextureCompressionSettings.TC_MASKS if kind != 'BaseColor' else u.TextureCompressionSettings.TC_DEFAULT)
        if kind == 'Normal':
            texture.set_editor_property('flip_green_channel', True)
        save(texture)
    texture_map[entry['material'], entry['kind']] = texture

materials = {}
for slot_name in source_report['materials']:
    path = BOSS + '/Zombie/Materials/M_' + slot_name
    material = lib.load_asset(path)
    if not material:
        material = assets.create_asset('M_' + slot_name, BOSS + '/Zombie/Materials', u.Material, u.MaterialFactoryNew())
        edit.set_base_material_usage(material, u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        for column, kind in enumerate(('BaseColor', 'Normal', 'OcclusionRoughnessMetallic')):
            node = edit.create_material_expression(material, u.MaterialExpressionTextureSample, -400, column * 220)
            node.set_editor_property('texture', texture_map[slot_name, kind])
            node.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind == 'Normal' else u.MaterialSamplerType.SAMPLERTYPE_MASKS if kind != 'BaseColor' else u.MaterialSamplerType.SAMPLERTYPE_COLOR)
            if kind == 'BaseColor':
                edit.connect_material_property(node, 'RGB', u.MaterialProperty.MP_BASE_COLOR)
            elif kind == 'Normal':
                edit.connect_material_property(node, 'RGB', u.MaterialProperty.MP_NORMAL)
            else:
                for channel, prop in (('R', u.MaterialProperty.MP_AMBIENT_OCCLUSION), ('G', u.MaterialProperty.MP_ROUGHNESS), ('B', u.MaterialProperty.MP_METALLIC)):
                    edit.connect_material_property(node, channel, prop)
        edit.recompile_material(material)
        save(material)
    materials[slot_name] = material
slots = list(mesh.get_editor_property('materials'))
assert len(slots) == len(materials), slots
for slot in slots:
    name = str(slot.material_slot_name)
    assert name in materials, name
    slot.set_editor_property('material_interface', materials[name])
mesh.set_editor_property('materials', slots)
save(mesh)

# The artist's Bag slot has zero faces. Retain only actual rendered materials.
for unused in ('Materials/M_Zombie_Bag', 'Textures/Zombie_Bag_BaseColor', 'Textures/Zombie_Bag_Normal', 'Textures/Zombie_Bag_OcclusionRoughnessMetallic'):
    path = BOSS + '/Zombie/' + unused
    if lib.does_asset_exist(path):
        assert lib.delete_asset(path), path

profile = lib.load_asset(BOSS + '/DA_ZombieBoss')
if not profile:
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class', u.MCBossProfile)
    profile = assets.create_asset('DA_ZombieBoss', BOSS, u.MCBossProfile, factory)
    profile.set_editor_property('skeletal_mesh', mesh)
    profile.set_editor_property('mesh_transform', u.Transform(location=u.Vector(0,0,-110), rotation=u.Rotator(pitch=0,yaw=-90,roll=0), scale=u.Vector(1.8,1.8,1.8)))
    phase = u.MCBossPhaseDefinition()
    phase.health_fraction = .5
    phase.movement_multiplier = 1.2
    profile.set_editor_property('phases', [phase])
    claw = u.MCBossAttackDefinition()
    slam = u.MCBossAttackDefinition()
    slam.attack_id = 'Slam'
    slam.minimum_phase = 1
    slam.damage = 30
    slam.range = 300
    slam.half_angle_degrees = 85
    slam.windup_seconds = 1.2
    slam.cooldown_seconds = 4
    profile.set_editor_property('attacks', [claw, slam])
    save(profile)

def blueprint(name, directory, parent):
    bp = lib.load_asset(directory + '/' + name)
    if not bp:
        factory = u.BlueprintFactory()
        factory.set_editor_property('parent_class', parent)
        bp = assets.create_asset(name, directory, u.Blueprint, factory)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    return bp, u.get_default_object(bp.generated_class())

boss_bp, boss_defaults = blueprint('BP_ZombieBoss', BOSS, u.MCBossCharacter)
boss_defaults.set_editor_property('profile', profile)
boss_defaults.get_editor_property('mesh').set_skeletal_mesh_asset(mesh)
boss_defaults.get_editor_property('mesh').set_relative_transform(profile.get_editor_property('mesh_transform'), False, True)
save(boss_bp)
chest_bp, chest_defaults = blueprint('BP_RewardChest', ROGUE, u.MCRewardChest)
chest_defaults.set_editor_property('selection_policy', u.MCRewardSelectionPolicy.CHOOSE_ONE)
save(chest_bp)

backup = ROOT / 'Saved/RogueReview/L_Mouth.before-roguelike.umap'
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    shutil.copy2(ROOT / 'Content/Maps/L_Mouth.umap', backup)
assert level.load_level(MAP)
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
all_actors = actors.get_all_level_actors()
tongue = next(a for a in all_actors if isinstance(a, u.MCTongue) and a.get_name() == 'MCTongue_0')
tongue.call_method('RebuildSurface')
center, extent = tongue.get_actor_bounds(False)
report['tongue_bounds'] = [str(center), str(extent)]

def owned(tag, cls, location, label):
    matches = [a for a in actors.get_all_level_actors() if a.actor_has_tag(tag)]
    assert len(matches) <= 1, tag
    actor = matches[0] if matches else actors.spawn_actor_from_class(cls, location)
    assert actor
    actor.set_editor_property('tags', [u.Name(tag)])
    actor.set_actor_label(label)
    return actor

zones = []
for i, (x, y) in enumerate(((-1,-1),(-1,1),(1,-1),(1,1))):
    p = center + u.Vector(x*extent.x*.30, y*extent.y*.30, 0)
    zone = owned('MC_RewardZone_' + str(i), u.MCRewardDropZone, p, 'Reward Drop Zone ' + str(i+1))
    zone.set_actor_location(p, False, True)
    zone.get_editor_property('area').set_box_extent(u.Vector(extent.x*.24, extent.y*.24, 350), False)
    zone.set_editor_property('allowed_landing_surface', tongue)
    zone.set_editor_property('inward_margin', 80)
    zones.append(zone)
    report['zones'].append({'actor':zone.get_name(), 'center':str(p)})

director = owned('MC_RewardDirector', u.MCRoguelikeDirector, center, 'Task Reward Director')
director.set_editor_property('chest_class', chest_bp.generated_class())
director.set_editor_property('perk_table', table)
director.set_editor_property('candidates_per_zone', 8)

# Adopt the artist's placed chest while preserving its apparent size and location.
placed = next((a for a in actors.get_all_level_actors() if a.actor_has_tag('MC_PlacedRewardChest')), None)
if not placed:
    original_bottom = next(a for a in all_actors if a.get_name() == 'StaticMeshActor_0')
    original_top = next(a for a in all_actors if a.get_name() == 'StaticMeshActor_4')
    assert '/SM_Chest_Bot.' in original_bottom.static_mesh_component.static_mesh.get_path_name()
    assert '/SM_Chest_Top.' in original_top.static_mesh_component.static_mesh.get_path_name()
    pos = original_bottom.get_actor_location()
    scale = original_bottom.get_actor_scale3d()
    bottom_bounds = original_bottom.get_actor_bounds(False)
    pos.z = bottom_bounds[0].z - bottom_bounds[1].z
    placed = owned('MC_PlacedRewardChest', chest_bp.generated_class(), pos, 'Reward Chest (Choose One)')
    placed.set_actor_rotation(original_bottom.get_actor_rotation(), True)
    placed.set_actor_scale3d(scale / .35)
    # A separate authored box serves this large existing chest, not random falling rewards.
    box = owned('MC_PlacedChestZone', u.MCRewardDropZone, pos, 'Placed Chest Safe Area')
    box.get_editor_property('area').set_box_extent(u.Vector(620,620,350), False)
    box.set_editor_property('allowed_landing_surface', tongue)
    box.set_editor_property('allow_reward_drops', False)
    placed.set_editor_property('placed_drop_zone', box)
    placed.set_editor_property('placed_reward', True)
    actors.destroy_actor(original_top)
    actors.destroy_actor(original_bottom)

# Explicit nav volume. Actual pathing will be checked by the rendered demonstration.
assert u.MCRoguelikeEditorLibrary.configure_boss_navigation(world, center, extent + u.Vector(150,150,500))
for actor in actors.get_all_level_actors():
    if actor.actor_has_tag('MC_ZombieBoss'):
        assert actors.destroy_actor(actor)
report['boss_spawn'] = 'F3 only; no map boss'
report['profile'] = profile.get_path_name()
assert level.save_current_level()
report['map'] = MAP
(ROOT / 'Saved/RogueReview/FoundationAuthoring.json').write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('MC_ROGUE_AUTHOR_PASS ' + json.dumps(report))
u.SystemLibrary.quit_editor()
