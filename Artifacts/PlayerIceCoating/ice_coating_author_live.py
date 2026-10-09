import json
from pathlib import Path
import unreal as u

e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
source = u.load_asset('/Game/Art/Materials/ice/M_Ice')
path = '/Game/Gameplay/Cold/M_PlayerIceCoating'
assert source and not a.does_asset_exist(path)
mat = a.duplicate_asset(source.get_path_name().split('.')[0], path)
assert mat
front = e.get_material_property_input_node(mat, u.MaterialProperty.MP_FRONT_MATERIAL)
assert front
front_pin = str(e.get_material_property_input_node_output_name(mat, u.MaterialProperty.MP_FRONT_MATERIAL))
original_mask = e.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY_MASK)
original_mask_pin = str(e.get_material_property_input_node_output_name(mat, u.MaterialProperty.MP_OPACITY_MASK))
original_wpo = e.get_material_property_input_node(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
original_wpo_pin = str(e.get_material_property_input_node_output_name(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET))

def node(cls, x, y, **values):
    n = e.create_material_expression(mat, cls, x, y)
    n.set_editor_properties(values)
    return n

def link(n, output, target, pin):
    assert e.connect_material_expressions(n, output, target, pin), pin

amount = node(u.MaterialExpressionScalarParameter, -1550, 1700,
    parameter_name='IceAmount', default_value=0.0, group='Player Freeze', slider_min=0.0, slider_max=1.0,
    desc='Driven by the replicated player freeze meter. 0 is clear, 1 is fully iced.')
position = node(u.MaterialExpressionPreSkinnedPosition, -1550, 1900)
interpolator = node(u.MaterialExpressionVertexInterpolator, -1290, 1900)
link(position, '', interpolator, '')
growth = node(u.MaterialExpressionCustom, -990, 1700,
    description='Ice patches grow on the animated body as the freeze meter fills',
    code='float a=saturate(Amount);\nif(a<=0.001) return 0.0;\nif(a>=0.999) return 1.0;\nfloat patches=saturate(0.5+0.25*sin(P.x*0.073+sin(P.z*0.045)*2.0)+0.25*sin(P.y*0.066+P.z*0.032));\nreturn smoothstep(patches-0.12,patches+0.12,a);',
    output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
inputs = []
for name in ['P', 'Amount']:
    entry = u.CustomInput()
    entry.set_editor_property('input_name', name)
    inputs.append(entry)
growth.set_editor_property('inputs', inputs)
link(interpolator, '', growth, 'P')
link(amount, '', growth, 'Amount')
dither = node(u.MaterialExpressionMaterialFunctionCall, -630, 1700,
    material_function=u.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
pins = list(e.get_material_expression_input_names(dither))
assert any('Alpha' in str(p) for p in pins), pins
link(growth, '', dither, next(str(p) for p in pins if 'Alpha' in str(p)))
mask = dither
if original_mask:
    mask = node(u.MaterialExpressionMultiply, -330, 1700)
    link(original_mask, original_mask_pin, mask, 'A')
    link(dither, '', mask, 'B')
assert e.connect_material_property(mask, '', u.MaterialProperty.MP_OPACITY_MASK)

normal = node(u.MaterialExpressionVertexNormalWS, -1550, 2130)
thickness = node(u.MaterialExpressionScalarParameter, -1550, 2330,
    parameter_name='IceThickness', default_value=1.2, group='Player Freeze', slider_min=0.0, slider_max=4.0,
    desc='Shell expansion in world centimetres at full freeze. Does not affect collision.')
grow_thickness = node(u.MaterialExpressionMultiply, -1290, 2330)
link(amount, '', grow_thickness, 'A'); link(thickness, '', grow_thickness, 'B')
offset = node(u.MaterialExpressionMultiply, -990, 2200)
link(normal, '', offset, 'A'); link(grow_thickness, '', offset, 'B')
if original_wpo:
    addition = node(u.MaterialExpressionAdd, -630, 2200)
    link(original_wpo, original_wpo_pin, addition, 'A'); link(offset, '', addition, 'B')
    offset = addition
assert e.connect_material_property(offset, '', u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
mat.set_editor_properties({'blend_mode': u.BlendMode.BLEND_MASKED, 'used_with_skeletal_mesh': True})
errors = list(e.recompile_material(mat))
assert not errors, errors
assert e.get_material_property_input_node(mat, u.MaterialProperty.MP_FRONT_MATERIAL) == front
assert str(e.get_material_property_input_node_output_name(mat, u.MaterialProperty.MP_FRONT_MATERIAL)) == front_pin
a.set_metadata_tag(mat, 'PlayerIce.Source', source.get_path_name())
assert a.save_loaded_asset(mat, only_if_is_dirty=False)
out = Path(u.Paths.project_dir())/'Artifacts/PlayerIceCoating'
out.mkdir(parents=True, exist_ok=True)
report = {'source': source.get_path_name(), 'material': mat.get_path_name(), 'compile_errors': errors,
    'source_substrate_graph_preserved': True, 'amount': 0.0, 'thickness_cm': 1.2, 'presentation': 'masked ice shell following the character pose'}
(out/'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('PLAYER_ICE_MATERIAL_SAVED ' + json.dumps(report))
