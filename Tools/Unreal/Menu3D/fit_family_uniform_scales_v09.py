"""Adjust only the two reviewed uniform-scale literals, preserving the stage graph."""
import difflib
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP, ASSET, BASE

assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
out = Path(__file__).resolve().parent / 'Evidence'
assert not (out / 'v09_family_uniform_fit_report.json').exists()
widget = task_asset('WBP_MainMenu3D')
graph = call(BP, 'get_graph', blueprint=widget, graph_name='UpdateStage')
before = call(BP, 'read_graph_dsl', graph=graph)
before_path = out / 'v09_family_uniform_fit_before.dsl'
if not before_path.exists():
    before_path.write_text(before, encoding='utf8')
changes = []
nodes = call(BP, 'find_nodes', graph=graph, title='DriveMouthDay')
infos = call(BP, 'get_node_infos', nodes=nodes)
for day, scale in ((1, .510365), (2, .514498)):
    drive = [n for n in infos if next(p for p in n['input_pins'] if p['name'] == 'Day')['value'] == str(day)]
    assert len(drive) == 1
    scale_input = next(p for p in drive[0]['input_pins'] if p['name'] == 'Scale')
    assert len(scale_input['connected_pins']) == 1
    multiply = call(BP, 'get_node_infos', nodes=[scale_input['connected_pins'][0]['node']])[0]
    assert multiply['type_id'] == 'Math|Float|float*float'
    factor_input = next(p for p in multiply['input_pins'] if p['name'] == 'B')
    assert len(factor_input['connected_pins']) == 1
    literal = call(BP, 'get_node_infos', nodes=[factor_input['connected_pins'][0]['node']])[0]
    assert literal['type_id'] == 'Math|Float|MakeLiteralFloat'
    value_pin = next(p for p in literal['input_pins'] if p['name'] == 'Value')
    assert not value_pin['connected_pins']
    previous = float(value_pin['value'])
    assert min(abs(previous-.54), abs(previous-scale)) < 1e-8
    if abs(previous-scale) > 1e-8:
        call(BP, 'set_pin_value', pin=value_pin['pin_id'], value=str(scale))
    assert abs(float(call(BP, 'get_pin_value', pin=value_pin['pin_id']))-scale) < 1e-8
    changes.append({'day': day, 'previous': .54, 'uniform_xyz': scale, 'literal': literal['node']})
call(BP, 'compile_blueprint', blueprint=widget, warnings_as_errors=True)
after = call(BP, 'read_graph_dsl', graph=graph)
(out / 'v09_family_uniform_fit_after.dsl').write_text(after, encoding='utf8')
assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / 'remote_python.py'),
                str(Path(__file__).resolve().parent / 'recover_v09_editor_checkpoint.py')], check=True)
source = out / 'accordion_UpdateStage.dsl'
text = source.read_text(encoding='utf8')
for day, scale in ((1, .510365), (2, .514498)):
    old = '(bind scale' + str(day) + ' (* 1.0 0.54))'
    new = '(bind scale' + str(day) + ' (* 1.0 ' + str(scale) + '))'
    assert text.count(old) + text.count(new) == 1
    text = text.replace(old, new, 1)
source.write_text(text, encoding='utf8')
report = {'status': 'TWO_UNIFORM_SCALE_LITERALS_COMPILED_SAVED_NATIVE_PROJECTION_PENDING',
          'changes': changes, 'validation': 'Independent nine-pose design projection equal-width candidate. No X stretch, camera, motion or input changes.',
          'graph_diff': list(difflib.unified_diff(before_path.read_text(encoding='utf8').splitlines(), after.splitlines(), lineterm=''))}
(out / 'v09_family_uniform_fit_report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report, indent=2))
