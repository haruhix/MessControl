import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
settings=u.get_default_object(u.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
for key,value in u._mc_ui_play_settings.items(): settings.set_editor_property(key,value)
del u._mc_ui_play_settings
u.log('MC_UI_SOLO_SETTINGS_RESTORED')
