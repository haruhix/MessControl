import unreal as u
import json
from pathlib import Path
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
rows=[]
for a in actors:
    if isinstance(a,u.MCArenaToothSocket) or 'MCCameraBounds' in [str(t) for t in a.tags] or isinstance(a,u.MCTongue):
        origin,extent=a.get_actor_bounds(False)
        rows.append(dict(name=a.get_actor_label(),type=a.get_class().get_name(),location=str(a.get_actor_location()),origin=str(origin),extent=str(extent)))
    if isinstance(a,u.StaticMeshActor) and a.static_mesh_component.static_mesh and '/FromBlender2/' in a.static_mesh_component.static_mesh.get_path_name():
        origin,extent=a.get_actor_bounds(False)
        rows.append(dict(name=a.get_actor_label(),mesh=a.static_mesh_component.static_mesh.get_path_name(),origin=str(origin),extent=str(extent),collision=str(a.static_mesh_component.get_collision_enabled())))
Path(u.Paths.project_dir(),'Saved/ApprovalArenaAudit.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
u.log('MC_APPROVAL_ARENA_AUDIT '+json.dumps(rows))
