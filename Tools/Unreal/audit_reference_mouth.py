import unreal as u, json
from pathlib import Path
data=[]
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    entry={'label':a.get_actor_label(),'class':a.get_class().get_name(),'path':a.get_path_name(),'location':str(a.get_actor_location()),'rotation':str(a.get_actor_rotation()),'scale':str(a.get_actor_scale3d())}
    p,e=a.get_actor_bounds(False); entry['bounds']=[str(p),str(e)]
    entry['meshes']=[{'path':c.static_mesh.get_path_name() if c.static_mesh else '', 'materials':[c.get_material(i).get_path_name() if c.get_material(i) else '' for i in range(c.get_num_materials())]} for c in a.get_components_by_class(u.StaticMeshComponent)]
    lights=a.get_components_by_class(u.LightComponent)
    if lights:
        entry['lights']=[{'intensity':c.get_editor_property('intensity'),'color':str(c.get_editor_property('light_color'))} for c in lights]
    data.append(entry)
Path(u.Paths.project_saved_dir(),'ReferenceMouthAudit.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
u.log('MC_REFERENCE_MOUTH_AUDITED')
