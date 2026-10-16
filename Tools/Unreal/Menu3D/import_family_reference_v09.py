"""Stage a reviewed Human/Predator source in the isolated native review map.

Initial surface defaults are explicitly interim until that mesh's Painter maps
are released. Keeps the existing character, skeleton and Blueprint motion.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
request = json.loads((root / 'Tools/Unreal/Menu3D/Evidence/family_v09_import_request.json').read_text(encoding='utf8'))
day = int(request['day'])
assert day in (1, 2)
tag = 'DAY' + str(day).zfill(2)
handoff_base = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v09_FamilyReferenceForms'
handoff = (handoff_base / tag / request['candidate']).resolve()
assert handoff.is_relative_to(handoff_base)
fbx = handoff / ('SK_' + tag + '.fbx')
assert hashlib.sha256(fbx.read_bytes()).hexdigest() == request['fbx_sha256']
metadata = json.loads((handoff / 'unreal_mouth_handoff.json').read_text(encoding='utf8'))
assert metadata['day'] == tag and metadata['paired_center_seams']
assert metadata['source_unit_to_cm'] == 100. and metadata['fbx_geometry_scale'] == 1.
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
actor = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_' + tag)
assert len(actor) == 1
component = actor[0].get_component_by_class(u.SkeletalMeshComponent)
previous_mesh = component.get_editor_property('skeletal_mesh_asset')
skeleton = previous_mesh.get_editor_property('skeleton')
name = 'SK_' + tag + '_Reference_v09'
path = '/Game/UI/MainMenu3D/Meshes/' + name
assert not u.EditorAssetLibrary.does_asset_exist(path), 'Preserve a staged asset; use a new revision for repair'
options = u.FbxImportUI()
for field, value in [('automated_import_should_detect_type', False), ('import_mesh', True),
                     ('import_as_skeletal', True), ('import_materials', False), ('import_textures', False),
                     ('import_animations', False), ('create_physics_asset', False), ('skeleton', skeleton),
                     ('mesh_type_to_import', u.FBXImportType.FBXIT_SKELETAL_MESH),
                     ('original_import_type', u.FBXImportType.FBXIT_SKELETAL_MESH)]:
    options.set_editor_property(field, value)
data = options.get_editor_property('skeletal_mesh_import_data')
for field, value in [('convert_scene', False), ('convert_scene_unit', False), ('force_front_x_axis', False),
                     ('import_morph_targets', True), ('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)]:
    data.set_editor_property(field, value)
task = u.AssetImportTask()
for field, value in [('filename', str(fbx)), ('destination_path', '/Game/UI/MainMenu3D/Meshes'),
                     ('destination_name', name), ('automated', True), ('save', False), ('replace_existing', False),
                     ('options', options), ('factory', u.FbxFactory())]:
    task.set_editor_property(field, value)
assets = u.AssetToolsHelpers.get_asset_tools()
assets.import_asset_tasks([task])
mesh = u.load_asset(path)
assert mesh and mesh.get_editor_property('skeleton') == skeleton
assert sorted(str(n) for n in mesh.get_all_morph_target_names()) == ['Pose_000', 'Pose_025', 'Pose_050', 'Pose_075']
assert len(mesh.get_editor_property('materials')) == 2
base = '/Game/UI/MainMenu3D/ReferenceFinish/Materials/'
materials = []
for part in ('Enamel', 'Gum'):
    material_path = base + 'MI_' + tag + '_' + part + '_v09'
    material = u.load_asset(material_path)
    assert not material, 'Do not replace an already authored material'
    material = assets.create_asset(material_path.rsplit('/', 1)[1], base.rstrip('/'),
                                   u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
    parent = u.load_asset(base + 'M_Menu' + part + '_v08')
    assert material and parent
    u.MaterialEditingLibrary.set_material_instance_parent(material, parent)
    assert u.EditorAssetLibrary.save_loaded_asset(material)
    materials.append(material)
component.set_skeletal_mesh_asset(mesh)
slots = list(mesh.get_editor_property('materials'))
for i, material in enumerate(materials):
    component.set_material(i, material)
    slots[i].set_editor_property('material_interface', material)
mesh.set_editor_property('materials', slots)
assert u.EditorAssetLibrary.save_loaded_asset(mesh)
assert u.EditorAssetLibrary.save_loaded_asset(skeleton)
assert levels.save_current_level()
report = {'status': 'STAGED_WITH_INTERIM_PHYSICAL_DEFAULTS_PENDING_NATIVE_FIT_AND_OWN_PAINTER_MAPS',
          'day': day, 'source_fbx': str(fbx), 'source_sha256': request['fbx_sha256'],
          'mesh': path, 'previous_mesh': previous_mesh.get_path_name(), 'skeleton': skeleton.get_path_name(),
          'bounds': str(mesh.get_bounds()), 'materials': [m.get_path_name() for m in materials],
          'validation': request['validation'], 'startup_map_changed': False,
          'character_and_motion_graphs_changed': False}
(root / ('Tools/Unreal/Menu3D/Evidence/family_v09_day' + str(day) + '_import_report.json')).write_text(
    json.dumps(report, indent=2), encoding='utf8')
u.log('MC_FAMILY_REFERENCE_STAGED ' + json.dumps(report))
