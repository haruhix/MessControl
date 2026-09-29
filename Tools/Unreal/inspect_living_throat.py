"""Read the currently open arena without loading or changing the map."""
import unreal as u
import json
from pathlib import Path
rows=[]
def vec(v): return [round(v.x,2),round(v.y,2),round(v.z,2)]
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if not any(s in a.get_actor_label().lower() for s in ('throat','hole','gum','wall','tongue','palat','uvula','start')): continue
    p,e=a.get_actor_bounds(False)
    row=dict(name=a.get_actor_label(),path=a.get_path_name(),cls=a.get_class().get_name(),location=vec(a.get_actor_location()),rotation=str(a.get_actor_rotation()),scale=vec(a.get_actor_scale3d()),bounds=[vec(p),vec(e)])
    row['meshes']=[dict(path=c.static_mesh.get_path_name() if c.static_mesh else '',materials=[c.get_material(i).get_path_name() if c.get_material(i) else '' for i in range(c.get_num_materials())]) for c in a.get_components_by_class(u.StaticMeshComponent)]
    rows.append(row)
Path(u.Paths.project_saved_dir(),'LivingThroatAudit.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
u.log('MC_LIVING_THROAT_AUDIT_COMPLETE')
