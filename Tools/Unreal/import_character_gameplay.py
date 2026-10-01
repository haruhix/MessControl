"""Import the derived Character.blend skin and adopt it only after rig validation.

Run after build_character_gameplay.py and a current Editor C++ build. The saved
game skeleton, physics asset, bone map and all existing animation clips are kept.
"""
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
SOURCE = ROOT / 'ArtSource/CharacterCurrent'
DEST = '/Game/Gameplay/CharacterCurrent'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
report = json.loads((SOURCE / 'CharacterReport.json').read_text(encoding='utf8'))
appearance = lib.load_asset('/Game/Data/DA_PlayerAppearance')
original = lib.load_asset('/Game/Art/Meshes/Character/SM_Teeth_rig')
parent = lib.load_asset('/Game/Art/Materials/M_TeethGameplay')
assert appearance and original and parent
assert {'ARM', 'Albedo Texture', 'Normal Texture'} <= {str(n) for n in edit.get_texture_parameter_names(parent)}
assert {'Coffee', 'Damage', 'HitFlash', 'BodyStretch'} <= {str(n) for n in edit.get_scalar_parameter_names(parent)}
u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')

task = u.AssetImportTask()
task.filename = str(SOURCE / 'SK_CharacterGameplay.fbx')
task.destination_path = DEST
task.destination_name = 'SK_CharacterGameplay'
task.automated = True
task.replace_existing = True
task.replace_existing_settings = True
task.save = True
task.factory = u.FbxFactory()
options = u.FbxImportUI()
options.import_mesh = True
options.import_as_skeletal = True
options.mesh_type_to_import = u.FBXImportType.FBXIT_SKELETAL_MESH
options.import_materials = False
options.import_textures = False
options.import_animations = False
options.create_physics_asset = False
options.skeleton = original.get_editor_property('skeleton')
data = options.skeletal_mesh_import_data
data.set_editor_property('import_morph_targets', True)
data.set_editor_property('update_skeleton_reference_pose', False)
data.set_editor_property('use_t0_as_ref_pose', False)
data.set_editor_property('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
task.options = options
assets.import_asset_tasks([task])
mesh = lib.load_asset(DEST + '/SK_CharacterGameplay')
assert isinstance(mesh, u.SkeletalMesh)
assert mesh.get_editor_property('skeleton') == options.skeleton
actual = {str(m.get_name()) for m in mesh.get_editor_property('morph_targets')}
expected = set(report['shape_keys']) - {'Basis'}
# Unreal discards sub-threshold numerical noise. The artist's closed M/B/P
# equals Basis to visual precision; the runtime still suppresses other poses
# for this viseme, so closed lips are represented by the neutral mesh.
required = {n for n in expected if report['morph_max_delta_cm'][n] >= .01}
assert required <= actual <= expected, (sorted(required - actual), sorted(actual - expected))

# Compare every saved game bone/socket before switching the player profile.
world = u.get_editor_subsystem(u.EditorActorSubsystem)
actors, components = [], []
try:
    for asset in (original, mesh):
        actor = world.spawn_actor_from_class(u.SkeletalMeshActor, u.Vector(0, 0, -5000))
        actors.append(actor)
        component = actor.get_component_by_class(u.SkeletalMeshComponent)
        component.set_skeletal_mesh_asset(asset)
        components.append(component)
    names = list(components[0].get_all_socket_names())
    assert set(names) == set(components[1].get_all_socket_names())
    for name in names:
        a = components[0].get_socket_transform(name, u.RelativeTransformSpace.RTS_COMPONENT)
        b = components[1].get_socket_transform(name, u.RelativeTransformSpace.RTS_COMPONENT)
        assert a.translation.distance(b.translation) < .05, str(name)
        assert a.scale3d.distance(b.scale3d) < .002, (str(name), str(a.scale3d), str(b.scale3d))
        assert abs(sum(getattr(a.rotation, k) * getattr(b.rotation, k) for k in ('x', 'y', 'z', 'w'))) > .99999, str(name)
finally:
    for actor in actors:
        world.destroy_actor(actor)

textures = {}
for entry in report['textures']:
    texture_task = u.AssetImportTask()
    texture_task.filename = str(SOURCE / entry['file'])
    texture_task.destination_path = DEST + '/Textures'
    texture_task.destination_name = Path(entry['file']).stem
    texture_task.automated = True
    texture_task.replace_existing = True
    texture_task.save = True
    assets.import_asset_tasks([texture_task])
    texture = lib.load_asset(texture_task.destination_path + '/' + texture_task.destination_name)
    assert isinstance(texture, u.Texture2D)
    kind = entry['kind']
    texture.set_editor_property('srgb', kind == 'BaseColor')
    texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP if kind == 'Normal' else u.TextureCompressionSettings.TC_MASKS if kind != 'BaseColor' else u.TextureCompressionSettings.TC_DEFAULT)
    if kind == 'Normal':
        texture.set_editor_property('flip_green_channel', True)
    assert lib.save_loaded_asset(texture, only_if_is_dirty=False)
    textures[entry['slot'], kind] = texture

# Both materials use the existing BodyStretch WPO. Slot zero additionally
# receives the character's Coffee/Damage/HitFlash dynamic parameters at runtime.
edit.set_base_material_usage(parent, u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
edit.recompile_material(parent)
assert lib.save_loaded_asset(parent, only_if_is_dirty=False)
materials = []
for index, name in enumerate(('MI_Character', 'MI_Bag')):
    path = DEST + '/Materials/' + name
    material = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if not material:
        material = assets.create_asset(name, DEST + '/Materials', u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(material, parent)
    edit.update_material_instance(material)
    for kind, parameter in (('BaseColor', 'Albedo Texture'), ('Normal', 'Normal Texture'), ('OcclusionRoughnessMetallic', 'ARM')):
        edit.set_material_instance_texture_parameter_value(material, parameter, textures[index, kind])
        # UE 5.8's setter returns false even after applying the value; read it back.
        assert edit.get_material_instance_texture_parameter_value(material, parameter) == textures[index, kind], (name, parameter)
    for parameter in ('Roughness Strength', 'Metalic'):
        edit.set_material_instance_scalar_parameter_value(material, parameter, 1)
        assert edit.get_material_instance_scalar_parameter_value(material, parameter) == 1, (name, parameter)
    edit.update_material_instance(material)
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)
    materials.append(material)
slots = list(mesh.get_editor_property('materials'))
assert len(slots) == 2, [str(s.material_slot_name) for s in slots]
for index, slot in enumerate(slots):
    # Legacy FBX keeps polygon material order; verify it before assigning.
    assert str(slot.material_slot_name) == ('Material_010' if index == 0 else 'Material_003'), str(slot.material_slot_name)
    slot.set_editor_property('material_interface', materials[index])
mesh.set_editor_property('materials', slots)
mesh.set_editor_property('physics_asset', appearance.get_editor_property('physics_asset'))
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)

gaze = lib.load_asset('/Game/Data/DA_Gaze')
settings = gaze.get_editor_property('settings')
for name, value in {'pupil_rest': 1., 'pupil_danger': .65, 'pupil_pain': .75, 'pupil_positive': 1.6, 'pupil_focus': .8}.items():
    settings.set_editor_property(name, value)
gaze.set_editor_property('settings', settings)
assert lib.save_loaded_asset(gaze, only_if_is_dirty=False)
appearance.set_editor_property('skeletal_mesh', mesh)
appearance.set_editor_property('material', materials[0])
assert lib.save_loaded_asset(appearance, only_if_is_dirty=False)
u.log('MC_CHARACTER_IMPORT_PASS morphs=' + str(len(actual)) + ' bones=' + str(len(names)) + ' materials=' + str([str(s.material_slot_name) for s in slots]))
u.SystemLibrary.quit_editor()
