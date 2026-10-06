"""Create only the frontend map; the authored mouth map is never loaded or saved."""
import json
import unreal as u

path = '/Game/Maps/L_MainMenu'
mode = u.load_class(None, '/Script/MessControl.MCMainMenuGameMode')
assert mode, 'Compile MCMainMenuGameMode before creating the frontend map'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved assets'
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
if u.EditorAssetLibrary.does_asset_exist(path):
    assert levels.load_level(path)
else:
    assert levels.new_level(path)
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world
world.get_world_settings().set_editor_property('default_game_mode', mode)
assert levels.save_current_level()
print('MC_MAIN_MENU_MAP_READY ' + json.dumps({'map': path, 'mode': mode.get_path_name()}))
