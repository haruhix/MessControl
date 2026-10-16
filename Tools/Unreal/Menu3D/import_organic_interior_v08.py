"""Import the independently verified detailed interior as real static meshes.

FBX nodes contain 100x model transforms with cm metadata. Bake those node
transforms once; do not also multiply the geometry or import uniform scale.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
handoff = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior/UV_Organic_r05_Candidate01'
qa = json.loads((handoff / 'QA_Independent/final_interior_validation.json').read_text(encoding='utf8'))
assert qa['technical_texture_import_gate'] == 'PASS'
expected = {
    'Interior': {'sha256': '1dcb142957e0efe25aba29204593c7d401347b3b7d693e53b7cf0a0fe1b1d8ce',
                 'minimum': [-941.545, -1403.567, -632.410], 'maximum': [941.545, -319.939, 636.410]},
    'Tongue': {'sha256': '2fedc350f4ac98a4d446ab227c8d6b5ffa27118710b3d25a202fd4de8a8090f8',
               'minimum': [-805., -505., -475.], 'maximum': [805., -65., -79.705]},
}
for part, info in expected.items():
    filename = 'SM_DAY03_' + part + '.fbx'
    assert qa['frozen_files'][filename] == info['sha256']
    assert hashlib.sha256((handoff / filename).read_bytes()).hexdigest() == info['sha256']
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assets = u.AssetToolsHelpers.get_asset_tools()
report = {'status': 'ORGANIC_INTERIOR_GEOMETRY_BOUND_WITH_INTERIM_SURFACE_DEFAULTS',
          'world': world.get_path_name(), 'bindings': [],
          'unit_policy': 'UnitScaleFactor1cm + FBXModelScale100 baked once by TransformVertexToAbsolute; import_uniform_scale1',
          'validation': 'Independent source/FBX/UV/OBJ gates pass. Native clip/character/motion/PBR still pending.'}
staged = []
for part, info in expected.items():
    name = 'SM_DAY03_' + part + '_Organic_v08'
    path = '/Game/UI/MainMenu3D/Meshes/' + name
    assert not u.EditorAssetLibrary.does_asset_exist(path), 'Preserve earlier staged geometry'
    options = u.FbxImportUI()
    for field, value in [('automated_import_should_detect_type', False), ('import_mesh', True),
                         ('import_as_skeletal', False), ('import_materials', False), ('import_textures', False),
                         ('import_animations', False), ('mesh_type_to_import', u.FBXImportType.FBXIT_STATIC_MESH),
                         ('original_import_type', u.FBXImportType.FBXIT_STATIC_MESH)]:
        options.set_editor_property(field, value)
    data = options.get_editor_property('static_mesh_import_data')
    for field, value in [('convert_scene', False), ('convert_scene_unit', False), ('force_front_x_axis', False),
                         ('import_uniform_scale', 1.), ('transform_vertex_to_absolute', True),
                         ('bake_pivot_in_vertex', False), ('combine_meshes', True), ('auto_generate_collision', False),
                         ('generate_lightmap_u_vs', False), ('normal_import_method', u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)]:
        data.set_editor_property(field, value)
    task = u.AssetImportTask()
    for field, value in [('filename', str(handoff / ('SM_DAY03_' + part + '.fbx'))),
                         ('destination_path', '/Game/UI/MainMenu3D/Meshes'), ('destination_name', name),
                         ('automated', True), ('save', False), ('replace_existing', False),
                         ('options', options), ('factory', u.FbxFactory())]:
        task.set_editor_property(field, value)
    assets.import_asset_tasks([task])
    mesh = u.load_asset(path)
    assert mesh
    bounds = mesh.get_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    low = [origin.x - extent.x, origin.y - extent.y, origin.z - extent.z]
    high = [origin.x + extent.x, origin.y + extent.y, origin.z + extent.z]
    max_error = max(abs(a-b) for actual, target in ((low, info['minimum']), (high, info['maximum']))
                    for a, b in zip(actual, target))
    assert max_error < .1, (part, low, high, 'Unit/axis mismatch: do not bind')
    actors = u.GameplayStatics.get_all_actors_with_tag(world, 'Menu3D_DAY03_' + part)
    assert len(actors) == 1
    component = actors[0].get_component_by_class(u.StaticMeshComponent)
    material = u.load_asset('/Game/UI/MainMenu3D/ReferenceFinish/Materials/MI_DAY03_' + part + '_v08')
    assert material and len(mesh.get_editor_property('static_materials')) == 1
    mesh.set_material(0, material)
    assert u.EditorAssetLibrary.save_loaded_asset(mesh)
    staged.append((part, component, mesh, material, low, high, max_error))

# Both meshes must pass units/bounds before either scene binding is changed.
for part, component, mesh, material, low, high, max_error in staged:
    before = component.get_editor_property('static_mesh')
    transform = str(component.get_relative_transform())
    assert component.set_static_mesh(mesh)
    component.set_material(0, material)
    assert str(component.get_relative_transform()) == transform
    report['bindings'].append({'part': part, 'mesh': mesh.get_path_name(),
                               'previous': before.get_path_name(), 'relative_transform': transform,
                               'minimum_cm': low, 'maximum_cm': high, 'bounds_max_error_cm': max_error,
                               'material': material.get_path_name()})
assert levels.save_current_level()
(root / 'Tools/Unreal/Menu3D/Evidence/v08_organic_interior_import_report.json').write_text(
    json.dumps(report, indent=2), encoding='utf8')
u.log('MC_ORGANIC_INTERIOR_IMPORTED ' + json.dumps(report))
