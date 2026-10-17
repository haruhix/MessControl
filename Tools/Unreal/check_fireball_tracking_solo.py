"""Solo native blink/fire-nut: move during the cast, then dodge after release.

Only observer placement, other-ability readiness and observer HP are controlled.
Native AI supplies aiming, animation, projectile release, flight and collision.
"""
from pathlib import Path
tracking_scope=__doc__;base=Path(__file__).with_name('check_fire_nut_hand_solo.py')
code=base.read_text().rsplit('builtins.mc_fire_nut_check=FireNutCheck()',1)[0]
code=code.replace('Artifacts/FireNutHandFix_20261017','Artifacts/FireballTracking_20261017').replace('1100','2800').replace('speed is doubled','speed is increased')
exec(compile(code,str(base),'exec'));__doc__=tracking_scope

class TrackingCheck(FireNutCheck):
 def __init__(self):
  self.cast_at=None;self.move_steps=0;self.last_move=-100;self.aim_errors=[];self.markers=[];self.dodged=False;self.fixed_errors=[];self.terminal=False;self.hit_count=None;self.release_error=None
  super().__init__()
 def record(self,now):
  attack=self.mage.get_editor_property('Attack');state=self.mage.get_editor_property('State')
  if attack==u.MCNutBossAttack.FIREBALL and state==u.MCNutBossState.TELEGRAPH:
   if self.cast_at is None:
    self.cast_at=now;self.cast_start=self.h.get_actor_location();delta=self.cast_start-self.mage.get_actor_location();self.axis=u.Vector(-delta.y,delta.x,0);self.axis=self.axis/max(1.,self.axis.length());to_center=self.center-self.cast_start
    if to_center.x*self.axis.x+to_center.y*self.axis.y<0:self.axis=-self.axis
   age=now-self.cast_at
   if (age>.25 and self.move_steps==0) or (age>.55 and self.move_steps==1):
    self.move_steps+=1;self.place(self.cast_start+self.axis*(150*self.move_steps));self.last_move=now
   if now-self.last_move>.09:
    self.aim_errors.append(self.mage.get_editor_property('LockedTarget').distance(self.h.get_actor_location()))
   for effect in u.GameplayStatics.get_all_actors_of_class(self.w,u.MCNutCombatEffect):
    if effect.get_editor_property('SourceActor')!=self.mage:continue
    kind=effect.get_editor_property('Cue').get_editor_property('Type')
    if kind in [u.MCNutCombatCue.CAST_CHARGE,u.MCNutCombatCue.FIRE_CAST]:
     self.markers.append(dict(type=str(kind),visible=any(effect.get_editor_property(k).is_visible() for k in ['Warning','DropWarnings','DropShadows'])))
  previous=self.shot;super().record(now)
  if self.shot is not None and previous is None:
   self.release_error=u.Vector(*self.flight['target']).distance(self.h.get_actor_location())
   self.place(self.h.get_actor_location()+self.axis*500);self.dodged=True
  if self.shot is not None:
   try:valid=u.SystemLibrary.is_valid(self.shot)
   except TypeError as ex:
    if 'ObjectInstance is null' not in str(ex):raise
    valid=False
   if valid:
    self.fixed_errors.append(self.shot.get_editor_property('Flight').get_editor_property('Target').distance(u.Vector(*self.flight['target'])))
    if self.shot.get_editor_property('Impact').get_editor_property('bImpacted'):
     self.terminal=True;self.hit_count=self.shot.get_editor_property('DamageApplications')
 def finish(self,error=None):
  if self.done:return
  if error is None:
   try:
    self.check('Two real movements during the cast are followed by live aim',self.move_steps==2 and len(self.aim_errors)>8 and max(self.aim_errors)<2,samples=len(self.aim_errors),max_error_cm=max(self.aim_errors,default=-1))
    self.check('The projectile targets the player position at release',self.release_error is not None and self.release_error<1,error_cm=self.release_error)
    self.check('The charging hand shows no floor destination marker',len(self.markers)>10 and not any(m['visible'] for m in self.markers),samples=len(self.markers))
    self.check('A real post-release sidestep escapes the fixed projectile',self.dodged and self.terminal and self.hit_count==0 and self.fixed_errors and max(self.fixed_errors)<.01,max_target_drift_cm=max(self.fixed_errors,default=-1),damage_applications=self.hit_count)
   except Exception:error=traceback.format_exc()
  super().finish(error)
  p=self.out/'SoloCheck.json';result=json.loads(p.read_text());result.update(scope=tracking_scope,tracking=dict(moves=self.move_steps,aim_errors=self.aim_errors,release_error=self.release_error,marker_samples=self.markers,target_drift=self.fixed_errors,terminal=self.terminal,damage_applications=self.hit_count));p.write_text(json.dumps(result,indent=2))

builtins.mc_fireball_tracking_check=TrackingCheck()
