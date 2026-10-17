"""Short solo collision proof on an authored V10 crown.

Temporary fixture: pause directors, spawn one knight with the saved boss profile,
set only its ability readiness and place its target on the tooth's crown. The
native scheduler, telegraph, roll movement, blocking sweep and tooth HP run
normally; no attack state or tooth health is fabricated.
"""
import unreal as u,json,time,traceback,builtins,runpy
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Artifacts/NutRollToothDamage_20261017'
Base=runpy.run_path(str(root/'Tools/Unreal/check_arena_v10_solo.py'),init_globals=dict(ARENA_CHECK_INSTANTIATE=False,ARENA_CHECK_OUTPUT=str(out)))['ArenaCheck']

class RollToothCheck(Base):
 def __init__(self):
  super().__init__()
  self.subject=self.teeth[7];self.impact_at=None;self.saw_tell=False;self.rows=[];self.boss=None
  self.hero.set_primary_input_held(False);self.hero.character_movement.disable_movement()
  self.tongue.set_editor_property('bAutomaticYawns',False)
  center,extent,_=u.SystemLibrary.get_component_bounds(self.subject.body)
  self.center=center;self.extent=extent
  settings=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain').get_editor_property('settings').get_editor_property('boss')
  self.damage=settings.get_editor_property('RollDamage')
  radius=settings.get_editor_property('TankHeight')*100/220
  tongue_center,_,_=u.SystemLibrary.get_component_bounds(self.tongue.surface)
  inward=tongue_center-center;inward.z=0;inward=u.MathLibrary.normal(inward)
  # Approach diagonally from the tongue interior. The side of the new crown
  # overhangs tissue: a straight Y approach starts too near the footprint edge
  # for the native roll tell's minimum 200 cm locked corridor to fit.
  start=center+inward*650;floor=self.floor(start);assert floor is not None
  pose=u.Transform(location=floor+u.Vector(0,0,radius+4))
  lib=u.get_default_object(u.GameplayStatics)
  self.boss=lib.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.MCNutBoss,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  base=u.MCNutEnemySettings();base.set_editor_properties(dict(BodyRadius=radius,MaxHealth=1400.,MoveSpeed=settings.get_editor_property('TankMoveSpeed'),HopHeight=4.))
  for k,v in dict(Tongue=self.tongue,BossRole=u.MCNutBossRole.TANK,BossSettings=settings,Settings=base,Health=1400.,ShieldHealth=500.,State=u.MCNutBossState.IDLE,Target=self.hero).items():self.boss.set_editor_property(k,v)
  lib.call_method('FinishSpawningActor',args=(self.boss,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  self.boss.set_actor_rotation(u.MathLibrary.find_look_at_rotation(start,center),False)
  self.hero.set_actor_location(center+u.Vector(0,0,extent.z+self.hero.capsule_component.get_scaled_capsule_half_height()+4),False,True)
  now=u.GameplayStatics.get_time_seconds(self.world)
  for k in ['NextMeleeAt','NextChargeAt','NextJumpAt','NextRollAt','NextSpecialAt']:self.boss.set_editor_property(k,now+300)
  self.boss.set_editor_property('NextRollAt',now);self.boss.set_editor_property('NextSpecialAt',now)
  self.hp0=self.hp();self.other_hp={t.get_path_name():t.get_editor_property('State').get_editor_property('Health') for t in self.teeth if t!=self.subject}
  tangent=u.Vector(inward.y,-inward.x,0)
  self.eye=center+inward*1250+tangent*950+u.Vector(0,0,650);self.look=center+inward*200+u.Vector(0,0,130)
  print('NUT_ROLL_TOOTH_CHECK_STARTED',self.subject.get_path_name(),'damage',self.damage)
 def hp(self):return self.subject.get_editor_property('State').get_editor_property('Health')
 def tick(self,dt):
  if self.done:return
  try:
   assert time.monotonic()-self.started<60,'Wall limit'
   now=u.GameplayStatics.get_time_seconds(self.world)-self.t0
   hs=self.hero.status.get_editor_property('State');hs.set_editor_property('Health',100.);self.hero.status.set_editor_property('State',hs)
   state=self.boss.get_editor_property('State');attack=self.boss.get_editor_property('Attack')
   self.rows.append(dict(time=now,state=str(state),attack=str(attack),boss=list(self.boss.get_actor_location().to_tuple()),forward=list(self.boss.get_editor_property('AttackForward').to_tuple()),tooth_health=self.hp()))
   if attack==u.MCNutBossAttack.ROLL and state==u.MCNutBossState.TELEGRAPH:
    assert self.hp()==self.hp0,'Tooth damaged during windup'
    if not self.saw_tell:self.saw_tell=True;self.shot('01_RollWindup',self.eye,self.look)
   if self.hp()<self.hp0 and self.impact_at is None:
    self.check('native roll warning leaves the tooth intact',self.saw_tell)
    self.check('a real rolling collision damages the authored V10 tooth',attack==u.MCNutBossAttack.ROLL and state==u.MCNutBossState.EXECUTING and abs(self.hp()-(self.hp0-self.damage))<.01)
    self.check('roll damage reveals the chipped crown and branching cracks',self.subject.damage_stage()==u.MCArenaToothDamageStage.CHIPPED and self.subject.visual.static_mesh.get_name().endswith('_Chipped'))
    self.check('teeth outside the collision retain their HP',all(t.get_editor_property('State').get_editor_property('Health')==self.other_hp[t.get_path_name()] for t in self.teeth if t!=self.subject))
    self.impact_at=now;self.after_hp=self.hp();self.shot('02_RollImpact',self.eye,self.look)
   if self.impact_at is not None and now-self.impact_at>.3:
    self.check('the collision does not drain health on every movement substep',self.hp()==self.after_hp)
    self.shot('03_ChippedAfterImpact',self.eye,self.look);self.finish()
   assert now<9,'Native roll did not collide with the tooth'
  except Exception:self.finish(traceback.format_exc())
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.hero.set_primary_input_held(False)
  if self.boss and u.SystemLibrary.is_valid(self.boss):self.boss.destroy_actor()
  self.capture.destroy_actor()
  self.engine.set_editor_property('FixedFrameRate',self.old['fps']);self.engine.set_editor_property('bUseFixedFrameRate',self.old['fixed']);self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  report=dict(passed=error is None,error=error,checks=self.checks,rows=self.rows,captures=self.captures,damage=self.damage,hp_before=self.hp0,hp_after=self.hp(),restored_engine_settings=self.old,scope=__doc__)
  (out/'SoloCheck.json').write_text(json.dumps(report,indent=2))
  print('NUT_ROLL_TOOTH_SOLO_RESULT',report['passed'],error)

builtins.mc_nut_roll_tooth_check=RollToothCheck()
