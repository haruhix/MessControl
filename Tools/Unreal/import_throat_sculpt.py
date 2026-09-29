import unreal as u, json
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve(); lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
task=u.AssetImportTask(); task.filename=str(root/'ArtSource/LivingThroat/SK_LivingThroat.fbx'); task.destination_path='/Game/Gameplay/Throat'; task.destination_name='SK_Throat'
task.automated=True; task.replace_existing=True; task.save=True; task.factory=u.FbxFactory()
opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=True; opt.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH
opt.import_materials=False; opt.import_textures=False; opt.import_animations=False; opt.create_physics_asset=False
opt.skeletal_mesh_import_data.set_editor_property('import_morph_targets',True); opt.skeletal_mesh_import_data.set_editor_property('use_t0_as_ref_pose',False)
opt.skeletal_mesh_import_data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
task.options=opt; assets.import_asset_tasks([task])
mesh=lib.load_asset('/Game/Gameplay/Throat/SK_Throat'); assert isinstance(mesh,u.SkeletalMesh)
morphs={str(m.get_name()) for m in mesh.get_editor_property('morph_targets')}
assert {'SwallowOpen','Breath','Peristalsis'}.issubset(morphs),str(morphs)
mat=lib.load_asset('/Game/Art/Materials/M_LivingTissue')
edit.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_SKELETAL_MESH); edit.set_base_material_usage(mat,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)
edit.recompile_material(mat); lib.save_loaded_asset(mat,only_if_is_dirty=False)
slots=list(mesh.get_editor_property('materials'))
for slot in slots: slot.set_editor_property('material_interface',lib.load_asset('/Game/Art/Materials/MI_LivingThroat'))
mesh.set_editor_property('materials',slots); lib.save_loaded_asset(mesh,only_if_is_dirty=False)
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor,u.MCThroat): actor.get_editor_property('sculpted_tissue').set_skeletal_mesh_asset(mesh)
(root/'Saved/ThroatSculptImport.json').write_text(json.dumps({'morphs':sorted(morphs),'bounds':str(mesh.get_bounds())},indent=2),encoding='utf-8')
u.log('MC_THROAT_SCULPT_IMPORTED')
