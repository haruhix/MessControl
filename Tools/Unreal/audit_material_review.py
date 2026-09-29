import unreal as u, json
from pathlib import Path
e=u.MaterialEditingLibrary; lib=u.EditorAssetLibrary
data={'materials':{},'lights':[]}
for path in ['/Game/Art/Materials/M_Enamel','/Game/Gameplay/Care/M_ArenaToothCare','/Game/Gameplay/Care/M_ArenaToothRelief','/Game/Gameplay/Arena/Pressure/MI_Pressure_Soft','/Game/Art/Materials/MI_ArenaToothReference']:
    m=lib.load_asset(path); row={}
    if isinstance(m,u.MaterialInstanceConstant):
        row['parent']=m.parent.get_path_name()
        row['scalars']={str(n):e.get_material_instance_scalar_parameter_value(m,n) for n in e.get_scalar_parameter_names(m)}
        row['vectors']={str(n):str(e.get_material_instance_vector_parameter_value(m,n)) for n in e.get_vector_parameter_names(m)}
    else:
        row['model']=str(m.get_editor_property('shading_model'))
        row['nodes']=[]
        for n in e.get_material_expressions(m):
            item={'class':n.get_class().get_name(),'name':n.get_name()}
            for prop in ('parameter_name','default_value','constant','code','description'):
                try:item[prop]=str(n.get_editor_property(prop))
                except Exception:pass
            row['nodes'].append(item)
    data['materials'][path]=row
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if isinstance(a,u.Light):
        c=a.get_component_by_class(u.LightComponent)
        data['lights'].append({'label':a.get_actor_label(),'location':str(a.get_actor_location()),'intensity':c.intensity,'color':str(c.light_color),'specular':c.get_editor_property('specular_scale')})
Path(u.Paths.project_saved_dir(),'MaterialReviewAudit.json').write_text(json.dumps(data,indent=2))
