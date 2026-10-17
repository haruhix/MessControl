"""Release only this UI check's explicit Python roots after ending solo PIE."""
import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
for name in ('_mc_boss_runtime_fixture','_mc_boss_runtime_short_height','_mc_boss_runtime_long_height'):
    if hasattr(u,name): delattr(u,name)
u.log('MC_BOSS_RUNTIME_FIXTURE_CLEANED')
