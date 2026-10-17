"""Short solo check of the saved fire-nut: native blink/cast, palm and speed.

The fixture places one wizard/observer, holds other abilities on cooldown and
restores observer HP. Native AI performs the full teleport/fire-nut chain.
"""
from pathlib import Path
scope=__doc__;base=Path(__file__).with_name('check_mage_summons_solo.py')
code=base.read_text().rsplit('builtins.mc_mage_summon_check=SummonCheck()',1)[0]
code=code.replace('Artifacts/MageSummons_20261016','Artifacts/FireNutHandFix_20261017')
exec(compile(code,str(base),'exec'));__doc__=scope

class FireNutCheck(SummonCheck):
 def __init__(self):
  super().__init__();self.shot=None;self.release=None;self.flight=None;self.was_cast=False;self.blink=False;self.finished_cast=False
  self.origin_errors=[];self.charge_errors=[];self.motion=[];self.captures=[];self.rows=[];self.last_record=-1
  self.model=self.mage.get_editor_property('MageModel')
  for food in self.foods:food.destroy_actor()
  self.foods=[]
  for key in ['NextSummonAt','NextRainAt','NextMeleeAt']:self.mage.set_editor_property(key,self.now()+300)
  for key in ['NextFireballAt','NextTeleportAt','NextSpecialAt']:self.mage.set_editor_property(key,self.now())
 def palm(self):return u.MathLibrary.transform_location(self.model.get_socket_transform('hand_r',u.RelativeTransformSpace.RTS_WORLD),u.Vector(0,8,0))
 def record(self,now):
  attack=self.mage.get_editor_property('Attack');state=self.mage.get_editor_property('State')
  if attack!=u.MCNutBossAttack.FIREBALL:return
  self.was_cast=True
  palm=self.palm()
  if state==u.MCNutBossState.TELEGRAPH:
   for effect in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutCombatEffect):
    cue=effect.get_editor_property('Cue')
    if cue.get_editor_property('Type') in [u.MCNutCombatCue.FIRE_CAST,u.MCNutCombatCue.CAST_CHARGE]:
     self.charge_errors.append(effect.get_editor_property('Flames').get_world_location().distance(palm))
  shots=u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutSpellProjectile)
  if shots and self.shot is None:
   self.shot=shots[0];self.release=now;f=self.shot.get_editor_property('Flight');start=f.get_editor_property('Start');target=f.get_editor_property('Target');duration=f.get_editor_property('Seconds')
   self.flight=dict(start=list(start.to_tuple()),target=list(target.to_tuple()),seconds=duration,speed=start.distance(target)/duration,release=now)
  if self.release is not None and now-self.release<.09:self.origin_errors.append(u.Vector(*self.flight['start']).distance(palm))
  if self.shot is not None and u.SystemLibrary.is_valid(self.shot) and not self.shot.get_editor_property('Impact').get_editor_property('bImpacted'):
   self.motion.append(dict(time=now,pos=list(self.shot.get_actor_location().to_tuple())))
  self.rows.append(dict(age=now-self.at,state=str(state),palm=list(palm.to_tuple()),shot=self.shot is not None))
  if now-self.lastcap<1/20-.001:return
  self.lastcap=now
  forward=u.MathLibrary.get_forward_vector(self.mage.get_actor_rotation());right=u.MathLibrary.get_right_vector(self.mage.get_actor_rotation());p=self.mage.get_actor_location();center=u.SystemLibrary.get_component_bounds(self.tongue.get_editor_property('Surface'))[0]
  side=1 if (p+right*300-center).length_squared()<(p-right*300-center).length_squared() else -1
  aim=p+forward*130+u.Vector(0,0,70);eye=p+forward*750+right*(side*330)+u.Vector(0,0,330)
  self.capture.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,aim),False,True);self.cc.capture_scene()
  name=f'fire_nut_{len(self.captures):04}.png';u.RenderingLibrary.export_render_target(self.w,self.target,str(self.frames),name)
  self.captures.append(dict(time=now-self.at,file=str(self.frames/name),since_release=now-self.release if self.release is not None else None))
 def finish(self,error=None):
  if self.done:return
  self.done=True;u.unregister_slate_post_tick_callback(self.handle);self.h.set_primary_input_held(False)
  self.engine.set_editor_properties(dict(bUseFixedFrameRate=self.old['fixed'],FixedFrameRate=self.old['fps']));self.perf.set_editor_property('bThrottleCPUWhenNotForeground',self.old['throttle'])
  result=dict(passed=error is None,error=error,checks=self.checks,flight=self.flight,origin_errors=self.origin_errors,charge_errors=self.charge_errors,motion=self.motion,captures=self.captures,samples=self.rows,preferences_restored=True,scope=__doc__)
  (self.out/'SoloCheck.json').write_text(json.dumps(result,indent=2))
  for a in reversed(self.spawned):
   try:
    if u.SystemLibrary.is_valid(a):a.destroy_actor()
   except TypeError as ex:
    if 'ObjectInstance is null' not in str(ex):raise
  print('FIRE_NUT_SOLO_RESULT',dict(passed=result['passed'],error=error,checks=self.checks,flight=self.flight,frames=len(self.captures)))
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   assert time.monotonic()-self.started<55,'wall limit'
   now=self.now()
   if now<=self.last:return
   self.last=now;hp=self.h.status.get_editor_property('State');hp.set_editor_property('Health',100.);self.h.status.set_editor_property('State',hp)
   self.blink|=self.mage.get_editor_property('Attack')==u.MCNutBossAttack.TELEPORT
   self.record(now)
   if self.was_cast and self.mage.get_editor_property('State')==u.MCNutBossState.IDLE:self.finished_cast=True
   if self.release is not None and now-self.release>2:
    self.check('Native teleport still precedes the fire-nut cast',self.blink)
    self.check('Saved and sanitized fire-nut speed is doubled',abs(self.flight['speed']-1100)<1 and self.mage.get_editor_property('BossSettings').get_editor_property('FireballSpeed')==1100,speed_cm_s=self.flight['speed'])
    self.check('The projectile leaves the casting right palm',bool(self.origin_errors) and min(self.origin_errors)<12,min_error_cm=min(self.origin_errors,default=-1))
    self.check('The charging flames track the casting right palm',len(self.charge_errors)>10 and sorted(self.charge_errors)[len(self.charge_errors)//2]<8,median_error_cm=sorted(self.charge_errors)[len(self.charge_errors)//2] if self.charge_errors else -1)
    speeds=[math.dist(a['pos'],b['pos'])/(b['time']-a['time']) for a,b in zip(self.motion,self.motion[1:]) if b['time']>a['time'] and math.dist(a['pos'],b['pos'])>1]
    self.check('Actual projectile movement uses the faster flight',len(speeds)>3 and abs(sorted(speeds)[len(speeds)//2]-1100)<60,samples=len(speeds),median_speed=sorted(speeds)[len(speeds)//2] if speeds else -1)
    self.check('The complete cast returns to idle',self.finished_cast)
    self.finish()
   assert now-self.at<18,'native teleport/fire-nut chain did not finish'
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

builtins.mc_fire_nut_check=FireNutCheck()
