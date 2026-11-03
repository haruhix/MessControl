"""Check a real pending ordinary reward across F3; never choose, skip or reroll."""
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
w=u.EditorLevelLibrary.get_pie_worlds(True)[0];pc=u.GameplayStatics.get_player_controller(w,0);ps=pc.get_editor_property('player_state')
def obj(cls,viewport=False):return next(x for x in u.ObjectIterator(cls) if x.get_world()==w and x.get_owning_player()==pc and (not viewport or x.is_in_viewport()))
def tree(x):return next(t for t in u.ObjectIterator(u.WidgetTree) if t.get_outer()==x)
def find(x,n):return u.find_object(tree(x),n)
root=obj(u.MCUnifiedUIWidget,True);source=obj(u.MCPerkChoiceWidget,True)
chest=next(x for x in u.GameplayStatics.get_all_actors_of_class(w,u.MCRewardChest) if x.actor_has_tag('MC_UIV2_Reward_QA_20261104'))
g=chest.get_choice_offer_id(ps).to_string()
assert g and g.replace('-','').strip('0'),g
perks=ps.get_editor_property('perks')
cards=[str(x.get_path_name()) for x in u.ObjectIterator(u.MCPerkCardWidget) if x.get_world()==w and x.get_owning_player()==pc and x.get_typed_outer(u.MCPerkChoiceWidget)==source]
hub=obj(u.MCHubWidget,True)
groups={name:str(find(hub,name).get_visibility()) for name in ('HubReadinessPanel','HubBottomBar','HubFooterBackdrop','HubSelectionFooter') if find(hub,name)}
d={'source':source.get_path_name(),'offer':g,'cards':sorted(cards),'screen':find(root,'InterfaceScreens').get_active_widget_index(),'sourceRoot':str(find(source,'RewardBackdrop').get_visibility()),'perks':json.loads(u.ToolsetLibrary.get_object_properties(perks,['PassivePerkIDs','OwnedToolPerkIDs','ActiveSkillSlots'])),'choices':ps.get_editor_property('PersonalChoicesTaken'),'hubGroups':groups}
Path('E:/CodexWork/MessControl/Validation/UIHubReward_20261104/RewardCurrent.json').write_text(json.dumps(d),encoding='utf-8')
"""
s=R.RemoteExecution();report={'passed':False,'choiceSubmitted':False}
def statement(code):return s.run_command('exec('+repr(code)+", {'__name__':'__main__'})",exec_mode=R.MODE_EXEC_STATEMENT,unattended=True,raise_on_failure=True)
def state():
    statement(C);return json.loads((O/'RewardCurrent.json').read_text(encoding='utf-8'))
try:
    s.start();deadline=time.monotonic()+8;nodes=[]
    while time.monotonic()<deadline:
        nodes=[n for n in s.remote_nodes if n.get('project_name')=='MessControl']
        if len(nodes)==1:break
        time.sleep(.2)
    assert len(nodes)==1;s.open_command_connection(nodes[0]['node_id'])
    before=state();assert before['screen']==9 and len(before['cards'])==3,before
    assert before['hubGroups'] and all('HIDDEN' in x or 'COLLAPSED' in x for x in before['hubGroups'].values())
    result=subprocess.run([sys.executable,str(P/'Tools/Unreal/UI/check_ui_v2_f3_solo.py'),'--case','RewardF3','--game-only','--output',str(O)],capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr;print(result.stdout,flush=True)
    after=state()
    for key in ('source','offer','cards','screen','perks','choices'):assert before[key]==after[key],(key,before,after)
    statement("import gc,unreal as u\ngc.collect()\nu.SystemLibrary.collect_garbage()")
    final=state();assert final['source']==before['source'] and final['screen']==9
    recipe=str(P/'Tools/Unreal/UI/probe_unified_hub_reward.py')
    statement("from pathlib import Path\np="+repr(recipe)+"\nexec(compile(Path(p).read_text(encoding='utf-8'),p,'exec'),{'__name__':'__main__','__file__':p})")
    for ext in ('json','png'):shutil.copy2(O/('LiveLatest.'+ext),O/('RewardNativeOfferFinal.'+ext))
    report.update(passed=True,before=before,after=after,gcRetainedSource=True,legacyHubChromeSuppressed=True)
finally:
    s.stop();(O/'RewardSoloReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'passed':report['passed'],'choiceSubmitted':False,'sameOffer':report.get('passed'),'sameInventory':report.get('passed')}))
