"""Keep cavity/tongue behind closed jaws and fit their mask in local space.

This replaces the rejected openness-to-zero collapse. The existing row height
drives a nonzero 3D envelope, while the mask is divided by that same envelope.
No shell is hidden merely because it loses selection.
"""
import difflib
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP

assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
out = Path(__file__).resolve().parent / 'Evidence'
report_path = out / 'v09_interior_row_envelope_report.json'
assert not report_path.exists()
widget = task_asset('WBP_MainMenu3D')
graph = call(BP, 'get_graph', blueprint=widget, graph_name='UpdateStage')
before = call(BP, 'read_graph_dsl', graph=graph)
(out/'v09_interior_row_envelope_before.dsl').write_text(before, encoding='utf8')
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

def connect(output, target):
    call(BP, 'connect_pins', output_pin=output, input_pin=target['pin_id'])

def disconnect(target):
    assert len(target['connected_pins']) == 1
    call(BP, 'break_pins', output_pin=target['connected_pins'][0], input_pin=target['pin_id'])

setters = [info(ref) for ref in call(BP, 'find_nodes', graph=graph, title='Scalar')]
assert len(setters) == 6
plans = []
for setter in setters:
    assert pin(setter, 'ParameterName')['value'] == 'CardHalfHeight'
    scale = upstream(pin(setter, 'execute'))
    assert scale['type_id'] == 'Transformation|SetRelativeScale3D'
    vector = upstream(pin(scale, 'NewScale3D'))
    maximum = upstream(pin(vector, 'Z'))
    assert maximum['type_id'] == 'Math|Float|Max(Float)'
    getter = upstream(pin(maximum, 'A'))
    day = int(getter['type_id'].split('GetOpen')[-1])
    division = upstream(pin(setter, 'ParameterValue'))
    assert division['type_id'] == 'Math|Float|float/float'
    height_pin = pin(division, 'A')
    assert len(height_pin['connected_pins']) == 1
    height = height_pin['connected_pins'][0]
    cast = upstream(pin(scale, 'execute'))
    hidden = upstream(pin(cast, 'execute'))
    assert hidden['type_id'].endswith('|SetActorHiddenInGame')
    condition = upstream(pin(hidden, 'bNewHidden'))
    candidates = []
    for p in condition['input_pins']:
        if len(p['connected_pins']) == 1:
            n = upstream(p)
            if n['type_id'] == 'Math|Float|float<float':
                assert upstream(pin(n, 'A'))['type_id'] == getter['type_id']
            else:
                candidates.append(p['connected_pins'][0])
    assert len(candidates) == 1
    plans.append(dict(day=day, vector=vector, division=division, height=height,
                      hidden=hidden, row_hidden=candidates[0]))
assert sorted(p['day'] for p in plans) == [1,1,2,2,3,3]
call(BP,'get_node_type_pins',graph=graph,type_id='Math|Float|SafeDivide')

vectors = set()
changes = []
for plan in plans:
    vector = plan['vector']
    key = vector['node']['refPath']
    if key not in vectors:
        position = vector['position']
        ratio = info(call(BP, 'create_node', graph=graph, type_id='Math|Float|SafeDivide',
                          pos={'x':position['x']-240,'y':position['y']+90}))
        connect(plan['height'], pin(ratio,'A'))
        call(BP,'set_pin_value',pin=pin(ratio,'B')['pin_id'],value='370.0')
        disconnect(pin(vector,'Z'))
        connect(ratio['output_pins'][0]['pin_id'],pin(vector,'Z'))
        vectors.add(key)
    disconnect(pin(plan['division'],'A'))
    call(BP,'set_pin_value',pin=pin(plan['division'],'A')['pin_id'],value='370.0')
    disconnect(pin(plan['hidden'],'bNewHidden'))
    connect(plan['row_hidden'],pin(plan['hidden'],'bNewHidden'))
    changes.append({'day':plan['day'],'scale':'rowHeight/370',
                    'mask':'370/(2.6666666666666665*parentUniformScale)',
                    'visibility':'Existing row hidden flag only; no openness cutoff'})
assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
call(BP,'compile_blueprint',blueprint=widget,warnings_as_errors=True)
after = call(BP,'read_graph_dsl',graph=graph)
(out/'v09_interior_row_envelope_after.dsl').write_text(after,encoding='utf8')
subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'remote_python.py'),
                str(Path(__file__).resolve().parent/'recover_v09_editor_checkpoint.py')],check=True)
source_path=out/'accordion_UpdateStage.dsl'
source=source_path.read_text(encoding='utf8')
for day in (1,2,3):
    old='(or hidden'+str(day)+' (< open'+str(day)+' 0.001))'
    assert source.count(old)==2
    source=source.replace(old,'hidden'+str(day))
    old='(select (< open'+str(day)+' 0.001) 0.001 open'+str(day)+')'
    assert source.count(old)==2
    source=source.replace(old,'(/ (select isStage height'+str(day)+' 370.0) 370.0)')
    old='(/ (select isStage height'+str(day)+' 370.0) (* 2.6666666666666665 scale'+str(day)+'))'
    assert source.count(old)==2
    source=source.replace(old,'(/ 370.0 (* 2.6666666666666665 scale'+str(day)+'))')
source_path.write_text(source,encoding='utf8')
report={'status':'ROW_ENVELOPE_COMPILED_SAVED_NATIVE_VISUAL_PENDING','changes':changes,
        'rejected_previous_driver':'max(Open,.001) over-shrank shell and exposed black corner gaps',
        'graph_diff':list(difflib.unified_diff(before.splitlines(),after.splitlines(),lineterm=''))}
report_path.write_text(json.dumps(report,indent=2),encoding='utf8')
print(report['status'])
