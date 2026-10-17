"""Close only an idle editor with no unrelated unsaved content."""
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
theme = u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
if theme:
    assert u.EditorAssetLibrary.save_loaded_asset(theme, only_if_is_dirty=False)
dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
unrelated = [p.get_name() for p in dirty if not p.get_name().startswith('/Game/Gameplay/UI/') and not p.get_name().startswith('/Engine/Transient')]
assert not unrelated, 'Preserve unrelated unsaved packages: ' + str(unrelated)
u.SystemLibrary.execute_console_command(None, 'QUIT_EDITOR')
