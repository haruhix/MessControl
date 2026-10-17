import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
asset=u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
assert u.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False)
u.log('MC_UI_RESTORED_THEME_SAVED')
