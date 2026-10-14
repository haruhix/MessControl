"""Read-only live saved-level geometry and defaults evidence; never starts PIE."""
import json
from pathlib import Path
import unreal as u

out = Path(u.Paths.project_dir()) / 'Artifacts/CataclysmStorm_20261014'
out.mkdir(parents=True, exist_ok=True)
editor = u.get_editor_subsystem(u.EditorActorSubsystem)
rows = []
for actor in editor.get_all_level_actors():
    if not ('gum' in actor.get_actor_label().lower() or isinstance(actor, u.MCTongue)):
        continue
    center, extent = actor.get_actor_bounds(True)
    components = []
    for component in actor.get_components_by_class(u.PrimitiveComponent):
        mesh = component.get_editor_property('static_mesh') if isinstance(component, u.StaticMeshComponent) else None
        components.append(dict(name=component.get_name(), collision=str(component.get_collision_enabled()),
                               profile=str(component.get_collision_profile_name()), mesh=mesh.get_path_name() if mesh else None,
                               pawn_response=str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN)),
                               dynamic_response=str(component.get_collision_response_to_channel(u.CollisionChannel.ECC_WORLD_DYNAMIC))))
    rows.append(dict(label=actor.get_actor_label(), path=actor.get_path_name(),
                     center=[center.x, center.y, center.z], extent=[extent.x, extent.y, extent.z], components=components))
data = dict(actors=rows, dirty=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
            dirty_maps=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
(out / 'GeometryAudit.json').write_text(json.dumps(data, indent=2), encoding='utf-8')
print(json.dumps(data))
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
lower_gum = next(a for a in editor.get_all_level_actors() if a.get_path_name().endswith('StaticMeshActor_218'))
ignored = [a for a in editor.get_all_level_actors() if a != lower_gum]
hits = []
for x in range(-2600, 1201, 150):
    for y in range(-1900, 1901, 150):
        hit = u.SystemLibrary.line_trace_single_for_objects(world, u.Vector(x, y, 450), u.Vector(x, y, -500),
            [u.ObjectTypeQuery.ECC_WORLD_STATIC], True, ignored, u.DrawDebugTrace.NONE, True)
        if hit:
            details = u.get_default_object(u.GameplayStatics).call_method('BreakHitResult', args=(hit,))
            point, normal = details[5], details[7]
            if normal.z >= .71 and point.z >= -100:
                hits.append(dict(point=[point.x, point.y, point.z], normal=[normal.x, normal.y, normal.z]))
data['lower_gum_walkable_samples'] = hits
data['lower_gum_walkable_max_z'] = max((h['point'][2] for h in hits), default=None)
(out / 'GeometryAudit.json').write_text(json.dumps(data, indent=2), encoding='utf-8')
print(json.dumps(dict(walkable_count=len(hits), max_z=data['lower_gum_walkable_max_z'], highest=sorted(hits, key=lambda h: h['point'][2], reverse=True)[:10])))
