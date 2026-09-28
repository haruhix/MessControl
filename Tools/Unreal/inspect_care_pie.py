"""Read the active play session through the native MCP editor console."""
import json
from pathlib import Path
import unreal as u
def v(p): return [p.x,p.y,p.z]
rows=[]
for w in u.EditorLevelLibrary.get_pie_worlds(True):
    actors=[]
    for cls in [u.MCArenaTooth,u.MCToothCharacter,u.MCMouthSurface]:
        for a in u.GameplayStatics.get_all_actors_of_class(w,cls):
            r=dict(name=a.get_name(),path=a.get_path_name(),location=v(a.get_actor_location()),authority=a.has_authority())
            if isinstance(a,u.MCArenaTooth):
                origin,extent,radius=u.SystemLibrary.get_component_bounds(a.visual)
                r.update(bounds=[v(origin),v(extent)],mask=list(a.grime_mask),coffee=a.status.state.coffee_left,material=a.visual.get_material(0).get_path_name())
            actors.append(r)
    rows.append(dict(world=w.get_path_name(),actors=actors))
Path(u.Paths.project_saved_dir(),'CarePIEInspect.json').write_text(json.dumps(rows,indent=2))
u.log('MC_CARE_PIE_INSPECTED')
