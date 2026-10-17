"""Load the authored reference widget after a cold native editor build."""
import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
u.MCGameplayUIEditorLibrary.ensure_gameplay_ui_preview_registered()
bp=u.load_asset('/Game/Gameplay/UI/WBP_BossHealth_Reference')
assert bp
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=True)
theme=u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
assert theme
mapping={str(key):value for key,value in theme.get_editor_property('widget_classes').items()}
assert len(mapping)==len(theme.get_editor_property('widget_classes'))
assert 'WBP_BossHealth_Reference' in str(mapping['BossHealth'])
u.log('MC_BOSS_REFERENCE_CHECK_READY')
