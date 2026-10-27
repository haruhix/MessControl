"""Import two bone-only clips onto the saved player skeleton after C++ build.
Only A_Teeth_Glide1, A_Teeth_Glide2 and DA_ToothAnimation are saved.
The current hero, skeleton, customization and appearance assets are not replaced.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
SOURCE=ROOT/'ArtSource/CharacterAnimation/Glide_20261027'
DEST='/Game/Gameplay/CharacterCurrent/Animations/Glide'
lib=u.EditorAssetLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
appearance=lib.load_asset('/Game/Data/DA_PlayerAppearance')
mesh=appearance.get_editor_property('skeletal_mesh')
assert mesh.get_path_name()=='/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay.SK_CharacterGameplay',mesh.get_path_name()
skeleton=mesh.get_editor_property('skeleton')
assert skeleton.get_path_name()=='/Game/Art/Meshes/Character/SM_Teeth_rig_Skeleton.SM_Teeth_rig_Skeleton',skeleton.get_path_name()
profile=lib.load_asset('/Game/Data/DA_ToothAnimation')
assert isinstance(profile,u.MCAnimationProfile),'Build the current Editor target before importing the new profile fields.'
flag='Interchange.FeatureFlags.Import.FBX'
previous=u.SystemLibrary.get_console_variable_int_value(flag)
clips={}
report={'mesh':mesh.get_path_name(),'skeleton':skeleton.get_path_name(),'saved':[],'clips':[]}
try:
    u.SystemLibrary.execute_console_command(None,flag+' 0')
    for label in ('Glide1','Glide2'):
        name='A_Teeth_'+label
        doc=json.loads((SOURCE/(name+'_facial.json')).read_text(encoding='utf8'))
        task=u.AssetImportTask();task.filename=str(SOURCE/(name+'.fbx'))
        task.destination_path=DEST;task.destination_name=name
        task.automated=True;task.replace_existing=True;task.save=False;task.factory=u.FbxFactory()
        options=u.FbxImportUI();options.automated_import_should_detect_type=False
        options.import_mesh=False;options.import_as_skeletal=True
        options.mesh_type_to_import=u.FBXImportType.FBXIT_ANIMATION
        options.import_animations=True;options.skeleton=skeleton
        options.import_materials=False;options.import_textures=False;options.create_physics_asset=False
        data=options.anim_sequence_import_data
        data.set_editor_property('animation_length',u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property('import_bone_tracks',True)
        data.set_editor_property('use_default_sample_rate',False)
        data.set_editor_property('custom_sample_rate',24)
        data.set_editor_property('import_custom_attribute',False)
        task.options=options;assets.import_asset_tasks([task])
        imported=[lib.load_asset(p) for p in task.imported_object_paths]
        assert len(imported)==1 and isinstance(imported[0],u.AnimSequence),task.imported_object_paths
        clip=imported[0]
        assert clip.get_editor_property('skeleton')==skeleton
        assert abs(clip.get_play_length()-doc['duration'])<.01,(name,clip.get_play_length(),doc['duration'])
        clip.set_editor_property('enable_root_motion',False)
        clip.set_editor_property('force_root_lock',True)
        clip.set_editor_property('root_motion_root_lock',u.RootMotionRootLock.REF_POSE)
        # Owned float curves are sampled by MCExpressionComponent, never by a
        # second morph writer or a replacement facial carrier mesh.
        for existing in u.AnimationLibrary.get_animation_curve_names(clip,u.RawCurveTrackTypes.RCT_FLOAT):
            if str(existing).startswith('MCSlide_'):u.AnimationLibrary.remove_curve(clip,existing,False)
        times=[sample['time'] for sample in doc['samples']]
        for morph in doc['active_curves']:
            curve='MCSlide_'+morph
            u.AnimationLibrary.add_curve(clip,curve,u.RawCurveTrackTypes.RCT_FLOAT,False)
            u.AnimationLibrary.add_float_curve_keys(clip,curve,times,[sample['values'][morph] for sample in doc['samples']])
            assert u.AnimationLibrary.does_curve_exist(clip,curve,u.RawCurveTrackTypes.RCT_FLOAT),curve
        if label=='Glide2':
            contacts=json.loads((SOURCE/'Glide2_ContactCurves.json').read_text(encoding='utf8'))
            for curve,keys in contacts['curves'].items():
                u.AnimationLibrary.add_curve(clip,curve,u.RawCurveTrackTypes.RCT_FLOAT,False)
                u.AnimationLibrary.add_float_curve_keys(clip,curve,[key['time'] for key in keys],[key['value'] for key in keys])
        lib.set_metadata_tag(clip,'MC_SlideSourceSHA256','9414109ffd1b74263dd629d7aa5f5279f37e193ecd99124a8e0d635b608cdeaf')
        lib.set_metadata_tag(clip,'MC_SlideFBXSHA256',hashlib.sha256((SOURCE/(name+'.fbx')).read_bytes()).hexdigest())
        assert lib.save_loaded_asset(clip,only_if_is_dirty=False)
        clips[label]=clip;report['saved'].append(clip.get_path_name())
        report['clips'].append({'asset':clip.get_path_name(),'duration':clip.get_play_length(),'curves':list(doc['active_curves']),'source_timeline':doc['source_timeline']})
finally:
    u.SystemLibrary.execute_console_command(None,flag+' '+str(previous))
profile.set_editor_property('glide',clips['Glide1'])
# The replicated Slipping state consumes the complete failed-balance/fall clip.
# Balance recovery still consumes only Glide1; no clip is trimmed here.
profile.set_editor_property('glide_strong',clips['Glide2'])
assert lib.save_loaded_asset(profile,only_if_is_dirty=False)
report['saved'].append(profile.get_path_name())
(SOURCE/'ImportReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_GLIDE_IMPORT_PASS '+json.dumps(report))
