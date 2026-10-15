"""Import ten supplied knight actions against the existing skeleton only.

Never replaces the mesh, skeleton, walk/charge clips or ball transformation.
Run with the existing editor idle; profile connection is a separate step after
the native block/stun/attack slots have been compiled.
"""
import unreal as u,json,hashlib,importlib.util
from pathlib import Path
ROOT=Path(u.Paths.project_dir());OUT=ROOT/'Artifacts/NutKnightAnimations_20261016'
SOURCE=ROOT/'ArtSource/NutKnightUpdate';DEST='/Game/Gameplay/CoreLoop/NutTank/Animations/KnightUpdate'
spec=importlib.util.spec_from_file_location('nut_style_import',Path(__file__).with_name('integrate_nut_style_v2.py'))
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)

def main():
 assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
 assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
 assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
 export=json.loads((SOURCE/'ExportReport.json').read_text());assert export['status']=='complete' and export['source_preserved']
 mesh=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/SK_NutTank');skeleton=mesh.get_editor_property('skeleton')
 legacy=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Idle')
 before=helper.reference_snapshot(skeleton)
 report={'clips':[],'protected_assets_unchanged':False}
 options=u.AnimPoseEvaluationOptions();options.set_editor_property('optional_skeletal_mesh',mesh);options.set_editor_property('extract_root_motion',False)
 for record in export['clips']:
  name=Path(record['file']).stem;target=DEST+'/'+name
  assert not u.EditorAssetLibrary.does_asset_exist(target),target
  task=u.AssetImportTask();task.filename=str(SOURCE/record['file']);task.destination_path=DEST;task.destination_name=name
  task.automated=True;task.save=False;task.replace_existing=False;task.factory=u.FbxFactory()
  task.options=helper.animation_options(skeleton,legacy,record['fps'],name)
  u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);assert len(task.imported_object_paths)==1,task.imported_object_paths
  clip=u.load_asset(task.imported_object_paths[0]);assert isinstance(clip,u.AnimSequence) and clip.get_editor_property('skeleton')==skeleton
  if helper.package_path(clip)!=target:
   assert helper.package_path(clip).startswith(DEST+'/')
   assert u.EditorAssetLibrary.rename_asset(clip.get_path_name(),target);clip=u.load_asset(target)
  assert abs(clip.get_play_length()-record['duration_seconds'])<.004
  clip.set_editor_property('enable_root_motion',False);clip.set_editor_property('force_root_lock',True)
  clip.set_editor_property('root_motion_root_lock',u.RootMotionRootLock.REF_POSE)
  # Suppress procedural guard/breath overlays over the supplied authored motion.
  assert u.AnimationLibrary.add_curve(clip,'MCAuthoredCartoon',u.RawCurveTrackTypes.RCT_FLOAT,False) is None
  u.AnimationLibrary.add_float_curve_keys(clip,'MCAuthoredCartoon',[0.,clip.get_play_length()],[1.,1.])
  u.EditorAssetLibrary.set_metadata_tag(clip,'MCNutKnightSourceAction',record['source_action'])
  u.EditorAssetLibrary.set_metadata_tag(clip,'MCNutKnightSourceSHA256',export['source_sha256_before'])
  samples=[]
  for frame in range(round(record['duration_seconds']*record['fps'])+1):
   t=frame/record['fps'];pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,t,options)
   assert u.AnimPoseExtensions.is_valid(pose)
   pts={n:helper.transform_values(u.AnimPoseExtensions.get_bone_pose(pose,n,u.AnimPoseSpaces.WORLD)) for n in ['root','root_x','hand_l','hand_r','foot_l','foot_r','head_x']}
   samples.append({'time':t,'bones':pts})
  assert u.EditorAssetLibrary.save_loaded_asset(clip)
  report['clips'].append({**record,'asset':clip.get_path_name(),'poses':samples})
  (OUT/'ImportReport.json').write_text(json.dumps(report,indent=2))
 assert helper.reference_snapshot(skeleton)==before,'Skeleton reference changed'
 baseline=json.loads((OUT/'Baseline.json').read_text())
 for p,sha in baseline['protected_asset_hashes'].items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==sha,p
 report.update(protected_assets_unchanged=True,skeleton_reference_unchanged=True,passed=True)
 (OUT/'ImportReport.json').write_text(json.dumps(report,indent=2));print('NUT_KNIGHT_IMPORT_PASS',len(report['clips']))

if __name__=='__main__':main()
