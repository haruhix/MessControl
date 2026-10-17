"""Import the seven artist clips without replacing existing meshes or walking."""
import unreal as u,json,hashlib,importlib.util,math,shutil
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();SOURCE=ROOT/'ArtSource/NutWizardUpdate_20261017';OUT=ROOT/'Artifacts/NutWizardUpdate_20261017'
DEST='/Game/Gameplay/CoreLoop/NutWizard/ArtistUpdate_20261017'
spec=importlib.util.spec_from_file_location('wizard_import_helper',Path(__file__).with_name('integrate_nut_style_v2.py'))
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)

def main():
 assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
 export=json.loads((SOURCE/'ExportReport.json').read_text());assert export['status']=='complete' and export['source_preserved']
 original=u.load_asset('/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard');skeleton=original.get_editor_property('skeleton')
 reference=helper.reference_snapshot(skeleton);legacy=u.load_asset('/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Idle')
 profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain');before=profile.get_editor_property('settings').export_text()
 profile_backup=OUT/'DA_NutRain_Before.uasset'
 if not profile_backup.exists():shutil.copy2(ROOT/'Content/Gameplay/CoreLoop/DA_NutRain.uasset',profile_backup)
 results=[];options=u.AnimPoseEvaluationOptions();options.set_editor_properties(dict(optional_skeletal_mesh=original,extract_root_motion=False))
 for record in export['clips']:
  name=Path(record['file']).stem;path=DEST+'/Animations/'+name
  if u.EditorAssetLibrary.does_asset_exist(path):assert u.EditorAssetLibrary.get_metadata_tag(u.load_asset(path),'MCWizardUpdateSource')==export['source_sha256_before']
  task=u.AssetImportTask();task.set_editor_properties(dict(filename=str(SOURCE/record['file']),destination_path=DEST+'/Animations',destination_name=name,
   automated=True,save=False,replace_existing=True,replace_existing_settings=True,factory=u.FbxFactory()))
  task.options=helper.animation_options(skeleton,legacy,record['fps'],name)
  u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);assert len(task.imported_object_paths)==1
  clip=u.load_asset(task.imported_object_paths[0]);assert isinstance(clip,u.AnimSequence) and clip.get_editor_property('skeleton')==skeleton
  assert abs(clip.get_play_length()-record['duration_seconds'])<.004
  clip.set_editor_properties(dict(enable_root_motion=False,force_root_lock=True,root_motion_root_lock=u.RootMotionRootLock.REF_POSE))
  markers=['MCAuthoredWizard']+(['MCSpellGesture'] if record['label'] in ('Cast','Summon','Rain','Teleport') else [])
  for marker in markers:
   if not u.AnimationLibrary.does_curve_exist(clip,marker,u.RawCurveTrackTypes.RCT_FLOAT):u.AnimationLibrary.add_curve(clip,marker,u.RawCurveTrackTypes.RCT_FLOAT,False)
   u.AnimationLibrary.add_float_curve_keys(clip,marker,[0.,clip.get_play_length()],[1.,1.])
  u.EditorAssetLibrary.set_metadata_tag(clip,'MCWizardUpdateSource',export['source_sha256_before'])
  u.EditorAssetLibrary.set_metadata_tag(clip,'MCWizardSourceAction',record['source_action'])
  poses=[];root0=None;root_delta=0
  for frame in range(record['end_frame']-record['start_frame']+1):
   t=frame/record['fps'];pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,t,options);assert u.AnimPoseExtensions.is_valid(pose)
   points={str(n):helper.transform_values(u.AnimPoseExtensions.get_bone_pose(pose,n,u.AnimPoseSpaces.WORLD)) for n in ['root','root_x','head_x','hand_l','hand_r','foot_l','foot_r']}
   assert all(math.isfinite(v) for values in points.values() for v in values)
   if root0 is None:root0=points['root']
   root_delta=max(root_delta,max(abs(a-b) for a,b in zip(root0,points['root'])))
   poses.append(dict(time=t,bones=points))
  assert root_delta<.001,(record['label'],root_delta)
  assert u.EditorAssetLibrary.save_loaded_asset(clip)
  results.append(dict(**record,asset=clip.get_path_name(),poses=poses,root_max_delta=root_delta))
 mesh_path=DEST+'/SK_NutWizard_ArtistUpdate'
 if not u.EditorAssetLibrary.does_asset_exist(mesh_path):mesh=u.EditorAssetLibrary.duplicate_asset(original.get_path_name(),mesh_path)
 else:mesh=u.load_asset(mesh_path)
 deformer=u.load_asset('/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth');assert deformer
 mesh.set_editor_property('default_mesh_deformer',deformer)
 u.EditorAssetLibrary.set_metadata_tag(mesh,'MCWizardUpdateSource',export['source_sha256_before'])
 assert mesh.get_editor_property('skeleton')==skeleton
 assert u.EditorAssetLibrary.save_loaded_asset(mesh)
 assert helper.reference_snapshot(skeleton)==reference
 assert profile.get_editor_property('settings').export_text()==before
 report=dict(passed=True,clips=results,skeleton_reference_unchanged=True,profile_unchanged=True,derived_mesh=mesh.get_path_name(),mesh_deformer=deformer.get_path_name(),source_preserved=hashlib.sha256(Path(export['source']).read_bytes()).hexdigest()==export['source_sha256_before'])
 (OUT/'ImportReport.json').write_text(json.dumps(report,indent=2));print('WIZARD_UPDATE_IMPORTED',len(results),'source/skeleton/walking preserved')

if __name__=='__main__':
 previous=u.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
 try:
  u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0');main()
 finally:u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX '+str(previous))
