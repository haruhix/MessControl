"""Migrate only the pinned saved New UI trees, preserving original graphs/properties.
Run idle in the owned Editor. Explicit apply only after source build and E backups.
The native helper duplicates each saved tree, applies named structural upgrades,
compiles, preserves surviving GUIDs and rolls back the one asset on failure.
"""
import json,hashlib,shutil,traceback
from pathlib import Path
import unreal as u
ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path(globals().get('MC_UI_RESPONSIVE_OUTPUT','E:/CodexWork/MessControl/Validation/UILayout_20261104/SavedMigration')).resolve()
APPLY=globals().get('MC_UI_RESPONSIVE_APPLY',False)
assert type(APPLY) is bool and OUT.drive.upper()=='E:' and not OUT.exists()
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert hasattr(u.MCGameplayUIEditorLibrary,'migrate_unified_viewport_layout')
baseline=Path(globals().get('MC_UI_RESPONSIVE_BASELINE','E:/CodexWork/MessControl/Validation/UILayout_20261104/SavedAssetsBefore.json')).resolve()
assert baseline.drive.upper()=='E:'
entries=json.loads(baseline.read_text())['assets']
OUT.mkdir(parents=True)
report={'passed':False,'apply':APPLY,'assets':[],'scope':'Only16 pinned New UI assets, preserved saved-tree duplicate; no gameplay, profile or original Current tree changes'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
try:
 for e in entries:
  p=Path(e['package']);assert sha(p)==e['sha256'],'Saved source changed after review: '+str(p)
  backup=OUT/'BeforePackages'/p.relative_to(ROOT);backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup);assert sha(backup)==e['sha256']
 for e in entries:
  bp=u.load_asset(e['asset']);assert bp
  d=dict(e);graphs=['UbergraphPages','FunctionGraphs','MacroGraphs','DelegateSignatureGraphs']
  before=json.loads(u.ToolsetLibrary.get_object_properties(bp,graphs))
  d['graphsBefore']=before
  if APPLY:
   assert u.MCGameplayUIEditorLibrary.migrate_unified_viewport_layout(bp),'Native migration/compile/save rejected '+e['asset']
   d['graphsAfter']=json.loads(u.ToolsetLibrary.get_object_properties(bp,graphs))
   assert d['graphsAfter']==before,'Blueprint graph identity changed '+e['asset']
   d['savedSHA256']=sha(Path(e['package']))
  report['assets'].append(d)
  (OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
 assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
 report['passed']=True
except Exception:
 report['error']=traceback.format_exc();raise
finally:
 (OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_UI_RESPONSIVE_MIGRATION '+str(report['passed'])+' '+str(len(report['assets'])))
