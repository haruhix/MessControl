"""One real frontal pickaxe hit after compiling the overlay for mesh deformation."""
from pathlib import Path
path=Path(__file__).with_name('check_nut_knight_ball_solo.py')
code=path.read_text(encoding='utf-8').rsplit('builtins.mc_nut_knight_check=KnightCheck()',1)[0]
code=code.replace('Artifacts/NutKnightBall_20261016','Artifacts/NutKnightBodyFix_20261017/OverlayCheck')
exec(compile(code,str(path),'exec'))
MC_NUT_KNIGHT_REACTIONS_ONLY=True

class OverlayCheck(KnightCheck):
 def __init__(self):
  (Path(u.Paths.project_dir())/'Artifacts/NutKnightBodyFix_20261017/OverlayCheck').mkdir(exist_ok=True)
  super().__init__()
  self.component_scale=self.boss.get_editor_property('TankModel').get_editor_property('relative_scale3d')
  mat=u.load_asset('/Game/Gameplay/VFX/M_CombatHitOverlay')
  assert mat.get_editor_property('used_with_mesh_deformer')
 def begin(self,case):
  if case=='rear_hit':
   self.check('active GPU deformer survives the actual hit overlay',self.boss.get_editor_property('TankModel').get_mesh_deformer_instance() is not None)
   self.finish();return
  super().begin(case)

builtins.mc_nut_knight_check=OverlayCheck()
