"""Import the colleague's independently rigged third boss phase and eight clips.

Run in Development Editor after build_boss_phase3_animations.py. Existing first
phase packages and the original colleague mesh/skeleton remain untouched.
"""
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/BossPhase3'
DEST = '/Game/Gameplay/Boss/Phase3'
lib = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
manifest = json.loads((SOURCE / 'AnimationReport.json').read_text(encoding='utf8'))
assert manifest['original_source_preserved'] and manifest['derived_bones'] > 10
assert manifest['derived_reference_pose_unchanged']
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')

task = u.AssetImportTask()
task.filename = str(SOURCE / manifest['mesh_file'])
task.destination_path = DEST
task.destination_name = 'SK_BossPhase3'
task.automated = task.save = True
task.replace_existing = task.replace_existing_settings = True
task.factory = u.FbxFactory()
options = u.FbxImportUI()
options.set_editor_property('automated_import_should_detect_type', False)
options.set_editor_property('mesh_type_to_import', u.FBXImportType.FBXIT_SKELETAL_MESH)
options.set_editor_property('import_mesh', True)
options.set_editor_property('import_as_skeletal', True)
options.set_editor_property('import_animations', False)
options.set_editor_property('import_materials', False)
options.set_editor_property('import_textures', False)
options.set_editor_property('create_physics_asset', False)
existing = lib.load_asset(DEST + '/SK_BossPhase3') if lib.does_asset_exist(DEST + '/SK_BossPhase3') else None
if existing:
    options.set_editor_property('skeleton', existing.get_editor_property('skeleton'))
data = options.get_editor_property('skeletal_mesh_import_data')
data.set_editor_property('import_morph_targets', True)
data.set_editor_property('update_skeleton_reference_pose', False)
data.set_editor_property('use_t0_as_ref_pose', False)
data.set_editor_property('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
task.options = options
assets.import_asset_tasks([task])
mesh = lib.load_asset(DEST + '/SK_BossPhase3')
assert isinstance(mesh, u.SkeletalMesh), task.imported_object_paths
skeleton = mesh.get_editor_property('skeleton')
assert skeleton and skeleton.get_path_name().startswith(DEST + '/')
material = lib.load_asset('/Game/Art/Materials/Bosses/Guardian/MI_Boss')
assert material
slots = list(mesh.get_editor_property('materials'))
assert slots
for slot in slots:
    slot.material_interface = material
mesh.set_editor_property('materials', slots)
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)

tasks = []
for record in manifest['clips']:
    task = u.AssetImportTask()
    task.filename = str(SOURCE / record['file'])
    task.destination_path = DEST + '/Animations'
    task.destination_name = record['name']
    task.automated = task.save = True
    task.replace_existing = task.replace_existing_settings = True
    task.factory = u.FbxFactory()
    options = u.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type', False)
    options.set_editor_property('mesh_type_to_import', u.FBXImportType.FBXIT_ANIMATION)
    options.set_editor_property('import_mesh', False)
    options.set_editor_property('import_animations', True)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_textures', False)
    options.set_editor_property('skeleton', skeleton)
    data = options.get_editor_property('anim_sequence_import_data')
    data.set_editor_property('animation_length', u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    data.set_editor_property('use_default_sample_rate', True)
    data.set_editor_property('import_bone_tracks', True)
    data.set_editor_property('preserve_local_transform', True)
    data.set_editor_property('import_custom_attribute', False)
    task.options = options
    tasks.append(task)
assets.import_asset_tasks(tasks)
clips = {}
for task, record in zip(tasks, manifest['clips']):
    candidates = [lib.load_asset(path) for path in task.imported_object_paths]
    sequence = next((obj for obj in candidates if isinstance(obj, u.AnimSequence)), None)
    assert sequence, (record['name'], task.imported_object_paths)
    target = DEST + '/Animations/' + record['name']
    if sequence.get_path_name().split('.')[0] != target:
        assert not lib.does_asset_exist(target), (sequence.get_path_name(), target)
        assert lib.rename_asset(sequence.get_path_name(), target)
        sequence = lib.load_asset(target)
    assert sequence.get_editor_property('skeleton') == skeleton
    assert abs(sequence.get_play_length() - record['duration_seconds']) < .04
    # Legacy FBX strips Blender's Armature node: the mesh keeps its scale 100,
    # while the animation's static root track contains scale 1. Our root never
    # moves; inherit the skeleton's reference root and retain every child track.
    u.AnimationLibrary.remove_bone_animation(sequence, 'root', include_children=False, finalize=True)
    sequence.set_editor_property('enable_root_motion', False)
    assert lib.save_loaded_asset(sequence, only_if_is_dirty=False)
    clips[record['name']] = sequence

profile = lib.load_asset(DEST + '/DA_BossPhase3') if lib.does_asset_exist(DEST + '/DA_BossPhase3') else None
if not profile:
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class', u.MCBossProfile)
    profile = assets.create_asset('DA_BossPhase3', DEST, u.MCBossProfile, factory)
assert isinstance(profile, u.MCBossProfile)
profile.set_editor_property('skeletal_mesh', mesh)
profile.set_editor_property('mesh_transform', u.Transform(
    location=u.Vector(0, 0, -110), rotation=u.Rotator(pitch=0, yaw=-90, roll=0), scale=u.Vector(1, 1, 1)))
profile.set_editor_property('animation_class', None)
profile.set_editor_property('walk_speed', 175.)
profile.set_editor_property('phases', [])
for prop, name in (('idle_animation', 'Idle'), ('walk_animation', 'Walk'),
                   ('hurt_animation', 'Hurt'), ('death_animation', 'Death'), ('roar_animation', 'Roar')):
    profile.set_editor_property(prop, clips['AN_BossPhase3_' + name])
attacks = []
for name, damage, reach, weight in (('PunchLeft', 20, 250, 1), ('PunchRight', 24, 250, 1), ('Kick', 30, 290, .65)):
    record = next(r for r in manifest['clips'] if r['name'] == 'AN_BossPhase3_' + name)
    attack = u.MCBossAttackDefinition()
    attack.attack_id = name
    attack.damage = damage
    attack.range = reach
    attack.half_angle_degrees = 55 if name != 'Kick' else 35
    attack.selection_weight = weight
    attack.windup_seconds = record['impact_seconds']
    attack.active_seconds = .10
    attack.recovery_seconds = record['duration_seconds'] - record['impact_seconds'] - .10
    assert attack.recovery_seconds >= .05
    attack.cooldown_seconds = 1.7 if name != 'Kick' else 3.
    attack.animation = clips[record['name']]
    attacks.append(attack)
profile.set_editor_property('attacks', attacks)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)

bp = lib.load_asset(DEST + '/BP_BossPhase3')
if not bp:
    factory = u.BlueprintFactory()
    factory.set_editor_property('parent_class', u.MCBossCharacter)
    bp = assets.create_asset('BP_BossPhase3', DEST, u.Blueprint, factory)
assert isinstance(bp, u.Blueprint)
u.BlueprintEditorLibrary.compile_blueprint(bp)
cdo = u.get_default_object(bp.generated_class())
cdo.set_editor_property('profile', profile)
cdo.set_editor_property('start_awake', False)
component = cdo.get_editor_property('mesh')
component.set_skeletal_mesh_asset(mesh)
component.set_relative_transform(profile.get_editor_property('mesh_transform'), False, True)
component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
for index in range(len(slots)):
    component.set_material(index, material)
hitbox = cdo.get_editor_property('body_hitbox')
hitbox.set_capsule_size(100., 120., False)
hitbox.set_relative_location(u.Vector(0, 0, 10), False, True)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert lib.save_loaded_asset(bp, only_if_is_dirty=False)
assert lib.save_loaded_asset(skeleton, only_if_is_dirty=False)

# The colleague placed the unrigged source as an artist reference. Keep it in
# the editor, while gameplay/F3 uses the separate native boss Blueprint.
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/L_Mouth')
placeholders = []
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if not isinstance(actor, u.SkeletalMeshActor):
        continue
    component = actor.get_editor_property('skeletal_mesh_component')
    original_mesh = component.get_editor_property('skeletal_mesh_asset')
    if not original_mesh or original_mesh.get_path_name() != '/Game/FromBlender6/SK_Boss_stady3.SK_Boss_stady3':
        continue
    placeholders.append({'actor': actor.get_name(), 'previous_hidden': actor.get_editor_property('hidden'),
                         'previous_collision': str(component.get_collision_enabled())})
    actor.set_actor_hidden_in_game(True)
    actor.set_actor_enable_collision(False)
    component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
if placeholders:
    assert levels.save_current_level()

report = {'colleague_commit': 'bfe90a7', 'mesh': mesh.get_path_name(),
          'skeleton': skeleton.get_path_name(), 'bones': manifest['derived_bones'],
          'material': material.get_path_name(), 'profile': profile.get_path_name(),
          'blueprint': bp.get_path_name(), 'mesh_transform': str(profile.get_editor_property('mesh_transform')),
          'animation_root': 'Static root track removed; inherit own skeleton reference pose and units',
          'clips': [{**record, 'asset': clips[record['name']].get_path_name(),
                     'imported_duration': clips[record['name']].get_play_length()} for record in manifest['clips']],
          'source_placeholders_kept_in_editor': placeholders,
          'encounter': 'Explicit F3 only; no automatic phase transition'}
out = ROOT / 'Saved/BossPhase3Integration/AnimationImport.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('MC_BOSS_PHASE3_IMPORT_PASS ' + json.dumps(report))
u.SystemLibrary.quit_editor()
