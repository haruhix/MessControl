"""Import 24 V10 damage meshes and retain all existing damage-profile rows."""
import unreal as u,json
from pathlib import Path
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root=Path(u.Paths.project_dir()).resolve();source=root/'ContentSource/Meshes/ToothDamageV10'
out=Path(globals().get('TOOTH_DAMAGE_OUTPUT',root/'Artifacts/ToothDamageV10_20261017'));out.mkdir(parents=True,exist_ok=True)
folder='/Game/Gameplay/Arena/V10/Damage'
tools=u.AssetToolsHelpers.get_asset_tools();ss=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
materials={'MC_Damage_Enamel':u.load_asset('/Game/Art/Materials/M_ArenaTooth'),
 'MC_Damage_Dentin':u.load_asset('/Game/Gameplay/Arena/Damage/M_MC_Damage_Dentin'),
 'MC_Damage_Crack':u.load_asset('/Game/Gameplay/Arena/Damage/M_MC_Damage_Crack')}
assert all(materials.values())
report=[]
for row in json.loads((source/'Variants.json').read_text()):
 name=row['variant'];task=u.AssetImportTask()
 task.set_editor_properties(dict(filename=str(source/(name+'.fbx')),destination_path=folder,destination_name=name,
  automated=True,replace_existing=True,save=False,factory=u.FbxFactory()))
 options=u.FbxImportUI();options.set_editor_properties(dict(import_mesh=True,import_materials=False,import_textures=False,
  import_as_skeletal=False,mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH,automated_import_should_detect_type=False))
 options.static_mesh_import_data.set_editor_properties(dict(combine_meshes=True,generate_lightmap_u_vs=False,auto_generate_collision=False,
  convert_scene=True,convert_scene_unit=True,normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS))
 task.options=options;tools.import_asset_tasks([task]);paths=task.get_editor_property('imported_object_paths')
 assert len(paths)==1,(name,paths)
 mesh=u.load_asset(paths[0]);assert isinstance(mesh,u.StaticMesh)
 mesh.set_editor_property('allow_cpu_access',True)
 slots=[]
 for i,slot in enumerate(mesh.get_editor_property('static_materials')):
  slotname=str(slot.material_slot_name);assert slotname in materials,(name,slotname)
  mesh.set_material(i,materials[slotname]);slots.append(slotname)
 assert 'MC_Damage_Enamel' in slots and 'MC_Damage_Dentin' in slots,(name,slots)
 mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
 assert ss.get_num_uv_channels(mesh,0)==3,(name,ss.get_num_uv_channels(mesh,0))
 box=mesh.get_bounding_box();original=u.load_asset(row['source']).get_bounding_box()
 size=box.max-box.min;original_size=original.max-original.min
 assert abs(box.min.z-original.min.z)<.1 and all(.55*b<a<1.02*b for a,b in zip(size.to_tuple(),original_size.to_tuple())),(name,str(box),str(original))
 assert u.EditorAssetLibrary.save_loaded_asset(mesh)
 report.append(dict(mesh=mesh.get_path_name(),source=row['source'],slots=slots,uvs=3,min=list(box.min.to_tuple()),max=list(box.max.to_tuple()),cpu_access=True))
(out/'ImportedAssets.json').write_text(json.dumps(report,indent=2))
profile=u.load_asset('/Game/Gameplay/Arena/Damage/DA_ToothDamage');existing=list(profile.get_editor_property('Teeth'))
sources=json.loads((source/'Sources.json').read_text());new_paths={r['mesh'] for r in sources}
rows=[r for r in existing if r.get_editor_property('Intact').get_path_name() not in new_paths]
retained=len(rows)
row_type=u.get_type_from_struct(u.load_object(None,'/Script/MessControl.MCToothDamageMeshes'))
for s in sources:
 row=row_type();row.set_editor_properties(dict(Intact=u.load_asset(s['mesh']),Chipped=u.load_asset(folder+'/'+s['name']+'_Chipped'),Broken=u.load_asset(folder+'/'+s['name']+'_Broken')))
 assert all(row.get_editor_property(k) for k in ('Intact','Chipped','Broken'))
 rows.append(row)
row_paths=lambda values:[tuple(r.get_editor_property(k).get_path_name() for k in ('Intact','Chipped','Broken')) for r in values]
if row_paths(existing)!=row_paths(rows):
 profile.set_editor_property('Teeth',rows);assert u.EditorAssetLibrary.save_loaded_asset(profile)
linked=[{k:r.get_editor_property(k).get_path_name() for k in ('Intact','Chipped','Broken')} for r in profile.get_editor_property('Teeth')]
(out/'Profile.json').write_text(json.dumps(dict(retained=retained,new=12,rows=linked),indent=2))
print('TOOTH_DAMAGE_V10_INTEGRATED',len(report),'meshes;',retained,'old rows retained;',len(rows),'total')
