"""Record actual mouth assignments and texture/material parameter values."""
import unreal as u
import json
from pathlib import Path

lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
data = {'actors':[], 'materials':{}}
for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    label = a.get_actor_label()
    if not any(s in label.lower() for s in ('gum', 'gingiv', 'tongue', 'palate', 'cheek', 'throat', 'light')):
        continue
    row = {'label':label, 'class':a.get_class().get_name(), 'components':[]}
    for c in a.get_components_by_class(u.MeshComponent):
        row['components'].append({'name':c.get_name(), 'materials':[
            c.get_material(i).get_path_name() if c.get_material(i) else '' for i in range(c.get_num_materials())]})
    data['actors'].append(row)
for name in ('MI_MouthGum','MI_MouthCheek','MI_MouthPalate','MI_LivingThroat','M_LivingTissue','M_ThroatSculpt'):
    mat = lib.load_asset('/Game/Art/Materials/'+name)
    row = {'path':mat.get_path_name()}
    if isinstance(mat, u.MaterialInstanceConstant):
        row['parent'] = mat.parent.get_path_name()
        row['scalar'] = {str(p):edit.get_material_instance_scalar_parameter_value(mat,p)
                         for p in edit.get_scalar_parameter_names(mat.parent)}
        row['vector'] = {str(p):str(edit.get_material_instance_vector_parameter_value(mat,p))
                         for p in edit.get_vector_parameter_names(mat.parent)}
    else:
        row['nodes'] = [{'type':n.get_class().get_name(), 'desc':str(n.get_editor_property('desc')),
                         'code':n.get_editor_property('code') if isinstance(n,u.MaterialExpressionCustom) else ''}
                        for n in edit.get_material_expressions(mat)]
    data['materials'][name] = row
Path(u.Paths.project_saved_dir(),'TissueMaterialsAudit.json').write_text(json.dumps(data,indent=2))
u.log('MC_TISSUE_MATERIAL_AUDIT_COMPLETE')
