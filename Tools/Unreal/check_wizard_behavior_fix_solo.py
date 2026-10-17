"""Solo proof of idle arms during walking, live idle during rain and blink/fire-nut.

Uses the real F3 nut event, saved profile and native AI. The fixture controls
observer placement and ability readiness, restores observer HP, clears rain
food between stages and delivers three real Pickaxe hits. It never fabricates
damage, teleport, animation state, projectile spawn or attack results.
"""
from pathlib import Path
scope=__doc__;base=Path(__file__).with_name('check_nut_wizard_update_solo.py')
code=base.read_text().rsplit('builtins.mc_wizard_update_check=WizardCheck()',1)[0]
code=code.replace('Artifacts/NutWizardUpdate_20261017','Artifacts/WizardBehaviorFix_20261017')
code=code.replace("if self.case=='pressure' and self.tp_start is not None:","if self.case=='pressure' and self.tp_start is not None and self.mage.get_editor_property('Attack')==u.MCNutBossAttack.TELEPORT:")
code=code.replace('eye=aim+u.Vector(1100,-1650,850)','eye=aim+u.Vector(1100,-1650,850)*max(1.2,self.tp_start.distance(self.mage.get_editor_property("LockedTarget"))/1000)')
exec(compile(code,str(base),'exec'));__doc__=scope

class BehaviorCheck(WizardCheck):
 def __init__(self):
  super().__init__();builtins.mc_wizard_behavior_check=self
  self.model=self.mage.get_editor_property('MageModel');self.walk_errors=[];self.rain_errors=[];self.rain_heads=[]
  self.boss=self.mage.get_editor_property('BossSettings');self.idle=self.boss.get_editor_property('MageIdleAnimation');self.walk=self.boss.get_editor_property('MageWalkAnimation')
  self.options=u.AnimPoseEvaluationOptions();self.options.set_editor_property('optional_skeletal_mesh',self.model.get_skeletal_mesh_asset())
  self.arms=[f'{bone}_{side}' for side in ['l','r'] for bone in ['shoulder','arm_stretch','arm_twist','forearm_stretch','forearm_twist','hand']]
  self.legs=[f'{bone}_{side}' for side in ['l','r'] for bone in ['thigh_stretch','leg_stretch','foot']]
  self.after_teleport=None;self.projectile_seen=False;self.followup_at=None;self.teleport_at=None;self.teleport_distance=0
  self.stage('walk')
 def pose_error(self,clip,names,now):
  actual={}
  for name in names:
   child=self.model.get_socket_transform(name,u.RelativeTransformSpace.RTS_COMPONENT)
   parent=self.model.get_socket_transform(self.model.get_parent_bone(name),u.RelativeTransformSpace.RTS_COMPONENT)
   actual[name]=u.MathLibrary.make_relative_transform(child,parent)
  best=None
  for offset in [0.,-1/60,-2/60,-3/60]:
   sample=(now+offset)%clip.get_play_length();pose=u.AnimPoseExtensions.get_anim_pose_at_time(clip,sample,self.options)
   angle=distance=0.
   for name in names:
    expected=u.AnimPoseExtensions.get_bone_pose(pose,name,u.AnimPoseSpaces.LOCAL);a=actual[name];q=a.rotation;r=expected.rotation
    dot=abs(q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w)
    angle=max(angle,math.degrees(2*math.acos(min(1.,max(0.,dot)))))
    distance=max(distance,a.translation.distance(expected.translation))
   result=dict(angle=angle,translation=distance,offset=offset)
   if best is None or angle+distance*10<best['angle']+best['translation']*10:best=result
  return best
 def record(self,now):
  previous=len(self.captures);super().record(now)
  if len(self.captures)==previous:return
  age=now-self.at
  if self.case=='walk' and age>.7:
   arms=self.pose_error(self.idle,self.arms,now);legs=self.pose_error(self.walk,self.legs,now)
   self.walk_errors.append(dict(time=age,idle_arms=arms,walking_legs=legs));self.rows[-1].update(idle_arms=arms,walking_legs=legs)
  if self.case=='rain' and age>self.boss.get_editor_property('MageRainAnimationSeconds')+.4 and self.mage.get_editor_property('State')==u.MCNutBossState.EXECUTING:
   pose=self.pose_error(self.idle,['root_x','neck_x','head_x',*self.arms,*self.legs],now)
   self.rain_errors.append(dict(time=age,error=pose));self.rain_heads.append(self.rows[-1]['head']);self.rows[-1]['live_idle_error']=pose
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.h.set_primary_input_held(False)
  self.engine.set_editor_properties(dict(bUseFixedFrameRate=self.old['fixed'],FixedFrameRate=self.old['fps']))
  self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  result=dict(passed=error is None,error=error,checks=self.checks,samples=self.rows,captures=self.captures,walk_pose_checks=self.walk_errors,rain_idle_checks=self.rain_errors,teleport_distance=self.teleport_distance,restored_engine_settings=self.old,scope=__doc__)
  (self.out/'SoloCheck.json').write_text(json.dumps(result,indent=2))
  for actor in reversed(self.spawned):
   try:
    if u.SystemLibrary.is_valid(actor):actor.destroy_actor()
   except TypeError as ex:
    if 'ObjectInstance is null' not in str(ex):raise
  print('WIZARD_BEHAVIOR_RESULT',json.dumps(dict(passed=result['passed'],error=error,checks=self.checks,frames=len(self.captures),teleport_distance=self.teleport_distance)))
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<150,'wizard behavior wall limit'
   if u.GameplayStatics.is_game_paused(self.w):self.resolve_choice()
   now=self.now()
   if now<=self.last:return
   self.last=now;age=now-self.at
   hp=self.h.status.get_editor_property('State');hp.set_editor_property('Health',100.);self.h.status.set_editor_property('State',hp);self.h.set_editor_property('bInCoffee',False)
   assert u.SystemLibrary.is_valid(self.mage)
   state=self.mage.get_editor_property('State');attack=self.mage.get_editor_property('Attack');self.record(now)
   if self.case=='walk' and age>2.8:
    self.check('walking still moves the original body and legs',self.mage.get_actor_location().distance(self.start_pos)>100 and len(self.walk_errors)>10)
    arms=max(r['idle_arms']['angle'] for r in self.walk_errors);legs=max(r['walking_legs']['angle'] for r in self.walk_errors)
    self.check('both walking arm chains match the live idle animation',arms<3.,max_rotation_error=arms)
    self.check('walking legs retain the original walking animation',legs<3.,max_rotation_error=legs)
    self.stage('rain')
   elif self.case=='rain':
    if state==u.MCNutBossState.EXECUTING and attack==u.MCNutBossAttack.NUT_RAIN:self.saw=True
    if self.saw and state==u.MCNutBossState.IDLE:
     self.check('rain continues above the completed cast and a full idle pose',len(self.rain_errors)>8 and max(r['error']['angle'] for r in self.rain_errors)<3.,samples=len(self.rain_errors),max_rotation_error=max((r['error']['angle'] for r in self.rain_errors),default=-1))
     motion=max((math.dist(a,b) for a in self.rain_heads for b in self.rain_heads),default=0.)
     self.check('the post-cast idle keeps animating rather than freezing',motion>.03,head_motion_cm=motion)
     self.check('rain still spawns real falling nuts',self.event.get_editor_property('NutsSpawned')>0)
     for food in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCFoodActor):food.destroy_actor()
     self.stage('pressure')
    assert age<11,'rain did not return to idle'
   elif self.case=='pressure':
    if self.tp_start is None:
     self.tool_place()
     for key,when in [('hit1',.3),('hit2',1.),('hit3',1.7)]:
      if age>when:self.press(key)
     health=self.mage.get_editor_property('Health')
     if health<self.last_hp:self.damage_hits+=1;self.last_hp=health
    if attack==u.MCNutBossAttack.TELEPORT:
     if self.tp_start is None:
      self.check('three real hits retain the evasive teleport trigger',self.damage_hits>=3,hits=self.damage_hits)
      self.tp_start=self.mage.get_actor_location();self.teleport_at=now
      # Competing specials are ready while fire-nut is deliberately cooling.
      self.mage.set_editor_property('NextRainAt',0.);self.mage.set_editor_property('NextSummonAt',0.)
     self.teleport_distance=max(self.teleport_distance,self.mage.get_actor_location().distance(self.tp_start))
    elif self.tp_start is not None and attack!=u.MCNutBossAttack.NONE and self.after_teleport is None:
     self.after_teleport=attack;self.followup_at=now
     self.check('the first attack after teleport is fire-nut ahead of ready rain/summon',attack==u.MCNutBossAttack.FIREBALL)
     self.check('the full teleport animation finishes before fire-nut starts',now-self.teleport_at>=self.boss.get_editor_property('MageTeleportAnimationSeconds')+.25-.1,elapsed=now-self.teleport_at)
     self.check('teleport reaches the increased 12 to 20 metre distance',1190<=self.teleport_distance<=2010,distance_cm=self.teleport_distance)
    if self.followup_at is not None:
     self.projectile_seen|=bool(u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutSpellProjectile))
     if state==u.MCNutBossState.IDLE:
      self.check('automatic fire-nut creates a real projectile and fully finishes',self.projectile_seen and now-self.followup_at>=2.5,elapsed=now-self.followup_at)
      self.finish()
    assert age<16,'teleport/fire-nut chain did not finish'
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

builtins.mc_wizard_behavior_check=BehaviorCheck()
