"""Reimport fourteen corrected knight actions in place, preserving their consumers."""
import unreal as u
import hashlib, importlib.util, json, math
from pathlib import Path

ROOT=Path(u.Paths.project_dir())
OUT=ROOT/'Artifacts/NutKnightCorrected_20261016'
SOURCE=ROOT/'ArtSource/NutKnightCorrected'
DEST='/Game/Gameplay/CoreLoop/NutTank/Animations/KnightBallUpdate'
spec=importlib.util.spec_from_file_location('nut_corrected_import_helper',Path(__file__).with_name('integrate_nut_style_v2.py'))
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
BINDINGS={'idle':'Idle','unshielded_idle':'Idle','melee':'Attack1','melee2':'Attack2','combo':'Combo','block':'Block','hit':'Hit','shield_break':'Stun','stun_hit':'StunHit','jump':'Jump','death':'Death','transform':'BallStart','ball_end':'BallEnd','head_left':'HeadLeft','head_right':'HeadRight'}

def snapshot(clip):
    return {'length':clip.get_play_length(),
            'notifies':[event.export_text() for event in u.AnimationLibrary.get_animation_notify_events(clip)],
            'sync_markers':[marker.export_text() for marker in u.AnimationLibrary.get_animation_sync_markers(clip)],
            'float_curves':{str(n).casefold():[list(a) for a in u.AnimationLibrary.get_float_keys(clip,n)] for n in u.AnimationLibrary.get_animation_curve_names(clip,u.RawCurveTrackTypes.RCT_FLOAT)},
            'root_motion':{k:str(clip.get_editor_property(k)) for k in ['enable_root_motion','force_root_lock','root_motion_root_lock']}}

def main():
    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    export=json.loads((SOURCE/'ExportReport.json').read_text())
    baseline=json.loads((OUT/'Baseline.json').read_text())
    assert export['status']=='complete' and export['source_preserved'] and len(export['clips'])==14
    assert hashlib.sha256(Path(export['source']).read_bytes()).hexdigest()==baseline['source_sha256']
    for record in export['files']:
        assert hashlib.sha256((SOURCE/record['file']).read_bytes()).hexdigest()==record['sha256']
        assert record['bind_max_delta']<.0001 and record['fbx']['root_animation_constant']
    for relative,sha in baseline['target_hashes'].items():assert hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()==sha,relative
    mesh=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/SK_NutTank')
    skeleton=mesh.get_editor_property('skeleton')
    legacy=u.load_asset('/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Idle')
    reference=helper.reference_snapshot(skeleton)
    profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain')
    boss=profile.get_editor_property('settings').get_editor_property('boss')
    profile_before=boss.export_text()
    for field,label in BINDINGS.items():assert helper.package_path(boss.get_editor_property('tank_'+field+'_animation'))==DEST+'/A_NutKnight_'+label
    assert abs(boss.get_editor_property('tank_jump_animation_seconds')-154/30)<.004
    before={record['label']:snapshot(u.load_asset(DEST+'/'+Path(record['file']).stem)) for record in export['clips']}
    (OUT/'AssetBaseline.json').write_text(json.dumps({'reference':reference,'profile':profile_before,'clips':before},indent=2))
    report={'passed':False,'clips':[],'source':export['source'],'source_sha256':baseline['source_sha256']}
    opt=u.AnimPoseEvaluationOptions();opt.set_editor_property('optional_skeletal_mesh',mesh);opt.set_editor_property('extract_root_motion',False)
    for record in export['clips']:
        label=record['label'];name=Path(record['file']).stem;target=DEST+'/'+name
        assert abs(before[label]['length']-record['duration_seconds'])<.004,('Timing changed',label)
        task=u.AssetImportTask();task.filename=str(SOURCE/record['file']);task.destination_path=DEST;task.destination_name=name
        task.automated=True;task.save=False;task.replace_existing=True;task.replace_existing_settings=True;task.factory=u.FbxFactory()
        task.options=helper.animation_options(skeleton,legacy,record['fps'],name)
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        assert len(task.imported_object_paths)==1,task.imported_object_paths
        clip=u.load_asset(task.imported_object_paths[0])
        assert isinstance(clip,u.AnimSequence) and helper.package_path(clip)==target
        assert clip.get_editor_property('skeleton')==skeleton
        assert abs(clip.get_play_length()-record['duration_seconds'])<.004
        clip.set_editor_property('enable_root_motion',False);clip.set_editor_property('force_root_lock',True)
        clip.set_editor_property('root_motion_root_lock',u.RootMotionRootLock.REF_POSE)
        if not u.AnimationLibrary.does_curve_exist(clip,'MCAuthoredCartoon',u.RawCurveTrackTypes.RCT_FLOAT):
            u.AnimationLibrary.add_curve(clip,'MCAuthoredCartoon',u.RawCurveTrackTypes.RCT_FLOAT,False)
        u.AnimationLibrary.add_float_curve_keys(clip,'MCAuthoredCartoon',[0.,clip.get_play_length()],[1.,1.])
        after=snapshot(clip)
        assert after==before[label],('Animation settings/curves/notifies changed',label,before[label],after)
        u.EditorAssetLibrary.set_metadata_tag(clip,'MCNutKnightSourceAction',record['source_action'])
        u.EditorAssetLibrary.set_metadata_tag(clip,'MCNutKnightSourceSHA256',baseline['source_sha256'])
        samples=[];scales={};max_root_delta=0.;root0=None
        for frame in range(round(record['duration_seconds']*record['fps'])+1):
            t=frame/record['fps'];pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,t,opt)
            assert u.AnimPoseExtensions.is_valid(pose)
            for n in reference['bones']:
                values=helper.transform_values(u.AnimPoseExtensions.get_bone_pose(pose,n,u.AnimPoseSpaces.LOCAL))
                assert all(math.isfinite(v) for v in values),(label,frame,n)
                error=max(abs(a-b) for a,b in zip(values[7:],reference['local'][n][7:]))
                if error>.001:scales[n]=max(error,scales.get(n,0))
            points={n:helper.transform_values(u.AnimPoseExtensions.get_bone_pose(pose,n,u.AnimPoseSpaces.WORLD)) for n in ['root','root_x','hand_l','hand_r','foot_l','foot_r','head_x']}
            if root0 is None:root0=points['root']
            max_root_delta=max(max_root_delta,max(abs(a-b) for a,b in zip(root0,points['root'])))
            samples.append({'time':t,'bones':points})
        assert max_root_delta<.001,('Animated root after import',label,max_root_delta)
        assert u.EditorAssetLibrary.save_loaded_asset(clip)
        report['clips'].append({**record,'asset':clip.get_path_name(),'settings_preserved':True,'poses':samples,'scale_channels_differing_from_reference':scales,'root_max_delta':max_root_delta})
        (OUT/'ImportReport.json').write_text(json.dumps(report,indent=2))
        print('KNIGHT_CORRECTED_REIMPORTED',label)
    assert helper.reference_snapshot(skeleton)==reference,'Skeleton reference changed'
    assert boss.export_text()==profile_before,'Profile tuning changed'
    for relative,sha in baseline['protected_asset_hashes'].items():assert hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()==sha,relative
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    report.update(passed=True,skeleton_reference_unchanged=True,profile_unchanged=True,protected_assets_unchanged=True)
    (OUT/'ImportReport.json').write_text(json.dumps(report,indent=2))
    print('KNIGHT_CORRECTED_IMPORT_PASS',len(report['clips']))

if __name__=='__main__':
    # Force the FbxFactory policy used by the existing skeletal mesh and clips.
    previous=u.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
    try:
        u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
        main()
    finally:
        u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX '+str(previous))
