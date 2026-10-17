import unreal as u
# QA launches with -unattended, which intentionally skips normal editor authoring startup.
u.MCGameplayUIEditorLibrary.ensure_gameplay_ui_preview_registered()
