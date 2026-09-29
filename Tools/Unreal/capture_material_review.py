"""Consistent before/after material review; call again with REVIEW_MODE='restore'."""
import unreal as u
from pathlib import Path

mode = globals().get('REVIEW_MODE', 'Before')
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
all_actors = actors.get_all_level_actors()
if mode == 'restore':
    for actor in all_actors:
        if isinstance(actor, u.MCArenaToothSocket):
            actor.get_editor_property('preview').set_hidden_in_game(True)
else:
    camera = next((a for a in all_actors if a.get_actor_label() == 'LOOK | Material review'), None)
    if camera is None:
        camera = actors.spawn_actor_from_class(u.CameraActor, u.Vector())
        camera.set_actor_label('LOOK | Material review')
        camera.set_folder_path('Look/ReferenceLighting')
    eye, aim = u.Vector(-1050,-30,300), u.Vector(1500,-30,130)
    camera.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True)
    camera.camera_component.set_field_of_view(70)
    camera.camera_component.set_aspect_ratio(1.5)
    for actor in all_actors:
        if isinstance(actor, u.MCArenaToothSocket):
            actor.get_editor_property('preview').set_hidden_in_game(False)
    folder = Path(u.Paths.project_dir()).resolve()/'Artifacts/MaterialReview'
    folder.mkdir(parents=True,exist_ok=True)
    material_review_capture = u.AutomationLibrary.take_high_res_screenshot(
        1536,1024,str(folder/(mode+'.png')),camera=camera,force_game_view=True)
    u.get_editor_subsystem(u.LevelEditorSubsystem).editor_set_viewport_realtime(True)
u.log('MC_MATERIAL_REVIEW_'+mode)
