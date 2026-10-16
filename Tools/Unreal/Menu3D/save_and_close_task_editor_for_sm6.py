"""Save only known task packages and close the clean task editor for SM6 review."""
import json
from pathlib import Path
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/UI/MainMenu3D/L_MainMenu3D_RefFinish_v08'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
allowed = {
    '/Game/UI/MainMenu3D/BP_Menu3DPlayerController', '/Game/UI/MainMenu3D/WBP_MainMenu3D',
    *('/Game/UI/MainMenu3D/ReferenceFinish/Materials/M_Menu' + p + '_v08'
      for p in ('Tongue', 'Interior', 'Enamel', 'Gum')),
}
dirty = [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
assert set(dirty).issubset(allowed), 'Preserve unrelated unsaved packages: ' + str(set(dirty)-allowed)
assert hasattr(u.SystemLibrary, 'quit_editor')
for path in dirty:
    asset = u.load_asset(path)
    assert asset and u.EditorAssetLibrary.save_loaded_asset(asset)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
report = {'status': 'TASK_ONLY_PACKAGES_SAVED_REQUESTING_EDITOR_CLOSE', 'saved': dirty,
          'work_map': world.get_path_name(), 'scope': 'SM6 art review session only; no production renderer or C++ build'}
(Path(u.Paths.project_dir()).resolve() / 'Tools/Unreal/Menu3D/Evidence/editor_sm6_close_checkpoint.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report))
u.SystemLibrary.quit_editor()
