"""Bounded check of V10 teeth, real brush input, contact materials and new floor.

PIE-only fixtures pause directors, remove the opening coffee flood, place the
observer beside a tooth and apply coffee/1 HP native test damage. Saved assets
and existing user material edits are not saved or changed by this probe.
"""
import unreal as u,json,time,traceback,builtins
from pathlib import Path

class ArenaCheck:
 def __init__(self):
  self.out=Path(globals().get('ARENA_CHECK_OUTPUT',str(Path(u.Paths.project_dir())/'Artifacts/ArenaRefresh_20261017')))
  self.world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.world
  self.pc=u.GameplayStatics.get_player_controller(self.world,0);self.hero=self.pc.get_controlled_pawn();assert self.hero
  for _ in range(4):
   offer=self.pc.player_state.get_editor_property('LevelUpOffer')
   if len(offer.get_editor_property('PerkIDs'))!=3:break
   self.pc.call_method('ServerChooseLevelPerk',args=(offer.get_editor_property('OfferId'),0))
  self.pc.call_method('ServerSetPerkChoicePause',args=(False,));u.GameplayStatics.set_game_paused(self.world,False)
  mode=u.GameplayStatics.get_game_mode(self.world);mode.set_actor_tick_enabled(False)
  for c in mode.get_components_by_class(u.ActorComponent):
   if c.get_class().get_name()=='MCDayDirectorComponent':c.set_component_tick_enabled(False)
  for cls in [u.MCDayDirector,u.MCSingleDayDirector,u.MCGameDirector]:
   for a in u.GameplayStatics.get_all_actors_of_class(self.world,cls):a.set_actor_tick_enabled(False)
  for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCCoffeeFlood):a.destroy_actor()
  for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCFoodActor):a.destroy_actor()
  gs=u.GameplayStatics.get_game_state(self.world);gs.set_editor_property('Phase',u.MCShiftPhase.WORKING);gs.set_editor_property('dev_manual_events',True);gs.set_editor_property('bLobbyWaiting',False);gs.set_editor_property('bTutorialActive',False)
  self.hero.set_editor_property('bInCoffee',False)
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
  self.old=dict(fixed=self.engine.get_editor_property('bUseFixedFrameRate'),fps=self.engine.get_editor_property('FixedFrameRate'),throttle=self.perf.get_editor_property('bThrottleCPUWhenNotForeground'))
  self.teeth=sorted(u.GameplayStatics.get_all_actors_of_class(self.world,u.MCArenaTooth),key=lambda t:t.get_editor_property('State').get_editor_property('ToothId'))
  self.tongue=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCTongue)[0]
  self.checks=[];self.samples=[];self.captures=[];self.done=False;self.started=time.monotonic();self.t0=u.GameplayStatics.get_time_seconds(self.world);self.last=-1;self.grime0=None
  self.check('all twelve lower side teeth are active',len(self.teeth)==12 and [t.get_editor_property('State').get_editor_property('ToothId') for t in self.teeth]==list(range(1,13)))
  self.check('all twelve teeth are available respawn reserves',all(t.is_available() for t in self.teeth) and gs.available_arena_teeth()==12)
  mat=u.load_asset('/Game/Data/DA_ArenaTooth').get_editor_property('gameplay_material')
  ss=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
  self.check('all active crowns use the assigned gameplay material',all(t.visual.get_material(0).get_editor_property('parent')==mat for t in self.teeth))
  self.check('new tongue retains the previous pressure material',self.tongue.get_editor_property('SurfaceMaterial')==u.load_asset('/Game/Gameplay/Arena/Pressure/MI_Pressure_Soft'))
  decor=[a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.StaticMeshActor) if a.static_mesh_component.static_mesh and a.static_mesh_component.static_mesh.get_path_name().startswith('/Game/Gameplay/Arena/V10/SM_Teeth')]
  self.check('twelve upper crowns and both front rows remain decorative',len(decor)==14 and all(a.static_mesh_component.get_material(0)==mat for a in decor))
  self.floor_samples=[]
  for p in [u.Vector(x,y,0) for x,y in [(-1500,-150),(-700,-150),(0,-150),(1000,-150),(-700,800),(-700,-900)]]:
   point=self.floor(p);self.floor_samples.append(list(point.to_tuple()) if point else None)
  self.check('new tongue has matching playable collision',all(p is not None for p in self.floor_samples))
  self.subject=self.teeth[7]
  lib=u.get_default_object(u.GameplayStatics)
  self.capture=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.SceneCapture2D,u.Transform(),u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  lib.call_method('FinishSpawningActor',args=(self.capture,u.Transform(),u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.cc=self.capture.get_editor_property('capture_component2d')
  self.target=u.RenderingLibrary.create_render_target2d(self.world,1440,900,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.02,.02,.02,1),False,False)
  self.cc.set_editor_properties(dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=80.))
  self.cc.set_editor_property('post_process_settings',self.hero.get_editor_property('Camera').get_editor_property('post_process_settings'))
  (self.out/'ProbeEngineBaseline.json').write_text(json.dumps(self.old,indent=2))
  self.engine.set_editor_property('FixedFrameRate',60.);self.engine.set_editor_property('bUseFixedFrameRate',True);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
  self.case='review';self.handle=u.register_slate_post_tick_callback(self.tick)
  print('ARENA_V10_SOLO_STARTED')
 def check(self,name,passed):
  self.checks.append(dict(name=name,passed=bool(passed)));assert passed,name
 def floor(self,p):
  hit=self.tongue.surface.line_trace_component(p+u.Vector(0,0,3000),p-u.Vector(0,0,3000),True,False,False)
  for v in reversed(list(hit) if isinstance(hit,(tuple,list)) else [hit]):
   try:
    p=v.get_editor_property('impact_point')
    if isinstance(p,u.Vector):return p
   except Exception:pass
  if isinstance(hit,(tuple,list)) and hit and isinstance(hit[0],u.Vector):return hit[0]
  return None
 def shot(self,name,eye,aim):
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True)
  self.cc.capture_scene();u.RenderingLibrary.export_render_target(self.world,self.target,str(self.out),name+'.png')
  self.captures.append(str(self.out/(name+'.png')))
 def place(self,age):
  p=self.subject.get_actor_location();ext=self.subject.body.get_scaled_box_extent()
  offset=[80.,100.,125.,65.][int(age/.8)%4]
  pos=u.Vector(p.x,p.y-ext.y-offset,p.z)
  point=self.floor(pos);assert point is not None
  self.hero.character_movement.stop_movement_immediately()
  self.hero.set_actor_location_and_rotation(point+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+3),u.Rotator(yaw=90),False,True)
 def tick(self,dt):
  if self.done:return
  try:
   assert time.monotonic()-self.started<80,'Wall-time limit'
   now=u.GameplayStatics.get_time_seconds(self.world)-self.t0
   if now<=self.last:return
   self.last=now
   hs=self.hero.status.get_editor_property('State');hs.set_editor_property('Health',100.);self.hero.status.set_editor_property('State',hs)
   self.hero.set_editor_property('bInCoffee',False)
   if self.case=='review' and now>.6:
    self.shot('Overview',u.Vector(-2200,-170,640),u.Vector(900,-170,390))
    self.shot('GumContact_Right',u.Vector(-1100,500,480),u.Vector(-750,1410,120))
    self.shot('GumContact_Left',u.Vector(-1100,-850,480),u.Vector(-750,-1790,100))
    for t in self.teeth:t.set_coffee(1.)
    self.case='coffee';self.at=now
   elif self.case=='coffee' and now-self.at>.7:
    self.check('coffee care status can be applied to every active tooth',all(t.status.get_editor_property('State').get_editor_property('CoffeeLeft')>0 for t in self.teeth))
    self.shot('CoffeePlaque',u.Vector(-1100,500,480),u.Vector(-750,1410,120))
    self.grime0=sum(self.subject.get_editor_property('GrimeMask'));assert self.grime0>0
    self.hero.inventory.server_select(u.MCToolSlot.BRUSH);self.place(0)
    self.case='brush';self.at=now
   elif self.case=='brush':
    age=now-self.at;self.hero.inventory.server_select(u.MCToolSlot.BRUSH);self.hero.set_primary_input_held(True);self.place(age)
    grime=sum(self.subject.get_editor_property('GrimeMask'))
    self.samples.append(dict(time=age,grime=grime,hero=list(self.hero.get_actor_location().to_tuple())))
    if grime<self.grime0-10:
     self.check('real held Brush input cleans the new tooth mesh',True)
     self.hero.set_primary_input_held(False)
     for t in self.teeth:
      before=t.status.get_editor_property('State').get_editor_property('Health')
      assert t.receive_arena_hit(1.,u.Vector(0,1,0))
      assert t.status.get_editor_property('State').get_editor_property('Health')<before
     self.check('all twelve teeth receive ordinary arena damage',True)
     self.shot('Brushing',u.Vector(-1100,650,420),self.subject.get_actor_location()+u.Vector(0,0,160))
     self.finish()
    assert age<9,'No new-tooth grime was removed by real brush input'
  except Exception:self.finish(traceback.format_exc())
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle)
  self.hero.set_primary_input_held(False)
  self.capture.destroy_actor()
  self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  report=dict(passed=error is None,error=error,checks=self.checks,floor_samples=self.floor_samples,captures=self.captures,samples=self.samples,restored_engine_settings=self.old,grime_before=self.grime0,grime_after=sum(self.subject.get_editor_property('GrimeMask')))
  (self.out/'SoloCheck.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
  print('ARENA_V10_SOLO_RESULT',report['passed'],error,'checks',len(self.checks))

if globals().get('ARENA_CHECK_INSTANTIATE',True):
 builtins.mc_arena_v10_check=ArenaCheck()
