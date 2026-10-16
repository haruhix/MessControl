"""Enforce one stage-update owner without rebuilding the menu EventGraph.

The live WBP and PC both contained connected EventTick -> UpdateStage paths.
The intended owner is the PlayerController, before mouth mesh component ticks.
"""
import hashlib
import json
from pathlib import Path
from menu_native_client import call, ref, task_asset, BP, ASSET, BASE

out = Path(__file__).resolve().parent / 'Evidence'
assert not call('EditorToolset.EditorAppToolset', 'IsPIERunning')
widget = task_asset('WBP_MainMenu3D')
controller = task_asset('BP_Menu3DPlayerController')
graph = call(BP, 'get_graph', blueprint=widget, graph_name='EventGraph')
pc_graph = call(BP, 'get_graph', blueprint=controller, graph_name='EventGraph')
before_dsl = call(BP, 'read_graph_dsl', graph=graph)
pc_before = call(BP, 'read_graph_dsl', graph=pc_graph)
assert 'UpdateStage' in pc_before and 'EventTick' in pc_before
assert not (out / 'v08_tick_owner_before.dsl').exists(), 'Preserve previous attempt'
(out / 'v08_tick_owner_before.dsl').write_text(before_dsl, encoding='utf8')
protected = ('UpdateStage', 'DriveMouthDay', 'SelectDay', 'StepDay', 'Back', 'OpenConnection', 'UpdatePointerIntent')
graphs = {name: call(BP, 'get_graph', blueprint=widget, graph_name=name) for name in protected}
fingerprints = {name: hashlib.sha256(call(BP, 'read_graph_dsl', graph=g).encode()).hexdigest()
                for name, g in graphs.items()}
nodes = [ref(BASE + '/WBP_MainMenu3D.WBP_MainMenu3D:EventGraph.K2Node_Event_2'),
         ref(BASE + '/WBP_MainMenu3D.WBP_MainMenu3D:EventGraph.K2Node_CallFunction_174')]
info = call(BP, 'get_node_infos', nodes=nodes)
event, update = info
assert event['type_id'].endswith('UserInterface|EventTick'), event
assert update['type_id'].endswith('|UpdateStage'), update
exec_pin = next(p for p in event['output_pins'] if p['type_id'] == 'Exec')
assert len(exec_pin['connected_pins']) == 1 and exec_pin['connected_pins'][0]['node'] == nodes[1]
assert not any(p['connected_pins'] for p in update['output_pins'])
# Remove both the legacy event and its terminal call. Graph DSL writes may leave
# old events absent from their source, so merely omitting the event is not enough.
call(BP, 'delete_node', node=nodes[1])
call(BP, 'delete_node', node=nodes[0])
call(BP, 'compile_blueprint', blueprint=widget, warnings_as_errors=True)
after_dsl = call(BP, 'read_graph_dsl', graph=graph)
assert 'UserInterface|EventTick' not in after_dsl
assert pc_before == call(BP, 'read_graph_dsl', graph=pc_graph)
assert fingerprints == {name: hashlib.sha256(call(BP, 'read_graph_dsl', graph=g).encode()).hexdigest()
                        for name, g in graphs.items()}
call(ASSET, 'save_assets', asset_paths=[BASE + '/WBP_MainMenu3D'])
(out / 'v08_tick_owner_after.dsl').write_text(after_dsl, encoding='utf8')
report = {'status': 'SINGLE_PC_STAGE_OWNER_COMPILED_SAVED_PENDING_SOLO_PIE',
          'removed_connected_nodes': info, 'protected_graph_hashes': fingerprints,
          'controller_graph_unchanged': True, 'compiled_warnings_as_errors': True,
          'runtime_limit': 'Original compiled double advancement was not instrumented; two connected source paths were observed. New timing/readability requires native review.'}
(out / 'v08_tick_owner_report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report, indent=2))
