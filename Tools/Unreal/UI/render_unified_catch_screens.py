"""Actual saved Slate renders without actors; none are played gameplay scenarios."""
import json
from pathlib import Path
import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
out=Path('E:/CodexWork/MessControl/Validation/UICatch_20261104/Previews_Build03');out.mkdir(parents=True,exist_ok=True)
cases=[('Root_'+screen,'WBP_UIRoot',screen,'en',1920,1080,.55) for screen in ('Catch','HUDExtras','Loading','Boss','Alert')]
cases += [('Catch_'+s,'WBP_UICatch',s,'en',1280,720,.55) for s in ('Common','Rare','Epic','Legendary','WalletFull')]
cases += [('Catch_Long_DE','WBP_UICatch','LongText','de',960,720,.55),('Catch_JA','WBP_UICatch','Legendary','ja',960,720,.55)]
cases += [('HUD_'+s,'WBP_UIHUDExtras',s,'ru' if s in ('Respawn','ResultsLose') else 'en',1280,720,.55) for s in ('Respawn','ResultsLose','Drowning','Passive','Cataclysm','CataclysmDrowning')]
cases += [('HUD_Long_DE','WBP_UIHUDExtras','LongText','de',960,720,.55),('Loading_Failed_DE','WBP_UILoading','Failed','de',1280,720,.55),('Loading_Waiting_JA','WBP_UILoading','Waiting','ja',960,720,.55),('Boss_Long_DE','WBP_UIBoss','LongText','de',1920,1080,1.),('Alert_Long_DE','WBP_UIAlert','LongText','de',1280,720,1.)]
results=[]
for label,name,state,culture,width,height,clock in cases:
    cls=u.load_class(None,'/Game/Gameplay/UI/V2/'+name+'.'+name+'_C');assert cls,name
    filename=out/(label+'.png')
    assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(filename),u.IntPoint(width,height),state,culture,clock,1.0,True,u.MCUIVersion.NEW),label
    results.append({'label':label,'file':str(filename),'state':state,'culture':culture,'size':[width,height],'time':clock,'kind':'Actual Slate render; actor-free fixture'})
report={'renders':results,'dirtyContent':[x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirtyMaps':[x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(out/'RenderReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert not report['dirtyContent'] and not report['dirtyMaps'],report
print('NEW_SCREEN_SLATE_RENDERS',len(results))
