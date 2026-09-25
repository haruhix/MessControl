"""Export the artist's static geometry for non-destructive gameplay mesh preparation."""
from pathlib import Path
import unreal as u
out=Path(u.Paths.project_saved_dir())/'ArenaIntegration'; out.mkdir(parents=True,exist_ok=True)
for name in ('SM_Location','SM_Location_teeth'):
    mesh=u.load_asset('/Game/Art/Meshes/Arena/'+name)
    if not mesh: raise RuntimeError('Missing artist mesh '+name)
    task=u.AssetExportTask(); task.object=mesh; task.filename=str(out/(name+'.fbx'))
    task.automated=True; task.prompt=False; task.replace_identical=True
    task.exporter=u.StaticMeshExporterFBX(); task.options=u.FbxExportOption()
    task.options.set_editor_property('level_of_detail',False)
    task.options.set_editor_property('collision',False)
    if not u.Exporter.run_asset_export_task(task): raise RuntimeError('Export failed: '+name)
    u.log('MC_ARENA_EXPORT '+name+' triangles='+str(mesh.get_num_triangles(0)))
u.SystemLibrary.quit_editor()
