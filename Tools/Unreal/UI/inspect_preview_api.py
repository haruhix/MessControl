import unreal as u
for cls, name in ((u.MCGameplayUIEditorLibrary, 'validate_gameplay_ui_localization'), (u.MCUIDataLibrary, 'validate_content_table')):
    u.log(str(getattr(cls, name).__doc__))
u.log(str(u.MCGameplayUIEditorLibrary.validate_gameplay_ui_localization()))
