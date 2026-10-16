"""Use the existing collapse-then-Main route for the day-screen footer Back."""
import json
from pathlib import Path
from menu_native_client import call, ref, task_asset, BP, ASSET, BASE

assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
out = Path(__file__).resolve().parent / 'Evidence'
assert not (out / 'v08_footer_back_report.json').exists(), 'Preserve previous evidence'
widget = task_asset('WBP_MainMenu3D')
graph = call(BP, 'get_graph', blueprint=widget, graph_name='EventGraph')
prefix = BASE + '/WBP_MainMenu3D.WBP_MainMenu3D:EventGraph.'
event_ref = ref(prefix + 'K2Node_ComponentBoundEvent_11')
old_ref = ref(prefix + 'K2Node_CallFunction_233')
before = call(BP, 'get_node_infos', nodes=[event_ref, old_ref])
event, old = before
assert 'DaysBackButton' in event['type_id'], event
assert old['type_id'].endswith('|ShowPage'), old
assert next(p for p in old['input_pins'] if p['name'] == 'Index')['value'] == '0'
exec_out = next(p for p in event['output_pins'] if p['type_id'] == 'Exec')
assert len(exec_out['connected_pins']) == 1 and exec_out['connected_pins'][0]['node'] == old_ref
assert not any(p['connected_pins'] for p in old['output_pins'])
call(BP, 'get_node_type_pins', graph=graph, type_id='CallFunction|Back')
new_ref = call(BP, 'create_node', graph=graph, type_id='CallFunction|Back', pos=old['position'])
new = call(BP, 'get_node_infos', nodes=[new_ref])[0]
exec_in = next(p for p in new['input_pins'] if p['type_id'] == 'Exec')
call(BP, 'break_pins', output_pin=exec_out['pin_id'], input_pin=exec_out['connected_pins'][0])
call(BP, 'connect_pins', output_pin=exec_out['pin_id'], input_pin=exec_in['pin_id'])
call(BP, 'delete_node', node=old_ref)
call(BP, 'compile_blueprint', blueprint=widget, warnings_as_errors=True)
after = call(BP, 'get_node_infos', nodes=[event_ref, new_ref])
assert after[1]['type_id'].endswith('|Back')
assert next(p for p in after[0]['output_pins'] if p['type_id'] == 'Exec')['connected_pins'][0]['node'] == new_ref
call(ASSET, 'save_assets', asset_paths=[BASE + '/WBP_MainMenu3D'])
source = out / 'accordion_EventGraph.dsl'
text = source.read_text(encoding='utf8')
old_code = '(event OnClicked(DaysBackButton)\n  (CallFunction|ShowPage :Index 0)\n)'
new_code = '(event OnClicked(DaysBackButton)\n  (CallFunction|Back)\n)'
assert old_code in text
source.write_text(text.replace(old_code, new_code, 1), encoding='utf8')
report = {'status': 'FOOTER_BACK_USES_EXISTING_BACK_COMPILED_SAVED_PENDING_SOLO_PIE',
          'before': before, 'after': after, 'compiled_warnings_as_errors': True,
          'behavior': 'On day screen, first Back collapses active mouth; next Back returns Main. Existing focus/pointer quiet logic is reused.'}
(out / 'v08_footer_back_report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(report['status'])
