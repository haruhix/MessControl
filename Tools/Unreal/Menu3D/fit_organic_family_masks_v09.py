"""Replace only the four old box-interior mask denominator literals."""
import difflib
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP

assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
out = Path(__file__).resolve().parent / 'Evidence'
report_path = out / 'v09_organic_family_mask_report.json'
assert not report_path.exists()
widget = task_asset('WBP_MainMenu3D')
graph = call(BP, 'get_graph', blueprint=widget, graph_name='UpdateStage')
before = call(BP, 'read_graph_dsl', graph=graph)
(out / 'v09_organic_family_mask_before.dsl').write_text(before, encoding='utf8')
setters = call(BP, 'get_node_infos', nodes=call(BP, 'find_nodes', graph=graph, title='Scalar'))
assert len(setters) == 6
changes = []
for setter in setters:
    assert setter['type_id'] == 'Rendering|Material|SetScalarParameterValueOnMaterials'
    name = next(p for p in setter['input_pins'] if p['name'] == 'ParameterName')
    assert name['value'] == 'CardHalfHeight' and not name['connected_pins']
    value = next(p for p in setter['input_pins'] if p['name'] == 'ParameterValue')
    division = call(BP, 'get_node_infos', nodes=[value['connected_pins'][0]['node']])[0]
    assert division['type_id'] == 'Math|Float|float/float'
    denominator = next(p for p in division['input_pins'] if p['name'] == 'B')
    multiply = call(BP, 'get_node_infos', nodes=[denominator['connected_pins'][0]['node']])[0]
    assert multiply['type_id'] == 'Math|Float|float*float'
    factor = next(p for p in multiply['input_pins'] if p['name'] == 'A')
    literal = call(BP, 'get_node_infos', nodes=[factor['connected_pins'][0]['node']])[0]
    assert literal['type_id'] == 'Math|Float|MakeLiteralFloat'
    pin = next(p for p in literal['input_pins'] if p['name'] == 'Value')
    previous = float(pin['value'])
    assert min(abs(previous-2.), abs(previous-2.6666666666666665)) < 1e-7
    if abs(previous-2.) < 1e-7:
        call(BP, 'set_pin_value', pin=pin['pin_id'], value='2.6666666666666665')
        changes.append({'setter': setter['node'], 'literal': literal['node'], 'previous': previous,
                        'new': 2.6666666666666665})
assert len(changes) == 4
call(BP, 'compile_blueprint', blueprint=widget, warnings_as_errors=True)
after = call(BP, 'read_graph_dsl', graph=graph)
(out / 'v09_organic_family_mask_after.dsl').write_text(after, encoding='utf8')
assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / 'remote_python.py'),
                str(Path(__file__).resolve().parent / 'recover_v09_editor_checkpoint.py')], check=True)
source = out / 'accordion_UpdateStage.dsl'
text = source.read_text(encoding='utf8')
for day in (1, 2):
    old = '(* 2.0 scale' + str(day) + ')'
    assert text.count(old) == 2
    text = text.replace(old, '(* 2.6666666666666665 scale' + str(day) + ')')
source.write_text(text, encoding='utf8')
report = {'status': 'FOUR_ORGANIC_MASK_LITERALS_COMPILED_SAVED_NATIVE_FIT_PENDING',
          'changes': changes, 'formula': 'height/(2.6666666666666665*dayScale)',
          'graph_diff': list(difflib.unified_diff(before.splitlines(), after.splitlines(), lineterm=''))}
report_path.write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report, indent=2))
