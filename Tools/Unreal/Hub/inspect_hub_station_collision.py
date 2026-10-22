"""Read-only live collision inventory for the actual four cabins and vending prop.

Run only in the existing idle Hub editor. No build, PIE, map load, asset edits,
collision rebuild or package save. Writes metadata-only JSON to an E: file.
"""
from datetime import datetime, timezone
import json
from pathlib import Path


TARGETS = {'StaticMeshActor_103': '/Game/FromBlender11/SM_BusterShop',
           **{f'StaticMeshActor_{number}': '/Game/FromBlender11/SM_Cube_006'
              for number in (109,110,112,113)},
           **{f'StaticMeshActor_{number}': '/Game/FromBlender11/SM_Cube_006_002'
              for number in (108,111,114,115)}}
PAIRS = ((109,108),(110,111),(113,114),(112,115))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def vec(value):
    return [float(value.x),float(value.y),float(value.z)]


def read(obj, name):
    try:
        value = obj.get_editor_property(name)
        if hasattr(value,'get_path_name'):
            return value.get_path_name()
        if isinstance(value,(str,bool,int,float)) or value is None:
            return value
        return str(value)
    except Exception as error:
        return {'unexposed_or_unavailable':str(error)}


def run(output='E:/CodexWork/HubWardrobeRewards_20261022/StationCollision/Inspect01.json'):
    import unreal as u
    destination = Path(output).resolve()
    root = Path(u.Paths.project_dir()).resolve()
    require(destination.drive.upper()=='E:' and root.drive.upper()=='E:', 'All outputs must stay on E:')
    require((root/'MessControl.uproject').is_file(), 'Wrong project')
    require(not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve current Play session')
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(world and world.get_path_name().split('.',1)[0]=='/Game/Maps/Hub','Open Hub before inspection')
    actors = {actor.get_name():actor for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()}
    missing = sorted(set(TARGETS)-set(actors));require(not missing,'Actual prop actors missing: '+str(missing))
    mesh_editor = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    report = {'captured_utc':datetime.now(timezone.utc).isoformat(),'world':world.get_path_name(),
              'scope':__doc__,'actors':[],'meshes':{},'cabin_front_evidence':[],
              'limits':['Saved index is stale; this inspection reads live loaded packages instead.',
                        'Cooked Chaos pointers are native non-reflected data; primitive metadata and settings do not prove a packaged-game collision test.',
                        'No walk or capsule sweep has been run by this inspector. Existing valid shapes should be tested before rebuilding.']}
    for name,expected in TARGETS.items():
        actor=actors[name];component=actor.get_component_by_class(u.StaticMeshComponent)
        require(component and component.static_mesh, 'Actual prop lacks its mesh: '+name)
        mesh=component.static_mesh;package=mesh.get_path_name().split('.',1)[0]
        require(package==expected or package.startswith('/Game/Gameplay/Hub/Collision/'),
                'Unexpected replacement prop mesh; inspect before changing: '+mesh.get_path_name())
        minimum,maximum=component.get_local_bounds();rotation=actor.get_actor_rotation()
        report['actors'].append({'name':name,'label':actor.get_actor_label(),'mesh':mesh.get_path_name(),
            'location':vec(actor.get_actor_location()),'rotation':[float(rotation.pitch),float(rotation.yaw),float(rotation.roll)],
            'scale':vec(actor.get_actor_scale3d()),'local_bounds':{'min':vec(minimum),'max':vec(maximum)},
            'actor_collision_enabled':bool(actor.get_actor_enable_collision()),
            'component_collision_enabled':str(component.get_collision_enabled()),
            'profile':str(component.get_collision_profile_name()),
            'pawn_response':str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN)),
            'visibility_response':str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_VISIBILITY)),
            'camera_response':str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA)),
            'simulates_physics':bool(component.is_simulating_physics()),
            'character_step_up':read(component,'can_character_step_up_on'),
            'overlap_events':read(component,'generate_overlap_events')})
        if mesh.get_path_name() in report['meshes']:
            continue
        body=mesh.get_editor_property('body_setup')
        sections=mesh.get_num_sections(0)
        entry={'path':mesh.get_path_name(),'simple_count':int(mesh_editor.get_simple_collision_count(mesh)),
               'convex_count':int(mesh_editor.get_convex_collision_count(mesh)),
               'collision_complexity':str(mesh_editor.get_collision_complexity(mesh)),
               'lod0_triangles':int(mesh.get_num_triangles(0)),
               'section_collision':[bool(mesh_editor.is_section_collision_enabled(mesh,0,index)) for index in range(sections)],
               'body_setup':body.get_path_name() if body else None}
        if body:
            entry['body_settings']={key:read(body,key) for key in
                ('collision_trace_flag','double_sided_geometry','never_needs_cooked_collision_data')}
            entry['public_lod_build_scale']=vec(mesh_editor.get_lod_build_settings(mesh,0).get_editor_property('BuildScale3D'))
            geometry=body.get_editor_property('agg_geom')
            entry['primitive_counts']={}
            for kind in ('sphere_elems','box_elems','sphyl_elems','convex_elems','tapered_capsule_elems','level_set_elems'):
                try:
                    elements=list(geometry.get_editor_property(kind));entry['primitive_counts'][kind]=len(elements)
                    if kind=='box_elems':
                        entry['boxes']=[element.export_text() for element in elements[:24]]
                except Exception as error:
                    entry['primitive_counts'][kind]={'unexposed_or_unavailable':str(error)}
        report['meshes'][mesh.get_path_name()]=entry
    for body_id,door_id in PAIRS:
        body=actors[f'StaticMeshActor_{body_id}'];door=actors[f'StaticMeshActor_{door_id}']
        relative=u.MathLibrary.inverse_transform_location(body.get_actor_transform(),door.get_actor_location())
        axis=0 if abs(relative.x)>abs(relative.y) else 1
        report['cabin_front_evidence'].append({'body':body.get_name(),'door':door.get_name(),
            'door_origin_in_body_local_cm':vec(relative),'front_axis':'XY'[axis],
            'front_sign':1 if vec(relative)[axis]>0 else -1})
    report['same_mesh_uses_in_open_hub']={mesh_path:[actor.get_name() for actor in actors.values()
        if (component:=actor.get_component_by_class(u.StaticMeshComponent)) and component.static_mesh
        and component.static_mesh.get_path_name()==mesh_path] for mesh_path in report['meshes']}
    report['dirty_maps']=[package.get_path_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    report['dirty_content']=[package.get_path_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    u.log('MC_HUB_STATION_COLLISION_INSPECT '+str(destination))
    return report


if __name__=='__main__':
    run()
