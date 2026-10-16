"""Read-only scene contract for DAY03 remodel integration."""
import json
from pathlib import Path
import unreal as u

world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name().startswith('/Game/UI/MainMenu3D/L_MainMenu3D')
def vec(v):return [v.x,v.y,v.z]
report={'world':world.get_path_name(),'mouths':[],'cameras':[]}
for day in (1,2,3):
    actor=u.GameplayStatics.get_all_actors_with_tag(world,f'Menu3D_DAY0{day}')[0]
    comp=actor.get_component_by_class(u.SkeletalMeshComponent)
    mesh=comp.get_editor_property('skeletal_mesh_asset')
    b=mesh.get_bounds()
    children=[]
    for child in actor.get_attached_actors():
        root_component=child.get_editor_property('root_component')
        children.append({'name':child.get_actor_label(),'relative_location':vec(root_component.get_editor_property('relative_location')),
            'relative_scale':vec(root_component.get_editor_property('relative_scale3d'))})
    report['mouths'].append({'day':day,'actor':actor.get_path_name(),'mesh':mesh.get_path_name(),
        'location':vec(actor.get_actor_location()),'scale':vec(actor.get_actor_scale3d()),
        'bounds_origin':vec(b.origin),'bounds_extent':vec(b.box_extent),'children':children})
for actor in u.GameplayStatics.get_all_actors_of_class(world,u.CameraActor):
    camera=actor.get_component_by_class(u.CameraComponent)
    report['cameras'].append({'actor':actor.get_path_name(),'location':vec(actor.get_actor_location()),
        'rotation':str(actor.get_actor_rotation()),'fov':camera.field_of_view,'aspect':camera.aspect_ratio})
target=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/v06_scene_contract_before.json'
target.write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_V06_SCENE '+json.dumps(report))
