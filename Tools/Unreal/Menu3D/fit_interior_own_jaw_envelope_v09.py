"""Fit shell depth to its own jaw opening, independent of neighboring rows.

The conservative .5 closed scale encloses measured inner-gum landmarks. Local
mask height compensates for component scale to retain the existing row bounds.
"""
import difflib
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP

assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
out=Path(__file__).resolve().parent/'Evidence'
report_path=out/'v09_interior_own_jaw_envelope_report.json'
assert not report_path.exists()
widget=task_asset('WBP_MainMenu3D')
graph=call(BP,'get_graph',blueprint=widget,graph_name='UpdateStage')
before=call(BP,'read_graph_dsl',graph=graph)
(out/'v09_interior_own_jaw_before.dsl').write_text(before,encoding='utf8')
cache={}

def info(ref):
    key=ref['refPath']
    if key not in cache:cache[key]=call(BP,'get_node_infos',nodes=[ref])[0]
    return cache[key]

def pin(node,name,output=False):
    return next(p for p in node['output_pins' if output else 'input_pins'] if p['name']==name)

def upstream(p):
    assert len(p['connected_pins'])==1
    return info(p['connected_pins'][0]['node'])

def connect(source,target):
    call(BP,'connect_pins',output_pin=source,input_pin=target['pin_id'])

def disconnect(target):
    assert len(target['connected_pins'])==1
    call(BP,'break_pins',output_pin=target['connected_pins'][0],input_pin=target['pin_id'])

def create(type_id,position):
    return info(call(BP,'create_node',graph=graph,type_id=type_id,pos=position))

setters=[info(ref) for ref in call(BP,'find_nodes',graph=graph,title='Scalar')]
assert len(setters)==6
getters={}
for day in (1,2,3):
    matches=[info(ref) for ref in call(BP,'find_nodes',graph=graph,title='Open'+str(day))]
    matches=[n for n in matches if n['type_id'].endswith('|GetOpen'+str(day))]
    assert matches
    getters[day]=matches[0]
plans=[]
days={'InteriorActors':3,'TongueActors':3,'Interior1Actors':1,'Tongue1Actors':1,'Interior2Actors':2,'Tongue2Actors':2}
for setter in setters:
    assert pin(setter,'ParameterName')['value']=='CardHalfHeight'
    scale=upstream(pin(setter,'execute'))
    assert scale['type_id']=='Transformation|SetRelativeScale3D'
    vector=upstream(pin(scale,'NewScale3D'))
    ratio=upstream(pin(vector,'Z'))
    assert ratio['type_id']=='Math|Float|SafeDivide' and pin(ratio,'B')['value']=='370.0'
    division=upstream(pin(setter,'ParameterValue'))
    assert division['type_id']=='Math|Float|float/float'
    assert not pin(division,'A')['connected_pins'] and float(pin(division,'A')['value'])==370.
    height=pin(ratio,'A')['connected_pins'][0]
    cast=upstream(pin(scale,'execute'))
    loop=upstream(pin(cast,'Object'))
    array=upstream(pin(loop,'Array'))
    day=days[array['type_id'].split('|Get')[-1]]
    plans.append(dict(day=day,setter=setter,vector=vector,division=division,height=height))
assert sorted(p['day'] for p in plans)==[1,1,2,2,3,3]
for type_id in ('Math|Float|Lerp','Math|Float|SafeDivide'):
    call(BP,'get_node_type_pins',graph=graph,type_id=type_id)
lerps={}
changes=[]
for plan in plans:
    vector=plan['vector'];key=vector['node']['refPath'];day=plan['day']
    if key not in lerps:
        pos=vector['position']
        lerp=create('Math|Float|Lerp',{'x':pos['x']-280,'y':pos['y']+210})
        call(BP,'set_pin_value',pin=pin(lerp,'A')['pin_id'],value='0.5')
        call(BP,'set_pin_value',pin=pin(lerp,'B')['pin_id'],value='1.0')
        connect(getters[day]['output_pins'][0]['pin_id'],pin(lerp,'Alpha'))
        disconnect(pin(vector,'Z'))
        connect(lerp['output_pins'][0]['pin_id'],pin(vector,'Z'))
        lerps[key]=(day,lerp)
    assert lerps[key][0]==day
    lerp=lerps[key][1]
    connect(plan['height'],pin(plan['division'],'A'))
    pos=plan['setter']['position']
    inverse=create('Math|Float|SafeDivide',{'x':pos['x']-250,'y':pos['y']+250})
    connect(plan['division']['output_pins'][0]['pin_id'],pin(inverse,'A'))
    connect(lerp['output_pins'][0]['pin_id'],pin(inverse,'B'))
    disconnect(pin(plan['setter'],'ParameterValue'))
    connect(inverse['output_pins'][0]['pin_id'],pin(plan['setter'],'ParameterValue'))
    changes.append({'day':day,'relativeZ':'.5+.5*ownOpen','localHalfHeight':'rowHeight/(2.6666666666666665*parentUniformScale*relativeZ)',
                    'visibility':'Row hidden only; no openness cutoff'})
assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
call(BP,'compile_blueprint',blueprint=widget,warnings_as_errors=True)
after=call(BP,'read_graph_dsl',graph=graph)
(out/'v09_interior_own_jaw_after.dsl').write_text(after,encoding='utf8')
subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'remote_python.py'),
                str(Path(__file__).resolve().parent/'recover_v09_editor_checkpoint.py')],check=True)
source_path=out/'accordion_UpdateStage.dsl'
source=source_path.read_text(encoding='utf8')
for day in (1,2,3):
    old='(/ (select isStage height'+str(day)+' 370.0) 370.0)'
    z='(+ 0.5 (* 0.5 open'+str(day)+'))'
    assert source.count(old)==2
    source=source.replace(old,z)
    old='(/ 370.0 (* 2.6666666666666665 scale'+str(day)+'))'
    assert source.count(old)==2
    source=source.replace(old,'(/ (select isStage height'+str(day)+' 370.0) (* (* 2.6666666666666665 scale'+str(day)+') '+z+'))')
source_path.write_text(source,encoding='utf8')
report={'status':'OWN_JAW_ENVELOPE_COMPILED_SAVED_NATIVE_VISUAL_PENDING','changes':changes,
        'rejected_previous_driver':'rowHeight/370 over-covered all-closed corners with .667 scale',
        'graph_diff':list(difflib.unified_diff(before.splitlines(),after.splitlines(),lineterm=''))}
report_path.write_text(json.dumps(report,indent=2),encoding='utf8')
print(report['status'])
