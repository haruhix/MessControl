"""Promote the host's saved F1 physics preset to the shared Data Asset, outside PIE."""
import configparser
import math
from pathlib import Path
import unreal as ue

path=Path(ue.Paths.project_saved_dir())/'PhysicsTuning.ini'
if not path.exists(): raise RuntimeError('Save a preset in F1 Tooth Lab as host first.')
config=configparser.ConfigParser(); config.read(path)
asset=ue.EditorAssetLibrary.load_asset('/Game/Data/DA_ToothPhysics')
settings=asset.get_editor_property('settings')
for key,prop,lo,hi in [
    ('Knockback','knockback',150,1100),('Lift','lift',80,650),('FallThreshold','fall_threshold',100,600),
    ('RagdollSeconds','ragdoll_seconds',0.5,6),('GetUpSeconds','get_up_seconds',0.25,2),
    ('MuscleStrength','muscle_strength',2,35),('Damping','damping',0.3,2),('Mass','mass',3,20)]:
    value=config.getfloat('ToothPhysics',key)
    if not math.isfinite(value): raise ValueError('Non-finite '+key)
    settings.set_editor_property(prop,max(lo,min(hi,value)))
asset.set_editor_property('settings',settings)
ue.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False)
ue.log('Physics preset applied to DA_ToothPhysics. Commit the asset to share it.')
