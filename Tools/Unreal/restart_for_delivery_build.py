"""Close only the connected, clean MessControl editor before a native class rebuild."""
import json
from pathlib import Path
import unreal

dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
dirty += [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
if dirty:
    raise RuntimeError('Preserving unsaved edits; editor was not closed: ' + json.dumps(dirty))
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not world.get_path_name().startswith('/Game/Maps/L_Mouth.'):
    raise RuntimeError('Unexpected map; editor was not closed: ' + world.get_path_name())
location, rotation = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_level_viewport_camera_info()
state = dict(map='/Game/Maps/L_Mouth', location=[location.x, location.y, location.z],
             rotation=[rotation.pitch, rotation.yaw, rotation.roll])
path = Path(unreal.Paths.project_saved_dir()) / 'DeliveryZonesEditorState.json'
path.write_text(json.dumps(state), encoding='utf-8')
unreal.log('MC_DELIVERY_EDITOR_RESTART clean state saved; closing MessControl only')
unreal.SystemLibrary.quit_editor()
