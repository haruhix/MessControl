"""Export the eight saved arena tooth sources; does not save or modify the source packages."""
import json
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve();folder=root/'ContentSource/Meshes/ToothDamage';folder.mkdir(parents=True,exist_ok=True)
rows=[]
for name in ['SM_Teeth_L_01','SM_Teeth_L_02','SM_Teeth_L_03','SM_Teeth_l_04','SM_Teeth_R_01','SM_Teeth_R_02','SM_Teeth_R_03','SM_Teeth_R_04']:
    mesh=u.load_asset('/Game/Gameplay/Arena/V2/'+name);assert mesh
    task=u.AssetExportTask();task.object=mesh;task.filename=str(folder/(name+'.fbx'))
    task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=u.StaticMeshExporterFBX();task.options=u.FbxExportOption()
    task.options.set_editor_property('ascii',False);task.options.set_editor_property('level_of_detail',False)
    assert u.Exporter.run_asset_export_task(task),task.errors
    box=mesh.get_bounding_box();rows.append({'mesh':mesh.get_path_name(),'name':name,'min':list(box.min.to_tuple()),'max':list(box.max.to_tuple())})
(folder/'Sources.json').write_text(json.dumps(rows,indent=2))
print('TOOTH_SOURCES_EXPORTED',len(rows))
