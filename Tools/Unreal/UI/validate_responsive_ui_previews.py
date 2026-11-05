"""Render the actual saved single-root screens in an idle owned Editor.
Generated images/layouts are review evidence, not automatic artistic acceptance.
No PIE, source asset edits/saves or gameplay actions. Output must be a fresh E path.
"""
from pathlib import Path
import hashlib,json,traceback
import unreal as u
OUT=Path(globals().get('MC_UI_RESPONSIVE_REVIEW_OUTPUT','E:/CodexWork/MessControl/Validation/UILayout_20261104/Review09')).resolve()
assert OUT.drive.upper()=='E:' and not OUT.exists()
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
def dirty():return sorted(x.get_path_name() for x in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
before=dirty();assert not before
ROOTCLASS='/Game/Gameplay/UI/V2/WBP_UIRoot.WBP_UIRoot_C'
screens=['Menu','Gameplay','Hub','Wardrobe','Boosters','Settings','Team','Reward','Frontend','GameplayMenu','Catch','HUDExtras','Loading','Boss','Alert','Tutorial','Emotes']
cases=[(s,c,1280,720) for c in ['en','de'] for s in screens]
cases += [('Wardrobe',c,w,h) for c,w,h in [('ru',1280,720),('ja',1280,720),('en',960,720),('de',960,720),('ja',960,720),('en',1920,1080),('ru',1920,1080),('ja',1920,1080),('en',2560,1440)]]
cases += [(s,'de',960,720) for s in ['Boosters','Settings','Team','Reward','Frontend','GameplayMenu']]
# Nested panels are fixtures of this transient root, never edits to saved defaults.
variants=[]
for lang in ['en','de']:
 for tab in range(1,5): variants.append(('Settings',lang,960,720,{'settingsTab':tab}))
 for page in range(1,5): variants.append(('Frontend',lang,960,720,{'frontendPage':page}))
for mode in ['Settings','Audio','Connection','Leave','Exit','VideoConfirm']:
 variants.append(('GameplayMenu','de',960,720,{'gameplayMenuMode':mode}))
for mode in ['Replacement','Contract','WaitingForTeam','Pending']:
 variants.append(('Reward','de',960,720,{'rewardMode':mode}))
for mode in ['Selecting','ControlsLongText']:
 variants.append(('Hub','de',960,720,{'hubMode':mode}))
for mode in ['LongText','ResultsWin','ResultsLose','Drowning','Death','Respawn','Spectator']:
 variants.append(('HUDExtras','de',960,720,{'hudExtrasMode':mode}))
for mode in ['WalletFull','Legendary']: variants.append(('Catch','de',960,720,{'catchMode':mode}))
for category in [1,2]: variants.append(('Emotes','de',960,720,{'emoteCategory':category}))
for screen in ['Team','Wardrobe','Reward','Emotes']:
 variants.append((screen,'de',960,720,{'scenario':'LongText'}))
cases=globals().get('MC_UI_RESPONSIVE_REVIEW_CASES',cases+variants)
OUT.mkdir(parents=True);report={'exportPassed':False,'visualAcceptance':'PENDING_PERSONAL_REVIEW','cases':[],'scope':'Settled actual Slate renders of saved WBP_UIRoot; no actor/gameplay/network/cold-resource claim'}
cls=u.load_class(None,ROOTCLASS);assert cls
try:
 for case in cases:
  screen,lang,width,height=case[:4];options=case[4] if len(case)>4 else {}
  suffix=''.join('_'+str(k)+'_'+str(v) for k,v in options.items())
  f=OUT/(screen+'_'+lang+'_'+str(width)+'x'+str(height)+suffix+'.png');assert not f.exists()
  assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(f),u.IntPoint(width,height),screen,lang,.55,1.,True,u.MCUIVersion.NEW,json.dumps(options) if options else ''),str(f)
  layout=json.loads(Path(str(f)+'.layout.json').read_text(encoding='utf-8-sig'))
  retired=[n['name'] for n in layout['nodes'] if 'ScrollBox' in n['class'] and n['name'] in ['WardrobeViewport','ShopViewport','RewardViewport','FrontendViewport','GameplayMenuViewport','SettingsViewport','TeamViewport','WardrobeV2Scroll','EmoteV2Scroll','LoadingV2ControlsScroll']]
  report['cases'].append({'screen':screen,'culture':lang,'fixtureOptions':options,'size':[width,height],'image':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'nodeCount':len(layout['nodes']),'retiredOuterScrolls':retired})
  assert not retired,retired
  assert dirty()==before,'Read-only render dirtied packages'
  (OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 report['exportPassed']=True
except Exception:report['error']=traceback.format_exc();raise
finally:(OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_UI_RESPONSIVE_REVIEW_EXPORT '+str(report['exportPassed'])+' '+str(len(report['cases'])))
