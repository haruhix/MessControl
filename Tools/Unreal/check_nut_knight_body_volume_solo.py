"""Solo PIE smoke check with both knight meshes using volume-preserving skinning.

Uses the established real tool-hit/native scheduler fixture. Unlike checking
bone scale alone, this asserts active GPU deformers and captures rendered poses.
"""
from pathlib import Path

volume_scope=__doc__
path=Path(__file__).with_name('check_nut_knight_corrected_solo.py')
code=path.read_text(encoding='utf-8').rsplit('builtins.mc_nut_knight_check=CorrectedKnightCheck()',1)[0]
code=code.replace('Artifacts/NutKnightCorrected_20261016','Artifacts/NutKnightBodyFix_20261017')
exec(compile(code,str(path),'exec'))
__doc__=volume_scope

class BodyVolumeCheck(CorrectedKnightCheck):
 def __init__(self):
  super().__init__()
  self.volume_meshes=set()
 def record(self,now):
  super().record(now)
  if not u.SystemLibrary.is_valid(self.boss):return
  model=self.boss.get_editor_property('TankModel');mesh=model.get_skeletal_mesh_asset()
  graph=mesh.get_default_mesh_deformer()
  assert graph and graph.get_name()=='DG_DualQuatSkin_Morph_Cloth'
  instance=model.get_mesh_deformer_instance()
  assert instance is not None,('GPU deformer instance missing',mesh.get_path_name())
  self.volume_meshes.add(mesh.get_name())
  self.rows[-1].update(deformer=graph.get_path_name(),deformer_instance=instance.get_class().get_name())
 def finish(self,error=None):
  if not error:
   try:self.check('both shield variants use active volume-preserving GPU deformers',self.volume_meshes=={'SK_NutTank','SK_NutTank_NoShield'},meshes=sorted(self.volume_meshes))
   except Exception:error=traceback.format_exc()
  super().finish(error)

builtins.mc_nut_knight_check=BodyVolumeCheck()
