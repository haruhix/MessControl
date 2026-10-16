"""Commit all four mouth morph overrides before dependent visuals are drawn.

OverrideAnimationData(None,false,false,0,1) creates a no-asset SingleNode
evaluator once, then synchronously ticks/evaluates the current morph snapshot.
The generic mouth driver does not process character actors.
"""
import difflib
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP

assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
out=Path(__file__).resolve().parent/'Evidence'
report_path=out/'v09_mouth_morph_snapshot_report.json'
assert not report_path.exists()
widget=task_asset('WBP_MainMenu3D')
graph=call(BP,'get_graph',blueprint=widget,graph_name='DriveMouthDay')
before=call(BP,'read_graph_dsl',graph=graph)
(out/'v09_mouth_morph_snapshot_before.dsl').write_text(before,encoding='utf8')

cache={}
def info(ref):
    if ref['refPath'] not in cache:cache[ref['refPath']]=call(BP,'get_node_infos',nodes=[ref])[0]
    return cache[ref['refPath']]
def pin(n,name,output=False):return next(p for p in n['output_pins' if output else 'input_pins'] if p['name']==name)
nodes=[info(ref) for ref in call(BP,'find_nodes',graph=graph,title='Morph')]
nodes=[n for n in nodes if n['type_id'].endswith('|SetMorphTarget')]
assert len(nodes)>=4
def reaches_entry(node,visited=None):
    visited=set() if visited is None else visited
    key=node['node']['refPath']
    if 'K2Node_FunctionEntry' in key:return True
    if key in visited:return False
    visited.add(key)
    return any(reaches_entry(info(link['node']),visited) for p in node['input_pins'] if p['type_id']=='Exec' for link in p['connected_pins'])
last_candidates=[n for n in nodes if pin(n,'MorphTargetName')['value']=='Pose_075' and reaches_entry(n)]
assert len(last_candidates)==1,'Only the entry-reachable morph chain may change; preserve orphan historical nodes'
last=last_candidates[0]
previous_output=pin(last,'then',True)
assert len(previous_output['connected_pins'])==1
next_input=previous_output['connected_pins'][0]
next_node=info(next_input['node'])
assert next_node['type_id'].endswith('|SetScalarParameterValueOnMaterials')
assert pin(next_node,'ParameterName')['value']=='OpenAmount'
assert len(pin(last,'self')['connected_pins'])==1
type_id='Components|Animation|OverrideAnimationData'
call(BP,'get_node_type_pins',graph=graph,type_id=type_id)
position=last['position']
node=info(call(BP,'create_node',graph=graph,type_id=type_id,pos={'x':position['x']+260,'y':position['y']+110}))
assert pin(node,'InAnimToPlay')['value']==''
for name,value in [('bIsLooping','false'),('bIsPlaying','false'),('Position','0.0'),('PlayRate','1.0')]:
    call(BP,'set_pin_value',pin=pin(node,name)['pin_id'],value=value)
call(BP,'connect_pins',output_pin=pin(last,'self')['connected_pins'][0],input_pin=pin(node,'self')['pin_id'])
call(BP,'break_pins',output_pin=previous_output['pin_id'],input_pin=next_input)
call(BP,'connect_pins',output_pin=previous_output['pin_id'],input_pin=pin(node,'execute')['pin_id'])
call(BP,'connect_pins',output_pin=pin(node,'then',True)['pin_id'],input_pin=next_input)
assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
call(BP,'compile_blueprint',blueprint=widget,warnings_as_errors=True)
after=call(BP,'read_graph_dsl',graph=graph)
(out/'v09_mouth_morph_snapshot_after.dsl').write_text(after,encoding='utf8')
subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'remote_python.py'),
                str(Path(__file__).resolve().parent/'recover_v09_editor_checkpoint.py')],check=True)
source_path=out/'accordion_DriveMouthDay.dsl'
source=source_path.read_text(encoding='utf8')
marker='        (Rendering|Material|SetScalarParameterValueonMaterials :self mesh :ParameterName "OpenAmount"'
assert source.count(marker)==1
source=source.replace(marker,'        (Components|Animation|OverrideAnimationData :self mesh :bIsLooping false :bIsPlaying false :Position 0.0 :PlayRate 1.0)\n'+marker,1)
source_path.write_text(source,encoding='utf8')
report={'status':'MOUTH_MORPH_SNAPSHOT_COMPILED_SAVED_NATIVE_REVIEW_PENDING',
        'scope':'One node in DriveMouthDay after Pose_075, before material parameters; no character driver changes',
        'node':node['node'],'previous_output':previous_output['pin_id'],'next_input':next_input,
        'rollback':'Disconnect and delete new node, reconnect previous_output to next_input, compile/save widget',
        'source_basis':['SkeletalMeshComponent.cpp4433-4448 OverrideAnimationData ticks/evaluates synchronously',
                        'SkeletalMeshComponent.cpp4264 same SingleNode does not recreate instance',
                        'SkeletalMeshComponent.cpp3276-3302 morph overrides applied during PostAnimEvaluation'],
        'graph_diff':list(difflib.unified_diff(before.splitlines(),after.splitlines(),lineterm=''))}
report_path.write_text(json.dumps(report,indent=2),encoding='utf8')
print(report['status'])
