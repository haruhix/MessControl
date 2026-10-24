"""Capture the owned visual sandbox in solo PIE; restore all transient state."""
import builtins,json,time
from pathlib import Path
import unreal as u

BASE=Path('E:/CodexWork/RespawnReplacement_20261024')
OPTIONS=json.loads((BASE/'CaptureOptions.json').read_text())
class Capture:
 def __init__(self):
  self.callback=None;self.capture=None;self.engine=None;self.old=None;self.hidden_pawns=[];self.player=None;self.original_time=None;self.was_playing=False;self.report={}
  try:self.start()
  except Exception as exc:
   self.finish(str(exc));raise
 def start(self):
  self.index=0;self.warm=0;self.settle=0;self.started=time.monotonic()
  self.mode=OPTIONS.get('mode','poses');self.out=Path(OPTIONS['output'])
  assert self.mode in ('poses','video') and self.out.is_absolute() and self.out.drive.upper()=='E:' and not any(self.out.glob('*.png'))
  self.out.mkdir(parents=True,exist_ok=True)
  worlds=u.EditorLevelLibrary.get_pie_worlds(False);assert len(worlds)==1
  self.world=worlds[0];assert 'L_RespawnReplacementStage' in self.world.get_path_name()
  assert u.GameplayStatics.get_game_mode(self.world).get_class()==u.GameModeBase.static_class()
  sequences=[a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.LevelSequenceActor) if 'LoopTake' in [str(t) for t in a.tags]]
  assert len(sequences)==1;self.sequence=sequences[0]
  self.player=self.sequence.get_editor_property('sequence_player')
  self.was_playing=self.player.is_playing();self.original_time=self.player.get_current_time();self.player.pause()
  cameras=[a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.CineCameraActor) if 'PresentationCamera' in [str(t) for t in a.tags]]
  assert len(cameras)==1;self.cam=cameras[0]
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.old=(self.engine.get_editor_property('bUseFixedFrameRate'),self.engine.get_editor_property('FixedFrameRate'))
  self.engine.set_editor_property('FixedFrameRate',30.);self.engine.set_editor_property('bUseFixedFrameRate',True)
  lib=u.get_default_object(u.GameplayStatics);t=self.cam.get_actor_transform()
  self.capture=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.SceneCapture2D,t,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  lib.call_method('FinishSpawningActor',args=(self.capture,t,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.width=int(OPTIONS.get('width',1280));self.height=int(OPTIONS.get('height',720))
  assert self.width>=640 and self.height>=360 and self.width<=3840 and self.height<=2160
  self.target=u.RenderingLibrary.create_render_target2d(self.world,self.width,self.height,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.025,.02,.03,1),False,False)
  self.comp=self.capture.capture_component2d
  self.comp.set_editor_property('hidden_actors',list(u.GameplayStatics.get_all_actors_of_class(self.world,u.Pawn)))
  for k,v in dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=self.cam.camera_component.field_of_view,post_process_settings=self.cam.camera_component.post_process_settings,post_process_blend_weight=1.).items():self.comp.set_editor_property(k,v)
  if 'exposure_bias' in OPTIONS:
   pp=self.comp.post_process_settings;pp.override_auto_exposure_bias=True;pp.auto_exposure_bias=float(OPTIONS['exposure_bias']);self.comp.post_process_settings=pp
  self.comp.set_editor_property('show_flag_settings',[u.EngineShowFlagsSetting(show_flag_name='MotionBlur',enabled=False)])
  self.poses=OPTIONS.get('frames',[0,25,38,50,66,95]);self.total=OPTIONS.get('total',180)
  self.report={'map':self.world.get_path_name(),'mode':self.mode,'status':'running','fps':30,'resolution':[self.width,self.height],'frames':[],'audio':False,'network':False,'production_rules_exercised':False,'exposure_bias':self.comp.post_process_settings.auto_exposure_bias,'light_values':[{'label':a.get_actor_label(),'units':str(a.rect_light_component.intensity_units),'intensity':a.rect_light_component.intensity} for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.RectLight)]}
  self.callback=u.register_slate_post_tick_callback(self.tick)
 def position(self,frame):
  p=u.MovieSceneSequencePlaybackParams();p.set_editor_property('position_type',u.MovieScenePositionType.FRAME);p.set_editor_property('frame',u.FrameTime(u.FrameNumber(frame)));p.set_editor_property('update_method',u.UpdatePositionMethod.JUMP)
  self.player.set_playback_position(p)
 def tick(self,dt):
  try:
   if time.monotonic()-self.started>360:raise RuntimeError('Capture timeout')
   self.capture.set_actor_transform(self.cam.get_actor_transform(),False,False)
   if self.warm<24:
    self.position(0);self.comp.capture_scene();self.warm+=1
    if self.warm==24:
     if self.mode=='video':self.player.play()
     else:self.position(self.poses[0])
    return
   if self.mode=='poses' and self.settle<8:
    self.comp.capture_scene();self.settle+=1;return
   self.comp.capture_scene();name='%04d.png'%(self.poses[self.index] if self.mode=='poses' else self.index)
   u.RenderingLibrary.export_render_target(self.world,self.target,str(self.out),name)
   assert (self.out/name).exists()
   qt=self.player.get_current_time();frame=qt.time.frame_number.value+qt.time.sub_frame
   new=next(a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.SkeletalMeshActor) if 'NewBody' in [str(t) for t in a.tags])
   reserve=next(a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.StaticMeshActor) if 'ActualReserve' in [str(t) for t in a.tags])
   bounds_origin,bounds_extent=reserve.get_actor_bounds(False)
   self.report['frames'].append({'file':name,'sequence_frame':frame,'dt':u.GameplayStatics.get_world_delta_seconds(self.world),'new_body_transform':str(new.get_actor_transform()),'reserve_crown_top_z':bounds_origin.z+bounds_extent.z,'new_foot_l':list(new.skeletal_mesh_component.get_socket_location('toes_01_l').to_tuple()),'new_foot_r':list(new.skeletal_mesh_component.get_socket_location('toes_01_r').to_tuple())})
   self.index+=1
   if self.index>=(len(self.poses) if self.mode=='poses' else self.total):self.finish(None);return
   if self.mode=='poses':self.position(self.poses[self.index]);self.settle=0
   if self.index%30==0:self.write()
  except Exception as e:self.finish(str(e))
 def write(self):
  (self.out.parent/(self.out.name+'_Capture.json')).write_text(json.dumps(self.report,indent=2),encoding='utf8')
 def finish(self,error):
  if self.callback:u.unregister_slate_post_tick_callback(self.callback);self.callback=None
  try:
   if self.capture and u.SystemLibrary.is_valid(self.capture):self.capture.destroy_actor()
   for pawn,was_hidden in self.hidden_pawns:
    if u.SystemLibrary.is_valid(pawn):pawn.set_actor_hidden_in_game(was_hidden)
   if self.player and u.SystemLibrary.is_valid(self.player) and self.original_time:
    p=u.MovieSceneSequencePlaybackParams();p.set_editor_property('position_type',u.MovieScenePositionType.FRAME);p.set_editor_property('frame',self.original_time.time);p.set_editor_property('update_method',u.UpdatePositionMethod.JUMP)
    self.player.set_playback_position(p)
    if self.was_playing:self.player.play()
    else:self.player.pause()
  finally:
   if self.engine and self.old:
    self.engine.set_editor_property('bUseFixedFrameRate',self.old[0]);self.engine.set_editor_property('FixedFrameRate',self.old[1])
  self.report['status']='failed' if error else 'captured_pending_visual_review';self.report['error']=error;self.report['restored_engine_settings']=True
  if hasattr(self,'out'):self.write()
  u.log('RESPAWN_CAPTURE '+self.report['status'])

previous=getattr(builtins,'mc_respawn_sandbox_capture',None)
assert not previous or not previous.callback,'A sandbox capture is already running'
builtins.mc_respawn_sandbox_capture=Capture()
