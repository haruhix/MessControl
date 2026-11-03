"""Bounded real Settings navigation via Epic Slate MCP; no Apply and no microphone capture."""
import json,re,sys,time,shutil
from pathlib import Path
sys.path.insert(0,'E:/DEVGAME/MessControl/Tools/Unreal')
sys.path.insert(0,'E:/UE/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
from native_mcp_client import NativeMCP
import remote_execution as remote
out=Path('E:/CodexWork/MessControl/Validation/UISettingsTeam_20261103')
c=NativeMCP();S='SlateInspectorToolset.SlateInspectorToolset'
def call(name,args=None):
    result=c.tool('call_tool',{'toolset_name':S,'tool_name':name,'arguments':args or {}})
    if result.get('isError'):raise RuntimeError(result)
    return json.loads(result['content'][0]['text'])['returnValue']
session=remote.RemoteExecution()
session.start()
deadline=time.monotonic()+8
while time.monotonic()<deadline:
    nodes=[n for n in session.remote_nodes if n.get('project_name')=='MessControl']
    if len(nodes)==1:break
    time.sleep(.2)
assert len(nodes)==1
session.open_command_connection(nodes[0]['node_id'])
def probe(name):
    session.run_command('E:/DEVGAME/MessControl/Tools/Unreal/UI/probe_unified_settings_team.py',unattended=True,raise_on_failure=True)
    data=json.loads((out/'LiveLatest.json').read_text(encoding='utf-8'))
    for ext in ('json','png'):shutil.copy2(out/('LiveLatest.'+ext),out/(name+'.'+ext))
    return data
def snapshot():
    return call('Snapshot',{'ref':window,'maxDepth':45,'bIncludeSourceLocations':False})
def buttons(text):
    return [(label,ref) for label,ref in re.findall(r'button "([^"]+)"[^\n]*\[ref=(b\d+)\]',text)]
def click(label):
    for attempt in range(20):
        found=[ref for name,ref in buttons(snapshot()) if name==label]
        if len(found)==1:
            assert call('Click',{'ref':found[0]});time.sleep(.15);return
        time.sleep(.2)
    raise RuntimeError('Expected one visible '+label)
observer=call('Observe',{'ref':'','maxDepth':45})
try:
    text=call('Snapshot',{'ref':'','maxDepth':45,'bIncludeSourceLocations':False})
    windows=re.findall(r'^window .*NetMode: Standalone[^\n]*\[ref=(w\d+)\]',text,re.M)
    assert len(windows)==1
    window=windows[0]
    deadline=time.monotonic()+60
    while time.monotonic()<deadline:
        current=snapshot()
        if 'Подготавливаем локацию' not in current and 'Подготовка' not in current and '[focused]' in current:
            if any(label in ('Настройки','Графика') for label,_ in buttons(current)):break
        time.sleep(.2)
    else:raise RuntimeError('Menu readiness/focus not reached')
    if not any(label=='Графика' for label,_ in buttons(snapshot())):click('Настройки')
    r=probe('Adaptive_Graphics')
    if r['controls']['InterfaceScreens']['page']!=7:
        call('PressKey',{'key':'Enter'});r=probe('Adaptive_Graphics')
    assert r['controls']['InterfaceScreens']['page']==7
    session.run_command("__import__('unreal').SystemLibrary.collect_garbage()",exec_mode=remote.MODE_EXEC_STATEMENT,unattended=True,raise_on_failure=True)
    results=[]
    for label,index,name in [('Язык',2,'Adaptive_Language'),('Управление',3,'Adaptive_Controls'),('Голос',4,'Adaptive_Voice'),('Аудио',1,'Adaptive_Audio'),('Графика',0,'Adaptive_Graphics_Final')]:
        click(label)
        data=probe(name)
        if data['controls']['MenuSettings_Sections']['page']!=index:
            call('PressKey',{'key':'Enter'});data=probe(name)
        assert data['controls']['MenuSettings_Sections']['page']==index,data
        assert data['controls']['SettingsV2Sections']['page']==index,data
        results.append(name)
        print(name+': PASS',flush=True)
    session.run_command("__import__('unreal').SystemLibrary.collect_garbage()",exec_mode=remote.MODE_EXEC_STATEMENT,unattended=True,raise_on_failure=True)
    click('Язык');probe('Adaptive_Language_AfterGC')
    click('Графика')
    report={'passed':True,'tabs':5,'forcedGC':2,'input':'Epic native pointer with focused Enter fallback; hardware mouse unverified','applyClicked':False,'microphoneCaptureStarted':False,'evidence':results}
    (out/'SettingsSoloAdaptive.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))
finally:
    session.stop()
    call('Unobserve',{'identifier':observer})
