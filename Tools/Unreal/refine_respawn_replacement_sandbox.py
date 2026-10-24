"""Finish the isolated replacement take using an exact static arena patch.

Run from another clean map. Production gameplay and source packages are inputs.
Only the owned /Game/Tests/RespawnReplacement map and sequence are saved.
"""
import json,math
from pathlib import Path
import unreal as u
ROOT='/Game/Tests/RespawnReplacement'
MAP=ROOT+'/L_RespawnReplacementStage'
SEQ=ROOT+'/LS_ReplacementFinal'
TAG='MC.RespawnReplacement.Sandbox'
OUT=Path('E:/CodexWork/RespawnReplacementFinal_20261024')
END=180

def main():
 editor=u.get_editor_subsystem(u.UnrealEditorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem);actors=u.get_editor_subsystem(u.EditorActorSubsystem)
 assert not levels.is_in_play_in_editor()
 assert not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
 final_info=json.loads((OUT/'FinalClips.json').read_text())
 corrections={r['frame']:r['actor_z_correction_to_reference_feet'] for r in final_info['contact_guide']}
 assert all(f in corrections for f in [65,68,73,79,90,104,179])
 for required in ['SK_ReplacementPreview','A_OldBody_Final','A_NewBody_Final']:
  assert u.EditorAssetLibrary.does_asset_exist(ROOT+'/Performance/'+required), 'Bake final clips first: '+required
 before=editor.get_editor_world().get_path_name()
 if before.split('.')[0]!=MAP:assert levels.load_level(MAP)
 world=editor.get_editor_world()
 def role(name):
  found=[a for a in actors.get_all_level_actors() if name in [str(t) for t in a.tags]]
  assert len(found)==1,(name,len(found))
  assert TAG in [str(t) for t in found[0].tags]
  return found[0]
 ns={};path=Path(__file__).parent/'respawn_arena_patch.py';exec(compile(path.read_text(),str(path),'exec'),ns)
 patch=ns['build_patch'](world,actors,TAG)
 # Native tongue construction initializes pressure vertex data to zero.
 # Reproduce that rest state on this component, without editing the source mesh.
 u.MeshVertexPainterLibrary.paint_vertices_single_color(patch['components']['ActualTongue'],u.LinearColor(0,0,0,1),False)
 for name in ['TongueStage','GumBed','EmptySocket']:
  matches=[a for a in actors.get_all_level_actors() if name in [str(t) for t in a.tags]]
  for a in matches:
   assert TAG in [str(t) for t in a.tags];a.destroy_actor()
 tq=ns['make_support_query'](patch['components']['ActualTongue'])
 gq=ns['make_support_query'](patch['components']['ActualGum'])
 reserve=patch['reserve_actor'];rt=reserve.get_actor_transform();p=rt.translation
 # Find the real tongue below the inward clearance, never assume Z=0.
 land=None
 for distance in [420,480,540,600,660,720]:
  candidate=tq(u.Vector(p.x,p.y-distance,0))
  if candidate is not None:land=candidate;break
 assert land is not None,'No tongue support within the emergence reach'
 old_start=tq(u.Vector(land.x+175,land.y+15,0));assert old_start is not None
 old_end=tq(u.Vector(land.x+220,land.y-60,0));assert old_end is not None
 gum_hits=[gq(u.Vector(p.x+dx,p.y+dy,0)) for dx,dy in [(0,0),(-30,0),(30,0),(0,-45),(0,45)]]
 gum_hits=[v for v in gum_hits if v is not None]
 assert gum_hits,'No actual gum lip near the selected source'
 gum_z=max(v.z for v in gum_hits)
 crown_origin,crown_extent=reserve.get_actor_bounds(False)
 launch_z=max(gum_z,crown_origin.z+crown_extent.z-115.)
 launch_y=p.y-45
 lib=u.EditorAssetLibrary;assets=u.AssetToolsHelpers.get_asset_tools()
 sequence=lib.load_asset(SEQ) if lib.does_asset_exist(SEQ) else assets.create_asset('LS_ReplacementFinal',ROOT,u.LevelSequence,u.LevelSequenceFactoryNew())
 for binding in list(sequence.get_bindings()):binding.remove()
 for track in list(sequence.get_tracks()):sequence.remove_track(track)
 sequence.set_display_rate(u.FrameRate(30,1));sequence.set_playback_start(0);sequence.set_playback_end(END)
 sequence.set_view_range_start(0);sequence.set_view_range_end(6);sequence.set_work_range_start(0);sequence.set_work_range_end(6)
 def transform(a,keys):
  # Keep the idle editor view at the opening pose, too.
  _,initial_pos,initial_rot,initial_scale=keys[0]
  a.set_actor_location(u.Vector(*initial_pos),False,False)
  a.set_actor_rotation(u.Rotator(pitch=initial_rot[0],yaw=initial_rot[1],roll=initial_rot[2]),False)
  a.set_actor_scale3d(u.Vector(*initial_scale))
  binding=sequence.add_possessable(a);track=binding.add_track(u.MovieScene3DTransformTrack);sec=track.add_section();sec.set_range(0,END);sec.set_completion_mode(u.MovieSceneCompletionMode.RESTORE_STATE)
  channels=sec.get_all_channels()
  for frame,pos,rot,scale in keys:
   values=(*pos,rot[2],rot[0],rot[1],*scale)
   for ch,value in zip(channels,values):ch.add_key(u.FrameNumber(frame),float(value),interpolation=u.MovieSceneKeyInterpolation.AUTO)
  return binding
 def add_anim(binding,clip):
  track=binding.add_track(u.MovieSceneSkeletalAnimationTrack);sec=track.add_section();sec.set_range(0,END);sec.set_completion_mode(u.MovieSceneCompletionMode.RESTORE_STATE)
  params=sec.params;params.animation=clip;sec.params=params
 mesh=lib.load_asset(ROOT+'/Performance/SK_ReplacementPreview');assert mesh
 old=role('OldBody');new=role('NewBody')
 for a,name in [(old,'A_OldBody_Final'),(new,'A_NewBody_Final')]:
  comp=a.skeletal_mesh_component;comp.set_skeletal_mesh_asset(mesh);clip=lib.load_asset(ROOT+'/Performance/'+name);assert clip
  comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE);comp.set_animation(clip)
  comp.set_update_animation_in_editor(True)
  comp.set_editor_property('visibility_based_anim_tick_option',u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
 S=(1,1,1);R=(0,180,0)
 oz=old_start.z;ez=old_end.z
 old_keys=[(0,(old_start.x,old_start.y,oz),R,S),(5,(old_start.x-4,old_start.y-5,oz+4),(2,179,-15),S),(11,(old_end.x+25,old_end.y+8,ez+46),(0,175,-65),S),(15,(old_end.x+6,old_end.y+2,ez+42),(0,174,-82),S),(21,(old_end.x,old_end.y,ez+25),(0,174,-76),S),(30,(old_end.x,old_end.y,ez+25),(0,174,-76),S),(179,(old_end.x,old_end.y,ez+25),(0,174,-76),S)]
 add_anim(transform(old,old_keys),lib.load_asset(ROOT+'/Performance/A_OldBody_Final'))
 rr=rt.rotation.rotator();base=(rr.pitch,rr.yaw,rr.roll);sc=tuple(rt.scale3d.to_tuple())
 def rp(f,dy,dz,tilt=0):return (f,(p.x,p.y+dy,p.z+dz),(base[0],base[1],base[2]+tilt),sc)
 # Move back behind the real gingival lip before the reserve leaves the view.
 transform(reserve,[rp(0,0,0),rp(22,0,0),rp(26,0,0,-3),rp(30,0,0,2),rp(33,22,-40,-1),rp(36,100,-220),rp(39,180,-520),rp(42,205,-820),rp(179,205,-820)])
 # Continuous overlap: the small crown emerges while the large one withdraws.
 peak=max(land.z+165,launch_z+65)
 new_keys=[(0,(p.x,launch_y,launch_z-125),R,S),(35,(p.x,launch_y,launch_z-125),R,S),(38,(p.x,launch_y,launch_z-80),(-8,180,0),(1.025,1.025,.95)),(41,(p.x,launch_y-10,launch_z-35),(-10,180,0),S),(44,(p.x,launch_y-32,launch_z+12),(-8,180,-2),(.985,.985,1.03)),(48,(p.x,launch_y-105,peak-22),(-6,180,-2),S),(52,(p.x,(launch_y+land.y)*.5,peak),(-2,180,0),S),(57,(p.x,land.y+105,land.z+(peak-land.z)*.72),(5,180,2),S),(61,(p.x,land.y+38,land.z+55),(8,180,1),(.99,.99,1.02))]
 for f,pos,rot,s in [(65,(land.x,land.y),(6,180,0),(1.045,1.045,.92)),(68,(land.x,land.y),(2,180,-1),(1.045,1.045,.92)),(73,(land.x,land.y),(-2,180,1),(.99,.99,1.025)),(79,(land.x,land.y),R,S),(90,(land.x,land.y),(0,171,0),S),(104,(land.x,land.y),R,S),(179,(land.x,land.y),R,S)]:
  # Additional visual seating after recovery, checked in the capture camera.
  # The bone guide alone is insufficient to judge rendered sole contact.
  # Retain the original impact compression.
  recovery_seat=6.0*max(0.,min(1.,(f-73.)/17.))
  new_keys.append((f,(pos[0],pos[1],land.z+corrections.get(f,0)-recovery_seat),rot,s))
 add_anim(transform(new,new_keys),lib.load_asset(ROOT+'/Performance/A_NewBody_Final'))
 # Broader, lower-contrast light than the initial blocking pass.
 light_specs=[('Key',(-280,land.y-180,land.z+650),(1,.90,.80),90000,650,550),('Rim',(300,p.y+180,launch_z+450),(.67,.86,1),65000,450,400),('Fill',(-480,land.y+150,land.z+220),(.73,.92,1),23000,600,480)]
 for name,pos,col,power,width,height in light_specs:
  a=role(name);a.set_actor_location(u.Vector(*pos),False,False);a.set_actor_rotation(u.MathLibrary.find_look_at_rotation(a.get_actor_location(),u.Vector(land.x,land.y+100,land.z+70)),False)
  c=a.rect_light_component;c.set_editor_property('intensity_units',u.LightUnits.LUMENS);c.set_editor_property('intensity',power);c.set_editor_property('source_width',width);c.set_editor_property('source_height',height);c.set_editor_property('attenuation_radius',2600.)
  c.set_light_color(u.LinearColor(*col,1));c.set_cast_shadows(name!='Fill')
 camera=role('PresentationCamera');cc=camera.get_cine_camera_component()
 cc.set_editor_property('current_focal_length',38.)
 pp=cc.post_process_settings;pp.override_auto_exposure_method=True;pp.auto_exposure_method=u.AutoExposureMethod.AEM_MANUAL;pp.override_auto_exposure_bias=True;pp.auto_exposure_bias=-6.
 pp.override_bloom_intensity=True;pp.bloom_intensity=.08;pp.override_vignette_intensity=True;pp.vignette_intensity=.16
 pp.override_ambient_occlusion_intensity=True;pp.ambient_occlusion_intensity=.55
 cc.post_process_settings=pp
 # Keep old body, source gap and new body in one continuous camera move.
 target0=u.Vector(land.x-5,land.y+140,land.z+145);cam0=u.Vector(p.x-540,land.y-950,land.z+650)
 target1=u.Vector(land.x-55,land.y+70,land.z+65);cam1=u.Vector(land.x-420,land.y-730,land.z+400)
 def ck(frame,pos,target):
  r=u.MathLibrary.find_look_at_rotation(pos,target);return (frame,tuple(pos.to_tuple()),(r.pitch,r.yaw,r.roll),S)
 cb=transform(camera,[ck(0,cam0,target0),ck(32,cam0,target0),ck(57,cam0,target0+u.Vector(0,0,20)),ck(85,cam1,target1),ck(179,cam1,target1)])
 cuts=sequence.add_track(u.MovieSceneCameraCutTrack);cut=cuts.add_section();cut.set_range(0,END);cut.set_camera_binding_id(u.MovieSceneSequenceExtensions.get_binding_id(sequence,cb));cut.set_completion_mode(u.MovieSceneCompletionMode.RESTORE_STATE)
 audio=[]
 for sound_name,frame,volume in [('S_Hit',15,.14),('S_Jump',37,.22),('S_Step',65,.30)]:
  sound=lib.load_asset('/Game/Audio/'+sound_name);assert isinstance(sound,u.SoundWave)
  track=sequence.add_track(u.MovieSceneAudioTrack);section=track.add_section();section.set_sound(sound)
  section.set_range(frame,min(END,frame+math.ceil(sound.get_editor_property('duration')*30)))
  channels=section.get_all_channels();assert len(channels)>=2
  channels[0].set_default(volume);channels[1].set_default(1.)
  audio.append({'sound':sound.get_path_name(),'frame':frame,'volume':volume,'pitch':1.})
 role('LoopTake').set_sequence(sequence)
 assert lib.save_loaded_asset(sequence,only_if_is_dirty=False)
 assert levels.save_current_level()
 report={'map':MAP,'sequence':SEQ,'source_map_before':before,'arena_patch':patch['report'],'landing':str(land),'old_start':str(old_start),'old_end':str(old_end),'launch_gum_height':launch_z,'peak_root_height':peak,'new_body_keys':new_keys,'audio_cues':audio,'source_packages_saved':False,'gameplay_integration':False,'review':'pending'}
 (OUT/'FinalAuthoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
 u.log('RESPAWN_FINAL_STAGE_AUTHORED '+str(land))
main()
