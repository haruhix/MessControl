"""Read-only current-map evidence for the manual throat/uvula/collision redesign."""
import json
from pathlib import Path
import unreal as u


def path(obj):
    return obj.get_path_name() if obj else None


def vec(value):
    return [round(value.x, 4), round(value.y, 4), round(value.z, 4)]


def prop(obj, name):
    try:
        return str(obj.get_editor_property(name))
    except Exception:
        return None


world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
rows = []
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    components = actor.get_components_by_class(u.PrimitiveComponent)
    assets = []
    for comp in components:
        mesh = getattr(comp, 'static_mesh', None) if isinstance(comp, u.StaticMeshComponent) else None
        if isinstance(comp, u.SkeletalMeshComponent):
            mesh = comp.get_editor_property('skeletal_mesh_asset')
        if mesh:
            assets.append(mesh.get_name())
    identity = ' '.join([actor.get_actor_label(), actor.get_class().get_name()] + assets).lower()
    if not any(word in identity for word in ('throat', 'uvula', 'minitong', 'gum', 'exit', 'entry', 'disposal', 'palate')):
        continue
    center, extent = actor.get_actor_bounds(False)
    row = dict(name=actor.get_name(), label=actor.get_actor_label(), cls=actor.get_class().get_name(),
               transform=str(actor.get_actor_transform()), center=vec(center), extent=vec(extent),
               hidden=actor.is_hidden_ed(), properties={name: prop(actor, name) for name in (
                   'gate_center', 'gate_size', 'zone_center', 'zone_radius', 'zone_height',
                   'uvula_top', 'uvula_length', 'reverse_authored_open', 'brush_bin')}, components=[])
    for comp in components:
        mesh = getattr(comp, 'static_mesh', None) if isinstance(comp, u.StaticMeshComponent) else None
        if isinstance(comp, u.SkeletalMeshComponent):
            mesh = comp.get_editor_property('skeletal_mesh_asset')
        item = dict(name=comp.get_name(), cls=comp.get_class().get_name(), asset=path(mesh),
                    transform=str(comp.get_world_transform()), collision=str(comp.get_collision_enabled()),
                    profile=str(comp.get_collision_profile_name()), hidden=prop(comp, 'hidden_in_game'),
                    visible=comp.is_visible(), materials=[path(comp.get_material(i)) for i in range(comp.get_num_materials())])
        if isinstance(comp, u.SkeletalMeshComponent):
            item['bones'] = [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
            if mesh:
                item['morphs'] = [str(m.get_name()) for m in mesh.get_editor_property('morph_targets')]
        if isinstance(mesh, u.StaticMesh):
            body = mesh.get_editor_property('body_setup')
            item['collision_trace_flag'] = prop(body, 'collision_trace_flag') if body else None
            item['local_bounds'] = str(mesh.get_bounding_box())
            item['simple_collision_count'] = u.get_editor_subsystem(u.StaticMeshEditorSubsystem).get_simple_collision_count(mesh)
        row['components'].append(item)
    rows.append(row)
dirty = [path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
dirty += [path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
report = dict(world=path(world), pie_worlds=[path(w) for w in u.EditorLevelLibrary.get_pie_worlds(False)], dirty=dirty, actors=rows)
output = Path(u.Paths.project_saved_dir()) / 'ManualIntake' / 'ArenaBefore.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_MANUAL_INTAKE_ARENA ' + str(output))
