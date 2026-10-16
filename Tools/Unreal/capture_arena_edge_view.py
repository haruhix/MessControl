"""Capture the current editor view without changing its camera or saved lighting."""
import unreal as u,json,builtins
from pathlib import Path
out=Path(u.Paths.project_dir())/'Artifacts/ArenaEdges_20261017'
mode=globals().get('EDGE_CAPTURE_NAME','Before')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
view_path=out/'CaptureView.json'
if not view_path.exists():
 eye,rotation=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_level_viewport_camera_info()
 view_path.write_text(json.dumps(dict(eye=list(eye.to_tuple()),rotation=[rotation.pitch,rotation.yaw,rotation.roll],fov=80.,size=[960,1160]),indent=2))
view=json.loads(view_path.read_text());eye=u.Vector(*view['eye']);rotation=u.Rotator(pitch=view['rotation'][0],yaw=view['rotation'][1],roll=view['rotation'][2])
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
capture=actors.spawn_actor_from_class(u.SceneCapture2D,eye,rotation,transient=True)
try:
 cc=capture.get_editor_property('capture_component2d')
 target=u.RenderingLibrary.create_render_target2d(world,960,1160,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.02,.02,.02,1),False,False)
 cc.set_editor_properties(dict(texture_target=target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=80.))
 cc.capture_scene();u.RenderingLibrary.export_render_target(world,target,str(out),mode+'.png')
finally:actors.destroy_actor(capture)
print('ARENA_EDGE_CAPTURE',mode)
