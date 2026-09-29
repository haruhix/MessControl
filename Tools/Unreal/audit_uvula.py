"""Inspect authored uvula placement without changing the map."""
import unreal as u
import json
from pathlib import Path

def vec(v):
    return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]

rows = []
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if actor.get_class().get_name() != 'MCThroat':
        continue
    row = {'name': actor.get_actor_label(), 'location': vec(actor.get_actor_location()),
           'rotation': str(actor.get_actor_rotation()), 'scale': vec(actor.get_actor_scale3d())}
    for prop in ('uvula_top', 'uvula_length', 'gate_center', 'gate_size', 'zone_center'):
        value = actor.get_editor_property(prop)
        row[prop] = str(value)
    row['components'] = [{'name': c.get_name(), 'location': vec(c.get_world_location()),
                          'scale': vec(c.get_world_scale()), 'rotation': str(c.get_world_rotation())}
                         for c in actor.get_components_by_class(u.SceneComponent)
                         if 'Uvula' in c.get_name()]
    rows.append(row)
Path(u.Paths.project_saved_dir(), 'UvulaAudit.json').write_text(json.dumps(rows, indent=2))
u.log('MC_UVULA_AUDIT_COMPLETE')
