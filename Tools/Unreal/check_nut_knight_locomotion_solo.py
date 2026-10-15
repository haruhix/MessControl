"""Three-second solo observation of native idle -> chase -> stop -> idle.

Shares the bounded Knight fixture; no input attack or state fabrication.
The main combat report is preserved. Only observer placement and cooldown
readiness are controlled; the AI decides to move/stop from distance.
"""
from pathlib import Path
code=Path(__file__).with_name('check_nut_knight_ball_solo.py').read_text()
exec(compile(code.rsplit('builtins.mc_nut_knight_check=KnightCheck()',1)[0],str(Path(__file__).with_name('check_nut_knight_ball_solo.py')),'exec'))

class LocomotionCheck(KnightCheck):
 def __init__(self):
  super().__init__()
  self.out=self.out/'Locomotion';self.out.mkdir(exist_ok=True)
  self.frames=self.out/'GameplayFrames';self.frames.mkdir(exist_ok=True)
  self.case='locomotion';self.initial=self.boss.get_actor_location();self.stopped=None
 def tick(self,dt):
  if self.done or self.busy:return
  self.busy=True
  try:
   now=self.now()
   if now<=self.last:return
   self.last=now;age=now-self.at
   self.place(distance=700 if .5<age<1.6 else 250)
   self.record(now)
   self.rows[-1]['foot']=list(self.boss.get_editor_property('TankModel').get_socket_location('foot_r').to_tuple())
   if age>2.2 and self.stopped is None:self.stopped=self.boss.get_actor_location()
   if age>3.:
    self.check('native chase moves the boss',self.initial.distance(self.boss.get_actor_location())>100.)
    self.check('native proximity stops movement and keeps idle state',self.stopped.distance(self.boss.get_actor_location())<2. and all(r['state']==str(u.MCNutBossState.IDLE) for r in self.rows))
    self.check('quarter second blend remains configured',abs(self.boss.get_editor_property('BossSettings').get_editor_property('TankAnimationBlendSeconds')-.25)<.001)
    self.finish()
  except Exception:self.finish(traceback.format_exc())
  finally:self.busy=False

builtins.mc_nut_knight_check=LocomotionCheck()
