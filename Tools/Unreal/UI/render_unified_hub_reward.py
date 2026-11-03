"""Render the saved root screens and canonical component states without gameplay."""
import json
from pathlib import Path
import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
out=Path('E:/CodexWork/MessControl/Validation/UIHubReward_20261104/Previews');out.mkdir(parents=True,exist_ok=True)
cases=[('Root_'+screen,'WBP_UIRoot',screen,'en',1920,1080) for screen in ('Hub','Reward','Frontend','GameplayMenu')]
cases += [('Reward_RU','WBP_UIReward','','ru',1280,920),('Reward_Long_DE','WBP_UIReward','LongText','de',1280,1080),('Reward_Replacement','WBP_UIReward','Replacement','en',1280,1080),('Hub_Selecting','WBP_UIHub','Selecting','en',1920,1080),('GameplayMenu_Settings','WBP_UIGameplayMenu','Settings','ru',1280,1080),('GameplayMenu_Audio','WBP_UIGameplayMenu','Audio','en',1280,1080)]
results=[]
for label,name,state,culture,width,height in cases:
    cls=u.load_class(None,'/Game/Gameplay/UI/V2/'+name+'.'+name+'_C');assert cls
    image=out/(label+'.png')
    assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(image),u.IntPoint(width,height),state,culture,1.0,1.0,True,u.MCUIVersion.NEW),label
    results.append({'label':label,'file':str(image),'state':state,'culture':culture,'size':[width,height],'kind':'Actual Slate render; gameplay-free fixture'})
report={'renders':results,'dirtyContent':[x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirtyMaps':[x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(out/'RenderReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'rendered':len(results),'dirtyContent':report['dirtyContent'],'dirtyMaps':report['dirtyMaps']}))
