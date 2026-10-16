"""Remove the rejected direct-evaluation candidate without rewriting the graph."""
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP
assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
out=Path(__file__).resolve().parent/'Evidence'
report=json.loads((out/'v09_mouth_morph_snapshot_report.json').read_text(encoding='utf8'))
node=call(BP,'get_node_infos',nodes=[report['node']])[0]
def pin(name,output=False):return next(p for p in node['output_pins' if output else 'input_pins'] if p['name']==name)['pin_id']
call(BP,'break_pins',output_pin=report['previous_output'],input_pin=pin('execute'))
call(BP,'break_pins',output_pin=pin('then',True),input_pin=report['next_input'])
call(BP,'connect_pins',output_pin=report['previous_output'],input_pin=report['next_input'])
call(BP,'delete_node',node=report['node'])
call(BP,'compile_blueprint',blueprint=task_asset('WBP_MainMenu3D'),warnings_as_errors=True)
subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'remote_python.py'),str(Path(__file__).resolve().parent/'recover_v09_editor_checkpoint.py')],check=True)
source=out/'accordion_DriveMouthDay.dsl'
code=source.read_text(encoding='utf8')
line='        (Components|Animation|OverrideAnimationData :self mesh :bIsLooping false :bIsPlaying false :Position 0.0 :PlayRate 1.0)\n'
assert code.count(line)==1
source.write_text(code.replace(line,''),encoding='utf8')
(out/'v09_mouth_morph_snapshot_rollback.json').write_text(json.dumps({'status':'REJECTED_CANDIDATE_REMOVED_COMPILED_SAVED','reason':'Actual10 command boundary still mismatched; preserve05 shell fix only','node_removed':report['node']},indent=2),encoding='utf8')
print('REJECTED_MORPH_SNAPSHOT_ROLLED_BACK')
