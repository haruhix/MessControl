"""Close the owned, clean review Editor for native collision integration."""
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor(), 'Preserve an active PIE'
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve any play world'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved assets'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
u.log('MC_FRAGMENT_REVIEW_CLEAN_EDITOR_CLOSE')
u.SystemLibrary.execute_console_command(world, 'QUIT_EDITOR')
