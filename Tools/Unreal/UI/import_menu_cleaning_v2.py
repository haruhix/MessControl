"""Menu-only animation import and LevelSequence. Existing game rig is retained."""
import unreal as u,json,math,hashlib
from pathlib import Path
ROOT=Path('E:/DEVGAME/MessControl');OUT=ROOT/'ArtSource/UI/MenuSceneV2_20261022/Blender/Exports'
DEST='/Game/UI/MenuShowcase/AnatomyV2'
es=u.get_editor_subsystem(u.EditorActorSubsystem);ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith(DEST+'/L_MenuAnatomyV2.')
lib=u.EditorAssetLibrary;assets=u.AssetToolsHelpers.get_asset_tools()
source_mesh=lib.load_asset('/Game/Data/DA_PlayerAppearance').get_editor_property('skeletal_mesh')
# Facial curve metadata belongs to a skeleton. Keep menu acting entirely private.
skel=lib.load_asset(DEST+'/Animation/SKEL_MenuCleaner') or lib.duplicate_asset(source_mesh.skeleton.get_path_name(),DEST+'/Animation/SKEL_MenuCleaner')
mesh=lib.load_asset(DEST+'/Animation/SK_MenuCleanerActing')
mesh_fbx_sha=hashlib.sha256((OUT/'SK_MenuCleanerActing.fbx').read_bytes()).hexdigest()
flag='Interchange.FeatureFlags.Import.FBX';previous=u.SystemLibrary.get_console_variable_int_value(flag)
try:
 u.SystemLibrary.execute_console_command(None,flag+' 0')
 if not mesh or lib.get_metadata_tag(mesh,'MC_MenuMeshFBX_SHA256')!=mesh_fbx_sha:
  mt=u.AssetImportTask();mt.filename=str(OUT/'SK_MenuCleanerActing.fbx');mt.destination_path=DEST+'/Animation';mt.destination_name='SK_MenuCleanerActing';mt.automated=True;mt.replace_existing=True;mt.save=True;mt.factory=u.FbxFactory()
  mo=u.FbxImportUI();mo.automated_import_should_detect_type=False;mo.import_mesh=True;mo.import_as_skeletal=True;mo.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH;mo.import_animations=False;mo.import_materials=False;mo.import_textures=False;mo.create_physics_asset=False;mo.skeleton=skel
  mo.skeletal_mesh_import_data.set_editor_property('import_morph_targets',True)
  mo.skeletal_mesh_import_data.set_editor_property('update_skeleton_reference_pose',False)
  mo.skeletal_mesh_import_data.set_editor_property('use_t0_as_ref_pose',False)
  mo.skeletal_mesh_import_data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
  mt.options=mo;assets.import_asset_tasks([mt]);mesh=lib.load_asset(DEST+'/Animation/SK_MenuCleanerActing')
  assert mesh and mesh.skeleton==skel
  slots=list(mesh.materials);source_slots=list(source_mesh.materials)
  assert len(slots)==len(source_slots)
  for i,s in enumerate(slots):s.set_editor_property('material_interface',source_slots[i].material_interface)
  mesh.set_editor_property('materials',slots)
  lib.set_metadata_tag(mesh,'MC_MenuMeshFBX_SHA256',mesh_fbx_sha);lib.save_loaded_asset(mesh)
 t=u.AssetImportTask();t.filename=str(OUT/'A_Menu_CleanTooth_Loop_V2.fbx');t.destination_path=DEST+'/Animation';t.destination_name='A_MenuCleaningDirector_V2';t.automated=True;t.replace_existing=True;t.save=True;t.factory=u.FbxFactory()
 o=u.FbxImportUI();o.automated_import_should_detect_type=False;o.import_mesh=False;o.import_as_skeletal=True;o.mesh_type_to_import=u.FBXImportType.FBXIT_ANIMATION;o.import_animations=True;o.skeleton=skel;o.import_materials=False;o.import_textures=False;o.create_physics_asset=False
 o.anim_sequence_import_data.set_editor_property('animation_length',u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME);o.anim_sequence_import_data.set_editor_property('import_bone_tracks',True);o.anim_sequence_import_data.set_editor_property('use_default_sample_rate',False);o.anim_sequence_import_data.set_editor_property('custom_sample_rate',24)
 t.options=o;assets.import_asset_tasks([t])
finally:u.SystemLibrary.execute_console_command(None,flag+' '+str(previous))
clip=lib.load_asset(DEST+'/Animation/A_MenuCleaningDirector_V2')
assert clip and clip.get_editor_property('skeleton')==skel and abs(clip.get_play_length()-12)<.05,(clip.get_path_name() if clip else None,clip.skeleton.get_path_name() if clip else None,clip.get_play_length() if clip else None)
acting_path=ROOT/'ArtSource/UI/MenuSceneV2_20261022/Blender/Exports/Menu_ActingCurves.json'
if acting_path.exists():
 acting=json.loads(acting_path.read_text(encoding='utf8'))
 # Reimports preserve editor-authored curves; remove this owned clip's old
 # acting set so a previous Pupil_Contract cannot fight the new Pupil_Dilate.
 for old_curve in u.AnimationLibrary.get_animation_curve_names(clip,u.RawCurveTrackTypes.RCT_FLOAT):
  u.AnimationLibrary.remove_curve(clip,old_curve,False)
 for name,keys in acting['curves'].items():
  if u.AnimationLibrary.does_curve_exist(clip,name,u.RawCurveTrackTypes.RCT_FLOAT):u.AnimationLibrary.remove_curve(clip,name,False)
  u.AnimationLibrary.add_curve(clip,name,u.RawCurveTrackTypes.RCT_FLOAT,False)
  u.AnimationLibrary.add_float_curve_keys(clip,name,[v[0] for v in keys],[v[1] for v in keys])
  u.AnimationLibrary.add_curve_meta_data(skel,name,False)
  u.AnimationLibrary.set_curve_meta_data_morph_target(skel,name,True)
 lib.save_loaded_asset(clip);lib.save_loaded_asset(skel)
hero=next(a for a in es.get_all_level_actors() if 'MC_MenuV2_Hero' in [str(x) for x in a.tags]);brush=next(a for a in es.get_all_level_actors() if 'MC_MenuV2_Brush' in [str(x) for x in a.tags])
hero.skeletal_mesh_component.set_skeletal_mesh(mesh)
layout=json.loads((ROOT/'ArtSource/UI/MenuSceneV2_20261022/Blender/Menu_ContactLayout.json').read_text())
working_tooth=next(a for a in es.get_all_level_actors() if 'Tooth 04 |' in a.get_actor_label())
working_tooth.set_actor_scale3d(u.Vector(*layout['tooth_scale']))
working_tooth.set_actor_location(u.Vector(*layout['tooth_location']),False,True)
working_tooth.set_actor_rotation(u.Rotator(yaw=layout['tooth_yaw']),False)
hero.set_actor_location(u.Vector(*layout['hero_location']),False,True);hero.set_actor_scale3d(u.Vector(layout['hero_scale'],layout['hero_scale'],layout['hero_scale']));hero.set_actor_rotation(u.Rotator(yaw=layout['hero_yaw']),False)
hero.skeletal_mesh_component.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE);hero.skeletal_mesh_component.set_animation(clip)
hero.skeletal_mesh_component.set_update_animation_in_editor(True)
hero.skeletal_mesh_component.clear_morph_targets()
seqpath=DEST+'/LS_MenuCleaningV2';seq=lib.load_asset(seqpath)
if not seq:seq=assets.create_asset('LS_MenuCleaningV2',DEST,u.LevelSequence,u.LevelSequenceFactoryNew())
for b in seq.get_bindings():b.remove()
for tr in seq.get_tracks():seq.remove_track(tr)
seq.set_display_rate(u.FrameRate(24,1));seq.set_playback_start(0);seq.set_playback_end(288)
binding=seq.add_possessable(hero);track=binding.add_track(u.MovieSceneSkeletalAnimationTrack);sec=track.add_section();sec.set_range(0,288)
p=sec.params;p.animation=clip;sec.params=p
brushbinding=seq.add_possessable(brush);tr=brushbinding.add_track(u.MovieScene3DTransformTrack);sec=tr.add_section();sec.set_range(0,288)
channels=sec.get_all_channels();assert len(channels)==9
data=json.loads((OUT/'A_Menu_CleanTooth_Loop_V2_BrushWorld.json').read_text(encoding='utf8'))
ht=hero.get_actor_transform()
samples=[]
for sample in data['samples']:
 loc=sample['location_m'];q=sample['quaternion_wxyz']
 pos=ht.transform_location(u.Vector(loc[0]*100,-loc[1]*100,loc[2]*100))
 rot=(ht.rotation*u.Quat(-q[1],q[2],-q[3],q[0])).rotator()
 scale=ht.scale3d*(sample['scale'][0]/.01)
 values=[pos.x,pos.y,pos.z,rot.roll,rot.pitch,rot.yaw,scale.x,scale.y,scale.z]
 for channel,v in zip(channels,values):channel.add_key(u.FrameNumber(sample['frame']-1),v,interpolation=u.MovieSceneKeyInterpolation.LINEAR)
 samples.append({'frame':sample['frame']-1,'location':[pos.x,pos.y,pos.z],'rotation':[rot.pitch,rot.yaw,rot.roll]})
first=samples[0];brush.set_actor_location(u.Vector(*first['location']),False,True);brush.set_actor_rotation(u.Rotator(pitch=first['rotation'][0],yaw=first['rotation'][1],roll=first['rotation'][2]),False);brush.set_actor_scale3d(ht.scale3d*(data['samples'][0]['scale'][0]/.01))
camera=next(a for a in es.get_all_level_actors() if 'MC_MenuV2_Camera' in [str(x) for x in a.tags]);camera.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0)
cb=seq.add_possessable(camera);cut=seq.add_track(u.MovieSceneCameraCutTrack);cs=cut.add_section();cs.set_range(0,192)
camera_id=u.MovieSceneObjectBindingID();camera_id.set_editor_property('guid',cb.get_id());cs.set_camera_binding_id(camera_id);cs.set_range(0,288)
player=next((a for a in es.get_all_level_actors() if isinstance(a,u.LevelSequenceActor)),None)
if not player:player=es.spawn_actor_from_class(u.LevelSequenceActor,u.Vector())
player.set_sequence(seq);player.set_actor_label('MENU V2 | Cleaning loop')
settings=player.get_editor_property('playback_settings');settings.auto_play=True;settings.loop_count=u.MovieSceneSequenceLoopCount(-1);player.set_editor_property('playback_settings',settings)
lib.save_loaded_asset(seq);assert ls.save_current_level()
u.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq);u.LevelSequenceEditorBlueprintLibrary.set_current_time(20)
(ROOT/'Artifacts/MenuSceneV2_20261022').mkdir(exist_ok=True,parents=True)
(ROOT/'Artifacts/MenuSceneV2_20261022/AnimationImport.json').write_text(json.dumps({'clip':clip.get_path_name(),'skeleton':skel.get_path_name(),'length':clip.get_play_length(),'sequence':seq.get_path_name(),'brush_basis':'mirror Y; pending native frame review','brush_samples':samples},indent=2),encoding='utf8')
# Capture is explicit and separate; queuing a viewport resize here made review
# state depend on a later editor/window resize and is not an import operation.
