"""Keep cavity clipping tied to its jaw rather than neighboring UI row height."""
import difflib
import json
from pathlib import Path
import subprocess
import sys
from menu_native_client import call, task_asset, BP

assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
out=Path(__file__).resolve().parent/'Evidence'
report_path=out/'v09_interior_own_jaw_mask_report.json'
assert not report_path.exists()
widget=task_asset('WBP_MainMenu3D')
graph=call(BP,'get_graph',blueprint=widget,graph_name='UpdateStage')
before=call(BP,'read_graph_dsl',graph=graph)
(out/'v09_interior_own_jaw_mask_before.dsl').write_text(before,encoding='utf8')
cache={}
def info(ref):
    key=ref['refPath']
    if key not in cache:cache[key]=call(BP,'get_node_infos',nodes=[ref])[0]
    return cache[key]
def pin(n,name):return next(p for p in n['input_pins'] if p['name']==name)
def upstream(p):
    assert len(p['connected_pins'])==1
    return info(p['connected_pins'][0]['node'])
def disconnect(p):call(BP,'break_pins',output_pin=p['connected_pins'][0],input_pin=p['pin_id'])
setters=[info(r) for r in call(BP,'find_nodes',graph=graph,title='Scalar')]
assert len(setters)==6
plans=[]
for setter in setters:
    assert pin(setter,'ParameterName')['value']=='CardHalfHeight'
    inverse=upstream(pin(setter,'ParameterValue'))
    assert inverse['type_id']=='Math|Float|SafeDivide'
    lerp=upstream(pin(inverse,'B'))
    assert lerp['type_id']=='Math|Float|Lerp'
    assert float(pin(lerp,'A')['value'])==.5 and float(pin(lerp,'B')['value'])==1.
    division=upstream(pin(inverse,'A'))
    assert division['type_id']=='Math|Float|float/float'
    assert len(pin(division,'A')['connected_pins'])==1
    plans.append((setter,division))
for setter,division in plans:
    disconnect(pin(division,'A'))
    call(BP,'set_pin_value',pin=pin(division,'A')['pin_id'],value='370.0')
    disconnect(pin(setter,'ParameterValue'))
    call(BP,'connect_pins',output_pin=division['output_pins'][0]['pin_id'],input_pin=pin(setter,'ParameterValue')['pin_id'])
assert not call('EditorToolset.EditorAppToolset','IsPIERunning')
call(BP,'compile_blueprint',blueprint=widget,warnings_as_errors=True)
after=call(BP,'read_graph_dsl',graph=graph)
(out/'v09_interior_own_jaw_mask_after.dsl').write_text(after,encoding='utf8')
subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'remote_python.py'),
                str(Path(__file__).resolve().parent/'recover_v09_editor_checkpoint.py')],check=True)
source_path=out/'accordion_UpdateStage.dsl'
source=source_path.read_text(encoding='utf8')
for day in (1,2,3):
    old='(/ (select isStage height'+str(day)+' 370.0) (* (* 2.6666666666666665 scale'+str(day)+') (+ 0.5 (* 0.5 open'+str(day)+'))))'
    assert source.count(old)==2
    source=source.replace(old,'(/ 370.0 (* 2.6666666666666665 scale'+str(day)+'))')
source_path.write_text(source,encoding='utf8')
report={'status':'OWN_JAW_MASK_COMPILED_SAVED_NATIVE_VISUAL_PENDING',
        'scope':'Six cavity/tongue mask inputs only; relative Z=.5+.5*ownOpen and row-only visibility retained',
        'local_half_height':'370/(2.6666666666666665*parentUniformScale)',
        'world_mask_height':'277.5*(.5+.5*ownOpen), never exceeds .75*rowHeight when sumOpen<=1',
        'reason':'All-closed jaws must use the same inner geometry/mask as closed neighbors of an open day',
        'graph_diff':list(difflib.unified_diff(before.splitlines(),after.splitlines(),lineterm=''))}
report_path.write_text(json.dumps(report,indent=2),encoding='utf8')
print(report['status'])
