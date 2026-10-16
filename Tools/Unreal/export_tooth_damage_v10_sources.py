"""Export current active V10 crowns without changing source packages."""
import unreal as u,json,hashlib,shutil
from pathlib import Path
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root=Path(u.Paths.project_dir()).resolve()
out=root/'ContentSource/Meshes/ToothDamageV10';out.mkdir(parents=True,exist_ok=True)
art=root/'Artifacts/ToothDamageV10_20261017';art.mkdir(parents=True,exist_ok=True)
protected={}
for directory in ['Content/Gameplay/Arena/V10','Content/FromBlender10','Content/Art/Materials','Content/Art/Texture/Arena']:
 for f in (root/directory).rglob('*.uasset'):
  protected[f.relative_to(root).as_posix()]=hashlib.sha256(f.read_bytes()).hexdigest()
for rel in ['Content/Maps/L_Mouth.umap','Content/Data/DA_ArenaTooth.uasset','Content/Data/DA_RunRules.uasset']:
 protected[rel]=hashlib.sha256((root/rel).read_bytes()).hexdigest()
(art/'ProtectedFiles.json').write_text(json.dumps(protected,indent=2))
shutil.copy2(root/'Content/Gameplay/Arena/Damage/DA_ToothDamage.uasset',art/'DA_ToothDamage_Before.uasset')
rows=[]
for source in json.loads((root/'Artifacts/ArenaRefresh_20261017/Integration.json').read_text())['active']:
 mesh=u.load_asset(source['mesh']);assert mesh
 task=u.AssetExportTask();task.object=mesh;task.filename=str(out/(mesh.get_name()+'.fbx'))
 task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=u.StaticMeshExporterFBX();task.options=u.FbxExportOption()
 task.options.set_editor_property('ascii',False);task.options.set_editor_property('level_of_detail',False)
 assert u.Exporter.run_asset_export_task(task),task.errors
 box=mesh.get_bounding_box()
 rows.append(dict(mesh=mesh.get_path_name(),name=mesh.get_name(),min=list(box.min.to_tuple()),max=list(box.max.to_tuple())))
(out/'Sources.json').write_text(json.dumps(rows,indent=2))
print('V10_DAMAGE_EXPORTED',len(rows))
