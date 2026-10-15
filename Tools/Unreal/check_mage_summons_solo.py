"""Short solo PIE: native Mage cast revives two placed ground nuts; observe run,
actual speed, a primary Pickaxe kill and the surviving summon's five-second fuse.
Event/ground-nut placement, AI readiness and a review camera are explicit fixtures.
No fabricated summon actors, damage or attack phases.
"""
import unreal as u,builtins,json,time,traceback,math
from pathlib import Path

class SummonCheck:
 def __init__(self):
  self.out=Path(u.Paths.project_dir())/'Artifacts/MageSummons_20261016';self.frames=self.out/'GameplayFrames';self.frames.mkdir(exist_ok=True)
  self.w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert self.w
  self.pc=u.GameplayStatics.get_player_controller(self.w,0);self.h=self.pc.get_controlled_pawn();assert isinstance(self.h,u.MCToothCharacter)
  self.lib=u.get_default_object(u.GameplayStatics);self.spawned=[]
  for cls in [u.MCDayDirector,u.MCSingleDayDirector,u.MCGameDirector]:
   for a in u.GameplayStatics.get_all_actors_of_class(self.w,cls):a.set_actor_tick_enabled(False)
  u.GameplayStatics.get_game_mode(self.w).set_actor_tick_enabled(False)
  for a in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCCoffeeFlood):a.destroy_actor()
  self.h.set_editor_property('bInCoffee',False)
  self.gs=u.GameplayStatics.get_game_state(self.w)
  for k,v in {'phase':u.MCShiftPhase.WORKING,'dev_manual_events':True,'phase_ends_at':0,'bLobbyWaiting':False,'bTutorialActive':False}.items():self.gs.set_editor_property(k,v)
  self.resolve_choice();self.h.set_primary_input_held(False);self.h.character_movement.stop_movement_immediately();self.h.character_movement.disable_movement()
  self.tongue=u.GameplayStatics.get_all_actors_of_class(self.w,u.MCTongue)[0]
  center,_,_=u.SystemLibrary.get_component_bounds(self.tongue.get_editor_property('Surface'));self.center=self.floor(center);assert self.center is not None
  for a in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCFoodActor):
   if a.get_actor_location().distance(self.center)<1700:a.destroy_actor()
  self.place(self.center+u.Vector(1000,0,0))
  settings=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain').get_editor_property('settings');boss=settings.get_editor_property('boss')
  self.event=self.spawn(u.MCNutRainEvent,self.center);self.event.set_actor_tick_enabled(False);self.event.set_editor_property('Settings',settings);self.event.set_editor_property('Stage',u.MCNutRainStage.ENEMIES)
  nutmesh=u.load_asset('/Game/Gameplay/CoreLoop/SM_Walnut_Whole');self.foods=[]
  for offset in [u.Vector(-550,-280,0),u.Vector(-550,280,0)]:
   point=self.floor(self.center+offset);assert point is not None
   food=self.spawn(u.MCFoodActor,point+u.Vector(0,0,90));food.body.set_simulate_physics(False);food.body.set_enable_gravity(False);food.set_actor_tick_enabled(False)
   food.set_editor_property('ItemMesh',nutmesh);food.visual.set_static_mesh(nutmesh);scale=180/(nutmesh.get_bounds().box_extent.z*2);food.set_actor_scale3d(u.Vector(scale,scale,scale))
   food.set_editor_property('Phase',u.MCFoodPhase.FREE);food.set_editor_property('tags',[u.Name('MCNutRain')]);food.label.set_hidden_in_game(True);self.foods.append(food)
  self.event.get_editor_property('Nuts').extend(self.foods)
  base=u.MCNutEnemySettings();base.set_editor_property('BodyRadius',180.);base.set_editor_property('MoveSpeed',120.)
  pose=u.Transform(location=self.center+u.Vector(0,0,184));self.mage=self.defer(u.MCNutBoss,pose)
  for k,v in {'Tongue':self.tongue,'EncounterOwner':self.event,'BossRole':u.MCNutBossRole.MAGE,'BossSettings':boss,'Settings':base,'Health':1100.,'ShieldHealth':0.,'State':u.MCNutBossState.IDLE,'Target':self.h}.items():self.mage.set_editor_property(k,v)
  self.finish_spawn(self.mage,pose);self.mage.get_editor_property('Label').set_hidden_in_game(True)
  now=self.now()
  for k in ['NextMeleeAt','NextChargeAt','NextJumpAt','NextRollAt','NextFireballAt','NextRainAt','NextTeleportAt']:self.mage.set_editor_property(k,now+300)
  self.mage.set_editor_property('NextSummonAt',now);self.mage.set_editor_property('NextSpecialAt',now)
  self.engine=next(o for o in u.ObjectIterator(u.load_class(None,'/Script/Engine.Engine')) if o.get_path_name().startswith('/Engine/Transient.'))
  self.perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
  self.old={'fixed':self.engine.get_editor_property('bUseFixedFrameRate'),'fps':self.engine.get_editor_property('FixedFrameRate'),'throttle':self.perf.get_editor_property('bThrottleCPUWhenNotForeground')}
  self.engine.set_editor_property('FixedFrameRate',60.);self.engine.set_editor_property('bUseFixedFrameRate',True);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
  self.capture=self.spawn(u.SceneCapture2D,self.center);self.cc=self.capture.get_editor_property('capture_component2d');self.target=u.RenderingLibrary.create_render_target2d(self.w,960,540,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.03,.035,.05,1),False,False)
  self.cc.set_editor_properties(dict(texture_target=self.target,capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,fov_angle=65.,post_process_settings=self.h.get_editor_property('Camera').get_editor_property('post_process_settings')))
  self.started=time.monotonic();self.at=self.now();self.born=None;self.creeps=[];self.rows=[];self.captures=[];self.checks=[];self.done=False;self.busy=False;self.last=-1;self.lastcap=-1;self.kill=None;self.survivor=None;self.pressed=False;self.killed=False;self.exploded=False;self.casting=False
  self.handle=u.register_slate_post_tick_callback(self.tick);print('MAGE_SUMMON_SOLO_STARTED')
 def now(self):return u.GameplayStatics.get_time_seconds(self.w)
 def defer(self,cls,pose):
  a=self.lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.w,cls,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));self.spawned.append(a);return a
 def finish_spawn(self,a,pose):self.lib.call_method('FinishSpawningActor',args=(a,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 def spawn(self,cls,point):
  pose=u.Transform(location=point);a=self.defer(cls,pose);self.finish_spawn(a,pose);return a
 def floor(self,p):
  r=self.tongue.get_editor_property('Surface').line_trace_component(p+u.Vector(0,0,2500),p-u.Vector(0,0,2500),True,False,False)
  return r[0] if isinstance(r,(tuple,list)) and r and isinstance(r[0],u.Vector) else None
 def place(self,p,yaw=180):
  floor=self.floor(p);assert floor is not None
  self.h.character_movement.stop_movement_immediately();self.h.set_actor_location_and_rotation(floor+u.Vector(0,0,self.h.capsule_component.get_scaled_capsule_half_height()+3),u.Rotator(yaw=yaw),False,True)
 def resolve_choice(self):
  ps=self.pc.player_state
  for _ in range(4):
   offer=ps.get_editor_property('LevelUpOffer')
   if len(offer.get_editor_property('PerkIDs'))!=3:break
   self.pc.call_method('ServerChooseLevelPerk',args=(offer.get_editor_property('OfferId'),0))
  ps.get_editor_property('Perks').reset_perks();self.pc.call_method('ServerSetPerkChoicePause',args=(False,));u.GameplayStatics.set_game_paused(self.w,False)
 def check(self,name,condition,**data):self.checks.append({'name':name,'passed':bool(condition),**data});assert condition,(name,data)
 def record(self,now):
  for c in self.creeps:
   if not u.SystemLibrary.is_valid(c):continue
   model=c.get_editor_property('SummonModel')
   self.rows.append({'time':now-self.at,'game_time':now,'path':c.get_path_name(),'location':list(c.get_actor_location().to_tuple()),'settings_speed':c.get_editor_property('Settings').get_editor_property('MoveSpeed'),'rate':c.get_editor_property('RunPlayRate'),'foot_left':list(model.get_socket_transform('foot_l',u.RelativeTransformSpace.RTS_COMPONENT).translation.to_tuple()),'foot_right':list(model.get_socket_transform('foot_r',u.RelativeTransformSpace.RTS_COMPONENT).translation.to_tuple()),'visible':model.is_visible(),'static_visible':c.visual.is_visible(),'armed':c.get_editor_property('bArmed'),'exploded':c.get_editor_property('bExploded'),'dead':c.get_editor_property('bDefeated'),'health':c.get_editor_property('Health'),'fuse_start':c.get_editor_property('FuseStartedAt'),'fuse_end':c.get_editor_property('FuseExplodesAt'),'flame':c.get_editor_property('FuseFlame').is_visible(),'warning':c.get_editor_property('BlastWarning').is_visible(),'mesh':model.get_skeletal_mesh_asset().get_path_name()})
  if now-self.lastcap>=1/20-.001:
   self.lastcap=now
   if self.born is None:aim=self.center+u.Vector(0,0,120);eye=self.center+u.Vector(1100,-1450,800)
   else:
    valid=[c for c in self.creeps if u.SystemLibrary.is_valid(c) and not c.get_editor_property('bDefeated')]
    aim=sum((c.get_actor_location() for c in valid),u.Vector())/len(valid) if valid else self.lastaim
    self.lastaim=aim;aim=aim+u.Vector(0,0,40);eye=aim+u.Vector(400,-650,330)
   self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
   name=f'summon_{len(self.captures):04}.png';u.RenderingLibrary.export_render_target(self.w,self.target,str(self.frames),name);self.captures.append({'time':now-self.at,'file':str(self.frames/name),'stage':'summon' if self.born is None else 'pursuit' if not self.killed else 'fuse'})
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.h.set_primary_input_held(False)
  self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  for a in reversed([*self.spawned,*self.creeps]):
   if u.SystemLibrary.is_valid(a):a.destroy_actor()
  result={'passed':error is None,'error':error,'checks':self.checks,'samples':self.rows,'captures':self.captures,'restored_engine_settings':self.old,'scope':__doc__}
  (self.out/'SoloCheck.json').write_text(json.dumps(result,indent=2));print('MAGE_SUMMON_SOLO_RESULT',json.dumps({'passed':result['passed'],'error':error,'checks':self.checks,'frames':len(self.captures)}))
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<100,'Summon check timed out'
   if u.GameplayStatics.is_game_paused(self.w):self.resolve_choice()
   now=self.now()
   if now<=self.last:return
   self.last=now
   state=self.mage.get_editor_property('State')
   if state in [u.MCNutBossState.TELEGRAPH,u.MCNutBossState.EXECUTING] and self.mage.get_editor_property('Attack')==u.MCNutBossAttack.SUMMON:self.casting=True
   if self.born is None:
    self.creeps=list(u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutKamikaze))
    if self.creeps:
     self.check('native mage cast creates two new minions',self.casting and len(self.creeps)==2)
     self.check('ground nuts consumed by actual summon',all(not u.SystemLibrary.is_valid(f) or f.get_editor_property('Phase')==u.MCFoodPhase.DISPOSED for f in self.foods))
     self.born=now;self.kill=self.creeps[0];self.survivor=self.creeps[1];self.lastaim=self.kill.get_actor_location()
     self.cc.hide_actor_components(self.mage)
     self.mage.set_editor_property('NextSummonAt',now+300);self.mage.set_editor_property('NextSpecialAt',now+300)
    assert now-self.at<6.,'Mage failed to summon from the placed real ground nuts'
   self.record(now)
   if self.born is not None:
    age=now-self.born
    if age>1.4 and not self.pressed:
     p=self.kill.get_actor_location();self.place(p+u.Vector(120,0,0),180)
     self.h.inventory.server_select(u.MCToolSlot.PICKAXE)
     if self.h.inventory.get_editor_property('Selected')==u.MCToolSlot.PICKAXE and not self.pc.is_reward_menu_open():self.h.set_primary_input_held(True);self.h.set_primary_input_held(False);self.pressed=True
    if self.pressed and not self.killed and u.SystemLibrary.is_valid(self.kill) and self.kill.get_editor_property('bDefeated'):
     self.check('real Pickaxe kill disarms new minion',not self.kill.get_editor_property('bArmed') and not self.kill.get_editor_property('bExploded'));self.killed=True
    if u.SystemLibrary.is_valid(self.survivor) and self.survivor.get_editor_property('bExploded') and not self.exploded:
     self.exploded=True;start=self.survivor.get_editor_property('FuseStartedAt');end=self.survivor.get_editor_property('FuseExplodesAt')
     self.check('surviving minion retains five-second fuse',abs(end-start-5)<.01 and now-start>=5)
    if age>5.3:
     self.check('kill and independent detonation both observed',self.killed and self.exploded)
     rows=[r for r in self.rows if r['path']==self.survivor.get_path_name() and r['game_time']-self.born>.3 and r['game_time']-self.born<1.3]
     # Actor movement updates at 20 Hz, observation at 60 Hz. Include the
     # intervening stationary samples instead of treating each 18 cm step as
     # one 60 Hz frame of travel.
     measured=sum(math.dist(a['location'][:2],b['location'][:2]) for a,b in zip(rows,rows[1:]))/(rows[-1]['game_time']-rows[0]['game_time'])
     self.check('observed faster native pursuit',345<measured<375,average_cm_s=measured,settings_cm_s=rows[-1]['settings_speed'])
     self.check('authored run moves feet on visible skeletal model',all(r['visible'] and not r['static_visible'] and r['mesh'].endswith('/SK_NutSummon.SK_NutSummon') for r in rows) and max(r['rate'] for r in rows)>2 and math.dist(rows[0]['foot_left'],rows[len(rows)//2]['foot_left'])>1)
     self.check('fuse flame and warning retained',any(r['flame'] for r in self.rows) and any(r['warning'] for r in self.rows));self.finish()
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

builtins.mc_mage_summon_check=SummonCheck()
