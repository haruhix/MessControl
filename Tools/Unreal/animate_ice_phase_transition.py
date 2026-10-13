"""Author the radial frost front in the owned tongue material only.

Run in an idle MessControl Editor after the phase-one ice/frost split. This
does not save or compile packages: use MaterialTools.recompile and save only
M_TongueFrost after the graph changes pass. Phase one and the finished phase
two retain their previous shading when Frost Sweep is zero.
"""
import json
import shutil
from pathlib import Path

import unreal as u


project = Path(u.Paths.project_dir()).resolve()
assert project.name == 'MessControl'
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve active PIE'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
path = '/Game/Gameplay/Cold/Frost/M_TongueFrost'
dirty = [p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
assert path not in dirty, 'Preserve unsaved edits to the target material'
e = u.MaterialEditingLibrary
material = u.load_asset(path)
assert isinstance(material, u.Material)
nodes = list(e.get_material_expressions(material))
prefix = 'MC phase-two frost sweep | '
assert not any(str(n.get_editor_property('desc')).startswith(prefix) for n in nodes), \
    'This graph migration was already applied'
zone = next(n for n in nodes if isinstance(n, u.MaterialExpressionCustom)
    and str(n.get_editor_property('description')) == 'First-phase exact danger mask and inward frost feather')
parameters = {str(n.get_editor_property('parameter_name')): n for n in nodes
    if isinstance(n, (u.MaterialExpressionScalarParameter, u.MaterialExpressionVectorParameter))}
assert all(name in parameters for name in ('Frost Zone Enabled', 'Frost Zone Radius',
    'Frost Zone Center', 'Frost Zone Height'))
zone_inputs = dict(zip([str(s) for s in e.get_material_expression_input_names(zone)],
    list(e.get_inputs_for_material_expression(material, zone))))
world_pos = zone_inputs['P']
assert world_pos
slabs = [n for n in nodes if isinstance(n, u.MaterialExpressionSubstrateSlabBSDF)]
assert len(slabs) == 2
for slab in slabs:
    pins = [str(s) for s in e.get_material_expression_input_names(slab)]
    assert 'Emissive Color' in pins
    assert list(e.get_inputs_for_material_expression(material, slab))[pins.index('Emissive Color')] is None, \
        'Preserve an existing artist emissive input'

out = Path(u.Paths.project_saved_dir())/'Checks/IcePhaseTransition_20261013'
before = out/'before'
before.mkdir(parents=True, exist_ok=True)
source = project/'Content/Gameplay/Cold/Frost/M_TongueFrost.uasset'
backup = before/source.name
assert not backup.exists(), 'Preserve the original transition backup'
shutil.copy2(source, backup)
previous_code = str(zone.get_editor_property('code'))


def node(cls, x, y, **properties):
    value = e.create_material_expression(material, cls, x, y)
    value.set_editor_properties(properties)
    return value


def link(source_node, source_pin, target, target_pin):
    assert e.connect_material_expressions(source_node, source_pin, target, target_pin), target_pin


def input_pin(custom, name, source_node, source_pin=''):
    inputs = list(custom.get_editor_property('inputs'))
    assert name not in [str(i.get_editor_property('input_name')) for i in inputs]
    entry = u.CustomInput()
    entry.set_editor_property('input_name', name)
    custom.set_editor_property('inputs', inputs+[entry])
    link(source_node, source_pin, custom, name)


with u.ScopedEditorTransaction('Animate the second-phase radial frost front'):
    sweep = node(u.MaterialExpressionScalarParameter, -1890, 2460,
        parameter_name='Frost Sweep', default_value=0., group='Cold Throne Zone',
        desc=prefix+'One only while the second-phase frost front crosses the tongue')
    aura_radius = node(u.MaterialExpressionScalarParameter, -1890, 2630,
        parameter_name='Frost Aura Radius', default_value=700., group='Cold Throne Zone',
        desc=prefix+'Strong gameplay aura stays local while the visual front expands')
    input_pin(zone, 'Sweep', sweep)
    input_pin(zone, 'AuraRadius', aura_radius)
    zone.set_editor_property('code',
        'float distanceXY=length(P.xy-Center.xy);\n'
        'float heightOK=(abs(P.z-Center.z)<max(Height,0.0))?1.0:0.0;\n'
        '// Warmth suppression follows the strong aura, never the growing visual front.\n'
        'float hard=(distanceXY<=max(AuraRadius,0.0))?heightOK:0.0;\n'
        '// During the sweep, all elevations of the actual tongue can acquire frost.\n'
        'float visualHeight=lerp(heightOK,1.0,saturate(Sweep));\n'
        'float soft=(1.0-smoothstep(max(Radius-50.0,0.0),max(Radius,0.01),distanceXY))*visualHeight;\n'
        'return float2(hard,soft)*saturate(Enabled);')
    front = node(u.MaterialExpressionCustom, 1280, 1940,
        description='Brief cyan leading edge of the expanding second-phase frost',
        desc=prefix+'Shared Substrate emissive front; zero in phase one and after the sweep',
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
        code='float distanceXY=length(P.xy-Center.xy);\n'
            'float front=1.0-smoothstep(18.0,125.0,abs(distanceXY-max(Radius,0.0)));\n'
            'return float3(0.28,0.76,1.0)*front*saturate(Sweep)*1.65;')
    for name, source_node, pin in [('P', world_pos, ''),
            ('Center', parameters['Frost Zone Center'], 'RGB'),
            ('Radius', parameters['Frost Zone Radius'], ''), ('Sweep', sweep, '')]:
        input_pin(front, name, source_node, pin)
    for slab in slabs:
        link(front, '', slab, 'Emissive Color')

report = dict(path=path, backup=str(backup), previous_zone_code=previous_code,
    dirty_content_before=dirty, authored_nodes=[n.get_name() for n in e.get_material_expressions(material)
        if str(n.get_editor_property('desc')).startswith(prefix)],
    zone_node=zone.get_name(), front_node=front.get_name(),
    slabs=[n.get_name() for n in slabs], compiled=False, saved=False)
(out/'MaterialMigration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
u.log('MC_ICE_PHASE_FRONT_AUTHORED '+str(out/'MaterialMigration.json'))
