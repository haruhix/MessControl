import json
from pathlib import Path
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root = Path(u.Paths.project_dir()).resolve()
artifacts = root / 'Artifacts/FogBrawlSearch_20261014'
artifacts.mkdir(parents=True, exist_ok=True)
tools = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
folder = '/Game/Gameplay/FogBrawl'
report = {'materials': {}, 'sounds': []}

def node(mat, cls, x, y):
    return lib.create_material_expression(mat, cls, x, y)

def material(name, code, depth):
    path = folder + '/' + name
    mat = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name, folder, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(mat)
    mat.set_editor_properties({'blend_mode': u.BlendMode.BLEND_ADDITIVE, 'shading_model': u.MaterialShadingModel.MSM_UNLIT,
                              'two_sided': True, 'disable_depth_test': depth, 'use_translucency_vertex_fog': False})
    uv = node(mat, u.MaterialExpressionTextureCoordinate, -600, 180)
    mask = node(mat, u.MaterialExpressionCustom, -350, 180)
    custom_input = u.CustomInput()
    custom_input.set_editor_property('input_name', 'UV')
    mask.set_editor_properties({'code': code, 'output_type': u.CustomMaterialOutputType.CMOT_FLOAT1,
                               'inputs': [custom_input]})
    assert lib.connect_material_expressions(uv, '', mask, 'UV')
    color = node(mat, u.MaterialExpressionVertexColor, -600, -80)
    opacity = node(mat, u.MaterialExpressionMultiply, -100, 160)
    assert lib.connect_material_expressions(color, 'A', opacity, 'A')
    assert lib.connect_material_expressions(mask, '', opacity, 'B')
    assert lib.connect_material_property(color, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    assert lib.connect_material_property(opacity, '', u.MaterialProperty.MP_OPACITY)
    lib.recompile_material(mat)
    assert u.EditorAssetLibrary.save_loaded_asset(mat)
    report['materials'][name] = {'path': path, 'depth_test_disabled': depth, 'vertex_fog': False}

material('M_FogSearchGlow', 'float2 p = UV*2-1; return pow(saturate(1-dot(p,p)), 3.0);', False)
material('M_FogBeacon', 'float side = pow(saturate(1-abs(UV.x*2-1)),2.5); return side * smoothstep(0,0.12,UV.y) * pow(saturate(1-UV.y),0.55);', True)
material('M_FogStrikeTell', 'return 1.0;', True)
for wav in sorted((root / 'ContentSource/Audio/FogBrawl').glob('*.wav')):
    task = u.AssetImportTask()
    task.set_editor_properties({'filename': str(wav), 'destination_path': '/Game/Audio/FogBrawl',
                               'automated': True, 'replace_existing': False, 'save': True})
    tools.import_asset_tasks([task])
    paths = task.get_editor_property('imported_object_paths')
    assert len(paths) == 1, (wav, paths)
    sound = u.load_asset(paths[0])
    sound.set_editor_property('virtualization_mode', u.VirtualizationMode.DISABLED)
    assert u.EditorAssetLibrary.save_loaded_asset(sound)
    report['sounds'].append({'path': paths[0], 'duration': sound.get_editor_property('duration')})
(artifacts / 'AuthoredAssets.json').write_text(json.dumps(report, indent=2))
print('FOG_AUTHORED', json.dumps(report))
