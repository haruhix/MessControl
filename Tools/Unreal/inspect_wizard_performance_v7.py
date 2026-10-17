"""Small live saved-asset inspection for the performance prototype."""
import json
from pathlib import Path
import unreal as u

mesh=u.EditorAssetLibrary.load_asset('/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard')
skeleton=mesh.get_editor_property('skeleton')
pose=u.AnimPoseExtensions.get_reference_pose(skeleton)
names=[str(n) for n in u.AnimPoseExtensions.get_bone_names(pose)]
data=dict(mesh=mesh.get_path_name(),skeleton=skeleton.get_path_name(),bones=names,count=len(names))
Path(u.Paths.project_dir(),'Saved/Checks/WizardPerformanceSkeletonInspection.json').write_text(json.dumps(data,indent=2),encoding='utf8')
u.log('MC_WIZARD_V7_SKELETON '+json.dumps(data))
