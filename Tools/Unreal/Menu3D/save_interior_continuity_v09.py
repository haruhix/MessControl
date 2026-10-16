"""Save only the task-owned menu Blueprints after native PIE review."""
import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not getattr(u,'_mc_lower_review_handle',None)
for path in ('/Game/UI/MainMenu3D/WBP_MainMenu3D','/Game/UI/MainMenu3D/BP_Menu3DPlayerController'):
    asset=u.load_asset(path);assert asset
    assert u.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=True)
u.log('MC_INTERIOR_CONTINUITY_TASK_BLUEPRINTS_SAVED')
