"""Assign the user's exact M_ArenaTooth to all current arena teeth."""
import unreal as u,json,hashlib,shutil
from pathlib import Path

root=Path(u.Paths.project_dir());out=root/'Artifacts/ArenaToothMaterial_20261017';out.mkdir(exist_ok=True)
lib=u.EditorAssetLibrary;actors=u.get_editor_subsystem(u.EditorActorSubsystem);level=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not level.is_in_play_in_editor(),'Preserve existing PIE'
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name()=='/Game/Maps/L_Mouth.L_Mouth'
material=lib.load_asset('/Game/Art/Materials/M_ArenaTooth');assert material
markers=[];decorations=[]
for actor in actors.get_all_level_actors():
 if isinstance(actor,u.MCArenaToothSocket):
  c=actor.preview
  if c.static_mesh and c.static_mesh.get_path_name().startswith('/Game/Gameplay/Arena/V10/SM_Teeth'):markers.append((actor,c))
 elif isinstance(actor,u.StaticMeshActor):
  c=actor.static_mesh_component
  if c.static_mesh and c.static_mesh.get_path_name().startswith('/Game/Gameplay/Arena/V10/SM_Teeth'):decorations.append((actor,c))
assert len(markers)==12 and len(decorations)==14,(len(markers),len(decorations))
profile=lib.load_asset('/Game/Data/DA_ArenaTooth')
report=dict(material=material.get_path_name(),active_count=len(markers),decorative_count=len(decorations),parameters=[str(n) for n in u.MaterialEditingLibrary.get_scalar_parameter_names(material)],profile_before=profile.get_editor_property('gameplay_material').get_path_name(),actors=[],dirty_content_before=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
protected=[]
for directory in ['Content/Gameplay/Arena/V10/Edges','Content/FromBlender10']:
 protected.extend(p for p in (root/directory).rglob('*.uasset'))
protected.extend(root/p for p in ['Content/Art/Materials/M_ArenaTooth.uasset','Content/Art/Materials/Arena/MI_Gum_v2.uasset','Content/Gameplay/Arena/V10/SM_TongueSurface.uasset','Content/Gameplay/Arena/Pressure/MI_Pressure_Soft.uasset','Content/Data/DA_Tongue.uasset','Content/Data/DA_RunRules.uasset'])
report['protected_hashes']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
backup=out/'DiskBefore';backup.mkdir(exist_ok=True)
for p in [root/'Content/Maps/L_Mouth.umap',root/'Content/Data/DA_ArenaTooth.uasset']+[root/'Content'/Path(c.static_mesh.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset') for a,c in markers+decorations]:
 target=backup/p.name
 if not target.exists():shutil.copy2(p,target)
with u.ScopedEditorTransaction('Use M_ArenaTooth for current arena teeth'):
 for actor,c in markers+decorations:
  mesh=c.static_mesh
  report['actors'].append(dict(actor=actor.get_path_name(),label=actor.get_actor_label(),mesh=mesh.get_path_name(),materials_before=[c.get_material(i).get_path_name() for i in range(c.get_num_materials())]))
  for i in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(i,material)
  for i in range(c.get_num_materials()):c.set_material(i,material)
  assert lib.save_loaded_asset(mesh,only_if_is_dirty=True)
 profile.set_editor_property('gameplay_material',material);assert lib.save_loaded_asset(profile,only_if_is_dirty=True)
 assert level.save_current_level()
report['dirty_content_after']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['protected_differences']=[p for p,value in report['protected_hashes'].items() if hashlib.sha256((root/p).read_bytes()).hexdigest()!=value]
assert not report['protected_differences']
report['passed']=True
(out/'Assignment.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('ARENA_TOOTH_MATERIAL_ASSIGNED',material.get_path_name(),len(markers),len(decorations),'parameters',report['parameters'])
