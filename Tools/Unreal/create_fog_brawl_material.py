"""Author the fog event's local tooth pulse. Run with Unreal's PythonScript commandlet."""
import json
from pathlib import Path
import unreal as u

folder = '/Game/Gameplay/FogBrawl'
path = folder + '/M_FogToothPulse'
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
created = not lib.does_asset_exist(path)
if created:
    mat = u.AssetToolsHelpers.get_asset_tools().create_asset('M_FogToothPulse', folder, u.Material, u.MaterialFactoryNew())
    assert mat, path
    mat.set_editor_property('blend_mode', u.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('disable_depth_test', False)
    mat.set_editor_property('two_sided', False)
    def node(kind, x, y):
        return edit.create_material_expression(mat, kind, x, y)
    tint = node(u.MaterialExpressionVectorParameter, -650, -180)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', u.LinearColor(3,.018,.012,1))
    pulse = node(u.MaterialExpressionScalarParameter, -650, 0)
    pulse.set_editor_property('parameter_name', 'Pulse')
    pulse.set_editor_property('default_value', 1)
    rim = node(u.MaterialExpressionFresnel, -650, 160)
    rim.set_editor_property('exponent', 3)
    rim.set_editor_property('base_reflect_fraction', .10)
    glow = node(u.MaterialExpressionMultiply, -300, -60)
    assert edit.connect_material_expressions(tint, '', glow, 'A')
    assert edit.connect_material_expressions(pulse, '', glow, 'B')
    edge = node(u.MaterialExpressionMultiply, -60, 20)
    assert edit.connect_material_expressions(glow, '', edge, 'A')
    assert edit.connect_material_expressions(rim, '', edge, 'B')
    assert edit.connect_material_property(edge, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = node(u.MaterialExpressionConstant, -60, 180)
    opacity.set_editor_property('r', .85)
    assert edit.connect_material_property(opacity, '', u.MaterialProperty.MP_OPACITY)
else:
    mat = lib.load_asset(path)
    assert mat and mat.get_editor_property('blend_mode') == u.BlendMode.BLEND_ADDITIVE, 'Existing asset has an incompatible blend mode'
    assert 'Pulse' in {str(n) for n in edit.get_scalar_parameter_names(mat)}, 'Existing material has no Pulse parameter'
    assert 'Tint' in {str(n) for n in edit.get_vector_parameter_names(mat)}, 'Existing material has no Tint parameter'

errors = edit.recompile_material(mat)
assert not errors, errors
assert lib.save_loaded_asset(mat, only_if_is_dirty=True), path
assert lib.load_asset(path), 'Saved material cannot be reloaded'
report = {'asset':mat.get_path_name(), 'created':created, 'saved':True, 'shader_compile_errors':errors or [],
          'parameters':['Tint','Pulse'], 'depth_test':True, 'render_validation':'Deferred until project build and playthrough'}
directory = Path(u.Paths.project_saved_dir())/'FogBrawlImplementation_20261009'
directory.mkdir(parents=True, exist_ok=True)
(directory/'MaterialAuthoring.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_FOG_BRAWL_MATERIAL_AUTHORED')
