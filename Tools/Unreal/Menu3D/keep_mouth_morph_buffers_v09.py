"""Candidate: retain jaw render objects and warm only their closed-pose buffers.

Logical hidden rows use their existing masked material, with half-height -1.
No global render CVar, animation-mode change, or character driver is involved.
Pose000 has a bounded 1e-5 relative alternation to initialize both GPU buffers.
The journal records every edited input so the candidate can be rolled back.
"""
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP

assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
out=Path(__file__).resolve().parent/'Evidence'
receipt=out/'v09_morph_buffer_keepalive_candidate.json'
assert not receipt.exists()
widget=task_asset('WBP_MainMenu3D')
graph=call(BP,'get_graph',blueprint=widget,graph_name='DriveMouthDay')
before=call(BP,'read_graph_dsl',graph=graph)
(out/'v09_morph_buffer_keepalive_before.dsl').write_text(before,encoding='utf8')
cache={}
def info(ref):
    key=ref['refPath']
    if key not in cache:cache[key]=call(BP,'get_node_infos',nodes=[ref])[0]
    return cache[key]
def pin(n,name,output=False):return next(p for p in n['output_pins' if output else 'input_pins'] if p['name']==name)
def reaches_entry(node,visited=None):
    visited=set() if visited is None else visited
    key=node['node']['refPath']
    if 'K2Node_FunctionEntry' in key:return True
    if key in visited:return False
    visited.add(key)
    return any(reaches_entry(info(link['node']),visited) for p in node['input_pins'] if p['type_id']=='Exec' for link in p['connected_pins'])
def active(title,suffix):
    nodes=[info(r) for r in call(BP,'find_nodes',graph=graph,title=title)]
    return [n for n in nodes if n['type_id'].endswith(suffix) and reaches_entry(n)]
actor,=active('Hidden','|SetActorHiddenInGame')
component,=active('Hidden','|SetHiddenInGame')
pose,=[n for n in active('Morph','|SetMorphTarget') if pin(n,'MorphTargetName')['value']=='Pose_000']
mask,=[n for n in active('Scalar','|SetScalarParameterValueOnMaterials') if pin(n,'ParameterName')['value']=='CardHalfHeight']
hidden_source=pin(actor,'bNewHidden')['connected_pins']
assert len(hidden_source)==1
assert pin(component,'NewHidden')['connected_pins']==hidden_source
original_pose=pin(pose,'Value')['connected_pins'];assert len(original_pose)==1
original_half=pin(mask,'ParameterValue')['connected_pins'];assert len(original_half)==1
journal={'status':'PREPARED','graph':graph,'inputs':[],'created_nodes':[],
    'scope':'Jaw driver only. Cavity/tongue own-jaw shell and character drivers retained.',
    'closed_pose_weight':'canonical Pose000 * lerp(.99999,1,float(GetFrameCount()%2))',
    'hidden_policy':'Actor/component visible; logical Hidden selects CardHalfHeight=-1, clipping all material passes.'}
def persist():receipt.write_text(json.dumps(journal,indent=2),encoding='utf8')
def replace(p,source=None,value=None):
    journal['inputs'].append({'pin':p['pin_id'],'previous_connections':p['connected_pins'],'previous_value':p['value']})
    persist()
    for link in p['connected_pins']:call(BP,'break_pins',output_pin=link,input_pin=p['pin_id'])
    if source:call(BP,'connect_pins',output_pin=source,input_pin=p['pin_id'])
    else:call(BP,'set_pin_value',pin=p['pin_id'],value=value)
def new(type_id,x,y):
    n=info(call(BP,'create_node',graph=graph,type_id=type_id,pos={'x':x,'y':y}))
    journal['created_nodes'].append(n['node']);persist();return n
def wire(a,b):call(BP,'connect_pins',output_pin=a,input_pin=b)
def value(n,name,v):call(BP,'set_pin_value',pin=pin(n,name)['pin_id'],value=str(v))
persist()
try:
    frame=new('Utilities|GetFrameCount',-900,1900)
    parity=new('Math|Integer64|%(integer64)',-700,1900);value(parity,'B',2)
    cast=new('Math|Conversions|ToFloat(Integer64)',-500,1900)
    factor=new('Math|Float|Lerp',-300,1900);value(factor,'A',.99999);value(factor,'B',1.)
    product=new('Math|Float|Lerp',-100,1900);value(product,'A',0.)
    clip=new('Math|Float|SelectFloat',300,1900);value(clip,'A',-1.)
    wire(frame['output_pins'][0]['pin_id'],pin(parity,'A')['pin_id'])
    wire(parity['output_pins'][0]['pin_id'],cast['input_pins'][0]['pin_id'])
    wire(cast['output_pins'][0]['pin_id'],pin(factor,'Alpha')['pin_id'])
    wire(original_pose[0],pin(product,'B')['pin_id'])
    wire(factor['output_pins'][0]['pin_id'],pin(product,'Alpha')['pin_id'])
    wire(original_half[0],pin(clip,'B')['pin_id'])
    wire(hidden_source[0],pin(clip,'bPickA')['pin_id'])
    replace(pin(actor,'bNewHidden'),value='false')
    replace(pin(component,'NewHidden'),value='false')
    # The existing conditional may call SetHidden(false) on logically hidden
    # rows. This is idempotent; it does not recreate an already visible proxy.
    replace(pin(pose,'Value'),source=product['output_pins'][0]['pin_id'])
    replace(pin(mask,'ParameterValue'),source=clip['output_pins'][0]['pin_id'])
    call(BP,'compile_blueprint',blueprint=widget,warnings_as_errors=True)
    subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'remote_python.py'),
        str(Path(__file__).resolve().parent/'recover_v09_editor_checkpoint.py')],check=True)
    journal['status']='COMPILED_SAVED_NATIVE_CANDIDATE_REVIEW_PENDING'
    (out/'v09_morph_buffer_keepalive_after.dsl').write_text(call(BP,'read_graph_dsl',graph=graph),encoding='utf8')
    persist();print(journal['status'])
except Exception:
    journal['status']='INTERRUPTED_REQUIRES_JOURNAL_ROLLBACK';persist();raise
