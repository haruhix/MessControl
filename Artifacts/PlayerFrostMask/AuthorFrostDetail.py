import json
from pathlib import Path
import unreal as u

e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
mat = u.load_asset('/Game/Gameplay/Cold/M_PlayerIceCoating')
nodes = list(e.get_material_expressions(mat))
assert not any(isinstance(n, u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name')) == 'Frost Strength' for n in nodes)
front = e.get_material_property_input_node(mat, u.MaterialProperty.MP_FRONT_MATERIAL)
inputs = list(e.get_inputs_for_material_expression(mat, front))
names = [str(p) for p in e.get_material_expression_input_names(front)]
base_color = inputs[names.index('Base Color')]
roughness = inputs[names.index('Roughness')]
sample = next(n for n in nodes if isinstance(n, u.MaterialExpressionTextureSampleParameter2D) and str(n.get_editor_property('parameter_name')) == 'Frost Mask Texture')
assert base_color and roughness

def node(cls, x, y, **values):
    n = e.create_material_expression(mat, cls, x, y)
    n.set_editor_properties(values)
    return n

def link(source, output, target, pin):
    assert e.connect_material_expressions(source, output, target, pin), pin

with u.ScopedEditorTransaction('Make the revealed frost crystal branches readable on blue ice'):
    strength = node(u.MaterialExpressionScalarParameter, -1500, 2670,
        parameter_name='Frost Strength', group='Player Freeze', default_value=0.85, slider_min=0.0, slider_max=1.0,
        desc='White matte frost over the original M_Ice shading. Zero keeps the original ice color and roughness.')
    tint = node(u.MaterialExpressionVectorParameter, -1250, 2850,
        parameter_name='Frost Tint', group='Player Freeze', default_value=u.LinearColor(0.72, 0.88, 1.0, 1.0))
    detail = node(u.MaterialExpressionMultiply, -1250, 2670)
    link(sample, 'R', detail, 'A')
    link(strength, '', detail, 'B')
    color = node(u.MaterialExpressionLinearInterpolate, -950, 2670,
        desc='Bright fern crystals over the original ice color.')
    link(base_color, '', color, 'A')
    link(tint, 'RGB', color, 'B')
    link(detail, '', color, 'Alpha')
    link(color, '', front, 'Base Color')
    matte = node(u.MaterialExpressionLinearInterpolate, -950, 2870, const_b=0.65,
        desc='The crystal branches are rougher than the ice below.')
    link(roughness, '', matte, 'A')
    link(detail, '', matte, 'Alpha')
    link(matte, '', front, 'Roughness')
    errors = list(e.recompile_material(mat))
    assert not errors, errors
    assert a.save_loaded_asset(mat, only_if_is_dirty=False)
    out = Path(u.Paths.project_dir())/'Artifacts/PlayerFrostMask/MaterialBuild.json'
    report = json.loads(out.read_text(encoding='utf-8'))
    report.update(compile_errors=errors, frost_strength=0.85, frost_tint=[0.72, 0.88, 1.0],
        original_ice_shading_preserved='Original graph retained under adjustable frost color and roughness blends')
    out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('FROST_CRYSTAL_DETAIL_SAVED', json.dumps(report))
