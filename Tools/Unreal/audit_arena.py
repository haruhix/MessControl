"""Read-only report of the authored arena, collision assets and gameplay markers."""
from pathlib import Path
import json, traceback, unreal as u
report={}
def path(obj): return obj.get_path_name() if obj else None
def vec(v): return [round(v.x,3),round(v.y,3),round(v.z,3)]
try:
    levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
    if not levels.load_level('/Game/Maps/L_Mouth'): raise RuntimeError('Cannot load L_Mouth')
    actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
    meshes=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    report['actors']=[]
    for actor in actors:
        kind=actor.get_class().get_name()
        comps=actor.get_components_by_class(u.StaticMeshComponent)
        if not comps and kind not in ('PlayerStart','MCFoodDisposal'): continue
        origin,extent=actor.get_actor_bounds(False)
        item=dict(label=actor.get_actor_label(),kind=kind,location=vec(actor.get_actor_location()),rotation=str(actor.get_actor_rotation()),scale=vec(actor.get_actor_scale3d()),bounds_center=vec(origin),bounds_extent=vec(extent))
        if isinstance(actor,u.MCArenaToothSocket): item['tooth_id']=actor.get_editor_property('tooth_id')
        if isinstance(actor,u.MCFoodDisposal): item['brush_bin']=actor.get_editor_property('brush_bin')
        item['components']=[]
        for comp in comps:
            mesh=comp.static_mesh
            d=dict(mesh=path(mesh),collision=str(comp.get_collision_enabled()),profile=str(comp.get_collision_profile_name()),hidden=comp.get_editor_property('hidden_in_game'),materials=[path(comp.get_material(i)) for i in range(comp.get_num_materials())])
            if mesh:
                body=mesh.get_editor_property('body_setup')
                d.update(simple_collision=meshes.get_simple_collision_count(mesh),trace_flag=str(body.get_editor_property('collision_trace_flag')) if body else None,local_bounds=str(mesh.get_bounding_box()))
            item['components'].append(d)
        report['actors'].append(item)
    for name in ('DA_Day01','DA_CoffeeWater','DA_PlayerAppearance'):
        asset=u.load_asset('/Game/Data/'+name)
        report[name]=str(asset)
        if name=='DA_Day01':
            report['day_settings']={p:str(asset.get_editor_property(p)) for p in ('arena_half_size','flood_height','paddle_acceleration','flow_acceleration','anchor_reach')}
        if name=='DA_CoffeeWater': report['water_settings']=str(asset.get_editor_property('settings'))
except Exception:
    report['error']=traceback.format_exc()
finally:
    out=Path(u.Paths.project_saved_dir())/'ArenaAudit.json'
    out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    u.log('MC_ARENA_AUDIT '+str(out))
    u.SystemLibrary.quit_editor()
