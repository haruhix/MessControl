"""Create a separate tutorial map from the saved mouth arena without changing it."""
import json
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
previous = editor.get_editor_world().get_path_name().split('.')[0]
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved editor maps first'
assert not editor.get_game_world(), 'Stop PIE before creating a map'
destination = '/Game/Maps/L_Tutorial'
created = not u.EditorAssetLibrary.does_asset_exist(destination)
if created:
    assert levels.new_level_from_template(destination, '/Game/Maps/L_Mouth'), 'Could not create tutorial map'
    assert levels.save_current_level(), 'Could not save tutorial map'
    assert levels.load_level(previous), 'Could not restore original editor map'
assert u.EditorAssetLibrary.does_asset_exist(destination)
u.log('MC_TUTORIAL_MAP ' + json.dumps(dict(created=created, path=destination, restored=previous)))
