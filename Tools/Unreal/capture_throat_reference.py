"""Capture the actual editor geometry/materials from a saved detail camera.

Set UVULA_CAPTURE_MODE='open' or 'closed' before executing. Run 'restore' last.
"""
import unreal as u
from pathlib import Path

mode = globals().get('UVULA_CAPTURE_MODE','closed')
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
throat = next(a for a in actors.get_all_level_actors() if isinstance(a,u.MCThroat))
tissue = throat.get_editor_property('sculpted_tissue')
# Reinitialize the render state: an idle editor can retain its last evaluated
# morph buffers even after the override curves have been cleared.
mesh = tissue.get_editor_property('skeletal_mesh_asset')
tissue.set_skeletal_mesh_asset(None)
tissue.set_skeletal_mesh_asset(mesh)
tissue.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE,True)
if mode == 'restore':
    tissue.set_morph_target('SwallowOpen',0,False)
    throat.get_editor_property('label').set_visibility(True)
    u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
else:
    camera = next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='LOOK | Throat detail'),None)
    if camera is None:
        camera = actors.spawn_actor_from_class(u.CameraActor,u.Vector(-550,-30,250))
        camera.set_actor_label('LOOK | Throat detail')
        camera.set_folder_path('Look/ReferenceLighting')
    eye,aim = u.Vector(-550,-30,250),u.Vector(1450,-30,190)
    camera.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True)
    camera.camera_component.set_field_of_view(60)
    camera.camera_component.set_aspect_ratio(1.5)
    tissue.set_update_animation_in_editor(True)
    tissue.set_morph_target('SwallowOpen',1 if mode=='open' else 0,False)
    throat.get_editor_property('label').set_visibility(False)
    u.get_editor_subsystem(u.LevelEditorSubsystem).editor_set_game_view(True)
    u.get_editor_subsystem(u.LevelEditorSubsystem).editor_set_viewport_realtime(True)
    folder = Path(u.Paths.project_dir()).resolve()/'Artifacts/ThroatReference'
    folder.mkdir(parents=True,exist_ok=True)
    uvula_capture_task = u.AutomationLibrary.take_high_res_screenshot(
        1536,1024,str(folder/('Throat_'+mode+'.png')),camera=camera,delay=0.0,force_game_view=True)
    # A background editor may not redraw until a native CaptureViewport call.
    # Do not issue another capture until the task's output exists with a new timestamp.
u.log('MC_THROAT_REFERENCE_CAPTURE_'+mode.upper())
