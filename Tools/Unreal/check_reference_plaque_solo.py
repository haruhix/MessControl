"""Short solo check: real held brush, complete removal and visible bristle contact."""
import unreal as u,json,time,math,traceback,builtins
from pathlib import Path

class ReferencePlaqueCheck:
 def __init__(self):
  self.out=Path(globals().get('PLAQUE_OUTPUT_ROOT',str(Path(u.Paths.project_dir())/'Artifacts/GrimeReference_20261017')))/globals().get('PLAQUE_RUN_NAME','Final')
  self.out.mkdir(parents=True,exist_ok=True)
  self.w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.w
  self.pc=u.GameplayStatics.get_player_controller(self.w,0);self.hero=self.pc.get_controlled_pawn()
  assert self.w.get_net_mode()=='Standalone' if hasattr(self.w,'get_net_mode') else True
  for _ in range(4):
   offer=self.pc.player_state.get_editor_property('LevelUpOffer')
   if len(offer.get_editor_property('PerkIDs'))!=3:break
   self.pc.call_method('ServerChooseLevelPerk',args=(offer.get_editor_property('OfferId'),0))
  self.pc.call_method('ServerSetPerkChoicePause',args=(False,));u.GameplayStatics.set_game_paused(self.w,False)
  mode=u.GameplayStatics.get_game_mode(self.w);mode.set_actor_tick_enabled(False)
  for c in mode.get_components_by_class(u.ActorComponent):c.set_component_tick_enabled(False)
  for cls in [u.MCDayDirector,u.MCSingleDayDirector,u.MCGameDirector]:
   for a in u.GameplayStatics.get_all_actors_of_class(self.w,cls):a.set_actor_tick_enabled(False)
  for cls in [u.MCCoffeeFlood,u.MCFoodActor]:
   for a in u.GameplayStatics.get_all_actors_of_class(self.w,cls):a.destroy_actor()
  gs=u.GameplayStatics.get_game_state(self.w);gs.set_editor_property('Phase',u.MCShiftPhase.WORKING);gs.set_editor_property('dev_manual_events',True);gs.set_editor_property('bLobbyWaiting',False);gs.set_editor_property('bTutorialActive',False)
  self.hero.set_editor_property('bInCoffee',False)
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
  self.old=dict(fixed=self.engine.get_editor_property('bUseFixedFrameRate'),fps=self.engine.get_editor_property('FixedFrameRate'),throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground'))
  (self.out/'EngineBefore.json').write_text(json.dumps(self.old))
  self.engine.set_editor_property('FixedFrameRate',30.);self.engine.set_editor_property('bUseFixedFrameRate',True);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
  self.teeth=sorted(u.GameplayStatics.get_all_actors_of_class(self.w,u.MCArenaTooth),key=lambda t:t.state.tooth_id)
  self.tongue=u.GameplayStatics.get_all_actors_of_class(self.w,u.MCTongue)[0]
  for t in self.teeth:t.set_coffee(1.)
  self.subject=self.teeth[globals().get('PLAQUE_SUBJECT_INDEX',7)]
  self.brush_surface=next(c for c in self.subject.get_components_by_class(u.StaticMeshComponent) if c.get_name()=='BrushSurface')
  lib=u.get_default_object(u.GameplayStatics)
  self.capture=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.w,u.SceneCapture2D,u.Transform(),u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  lib.call_method('FinishSpawningActor',args=(self.capture,u.Transform(),u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.cc=self.capture.get_editor_property('capture_component2d')
  self.rt=u.RenderingLibrary.create_render_target2d(self.w,1200,800,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.02,.02,.02,1),False,False)
  self.cc.set_editor_properties(dict(texture_target=self.rt,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=62.))
  self.cc.set_editor_property('post_process_settings',self.hero.get_editor_property('Camera').get_editor_property('post_process_settings'))
  self.case='warmup';self.t0=self.now();self.wall=time.monotonic();self.last=-1;self.done=False;self.rows=[];self.checks=[];self.captures=[];self.frame_times=[];self.pose=-1;self.next_frame=0
  self.handle=u.register_slate_post_tick_callback(self.tick)
  print('REFERENCE_PLAQUE_CHECK_STARTED')
 def now(self):return u.GameplayStatics.get_time_seconds(self.w)
 def floor(self,p):
  hit=self.tongue.surface.line_trace_component(p+u.Vector(0,0,3000),p-u.Vector(0,0,3000),True,False,False)
  for v in reversed(list(hit) if isinstance(hit,(tuple,list)) else [hit]):
   try:
    p=v.get_editor_property('impact_point')
    if isinstance(p,u.Vector):return p
   except Exception:pass
  if isinstance(hit,(tuple,list)) and hit and isinstance(hit[0],u.Vector):return hit[0]
  return None
 def place(self,index):
  p=self.subject.get_actor_location();extent=self.subject.body.get_scaled_box_extent()
  offsets=[0.,-125.,125.]
  side=1. if p.y>0 else -1.
  pos=u.Vector(p.x+offsets[index%len(offsets)],p.y-side*(extent.y+95.),p.z)
  point=self.floor(pos);assert point is not None
  self.hero.character_movement.stop_movement_immediately()
  target=p+u.Vector(0,-side*extent.y,100)
  rotation=u.MathLibrary.find_look_at_rotation(pos,target);rotation.pitch=0;rotation.roll=0
  self.hero.set_actor_location_and_rotation(point+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+4),rotation,False,True)
  self.start_x=p.x
 def shot(self,name,gameplay=False):
  p=self.subject.get_actor_location()
  side=1. if p.y>0 else -1.
  eye=p+u.Vector(-390,-side*700,330);aim=p+u.Vector(0,-side*180,150)
  if gameplay:
   manager=self.pc.player_camera_manager;eye=manager.get_camera_location();rot=manager.get_camera_rotation()
  else:rot=u.MathLibrary.find_look_at_rotation(eye,aim)
  self.capture.set_actor_location_and_rotation(eye,rot,False,True)
  self.cc.capture_scene();u.RenderingLibrary.export_render_target(self.w,self.rt,str(self.out),name+'.png');self.captures.append(name+'.png')
  if name.startswith('Stroke_'):self.frame_times.append(dict(file=name+'.png',time=self.now()-self.at))
 def tick(self,dt):
  if self.done:return
  try:
   assert time.monotonic()-self.wall<100,'Wall-time limit'
   now=self.now()
   if now<=self.last:return
   self.last=now
   hs=self.hero.status.get_editor_property('State');hs.set_editor_property('Health',100.);self.hero.status.set_editor_property('State',hs)
   self.hero.set_editor_property('bInCoffee',False)
   if self.case=='warmup' and now-self.t0>.7:
    self.shot('PlaqueBefore');self.place(0);self.hero.inventory.server_select(u.MCToolSlot.BRUSH);self.hero.set_primary_input_held(True)
    self.at=now;self.case='brush';self.initial=sum(self.subject.get_editor_property('GrimeMask'))
   elif self.case=='brush':
    age=now-self.at;pose=int(age/2.0)
    self.pose=pose
    dx=self.start_x+[0.,-125.,125.][pose%3]-self.hero.get_actor_location().x
    if abs(dx)>8:
     self.hero.add_movement_input(u.Vector(1 if dx>0 else -1,0,0),min(1.,abs(dx)/60.),False)
    else:self.hero.character_movement.stop_movement_immediately()
    self.hero.set_primary_input_held(True)
    bristles=u.MathLibrary.transform_location(self.hero.get_editor_property('Brush').get_world_transform(),u.Vector(72,0,-20))
    direction=(self.subject.get_actor_location()-bristles);direction.z=0
    direction=direction/math.sqrt(sum(v*v for v in direction.to_tuple()))
    trace=self.brush_surface.line_trace_component(bristles-direction*120,bristles+direction*180,True,False,False)
    point=None
    for v in reversed(list(trace) if isinstance(trace,(tuple,list)) else [trace]):
     try:
      hit=v.get_editor_property('impact_point')
      if isinstance(hit,u.Vector):point=hit;break
     except Exception:pass
    if point is None and isinstance(trace,(tuple,list)) and trace and isinstance(trace[0],u.Vector):point=trace[0]
    error=math.sqrt(sum(v*v for v in (point-bristles).to_tuple())) if point else 9999.
    grime=sum(self.subject.get_editor_property('GrimeMask'))
    self.rows.append(dict(time=age,mask=grime,coffee_left=self.subject.status.state.coffee_left,contact_progress=self.hero.get_editor_property('ContactProgress'),touching=error<=16,point=list(point.to_tuple()) if point else None,bristles=list(bristles.to_tuple()),hero=list(self.hero.get_actor_location().to_tuple()),error=error))
    if age>=self.next_frame:
     self.shot('Stroke_%03d'%len(self.captures));self.next_frame=age+.1
    if self.subject.status.state.coffee_left==0:
     self.checks.append(dict(name='ordinary held brush completes the broad coating',passed=True,seconds=age))
     self.hero.set_primary_input_held(False);self.hero.character_movement.stop_movement_immediately();self.case='settle';self.clean_at=now
    elif age>30:
     self.finish('Broad coating did not finish within 30 simulation seconds')
   elif self.case=='settle':
    age=now-self.at
    if age>=self.next_frame:
     self.shot('Stroke_%03d'%len(self.captures));self.next_frame=age+.1
    if now-self.clean_at>.6:
     self.shot('PlaqueClean');self.shot('GameplayClean',True);self.finish()
  except Exception:self.finish(traceback.format_exc())
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.hero.set_primary_input_held(False)
  self.capture.destroy_actor()
  self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  report=dict(passed=error is None,error=error,checks=self.checks,samples=self.rows,captures=self.captures,frame_times=self.frame_times,engine_restored=self.old)
  (self.out/'SoloCheck.json').write_text(json.dumps(report,indent=2));print('REFERENCE_PLAQUE_CHECK_FINISHED',report['passed'],error)

if globals().get('REFERENCE_PLAQUE_INSTANTIATE',True):
 builtins.mc_reference_plaque_check=ReferencePlaqueCheck()
