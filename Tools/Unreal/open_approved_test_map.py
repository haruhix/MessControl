"""Open the saved normal arena for the next owned solo checks."""
import unreal as u
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor(), 'Preserve existing PIE'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve dirty maps'
assert levels.load_level('/Game/Maps/L_Mouth')
u.log('MC_APPROVED_NORMAL_TEST_MAP_OPEN')
