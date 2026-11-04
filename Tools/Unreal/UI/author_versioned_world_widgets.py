"""Append new editable world forms; preserve original Current roots, controls and graphs."""
import hashlib,json,shutil
from pathlib import Path
import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
names=['WBP_PlayerName_Phase1','WBP_VoiceIndicator','WBP_FreezeBar_Phase1','WBP_DeliveryCaption_Phase1','WBP_WorldStatus','WBP_UlcerProgress_Phase1','WBP_HubInteractionPrompt','WBP_GrillHologram']
root=Path('E:/DEVGAME/MessControl');out=Path('E:/CodexWork/MessControl/Validation/UIOverlayWorld_20261104/WorldAuthor');out.mkdir(parents=True,exist_ok=True)
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def tree(bp):
 for name in ('WidgetTree','WidgetTree_0'):
  found=u.find_object(bp,name)
  if isinstance(found,u.WidgetTree) and found.get_outer()==bp:return found
 raise AssertionError('Missing saved owned WidgetTree '+bp.get_path_name())
def named_nodes(owned):
 return {x.get_name():{'path':x.get_path_name(),'class':x.get_class().get_path_name(),'parent':x.get_parent().get_name() if x.get_parent() else None} for x in u.ObjectIterator(u.Widget) if x.get_outer()==owned}
prior=json.loads((out/'Report.json').read_text(encoding='utf-8')) if (out/'Report.json').is_file() else {}
prior_assets={item['asset']:item for item in prior.get('assets',[])}
report={'status':'in_progress','assets':[]}
for name in names:
 source=root/'Content/Gameplay/UI'/(name+'.uasset');backup=out/'BeforePackages'/source.name;backup.parent.mkdir(parents=True,exist_ok=True)
 if not backup.exists():shutil.copy2(source,backup)
 bp=u.load_asset('/Game/Gameplay/UI/'+name);assert bp
 owned=tree(bp);before=named_nodes(owned);original_root=next(x['parent'] is None and key for key,x in before.items() if x['parent'] is None)
 if 'WorldPresentationVersions' in before:
  assert u.MCGameplayUIEditorLibrary.append_world_ui_presentation(bp),name
  entry=dict(prior_assets.get(bp.get_path_name(),{'asset':bp.get_path_name(),'status':'already_versioned_preserved'}));entry['revalidated']=True;entry['savedSHA256']=digest(source)
  report['assets'].append(entry);continue
 assert u.MCGameplayUIEditorLibrary.append_world_ui_presentation(bp),name
 after=named_nodes(tree(bp));assert set(before)<=set(after),(name,'Original widget lost')
 for key,old in before.items():
  new=after[key];assert old['path']==new['path'] and old['class']==new['class'],(name,key,'Original identity changed')
  expected='WorldPresentationVersions' if key==original_root else old['parent']
  assert new['parent']==expected,(name,key,old,new)
 report['assets'].append({'asset':bp.get_path_name(),'status':'passed','beforeCount':len(before),'afterCount':len(after),'originalRoot':original_root,'originalNodesPreserved':True,'backupSHA256':digest(backup),'savedSHA256':digest(source)})
 (out/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
report['status']='passed';report['dirtyContent']=[x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()];report['dirtyMaps']=[x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
(out/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert not report['dirtyContent'] and not report['dirtyMaps'],report
print('VERSIONED_WORLD_EDITABLE_ASSETS',len(report['assets']))
