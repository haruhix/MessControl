"""Make the real cavity/tongue collapse with jaw openness before hiding.

Adds six component-scale calls to existing interior loops. Jaw morphs, actor
positions, camera, input, character visibility and the accordion are preserved.
"""
import difflib
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP

assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
out = Path(__file__).resolve().parent / 'Evidence'
report_path = out / 'v09_interior_opening_pop_report.json'
assert not report_path.exists()
widget = task_asset('WBP_MainMenu3D')
graph = call(BP, 'get_graph', blueprint=widget, graph_name='UpdateStage')
before = call(BP, 'read_graph_dsl', graph=graph)
(out / 'v09_interior_opening_pop_before.dsl').write_text(before, encoding='utf8')
cache = {}
def info(ref):
    key = ref['refPath']
    if key not in cache:
        cache[key] = call(BP, 'get_node_infos', nodes=[ref])[0]
    return cache[key]
def pin(node, name, output=False):
    return next(p for p in node['output_pins' if output else 'input_pins'] if p['name'] == name)
def upstream(input_pin):
    assert len(input_pin['connected_pins']) == 1
    return info(input_pin['connected_pins'][0]['node'])
def create(type_id, pos):
    return info(call(BP, 'create_node', graph=graph, type_id=type_id, pos=pos))
def connect(output_pin, input_pin):
    call(BP, 'connect_pins', output_pin=output_pin['pin_id'], input_pin=input_pin['pin_id'])

mapping = {'InteriorActors': (3, 'Interior', .90), 'TongueActors': (3, 'Tongue', .90),
           'Interior1Actors': (1, 'Interior', .90), 'Tongue1Actors': (1, 'Tongue', .90),
           'Interior2Actors': (2, 'Interior', .88), 'Tongue2Actors': (2, 'Tongue', .90)}
setters = [info(ref) for ref in call(BP, 'find_nodes', graph=graph, title='Scalar')]
assert len(setters) == 6
plans = []
for setter in setters:
    assert setter['type_id'] == 'Rendering|Material|SetScalarParameterValueOnMaterials'
    assert pin(setter, 'ParameterName')['value'] == 'CardHalfHeight'
    cast = upstream(pin(setter, 'execute'))
    assert cast['type_id'].endswith('|CastToStaticMeshActor')
    loop = upstream(pin(cast, 'Object'))
    array = upstream(pin(loop, 'Array'))
    array_name = array['type_id'].split('|Get')[-1]
    day, part, x_scale = mapping[array_name]
    hidden = upstream(pin(cast, 'execute'))
    assert hidden['type_id'].endswith('|SetActorHiddenInGame')
    condition = upstream(pin(hidden, 'bNewHidden'))
    candidates = []
    for p in condition['input_pins']:
        if len(p['connected_pins']) == 1:
            n = upstream(p)
            if n['type_id'] == 'Math|Float|float<float':
                candidates.append(n)
    assert len(candidates) == 1
    less = candidates[0]
    getter = upstream(pin(less, 'A'))
    assert getter['type_id'].endswith('|GetOpen' + str(day))
    threshold = pin(less, 'B')
    if threshold['connected_pins']:
        literal = upstream(threshold)
        assert literal['type_id'] == 'Math|Float|MakeLiteralFloat'
        threshold = pin(literal, 'Value')
    assert not threshold['connected_pins'] and abs(float(threshold['value'])-.05) < 1e-8
    plans.append({'setter': setter, 'day': day, 'part': part, 'x': x_scale,
                  'array': array_name, 'getter': getter, 'threshold': threshold})
assert set(p['array'] for p in plans) == set(mapping)
for type_id in ('Transformation|SetRelativeScale3D', 'Math|Vector|MakeVector', 'Math|Float|Max(Float)'):
    call(BP, 'get_node_type_pins', graph=graph, type_id=type_id)

vectors = {}
changes = []
for plan in plans:
    setter, day, x_scale = plan['setter'], plan['day'], plan['x']
    position = setter['position']
    vector_key = (day, x_scale)
    if vector_key not in vectors:
        maximum = create('Math|Float|Max(Float)', {'x': position['x']-650, 'y': position['y']+150})
        call(BP, 'set_pin_value', pin=pin(maximum, 'B')['pin_id'], value='0.001')
        connect(plan['getter']['output_pins'][0], pin(maximum, 'A'))
        vector = create('Math|Vector|MakeVector', {'x': position['x']-400, 'y': position['y']+100})
        call(BP, 'set_pin_value', pin=pin(vector, 'X')['pin_id'], value=str(x_scale))
        call(BP, 'set_pin_value', pin=pin(vector, 'Y')['pin_id'], value='1.0')
        connect(maximum['output_pins'][0], pin(vector, 'Z'))
        vectors[vector_key] = vector
    scale = create('Transformation|SetRelativeScale3D', {'x': position['x']-220, 'y': position['y']})
    connect(vectors[vector_key]['output_pins'][0], pin(scale, 'NewScale3D'))
    component_source = pin(setter, 'self')['connected_pins'][0]
    call(BP, 'connect_pins', output_pin=component_source, input_pin=pin(scale, 'self')['pin_id'])
    execute = pin(setter, 'execute')
    previous_exec = execute['connected_pins'][0]
    call(BP, 'break_pins', output_pin=previous_exec, input_pin=execute['pin_id'])
    call(BP, 'connect_pins', output_pin=previous_exec, input_pin=pin(scale, 'execute')['pin_id'])
    connect(scale['output_pins'][0], execute)
    call(BP, 'set_pin_value', pin=plan['threshold']['pin_id'], value='0.001')
    changes.append({'day': day, 'part': plan['part'], 'array': plan['array'],
                    'scale_node': scale['node'], 'relative_x': x_scale, 'relative_z': 'max(Open,0.001)',
                    'hide_threshold_before': .05, 'hide_threshold_after': .001})
assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
call(BP, 'compile_blueprint', blueprint=widget, warnings_as_errors=True)
after = call(BP, 'read_graph_dsl', graph=graph)
(out / 'v09_interior_opening_pop_after.dsl').write_text(after, encoding='utf8')
subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / 'remote_python.py'),
                str(Path(__file__).resolve().parent / 'recover_v09_editor_checkpoint.py')], check=True)
source_path = out / 'accordion_UpdateStage.dsl'
source = source_path.read_text(encoding='utf8')
for plan in plans:
    marker = '  (for child (Variables|Default|Get' + plan['array'] + ')'
    start = source.index(marker)
    end = source.find('\n  (for child ', start+len(marker))
    if end < 0:
        end = len(source)
    block = source[start:end]
    token = '(< open' + str(plan['day']) + ' 0.05)'
    assert block.count(token) == 1
    block = block.replace(token, '(< open' + str(plan['day']) + ' 0.001)', 1)
    marker2 = '        (Rendering|Material|SetScalarParameterValueonMaterials'
    assert block.count(marker2) == 1
    scale_line = '        (Transformation|SetRelativeScale3D :self staticMesh :NewScale3D (Math|Vector|MakeVector :X ' + str(plan['x']) + ' :Y 1.0 :Z (select (< open' + str(plan['day']) + ' 0.001) 0.001 open' + str(plan['day']) + ')))\n'
    block = block.replace(marker2, scale_line+marker2, 1)
    source = source[:start]+block+source[end:]
source_path.write_text(source, encoding='utf8')
report = {'status': 'SEAMLESS_INTERIOR_COLLAPSE_COMPILED_SAVED_NATIVE_MOTION_PENDING', 'changes': changes,
          'scope': 'Six existing cavity/tongue loops only; real3D Z collapse tracks the existing jaw openness; no early visibility cutoff',
          'graph_diff': list(difflib.unified_diff(before.splitlines(), after.splitlines(), lineterm=''))}
report_path.write_text(json.dumps(report, indent=2), encoding='utf8')
print(report['status'])
