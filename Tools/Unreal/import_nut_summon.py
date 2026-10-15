"""Import the supplied summon into its own folder/skeleton; retain all boss art."""
import unreal as u,json,hashlib,importlib.util
from pathlib import Path
ROOT=Path(u.Paths.project_dir());SOURCE=ROOT/'ArtSource/NutSummon';DEST='/Game/Gameplay/CoreLoop/NutSummon';OUT=ROOT/'Artifacts/MageSummons_20261016'
spec=importlib.util.spec_from_file_location('nut_style',Path(__file__).with_name('integrate_nut_style_v2.py'));helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
def task(name,file,options=None,folder=DEST):
 t=u.AssetImportTask();t.filename=str(SOURCE/file);t.destination_path=folder;t.destination_name=name;t.automated=True;t.save=False;t.replace_existing=False
 if options:t.factory=u.FbxFactory();t.options=options
 u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t]);return t
def main():
 assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
 assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
 assert not u.EditorAssetLibrary.does_directory_exist(DEST),'New source folder already exists; inspect before reimport'
 report=json.loads((SOURCE/'ExportReport.json').read_text());assert report['status']=='complete' and report['source_preserved']
 old=u.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX');u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
 try:
  options=u.FbxImportUI();options.set_editor_properties(dict(automated_import_should_detect_type=False,mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH,original_import_type=u.FBXImportType.FBXIT_SKELETAL_MESH,import_as_skeletal=True,import_mesh=True,import_animations=False,import_materials=False,import_textures=False,create_physics_asset=False))
  data=options.get_editor_property('skeletal_mesh_import_data');data.set_editor_properties(dict(update_skeleton_reference_pose=False,use_t0_as_ref_pose=False,normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS))
  mt=task('SK_NutSummon','SK_NutSummon.fbx',options);objects=[u.load_asset(p) for p in mt.imported_object_paths];mesh=next(x for x in objects if isinstance(x,u.SkeletalMesh));skeleton=mesh.get_editor_property('skeleton')
  assert mesh.get_path_name().startswith(DEST+'/') and skeleton.get_path_name().startswith(DEST+'/')
  textures={}
  for name in ['Walnut','HandEye']:
   t=task('T_Summon_'+name,'Textures/T_Summon_'+name+'.png',folder=DEST+'/Textures')
   textures[name]=next(u.load_asset(p) for p in t.imported_object_paths if isinstance(u.load_asset(p),u.Texture2D))
  materials={}
  for name in textures:
   mat=u.AssetToolsHelpers.get_asset_tools().create_asset('M_Summon_'+name,DEST+'/Materials',u.Material,u.MaterialFactoryNew());assert mat
   tex=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionTextureSample,-450,0);tex.set_editor_property('texture',textures[name])
   assert u.MaterialEditingLibrary.connect_material_property(tex,'RGB',u.MaterialProperty.MP_BASE_COLOR)
   rough=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionConstant,-450,180);rough.set_editor_property('r',.65)
   assert u.MaterialEditingLibrary.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
   u.MaterialEditingLibrary.recompile_material(mat);materials[name]=mat
  slots=list(mesh.get_editor_property('materials'));assignment=[]
  for slot in slots:
   label=str(slot.material_slot_name);slot.material_interface=materials['Walnut' if 'walnut' in label.lower() else 'HandEye'];assignment.append({'slot':label,'material':slot.material_interface.get_path_name()})
  mesh.set_editor_property('materials',slots)
  legacy=u.load_asset('/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Idle')
  at=task('A_NutSummon_Run','A_NutSummon_Run.fbx',helper.animation_options(skeleton,legacy,report['fps'],'A_NutSummon_Run'),DEST+'/Animations')
  clip=next(u.load_asset(p) for p in at.imported_object_paths if isinstance(u.load_asset(p),u.AnimSequence))
  assert clip.get_editor_property('skeleton')==skeleton and abs(clip.get_play_length()-.8)<.004
  clip.set_editor_property('enable_root_motion',False);clip.set_editor_property('force_root_lock',True);clip.set_editor_property('root_motion_root_lock',u.RootMotionRootLock.REF_POSE)
  opts=u.AnimPoseEvaluationOptions();opts.set_editor_property('optional_skeletal_mesh',mesh);opts.set_editor_property('extract_root_motion',False)
  poses=[]
  for i in range(25):
   p=u.AnimPoseExtensions.get_anim_pose_at_time(clip,i/30,opts);assert u.AnimPoseExtensions.is_valid(p)
   poses.append({'frame':i,'bones':{n:helper.transform_values(u.AnimPoseExtensions.get_bone_pose(p,n,u.AnimPoseSpaces.WORLD)) for n in ['root','root_x','foot_l','foot_r','hand_l','hand_r']}})
  for obj in [mesh,skeleton,clip,*materials.values(),*textures.values()]:assert u.EditorAssetLibrary.save_loaded_asset(obj)
  baseline=json.loads((OUT/'Baseline.json').read_text())
  for path,sha in baseline['legacy_assets'].items():assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==sha,path
  bounds=mesh.get_bounds();result={'passed':True,'mesh':mesh.get_path_name(),'skeleton':skeleton.get_path_name(),'run':clip.get_path_name(),'duration':clip.get_play_length(),'materials':assignment,'poses':poses,'bounds_origin':list(bounds.origin.to_tuple()),'bounds_extent':list(bounds.box_extent.to_tuple()),'all_boss_assets_preserved':True}
  (OUT/'ImportReport.json').write_text(json.dumps(result,indent=2));print('SUMMON_IMPORT_PASS',json.dumps({k:v for k,v in result.items() if k!='poses'}))
 finally:u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX '+str(old))
if __name__=='__main__':main()
