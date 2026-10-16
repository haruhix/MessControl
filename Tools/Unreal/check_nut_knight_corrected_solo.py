"""One solo PIE pass over corrected knight actions and actual gameplay transitions.

Uses the established native scheduler/tool-input fixture. Shortens the PIE-only
ball travel to one second and records scale stability along with combat results.
All fixtures disappear with PIE; no profile or map packages are saved.
"""
from pathlib import Path

scope=__doc__
base_path=Path(__file__).with_name('check_nut_knight_ball_solo.py')
code=base_path.read_text(encoding='utf-8').rsplit('builtins.mc_nut_knight_check=KnightCheck()',1)[0]
code=code.replace('Artifacts/NutKnightBall_20261016','Artifacts/NutKnightCorrected_20261016')
assert code.count('assert age<6.\n')==1
code=code.replace('assert age<6.\n','assert age<self.jump_budget\n')
exec(compile(code,str(base_path),'exec'))
__doc__=scope

class CorrectedKnightCheck(KnightCheck):
 def __init__(self):
  super().__init__()
  model=self.boss.get_editor_property('TankModel')
  ref=u.AnimPoseExtensions.get_reference_pose(model.get_skeletal_mesh_asset().get_editor_property('skeleton'))
  self.names=u.AnimPoseExtensions.get_bone_names(ref)
  self.scales={str(n):u.AnimPoseExtensions.get_bone_pose(ref,n,u.AnimPoseSpaces.WORLD).scale3d for n in self.names}
  self.component_scale=model.get_editor_property('relative_scale3d')
  self.scale_max=0.;self.component_scale_max=0.
  bs=self.boss.get_editor_property('BossSettings')
  self.jump_budget=(bs.get_editor_property('JumpWindup')+bs.get_editor_property('JumpFlightSeconds')
                    +bs.get_editor_property('TankJumpAnimationSeconds')*(1-bs.get_editor_property('TankJumpImpactFraction'))
                    +bs.get_editor_property('TankAnimationBlendSeconds')+.5)
 def begin(self,case):
  super().begin(case)
  if case=='roll':
   bs=self.boss.get_editor_property('BossSettings')
   bs.set_editor_property('RollDuration',1.);bs.set_editor_property('RollDurationMax',1.)
   self.boss.set_editor_property('BossSettings',bs)
 def record(self,now):
  super().record(now)
  if not u.SystemLibrary.is_valid(self.boss):return
  model=self.boss.get_editor_property('TankModel')
  ball=self.boss.get_editor_property('Attack')==u.MCNutBossAttack.ROLL and self.boss.get_editor_property('State') in [u.MCNutBossState.TELEGRAPH,u.MCNutBossState.EXECUTING,u.MCNutBossState.RECOVERY]
  error=max((model.get_socket_transform(n,u.RelativeTransformSpace.RTS_COMPONENT).scale3d-self.scales[str(n)]).length() for n in self.names)
  component_error=(model.get_editor_property('relative_scale3d')-self.component_scale).length()
  assert math.isfinite(error) and math.isfinite(component_error)
  if not ball:self.scale_max=max(self.scale_max,error)
  self.component_scale_max=max(self.component_scale_max,component_error)
  self.rows[-1].update(ball_scale_allowed=ball,bone_scale_error=error,component_scale_error=component_error)
 def finish(self,error=None):
  if not error:
   try:
    self.check('non-ball bone scales match skeleton throughout gameplay',self.scale_max<.0002,max_error=self.scale_max)
    self.check('mesh component scale remains fixed',self.component_scale_max<.0001,max_error=self.component_scale_max)
   except Exception:error=traceback.format_exc()
  super().finish(error)

builtins.mc_nut_knight_check=CorrectedKnightCheck()
