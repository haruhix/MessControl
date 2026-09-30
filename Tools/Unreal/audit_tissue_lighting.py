import unreal as u
from pathlib import Path
import json
rows=[]
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if isinstance(a,u.Light):
        c=a.get_component_by_class(u.LightComponent)
        rows.append(dict(name=a.get_actor_label(),type=a.get_class().get_name(),position=str(a.get_actor_location()),rotation=str(a.get_actor_rotation()),intensity=c.get_editor_property('intensity'),color=str(c.get_editor_property('light_color'))))
    if isinstance(a,u.SkyLight):
        c=a.light_component; rows.append(dict(name=a.get_actor_label(),type='SkyLight',intensity=c.intensity))
Path(u.Paths.project_dir(),'Saved/TissueLights.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
u.log('MC_TISSUE_LIGHTS '+json.dumps(rows))
