"""Capture two actual solo-PIE loops. Transient actors/settings are restored."""
import unreal as u,json,time,builtins
from pathlib import Path

OPTIONS=globals().get('MC_MENU_CAPTURE_OPTIONS',{})
class CaptureMenu:
 def __init__(self):
  self.callback=None;self.capture=None;self.engine=None;self.old=None;self.frame=0;self.warm=0;self.started=time.monotonic()
  self.out=Path(OPTIONS.get('output','E:/CodexWork/MessControlMenuAnimation_20261022/RuntimeDirectorV3'));self.out.mkdir(exist_ok=True)
  assert self.out.drive.upper()=='E:'
  self.total=int(OPTIONS.get('frames',576));self.report_path=self.out.parent/(self.out.name+'_Capture.json')
  assert not any(self.out.glob('*.png'))
  self.world=u.EditorLevelLibrary.get_pie_worlds(False)[0]
  assert u.GameplayStatics.get_game_mode(self.world).get_class().get_name()=='MCMainMenuGameMode'
  self.sequence=u.GameplayStatics.get_all_actors_of_class(self.world,u.LevelSequenceActor)[0]
  self.player=self.sequence.get_editor_property('sequence_player');self.player.pause()
  self.cam=next(a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.CameraActor) if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.old=(self.engine.get_editor_property('bUseFixedFrameRate'),self.engine.get_editor_property('FixedFrameRate'))
  self.engine.set_editor_property('FixedFrameRate',24.);self.engine.set_editor_property('bUseFixedFrameRate',True)
  lib=u.get_default_object(u.GameplayStatics)
  transform=self.cam.get_actor_transform()
  self.capture=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.SceneCapture2D,transform,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  lib.call_method('FinishSpawningActor',args=(self.capture,transform,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.target=u.RenderingLibrary.create_render_target2d(self.world,1600,900,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.02,.01,.015,1),False,False)
  self.comp=self.capture.get_editor_property('capture_component2d')
  for k,v in dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=self.cam.camera_component.field_of_view,post_process_settings=self.cam.camera_component.get_editor_property('post_process_settings'),post_process_blend_weight=1.).items():self.comp.set_editor_property(k,v)
  self.comp.set_editor_property('show_flag_settings',[u.EngineShowFlagsSetting(show_flag_name='MotionBlur',enabled=False),u.EngineShowFlagsSetting(show_flag_name='EyeAdaptation',enabled=False)])
  self.report={'source':'Actual solo PIE world SceneCapture2D, two runtime LevelSequence loops','fps':24,'frames':[],'audio':False,'network':False,'map':self.world.get_path_name(),'status':'running'}
  self.callback=u.register_slate_post_tick_callback(self.tick)
 def tick(self,dt):
  try:
   if time.monotonic()-self.started>360:raise RuntimeError('Capture timeout')
   if self.warm<12:
    self.comp.capture_scene();self.warm+=1
    if self.warm==12:
     p=u.MovieSceneSequencePlaybackParams();p.set_editor_property('position_type',u.MovieScenePositionType.TIME);p.set_editor_property('time',0.);p.set_editor_property('update_method',u.UpdatePositionMethod.JUMP);self.player.set_playback_position(p);self.player.play()
    return
   self.comp.capture_scene();name='%04d.png'%self.frame;u.RenderingLibrary.export_render_target(self.world,self.target,str(self.out),name)
   assert (self.out/name).exists()
   qt=self.player.get_current_time();frame=qt.time.frame_number.value+qt.time.sub_frame;seconds=frame*qt.rate.denominator/qt.rate.numerator
   self.report['frames'].append({'index':self.frame,'sequence_seconds':seconds,'game_dt':u.GameplayStatics.get_world_delta_seconds(self.world)})
   self.frame+=1
   if self.frame%48==0:self.write()
   if self.frame>=self.total:self.finish(None)
  except Exception as exc:self.finish(str(exc))
 def write(self):
  self.report_path.write_text(json.dumps(self.report,indent=2),encoding='utf8')
 def finish(self,error):
  if self.callback:u.unregister_slate_post_tick_callback(self.callback);self.callback=None
  if self.capture:self.capture.destroy_actor();self.capture=None
  if self.engine and self.old:
   self.engine.set_editor_property('bUseFixedFrameRate',self.old[0]);self.engine.set_editor_property('FixedFrameRate',self.old[1])
  self.player.play();self.report['status']='failed' if error else 'captured_pending_visual_review';self.report['error']=error;self.report['restored_settings']=True;self.write();u.log('MENU_RUNTIME_CAPTURE '+self.report['status'])

builtins.mc_menu_capture_v2=CaptureMenu()
