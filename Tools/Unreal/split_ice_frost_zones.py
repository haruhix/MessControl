"""Give the first boss phase slick M_Ice shading outside its local frost aura.

Edits only the owned M_TongueFrost material, preserving the original phase-two
and lab branch when Frost Zone Enabled is zero. Run in an idle MessControl
Editor; compile/save through the material toolset after this narrow graph edit.
"""
import json
from pathlib import Path
import unreal as u

e = u.MaterialEditingLibrary
assert Path(u.Paths.project_dir()).resolve().name == 'MessControl'
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve an existing PIE'
path = '/Game/Gameplay/Cold/Frost/M_TongueFrost'
material = u.load_asset(path)
assert isinstance(material, u.Material)
assert path not in [p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()], \
    'Preserve unsaved edits to the target material'
prefix = 'MC first-phase ice/frost split | '
nodes = list(e.get_material_expressions(material))
assert not any(str(n.get_editor_property('desc')).startswith(prefix) for n in nodes), \
    'This graph migration has already been applied'
plates = next(n for n in nodes if isinstance(n, u.MaterialExpressionCustom)
    and str(n.get_editor_property('description')) == 'Ice plates over a continuous frost bed outside the warm circles')
warm = next(n for n in nodes if isinstance(n, u.MaterialExpressionCustom)
    and str(n.get_editor_property('description')) == 'Short varied fade beginning at the exact warm-circle boundary')
brushed = next(n for n in nodes if isinstance(n, u.MaterialExpressionMultiply)
    and str(n.get_editor_property('desc')).startswith('MC brushed warming patch | Preserve frost growth'))
dither = e.get_material_property_input_node(material, u.MaterialProperty.MP_OPACITY_MASK)
assert 'Alpha Threshold' in [str(s) for s in e.get_material_expression_input_names(dither)]
world_pos = list(e.get_inputs_for_material_expression(material, warm))[
    [str(s) for s in e.get_material_expression_input_names(warm)].index('P')]
original_alpha = list(e.get_inputs_for_material_expression(material, brushed))[0]
assert world_pos and original_alpha
before = dict(path=path, plates=plates.get_editor_property('code'), warm=warm.get_editor_property('code'))

def node(cls, x, y, **props):
    value = e.create_material_expression(material, cls, x, y)
    value.set_editor_properties(props)
    return value

def link(src, pin, dest, name):
    assert e.connect_material_expressions(src, pin, dest, name), name

def append_input(custom, name, src, pin=''):
    inputs = list(custom.get_editor_property('inputs'))
    assert name not in [str(i.get_editor_property('input_name')) for i in inputs]
    entry = u.CustomInput()
    entry.set_editor_property('input_name', name)
    custom.set_editor_property('inputs', inputs+[entry])
    link(src, pin, custom, name)

with u.ScopedEditorTransaction('Separate phase-one slick ice and lethal frost'):
    enabled = node(u.MaterialExpressionScalarParameter, -1900, 1700,
        parameter_name='Frost Zone Enabled', default_value=0., group='Cold Throne Zone',
        desc=prefix+'One only in normal phase one; zero preserves the original material')
    center = node(u.MaterialExpressionVectorParameter, -1900, 1890,
        parameter_name='Frost Zone Center', default_value=u.LinearColor(0, 0, 0, 1), group='Cold Throne Zone',
        desc=prefix+'World-space boss floor point used by gameplay')
    radius = node(u.MaterialExpressionScalarParameter, -1900, 2080,
        parameter_name='Frost Zone Radius', default_value=700., group='Cold Throne Zone',
        desc=prefix+'Actual lethal-aura XY radius in centimetres')
    height = node(u.MaterialExpressionScalarParameter, -1900, 2270,
        parameter_name='Frost Zone Height', default_value=180., group='Cold Throne Zone',
        desc=prefix+'Actual lethal-aura height band in centimetres')
    zone = node(u.MaterialExpressionCustom, -1400, 1900,
        description='First-phase exact danger mask and inward frost feather',
        desc=prefix+'X is exact gameplay danger; Y fades dense frost inward at its edge',
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT2,
        code='float distanceXY=length(P.xy-Center.xy);\n'
            'float heightOK=(abs(P.z-Center.z)<max(Height,0.0))?1.0:0.0;\n'
            'float hard=(distanceXY<=max(Radius,0.0))?heightOK:0.0;\n'
            'float soft=(1.0-smoothstep(max(Radius-50.0,0.0),max(Radius,0.01),distanceXY))*heightOK;\n'
            'return float2(hard,soft)*saturate(Enabled);')
    for name, src, pin in [('P', world_pos, ''), ('Center', center, 'RGB'),
            ('Radius', radius, ''), ('Height', height, ''), ('Enabled', enabled, '')]:
        append_input(zone, name, src, pin)
    append_input(plates, 'LocalEnabled', enabled)
    append_input(plates, 'Zone', zone)
    old = str(plates.get_editor_property('code'))
    assert old.count('return float4(total,mix,plate,region);') == 1
    plates.set_editor_property('code', old.replace('return float4(total,mix,plate,region);',
        '// Phase one keeps the original glossy M_Ice across the tongue.\n'
        '// White fern frost identifies only the boss aura.\n'
        'total=lerp(total,a,saturate(LocalEnabled));\n'
        'mix*=lerp(1.0,Zone.y,saturate(LocalEnabled));\n'
        'return float4(total,mix,plate,region);'))
    append_input(warm, 'Zone', zone)
    old = str(warm.get_editor_property('code'))
    old_return = 'return lerp(1.0,outside,saturate(Enabled))*lerp(1.0,nextOutside,saturate(NextEnabled));'
    assert old.count(old_return) == 1
    warm.set_editor_property('code', old.replace(old_return,
        'float warmth=lerp(1.0,outside,saturate(Enabled))*lerp(1.0,nextOutside,saturate(NextEnabled));\n'
        '// A warm circle overlapping the strong aura cannot visually clear it.\n'
        'return max(warmth,Zone.x);'))
    alpha = node(u.MaterialExpressionCustom, 2050, 1300,
        description='Keep lethal frost visible where a brushed patch overlaps the aura',
        desc=prefix+'Preserve existing brushed alpha everywhere outside the exact danger zone',
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,
        code='return lerp(Brushed,Original,Zone.x);')
    append_input(alpha, 'Brushed', brushed)
    append_input(alpha, 'Original', original_alpha)
    append_input(alpha, 'Zone', zone)
    link(alpha, '', dither, 'Alpha Threshold')

report = dict(path=path, before=before, authored_nodes=[n.get_name()
    for n in e.get_material_expressions(material)
    if str(n.get_editor_property('desc')).startswith(prefix)], compiled=False, saved=False)
out = Path(u.Paths.project_saved_dir())/'Checks/IceVisualSplit_20261013/MaterialMigration.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
u.log('MC_ICE_VISUAL_GRAPH_AUTHORED '+str(out))
