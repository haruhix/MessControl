"""Read-only native preview bounds and camera, in the editor or solo PIE."""
import json
from pathlib import Path
import unreal as u

world=u.EditorLevelLibrary.get_game_world()
if world is None:world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and 'MainMenu3D' in world.get_path_name(),world
actors=u.GameplayStatics.get_all_actors_of_class(world,u.Actor)
snapshot=[{'label':a.get_actor_label(),'path':a.get_path_name(),'tags':[str(t) for t in a.tags]} for a in actors]
(Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/width_editor_state.json').write_text(json.dumps(snapshot,indent=2),encoding='utf8')
report={'source':'Actual native runtime/editor component bounds; not evaluated-pose vertex silhouettes','mouths':[]}
for day in range(1,4):
    tag='Menu3D_DAY0'+str(day)
    actor=next(a for a in actors if tag in [str(t) for t in a.tags])
    comp=actor.get_component_by_class(u.SkeletalMeshComponent)
    scale=actor.get_actor_scale3d()
    origin,extent=actor.get_actor_bounds(False,False)
    report['mouths'].append({'day':day,'actor':actor.get_path_name(),
        'bounds_width_before_actor_scale':2*extent.x/scale.x,'actor_scale':[scale.x,scale.y,scale.z],
        'world_bounds_origin':[origin.x,origin.y,origin.z],
        'world_bounds_extent':[extent.x,extent.y,extent.z],
        'visibility_anim_tick':str(comp.get_editor_property('visibility_based_anim_tick_option')),
        'update_rate_optimizations':comp.get_editor_property('enable_update_rate_optimizations')})
camera=next(a for a in actors if 'Menu3D_Camera' in [str(t) for t in a.tags])
report['camera']={'location':str(camera.get_actor_location()),'rotation':str(camera.get_actor_rotation()),
    'fov':camera.get_component_by_class(u.CameraComponent).get_editor_property('field_of_view')}
out=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/native_mouth_width_bounds.json'
out.write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_NATIVE_WIDTH_BOUNDS '+json.dumps(report))
