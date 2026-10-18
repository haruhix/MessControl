"""Focused solo: ordinary Q/held Q, actual flight arc/spin and physical throat entry.

Run after starting one standalone PIE. All fixtures/movement/camera edits are PIE
only. No direct delivery calls. The short probe restores engine settings and its
view target; stop PIE after Report.json appears. No package saves or networking.
"""
import unreal as u,json,time,math,builtins,traceback
from pathlib import Path

class FoodThrowCheck:
 def __init__(self):
  self.out=Path(globals().get('FOOD_THROW_OUTPUT',Path(u.Paths.project_dir())/'Artifacts/FoodThrow_20261017/Solo'));self.out.mkdir(parents=True,exist_ok=True)
  self.w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.w
  self.pc=u.GameplayStatics.get_player_controller(self.w,0);self.hero=self.pc.get_controlled_pawn();self.original_view=self.pc.get_view_target()
  self.lib=u.get_default_object(u.GameplayStatics)
  for _ in range(4):
   offer=self.pc.player_state.get_editor_property('LevelUpOffer')
   if len(offer.get_editor_property('PerkIDs'))!=3:break
   self.pc.call_method('ServerChooseLevelPerk',args=(offer.get_editor_property('OfferId'),0))
  self.pc.call_method('ServerSetPerkChoicePause',args=(False,));u.GameplayStatics.set_game_paused(self.w,False)
  mode=u.GameplayStatics.get_game_mode(self.w);mode.set_actor_tick_enabled(False)
  for c in mode.get_components_by_class(u.ActorComponent):c.set_component_tick_enabled(False)
  for cls in [u.MCDayDirector,u.MCSingleDayDirector,u.MCGameDirector]:
   for a in u.GameplayStatics.get_all_actors_of_class(self.w,cls):a.set_actor_tick_enabled(False)
  for cls in [u.MCFoodActor,u.MCCoffeeFlood]:
   for a in u.GameplayStatics.get_all_actors_of_class(self.w,cls):a.destroy_actor()
  gs=u.GameplayStatics.get_game_state(self.w);gs.set_editor_property('Phase',u.MCShiftPhase.WORKING);gs.set_editor_property('dev_manual_events',True);gs.set_editor_property('bLobbyWaiting',False);gs.set_editor_property('bTutorialActive',False)
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
  self.old=dict(fixed=self.engine.get_editor_property('bUseFixedFrameRate'),fps=self.engine.get_editor_property('FixedFrameRate'),throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground'))
  (self.out/'EngineBefore.json').write_text(json.dumps(self.old))
  self.engine.set_editor_property('FixedFrameRate',60.);self.engine.set_editor_property('bUseFixedFrameRate',True);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
  self.throat=u.GameplayStatics.get_all_actors_of_class(self.w,u.MCThroat)[0];self.tongue=u.GameplayStatics.get_all_actors_of_class(self.w,u.MCTongue)[0]
  self.collection=self.hero.get_editor_property('food_collection');assert abs(self.collection.get_editor_property('throw_speed')-1800)<.01
  values=self.throat.get_intake_aim_target();self.target=next(v for v in values if isinstance(v,u.Vector))
  art=self.throat.get_editor_property('authored_mouth').get_world_transform()
  self.inward=u.MathLibrary.transform_direction(art,u.Vector(0,.9747185974,.2234360218));self.right=u.MathLibrary.transform_direction(art,u.Vector(1,0,0))
  self.inward=self.inward/self.length(self.inward);self.right=self.right/self.length(self.right)
  flat=u.Vector(self.inward.x,self.inward.y,0);flat=flat/self.length(flat)
  floor=next((p for p in [self.floor(self.target-flat*d) for d in [650,850,1050,450]] if p is not None),None);assert floor is not None
  hero_pos=floor+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+4)
  rot=u.MathLibrary.find_look_at_rotation(hero_pos,self.target);rot.pitch=0;rot.roll=0
  self.hero.set_actor_location_and_rotation(hero_pos,rot,False,True);self.hero.character_movement.stop_movement_immediately()
  self.camera=self.spawn(u.CameraActor,u.Transform(location=hero_pos-flat*300+u.Vector(0,0,160)))
  self.camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(self.camera.get_actor_location(),self.target),True)
  self.pc.set_view_target_with_blend(self.camera,0.)
  self.capture=self.spawn(u.SceneCapture2D,u.Transform())
  self.cc=self.capture.get_editor_property('capture_component2d');self.rt=u.RenderingLibrary.create_render_target2d(self.w,1200,800,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.02,.02,.02,1),False,False)
  self.cc.set_editor_properties(dict(texture_target=self.rt,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=65.))
  eye=hero_pos-flat*120+self.right*650+u.Vector(0,0,260);aim=(hero_pos+self.target)*.5+u.Vector(0,0,40)
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True)
  self.fixtures=[];self.rows=[];self.checks={};self.events=[];self.frames=[];self.launch={};self.started=time.monotonic();self.done=False;self.stage='collect';self.at=self.now();self.last=-1;self.next_frame=self.at;self.count=int(self.throat.get_editor_property('food_swallowed'))
  for offset in [-26,0,26]:self.food(hero_pos+flat*85+self.right*offset-u.Vector(0,0,25))
  self.hero.call_method('ServerToggleFoodCollection',args=())
  self.handle=u.register_slate_post_tick_callback(self.tick);print('FOOD_THROW_SOLO_STARTED',self.target)
 def length(self,v):return math.sqrt(sum(a*a for a in v.to_tuple()))
 def now(self):return u.GameplayStatics.get_time_seconds(self.w)
 def spawn(self,cls,pose):
  a=self.lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.w,cls,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));self.lib.call_method('FinishSpawningActor',args=(a,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return a
 def floor(self,p):
  r=self.tongue.surface.line_trace_component(p+u.Vector(0,0,3000),p-u.Vector(0,0,3000),True,False,False)
  return next((v for v in r if isinstance(v,u.Vector)),None)
 def food(self,p,fragment=True):
  pose=u.Transform(location=p);f=self.lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.w,u.MCFoodActor,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  row=f.get_editor_property('food_data');row.set_editor_property('kind',u.MCFoodKind.FOOD);row.set_editor_property('fragment_scale',u.Vector(.18,.18,.18));row.set_editor_property('scale',u.Vector(.18,.18,.18));row.set_editor_property('spoil_seconds',600.)
  f.set_editor_property('food_data',row);f.set_editor_property('item_name','ThrowCheck');f.set_editor_property('item_mesh',u.load_asset('/Game/Stylized_Vegetables/Meshes/SM_Broccoli'));f.set_editor_property('fragment',fragment)
  self.lib.call_method('FinishSpawningActor',args=(f,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));self.fixtures.append(f);return f
 def go(self,stage):self.stage=stage;self.at=self.now()
 def throw(self,held):self.hero.call_method('SetThrowInputHeld',args=(held,))
 def shot(self,name):
  self.cc.capture_scene();u.RenderingLibrary.export_render_target(self.w,self.rt,str(self.out),name+'.png');self.frames.append(dict(file=name+'.png',time=self.now()))
 def tick(self,dt):
  if self.done:return
  try:
   assert time.monotonic()-self.started<100,'Wall time limit'
   now=self.now()
   if now<=self.last:return
   self.last=now;age=now-self.at
   self.hero.set_editor_property('bInCoffee',False)
   health=self.hero.status.get_editor_property('State');health.set_editor_property('Health',100.);self.hero.status.set_editor_property('State',health)
   count=int(self.throat.get_editor_property('food_swallowed'))
   row=dict(time=now,stage=self.stage,swallowed=count,held=len(self.collection.get_editor_property('pieces')),feedback=self.hero.get_editor_property('TaskSuccessAt'),foods=[])
   for f in self.fixtures:
    if not u.SystemLibrary.is_valid(f):continue
    body=f.get_editor_property('body');p=f.get_actor_location();phase=str(f.get_editor_property('phase'))
    rotation=f.get_actor_rotation()
    row['foods'].append(dict(name=f.get_name(),phase=phase,position=list(p.to_tuple()),rotation=[rotation.pitch,rotation.yaw,rotation.roll],velocity=list(body.get_physics_linear_velocity().to_tuple()),angular=list(body.get_physics_angular_velocity_in_radians().to_tuple())))
    if f.get_editor_property('stack_carrier') is None and 'FREE' in phase and f.get_name() not in self.launch and self.stage in ['single','held']:
     self.launch[f.get_name()]=dict(time=now,position=list(p.to_tuple()),velocity=list(body.get_physics_linear_velocity().to_tuple()),angular=list(body.get_physics_angular_velocity_in_radians().to_tuple()))
   if count>self.count:
    self.events.append(dict(time=now,count=count,held=row['held'],feedback=row['feedback']))
    if self.stage in ['single','held']:assert row['feedback']>=now-.1,'Every received piece needs immediate success feedback'
    self.count=count
   self.rows.append(row)
   if self.stage in ['single','held'] and now>=self.next_frame:self.shot('Flight_%03d'%len(self.frames));self.next_frame=now+.08
   if self.stage=='collect':
    assert age<7,'Three fragments were not collected'
    if row['held']==3:
     self.first=self.collection.get_editor_property('pieces')[-1];self.start=count;self.go('settle')
   elif self.stage=='settle' and age>.8:
    self.shot('BeforeThrow');self.throw(True);self.throw(False);self.go('single')
   elif self.stage=='single':
    assert age<4,'Ordinary Q aimed at the visible hole missed'
    if count==self.start+1:
     assert row['held']==2,'One Q should release one piece only'
     self.checks['single_q_received_with_remaining_stack']=True;self.throw(True);self.go('held')
   elif self.stage=='held':
    assert age<5,'Held Q did not deliver both remaining pieces'
    if count==self.start+3:
     self.throw(False);assert row['held']==0
     self.checks['held_q_three_pieces_each_received_once']=True;self.shot('AllReceived')
     # This unmarked food physically crosses only the front plane on its first
     # physics frame. Its center must be accepted before the old45cm threshold.
     self.loose=self.food(self.target-self.inward*8);self.loose.get_editor_property('body').set_enable_gravity(False);self.loose.get_editor_property('body').set_physics_linear_velocity(self.inward*900)
     self.before_loose=count;self.go('loose')
   elif self.stage=='loose':
    assert age<.7,'An unmarked loose piece entering the opening was not received'
    if count==self.before_loose+1:
     p=self.loose.get_actor_location();depth=sum(a*b for a,b in zip((p-self.target).to_tuple(),self.inward.to_tuple()))
     assert depth<45,('Entry is still delayed beyond the visible opening',depth)
     self.checks['first_frame_entry_before_old_depth']=dict(depth=depth,received=True)
     self.fast=self.food(self.target-self.inward*20);self.fast.get_editor_property('body').set_enable_gravity(False);self.fast.get_editor_property('body').set_linear_damping(0.);self.fast.get_editor_property('body').set_physics_linear_velocity(self.inward*45000)
     self.before_fast=count;self.go('fast')
   elif self.stage=='fast':
    assert age<.7,'A fast first-frame crossing skipped the intake'
    if count==self.before_fast+1:
     self.checks['fast_first_frame_sweep_received']=True
     # A genuine miss beyond the actual right rim remains in the world.
     self.miss=self.food(self.target+self.right*1000-self.inward*20);self.miss.get_editor_property('body').set_enable_gravity(False);self.miss.get_editor_property('body').set_physics_linear_velocity(self.inward*1800)
     self.before_miss=count;self.go('miss')
   elif self.stage=='miss' and age>.65:
    assert count==self.before_miss and self.miss.get_editor_property('phase')!=u.MCFoodPhase.DISPOSED,'Outside-rim miss was counted'
    self.checks['outside_aperture_does_not_score']=True;self.finish()
  except Exception:self.finish(traceback.format_exc())
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.throw(False)
  self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  self.pc.set_view_target_with_blend(self.original_view,0.);self.capture.destroy_actor();self.camera.destroy_actor()
  report=dict(passed=error is None,error=error,checks=self.checks,launch=self.launch,events=self.events,samples=self.rows,frames=self.frames,target=list(self.target.to_tuple()),engine_restored=self.old)
  (self.out/'Report.json').write_text(json.dumps(report,indent=2));print('FOOD_THROW_SOLO_FINISHED',report['passed'],error)

if globals().get('FOOD_THROW_INSTANTIATE',True):builtins.mc_food_throw_check=FoodThrowCheck()
