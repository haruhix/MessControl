"""Bounded solo pause/settings/F3 check. Does not Apply or save game settings."""
import json,sys,time,subprocess,shutil
from pathlib import Path
sys.dont_write_bytecode=True
P=Path('E:/DEVGAME/MessControl');O=Path('E:/CodexWork/MessControl/Validation/UIHubReward_20261104')
sys.path.insert(0,'E:/UE/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
import remote_execution as R
C="""
import json
from pathlib import Path
import unreal as u
w=u.EditorLevelLibrary.get_pie_worlds(True)[0]
assert u.GameplayStatics.get_current_level_name(w,True)=='Hub'
pc=u.GameplayStatics.get_player_controller(w,0)
def obj(cls,viewport=False):
    return next(x for x in u.ObjectIterator(cls) if x.get_world()==w and x.get_owning_player()==pc and (not viewport or x.is_in_viewport()))
def tree(owner):return next(t for t in u.ObjectIterator(u.WidgetTree) if t.get_outer()==owner)
def find(owner,name):
    node=u.find_object(tree(owner),name);assert node,name;return node
def click(name):
    v=obj(u.MCUnifiedGameplayMenuWidget)
    shape=find(v,'GameplayMenuV2_'+name)
    find(shape,'UniversalSurface').on_clicked.broadcast()
def save():
    root=obj(u.MCUnifiedUIWidget,True)
    sources=[s for s in u.ObjectIterator(u.MCGameplayMenuWidget) if s.get_world()==w and s.get_owning_player()==pc and s.is_in_viewport()]
    d={'root':find(root,'InterfaceScreens').get_active_widget_index(),'source':None,'page':None,'vsync':None}
    if sources:
        s=sources[0];d.update(source=s.get_path_name(),page=find(s,'GameplayMenuPages').get_active_widget_index(),vsync=find(s,'MenuVSync').is_checked())
    Path('E:/CodexWork/MessControl/Validation/UIHubReward_20261104/GameMenuCurrent.json').write_text(json.dumps(d),encoding='utf-8')
"""
s=R.RemoteExecution();report={'passed':False,'input':'Actual saved UMG delegates and native Slate F3; hardware input not claimed','applied':False}
def run(code):
    return s.run_command('exec('+repr(C+'\n'+code)+', {})',exec_mode=R.MODE_EXEC_STATEMENT,unattended=True,raise_on_failure=True)
def state():
    run('save()');return json.loads((O/'GameMenuCurrent.json').read_text(encoding='utf-8'))
def wait(root,page):
    end=time.monotonic()+12
    while time.monotonic()<end:
        d=state()
        if d['root']==root and d['page']==page:return d
        time.sleep(.2)
    raise AssertionError(d)
def capture(name):
    code="from pathlib import Path\np="+repr(str(P/'Tools/Unreal/UI/probe_unified_hub_reward.py'))+"\nexec(compile(Path(p).read_text(encoding='utf-8'),p,'exec'),{'__name__':'__main__','__file__':p})"
    s.run_command('exec('+repr(code)+', {})',exec_mode=R.MODE_EXEC_STATEMENT,unattended=True,raise_on_failure=True)
    for ext in ('json','png'):shutil.copy2(O/('LiveLatest.'+ext),O/(name+'.'+ext))
try:
    s.start();end=time.monotonic()+8;nodes=[]
    while time.monotonic()<end:
        nodes=[n for n in s.remote_nodes if n.get('project_name')=='MessControl']
        if len(nodes)==1:break
        time.sleep(.2)
    assert len(nodes)==1;s.open_command_connection(nodes[0]['node_id'])
    assert state()['root']==2
    run('assert u.MCGameplayUIEditorLibrary.toggle_pie_pause_menu(w)')
    wait(11,0);run("click('MenuSettings')")
    before=wait(11,1);original=before['vsync']
    run("v=find(obj(u.MCUnifiedGameplayMenuWidget),'GameplayMenuV2_MenuVSync');v.set_is_checked("+repr(not original)+");v.on_check_state_changed.broadcast("+repr(not original)+")")
    changed=wait(11,1);assert changed['vsync']!=original;capture('GameplayMenuSettings')
    result=subprocess.run([sys.executable,str(P/'Tools/Unreal/UI/check_ui_v2_f3_solo.py'),'--case','GameplayMenuF3','--game-only','--output',str(O)],check=True,capture_output=True,text=True);print(result.stdout,flush=True)
    after=wait(11,1);assert after['source']==before['source'] and after['vsync']==changed['vsync']
    run("v=find(obj(u.MCUnifiedGameplayMenuWidget),'GameplayMenuV2_MenuVSync');v.set_is_checked("+repr(original)+");v.on_check_state_changed.broadcast("+repr(original)+")")
    run("click('MenuSettingsBack')");wait(11,0);capture('GameplayMenuPause')
    run("click('MenuResume')");wait(2,None)
    report.update(passed=True,sourceRetainedAcrossF3=True,pendingSettingRetained=True,pendingSettingRestored=True,resumeReturnedHub=True)
finally:
    if not report['passed']:
        try:
            restore = repr(original) if 'original' in locals() else 'None'
            run("menus=[x for x in u.ObjectIterator(u.MCGameplayMenuWidget) if x.get_world()==w and x.get_owning_player()==pc and x.is_in_viewport()]\nif menus:\n    source=menus[0]\n    value="+restore+"\n    if value is not None:\n        control=find(source,'MenuVSync');control.set_is_checked(value);control.on_check_state_changed.broadcast(value)\n    source.handle_action(u.MCGameplayMenuAction.RESUME)")
            report['failureCleanup']='Pending checkbox restored and owned pause menu resumed'
        except Exception as error:report['cleanupError']=str(error)
    s.stop();(O/'GameplayMenuSoloReport.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report))
