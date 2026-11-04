"""Render actual saved overlay/world widget trees without gameplay actors or profile changes."""
import json
from pathlib import Path
import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
out=Path(globals().get('MC_OVERLAY_PREVIEW_OUTPUT','E:/CodexWork/MessControl/Validation/UIOverlayWorld_20261104/Previews_Build02'))
assert out.resolve().drive.upper()=='E:'
out.mkdir(parents=True,exist_ok=True)
base='/Game/Gameplay/UI/'
cases=[('Root_'+s,base+'V2/WBP_UIRoot',s,'en',1280,720) for s in ('Tutorial','Emotes')]
for state,culture in [('Default','en'),('Default','ru'),('LongText','de'),('FullComplete','de'),('ShortComplete','en'),('Ready','ja')]:
 cases.append(('Tutorial_'+state+'_'+culture,base+'V2/WBP_UITutorial',state,culture,960 if culture in ('de','ja') else 1280,720))
for state,culture in [('Default','en'),('Gestures','ru'),('Signals','de'),('Held','en'),('HeldNoSelection','en'),('Disabled','en'),('Empty','en'),('LongText','de')]:
 cases.append(('Emotes_'+state+'_'+culture,base+'V2/WBP_UIEmotes',state,culture,960 if culture=='de' else 1280,720))
world=[('Name','WBP_PlayerName_Phase1','Host','en'),('NameHelp','WBP_PlayerName_Phase1','Help','ru'),('NameLong','WBP_PlayerName_Phase1','LongText','de'),('Voice','WBP_VoiceIndicator','Default','en'),('Keycap','WBP_HubInteractionPrompt','Default','de'),('Freeze','WBP_FreezeBar_Phase1','Danger','de'),('FreezeWarm','WBP_FreezeBar_Phase1','Warming','ja'),('Delivery','WBP_DeliveryCaption_Phase1','Default','en'),('DeliveryExit','WBP_DeliveryCaption_Phase1','Exit','de'),('DeliveryError','WBP_DeliveryCaption_Phase1','SortingError','ja'),('FoodFresh','WBP_WorldStatus','FoodFill25','en'),('FoodSpoiled','WBP_WorldStatus','FoodSpoiled','en'),('EnemyLoss','WBP_WorldStatus','EnemyDamaged','en'),('Care','WBP_UlcerProgress_Phase1','Ready','en'),('CarePull','WBP_UlcerProgress_Phase1','Pull','de'),('CareLong','WBP_UlcerProgress_Phase1','LongText','ja')]
for label,name,state,culture in world:cases.append(('World_'+label,base+name,state,culture,640,400))
for state,culture in [('Idle','en'),('Default','ru'),('NoPrize','de')]:cases.append(('Grill_'+state+'_'+culture,base+'WBP_GrillHologram',state,culture,800,480))
for state in ('FoodReticle','FoodReticleFade','SprayReticle','PressureReticle','AntisepticReload','FogNavigation','ReticleCombined','LongText'):cases.append(('Reticle_'+state,base+'V2/WBP_UIReticle',state,'de',1280,720))
if u.EditorAssetLibrary.does_asset_exist(base+'V2/Widgets/WBP_WorldLabel_V2'):
 for state,culture in [('Default','en'),('Goal','de'),('Danger','ja'),('Numeric','en'),('Care','de'),('LongText','ja')]:cases.append(('WorldLabel_'+state+'_'+culture,base+'V2/Widgets/WBP_WorldLabel_V2',state,culture,800,480))
for state,culture in [('FishingControls','en'),('FishingWaiting','ru'),('FishingHooked','de'),('BasketballControls','en'),('BasketballPickup','ja'),('ControlsLongText','de')]:cases.append(('Hub_'+state+'_'+culture,base+'V2/WBP_UIHub',state,culture,960 if culture in ('de','ja') else 1280,720))
results=[]
for label,asset,state,culture,width,height in cases:
 name=asset.rsplit('/',1)[-1];cls=u.load_class(None,asset+'.'+name+'_C');assert cls,asset
 filename=out/(label+'.png')
 assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(filename),u.IntPoint(width,height),state,culture,.55,1.,True,u.MCUIVersion.NEW),label
 results.append({'label':label,'asset':asset,'state':state,'culture':culture,'size':[width,height],'file':str(filename),'method':'Actual Slate actor-free fixture'})
report={'renders':results,'dirtyContent':[x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirtyMaps':[x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(out/'RenderReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert not report['dirtyContent'] and not report['dirtyMaps'],report
print('OVERLAY_WORLD_SLATE_RENDERS',len(results))
