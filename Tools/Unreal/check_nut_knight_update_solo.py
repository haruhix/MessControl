"""One bounded solo PIE knight check, native scheduler and real primary tool hits.

Fixtures: isolated boss/hero placement, ability readiness, observer HP and a
40-durability shield for the break. No fabricated attack phases/damage. Captures
are a review camera under L_Mouth lighting, not an asset-gallery animation.
"""
import unreal as u,builtins,json,time,traceback,math
from pathlib import Path

class KnightCheck:
 def __init__(self):
  self.out=Path(u.Paths.project_dir())/'Artifacts/NutKnightAnimations_20261016';self.frames=self.out/'GameplayFrames';self.frames.mkdir(exist_ok=True)
  self.world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.world
  self.pc=u.GameplayStatics.get_player_controller(self.world,0);self.hero=self.pc.get_controlled_pawn();assert isinstance(self.hero,u.MCToothCharacter)
  ps=self.pc.player_state
  for _ in range(4):
   offer=ps.get_editor_property('LevelUpOffer')
   if len(offer.get_editor_property('PerkIDs'))!=3:break
   self.pc.call_method('ServerChooseLevelPerk',args=(offer.get_editor_property('OfferId'),0))
  ps.get_editor_property('Perks').reset_perks()
  self.pc.call_method('ServerSetPerkChoicePause',args=(False,));u.GameplayStatics.set_game_paused(self.world,False)
  mode=u.GameplayStatics.get_game_mode(self.world);mode.set_actor_tick_enabled(False)
  for c in mode.get_components_by_class(u.ActorComponent):
   if c.get_class().get_name()=='MCDayDirectorComponent':c.set_component_tick_enabled(False)
  for cls in [u.MCDayDirector,u.MCSingleDayDirector,u.MCGameDirector]:
   for d in u.GameplayStatics.get_all_actors_of_class(self.world,cls):d.set_actor_tick_enabled(False)
  for coffee in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCCoffeeFlood):coffee.destroy_actor()
  self.hero.set_editor_property('bInCoffee',False)
  gs=u.GameplayStatics.get_game_state(self.world);gs.set_editor_property('Phase',u.MCShiftPhase.WORKING);gs.set_editor_property('dev_manual_events',True);gs.set_editor_property('phase_ends_at',0);gs.set_editor_property('bLobbyWaiting',False);gs.set_editor_property('bTutorialActive',False)
  self.hero.set_primary_input_held(False);self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
  self.hero.character_movement.stop_movement_immediately();self.hero.character_movement.disable_movement()
  hs=self.hero.status.get_editor_property('State');hs.set_editor_property('Health',100.);hs.set_editor_property('MaxHealth',100.);self.hero.status.set_editor_property('State',hs)
  self.tongue=u.GameplayStatics.get_all_actors_of_class(self.world,u.MCTongue)[0]
  center,_,_=u.SystemLibrary.get_component_bounds(self.tongue.get_editor_property('Surface'))
  floor=self.floor(center);assert floor is not None
  for food in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCFoodActor):
   if food.get_actor_location().distance(floor)<1500:food.destroy_actor()
  self.anchor=floor+u.Vector(0,0,204);self.lib=u.get_default_object(u.GameplayStatics)
  pose=u.Transform(location=self.anchor)
  self.boss=self.lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.MCNutBoss,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  settings=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain').get_editor_property('settings').get_editor_property('boss')
  base=u.MCNutEnemySettings();base.set_editor_property('BodyRadius',200.);base.set_editor_property('MaxHealth',1000.);base.set_editor_property('MoveSpeed',160.);base.set_editor_property('HopHeight',4.)
  for k,v in {'Tongue':self.tongue,'BossRole':u.MCNutBossRole.TANK,'BossSettings':settings,'Settings':base,'Health':1400.,'ShieldHealth':500.,'State':u.MCNutBossState.IDLE,'Target':self.hero}.items():self.boss.set_editor_property(k,v)
  self.lib.call_method('FinishSpawningActor',args=(self.boss,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.boss.get_editor_property('Label').set_hidden_in_game(True)
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
  self.old={'fixed':self.engine.get_editor_property('bUseFixedFrameRate'),'fps':self.engine.get_editor_property('FixedFrameRate'),'throttle':self.perf.get_editor_property('bThrottleCPUWhenNotForeground')}
  self.engine.set_editor_property('FixedFrameRate',60.);self.engine.set_editor_property('bUseFixedFrameRate',True);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
  self.capture=self.lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.SceneCapture2D,u.Transform(),u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.lib.call_method('FinishSpawningActor',args=(self.capture,u.Transform(),u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.cc=self.capture.get_editor_property('capture_component2d');self.target=u.RenderingLibrary.create_render_target2d(self.world,960,540,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.03,.035,.05,1),False,False)
  self.cc.set_editor_properties(dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=60.))
  self.cc.set_editor_property('post_process_settings',self.hero.get_editor_property('Camera').get_editor_property('post_process_settings'))
  self.rows=[];self.captures=[];self.checks=[];self.inputs=[];self.last=-1;self.lastcap=-1;self.started=time.monotonic();self.done=False;self.busy=False
  self.reactions_only=globals().get('MC_NUT_KNIGHT_REACTIONS_ONLY',False)
  self.death_only=globals().get('MC_NUT_KNIGHT_DEATH_ONLY',False)
  self.begin('death' if self.death_only else 'block' if self.reactions_only else 'idle');self.handle=u.register_slate_post_tick_callback(self.tick);print('NUT_KNIGHT_SOLO_STARTED')
 def now(self):return u.GameplayStatics.get_time_seconds(self.world)
 def floor(self,p):
  r=self.tongue.get_editor_property('Surface').line_trace_component(p+u.Vector(0,0,2500),p-u.Vector(0,0,2500),True,False,False)
  for v in reversed(list(r) if isinstance(r,(tuple,list)) else [r]):
   try:
    point=v.get_editor_property('impact_point')
    if isinstance(point,u.Vector):return point
   except Exception:pass
  return r[0] if isinstance(r,(tuple,list)) and r and isinstance(r[0],u.Vector) else None
 def hp(self):return float(self.hero.status.get_editor_property('State').get_editor_property('Health'))
 def ready(self,name=None):
  now=self.now()
  for k in ['NextMeleeAt','NextChargeAt','NextJumpAt','NextRollAt','NextSpecialAt']:self.boss.set_editor_property(k,now+300)
  if name:
   self.boss.set_editor_property(name,now)
   if name!='NextMeleeAt':self.boss.set_editor_property('NextSpecialAt',now)
 def place(self,side=1,distance=280):
  p=self.floor(self.boss.get_actor_location()+u.Vector(side*distance,0,0));assert p is not None
  self.hero.character_movement.stop_movement_immediately()
  self.hero.set_actor_location_and_rotation(p+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()+3),u.Rotator(yaw=180 if side>0 else 0),False,True)
 def begin(self,case):
  self.case=case;self.at=self.now();self.fired=set();self.seen=False;self.hp0=self.hp();self.boss_hp0=self.boss.get_editor_property('Health');self.shield0=self.boss.get_editor_property('ShieldHealth')
  self.hero.set_primary_input_held(False);self.boss.set_actor_tick_enabled(case!='rear_hit');self.ready()
  if case.startswith('attack'):self.ready('NextMeleeAt')
  if case=='jump':self.place(distance=600);self.ready('NextJumpAt')
  if case in ['block','rear_hit','break','stun','death']:self.boss.set_actor_rotation(u.Rotator(),False)
  if case in ['block','rear_hit','break','stun','death']:
   self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
   hs=self.hero.status.get_editor_property('State');hs.set_editor_property('Health',100.);hs.set_editor_property('RepairLeft',0);hs.set_editor_property('CoffeeLeft',0);self.hero.status.set_editor_property('State',hs)
  if case=='break':self.boss.set_editor_property('ShieldHealth',40.);self.shield0=40.
  if case=='stun':self.expiry=self.boss.get_editor_property('AttackEndAt');self.stun_pos=self.boss.get_actor_location()
  if case=='death':self.boss.set_editor_property('Health',40.);self.boss.set_editor_property('ShieldHealth',0.);self.death_at=None
  print('NUT_KNIGHT_CASE',case)
 def press(self,key):
  if key in self.fired:return
  if self.hero.get_editor_property('ToothPhysics').get_body_state()!=u.MCBodyState.STANDING:return
  self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
  if self.hero.inventory.get_editor_property('Selected')!=u.MCToolSlot.PICKAXE or self.pc.is_reward_menu_open():return
  self.fired.add(key);self.press_at=self.now()-self.at;self.hero.set_primary_input_held(True);self.hero.set_primary_input_held(False);self.inputs.append({'case':self.case,'time':self.press_at})
 def check(self,name,condition,**extra):
  self.checks.append({'name':name,'passed':bool(condition),**extra});assert condition,(name,extra)
 def record(self,now):
  valid=u.SystemLibrary.is_valid(self.boss)
  if valid:
   model=self.boss.get_editor_property('TankModel');state=self.boss.get_editor_property('State')
   self.rows.append({'case':self.case,'time':now-self.at,'game_time':now,'state':str(state),'attack':str(self.boss.get_editor_property('Attack')),'variant':self.boss.get_editor_property('TankMeleeVariant'),'health':self.boss.get_editor_property('Health'),'shield':self.boss.get_editor_property('ShieldHealth'),'hero_health':self.hp(),'blocked':self.boss.get_editor_property('bLastToolHitBlocked'),'position':list(self.boss.get_actor_location().to_tuple()),'hand':list(model.get_socket_location('hand_r').to_tuple()),'head':list(model.get_socket_location('head_x').to_tuple()),'mesh':model.get_skeletal_mesh_asset().get_path_name(),'visible':model.is_visible(),'state_end':self.boss.get_editor_property('AttackEndAt')})
  if now-self.lastcap>=1/15-.001:
   self.lastcap=now;aim=self.anchor+u.Vector(40,0,30);eye=self.anchor+u.Vector(750,-1100,500)
   if self.case=='jump':
    aim=self.anchor+u.Vector(40,0,200);eye=self.anchor+u.Vector(1000,-1450,750)
   self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
   name=f'{self.case}_{len(self.captures):04}.png';u.RenderingLibrary.export_render_target(self.world,self.target,str(self.frames),name)
   self.captures.append({'case':self.case,'time':now-self.at,'game_time':now,'file':str(self.frames/name)})
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.hero.set_primary_input_held(False)
  self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  for a in [self.capture,self.boss]:
   if u.SystemLibrary.is_valid(a):a.destroy_actor()
  d={'passed':error is None,'error':error,'checks':self.checks,'samples':self.rows,'captures':self.captures,'inputs':self.inputs,'restored_engine_settings':self.old,'scope':__doc__}
  (self.out/('DeathCheck.json' if self.death_only else 'ReactionCheck.json' if self.reactions_only else 'SoloCheck.json')).write_text(json.dumps(d,indent=2));print('NUT_KNIGHT_SOLO_RESULT',json.dumps({'passed':d['passed'],'error':error,'checks':self.checks,'frames':len(self.captures)}))
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<180,'Solo check exceeded 180 wall seconds'
   if u.GameplayStatics.is_game_paused(self.world):
    ps=self.pc.player_state;offer=ps.get_editor_property('LevelUpOffer')
    if len(offer.get_editor_property('PerkIDs'))==3:self.pc.call_method('ServerChooseLevelPerk',args=(offer.get_editor_property('OfferId'),0));ps.get_editor_property('Perks').reset_perks()
    self.pc.call_method('ServerSetPerkChoicePause',args=(False,));u.GameplayStatics.set_game_paused(self.world,False)
   now=self.now()
   if now<=self.last:return
   self.last=now;age=now-self.at;valid=u.SystemLibrary.is_valid(self.boss)
   if valid:
    if self.case not in ['jump','walk']:self.place(-1 if self.case=='rear_hit' else 1)
    if self.case=='walk':self.place(distance=700)
   self.record(now)
   if self.case=='idle' and age>.6:self.begin('attack1')
   elif self.case.startswith('attack'):
    state=self.boss.get_editor_property('State')
    if state==u.MCNutBossState.EXECUTING:self.seen=True
    if self.seen and state==u.MCNutBossState.IDLE:
     index=int(self.case[-1]);expected=36. if index==3 else 12.
     self.check(self.case+' native contacts',abs((self.hp0-self.hp())-expected)<.01,damage=self.hp0-self.hp(),variant=self.boss.get_editor_property('TankMeleeVariant'))
     self.begin('attack'+str(index+1) if index<3 else 'jump')
    assert age<6.,'Native melee did not finish'
   elif self.case=='jump':
    if self.boss.get_editor_property('State')==u.MCNutBossState.EXECUTING:self.seen=True
    if self.seen and self.boss.get_editor_property('State')==u.MCNutBossState.IDLE:
     self.check('new jump completed',True);self.begin('walk')
    assert age<6.
   elif self.case=='walk' and age>1.:
    self.check('legacy walk used during native movement',self.boss.get_actor_location().distance(self.anchor)>50.)
    self.anchor=self.boss.get_actor_location();self.begin('block')
   elif self.case in ['block','rear_hit','break']:
    if age>.15:self.press('hit')
    if self.case=='break' and self.boss.get_editor_property('ShieldHealth')<=0:
     self.check('shield break enters real stun',self.boss.get_editor_property('State')==u.MCNutBossState.STUNNED)
     self.check('breaking hit keeps 20 percent health damage',abs(self.boss_hp0-self.boss.get_editor_property('Health')-8.)<.01)
     self.begin('stun')
    elif 'hit' in self.fired and age-self.press_at>1.:
     damage=self.boss_hp0-self.boss.get_editor_property('Health')
     self.check(self.case+' actual pickaxe damage',abs(damage-(8. if self.case=='block' else 40.))<.01,damage=damage)
     if self.case=='block':self.check('frontal block recorded',self.boss.get_editor_property('bLastToolHitBlocked'));self.begin('rear_hit')
     elif self.case=='rear_hit':self.check('rear hit bypasses block',not self.boss.get_editor_property('bLastToolHitBlocked'));self.begin('break')
     else:raise AssertionError('Shield did not break')
    assert age<8.,'Player could not deliver the fixture hit after recovery'
   elif self.case=='stun':
    if age>.9:self.press('stun_hit')
    assert abs(self.boss.get_editor_property('AttackEndAt')-self.expiry)<.01 or now>=self.expiry
    if now<self.expiry-.05:
     assert self.boss.get_editor_property('State')==u.MCNutBossState.STUNNED
     assert self.boss.get_actor_location().distance(self.stun_pos)<.1
    if now>self.expiry+.15:
     self.check('stun hit deals full damage and does not extend stun',abs(self.boss_hp0-self.boss.get_editor_property('Health')-40.)<.01 and self.boss.get_editor_property('State')==u.MCNutBossState.IDLE)
     self.check('shield mesh removed',self.boss.get_editor_property('TankModel').get_skeletal_mesh_asset().get_name()=='SK_NutTank_NoShield')
     self.begin('death')
   elif self.case=='death':
    if age>.15 and valid and not self.boss.get_editor_property('bDefeated'):self.press('death')
    if valid and self.boss.get_editor_property('bDefeated') and self.death_at is None:self.death_at=now
    if self.death_at is not None and now-self.death_at>4.7 and valid:self.seen=True
    if not valid:
     self.check('authored death retained for full clip',self.death_at is not None and now-self.death_at>=5.1 and self.seen,duration=now-self.death_at);self.finish()
    assert age<7.,'Death failed'
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

builtins.mc_nut_knight_check=KnightCheck()
