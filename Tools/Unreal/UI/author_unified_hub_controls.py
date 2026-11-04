"""Append source-backed Hub controls, retaining every original saved widget identity."""
import hashlib,json,shutil
from pathlib import Path
import unreal as u
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
out=Path('E:/CodexWork/MessControl/Validation/UIOverlayWorld_20261104/HubControlsAuthor');out.mkdir(parents=True,exist_ok=True)
source=Path('E:/DEVGAME/MessControl/Content/Gameplay/UI/V2/WBP_UIHub.uasset');backup=out/'WBP_UIHub.Before.uasset'
if not backup.exists():shutil.copy2(source,backup)
bp=u.load_asset('/Game/Gameplay/UI/V2/WBP_UIHub');assert bp

def tree():
 item=u.find_object(bp,'WidgetTree');assert isinstance(item,u.WidgetTree) and item.get_outer()==bp;return item

def nodes():
 owned=tree();return {x.get_name():{'path':x.get_path_name(),'class':x.get_class().get_path_name(),'parent':x.get_parent().get_name() if x.get_parent() else None} for x in u.ObjectIterator(u.Widget) if x.get_outer()==owned}

before=nodes();assert 'HubV2Root' in before
assert u.MCGameplayUIEditorLibrary.append_unified_hub_controls(bp)
after=nodes();assert all(after.get(key)==value for key,value in before.items()),'Original node changed'
assert {'HubV2ControlsReticle','HubV2ControlsBounds','HubV2ControlsPlate','HubV2ControlsScroll','HubV2ControlsText'}<=set(after)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
(out/'Report.json').write_text(json.dumps({'asset':bp.get_path_name(),'beforeCount':len(before),'afterCount':len(after),'originalNodesPreserved':True,'oldSHA256':hashlib.sha256(backup.read_bytes()).hexdigest(),'savedSHA256':hashlib.sha256(source.read_bytes()).hexdigest(),'sourceRecipe':'MCGameplayUIEditorLibrary.AppendUnifiedHubControls'},indent=2),encoding='utf-8')
print('HUB_CONTROL_BRANCHES_AUTHORED',len(before),len(after))
