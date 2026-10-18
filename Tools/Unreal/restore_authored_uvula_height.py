"""Restore only the new artist uvula's hanging placement, preserving the mouth.

Run in idle, clean L_Mouth. The original SM_MiniTong-derived mesh is already
connected to the native interactive component. This fixes its saved rest
translation; no mesh, runtime code, throat root or mouth morph is replaced.
The recorded pre-fix placement is guarded, so rerunning cannot add another lift.
"""
import hashlib
import json
import math
import shutil
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUT=ROOT/'Saved/UvulaLocation_20261018'
OUT.mkdir(parents=True,exist_ok=True)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name()=='/Game/Maps/L_Mouth.L_Mouth'
assert not u.EditorLevelLibrary.get_pie_worlds(False),'Preserve active gameplay'
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),'Preserve unsaved edits'
throats=[a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if isinstance(a,u.MCThroat)]
assert len(throats)==1
throat=throats[0]
uvula=throat.get_editor_property('uvula')
mouth=throat.get_editor_property('authored_mouth')
mesh=uvula.get_editor_property('static_mesh')
assert mesh.get_path_name()=='/Game/Gameplay/Arena/ManualIntake/SM_InteractiveUvula.SM_InteractiveUvula'
assert throat.get_editor_property('use_authored_uvula')
rest=throat.get_editor_property('authored_uvula_rest_transform')
assert abs(rest.translation.z-500.140662)<.01,'Rest placement changed or the lift is already applied; inspect before changing it again'

def xyz(v):return [v.x,v.y,v.z]
def values(t):return xyz(t.translation)+xyz(t.scale3d)+[getattr(t.rotation,k) for k in ('x','y','z','w')]
def same(a,b):return max(abs(x-y) for x,y in zip(values(a),values(b)))<.0001
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

parent=uvula.get_attach_parent()
parent_pose=parent.get_world_transform()
world_rest=u.MathLibrary.transform_location(parent_pose,rest.translation)
assert abs(world_rest.z-600.230667)<.1,'Common throat placement changed; preserve the new placement for inspection'
target_world=world_rest+u.Vector(0,0,500.)
new_translation=u.MathLibrary.inverse_transform_location(parent_pose,target_world)
new_rest=u.Transform()
for field in ('rotation','scale3d'):
    new_rest.set_editor_property(field,rest.get_editor_property(field))
new_rest.set_editor_property('translation',new_translation)
actor_pose=throat.get_actor_transform()
mouth_pose=mouth.get_world_transform()
mouth_mesh=mouth.get_editor_property('skeletal_mesh_asset')
expansion=throat.get_editor_property('manual_authored_expansion')
morph=mouth.get_morph_target('Open')
map_file=ROOT/'Content/Maps/L_Mouth.umap'
before_sha=sha(map_file)
source_files=[ROOT/'Content/Gameplay/Arena/ManualIntake/SM_InteractiveUvula.uasset',
    ROOT/'Content/FromBlender10/SM_MiniTong.uasset',ROOT/'Content/FromBlender4/SK_Exit.uasset']
source_shas={str(p.relative_to(ROOT)):sha(p) for p in source_files}
backup=OUT/'Backups'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
backup.mkdir(parents=True,exist_ok=False)
shutil.copy2(map_file,backup/'L_Mouth.umap')
assert sha(backup/'L_Mouth.umap')==before_sha
report=dict(revision='restore_authored_uvula_height_v1',active_mesh=mesh.get_path_name(),
    source_mesh='/Game/FromBlender10/SM_MiniTong',world_lift_cm=500.,backup=str(backup),
    before_rest=str(rest),after_rest=str(new_rest),neutral_world_before_cm=xyz(world_rest),
    neutral_world_after_cm=xyz(target_world),actor_before=str(actor_pose),mouth_before=str(mouth_pose),
    expansion_before=expansion,original_mesh_package_sha256=source_shas,map_before_sha256=before_sha)
with u.ScopedEditorTransaction('Restore new interactive uvula under palate'):
    throat.modify()
    throat.set_editor_property('authored_uvula_rest_transform',new_rest)
    throat.call_method('RebuildAppearance')
    assert same(throat.get_actor_transform(),actor_pose),'Throat root moved'
    assert same(mouth.get_world_transform(),mouth_pose),'User mouth placement moved'
    assert mouth.get_editor_property('skeletal_mesh_asset')==mouth_mesh
    assert throat.get_editor_property('manual_authored_expansion')==expansion
    assert abs(mouth.get_morph_target('Open')-morph)<.001
    assert uvula.get_editor_property('static_mesh')==mesh
    local_top=u.Vector(0,0,uvula.get_local_bounds()[1].z)
    expected_top=u.MathLibrary.transform_location(parent_pose,u.MathLibrary.transform_location(new_rest,local_top))
    actual_top=u.MathLibrary.transform_location(uvula.get_world_transform(),local_top)
    assert math.dist(xyz(expected_top),xyz(actual_top))<.05,'Animated upper attachment moved from the saved rest pivot'
    assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level(),'Map save failed'
for file in source_files:
    assert sha(file)==source_shas[str(file.relative_to(ROOT))],'An artist mesh package changed'
report.update(status='passed',map_after_sha256=sha(map_file),
    actual_top_world_cm=xyz(actual_top),upper_attachment_error_cm=math.dist(xyz(expected_top),xyz(actual_top)),
    throat_root_preserved=True,mouth_transform_mesh_and_expansion_preserved=True,original_mesh_packages_preserved=True,
    dirty_after=[p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+
        list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())])
(OUT/'HeightApplied.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(status=report['status'],active_mesh=report['active_mesh'],neutral_world_after_cm=report['neutral_world_after_cm'],
    actual_top_world_cm=report['actual_top_world_cm'],mouth_transform_preserved=True,output=str(OUT/'HeightApplied.json')),ensure_ascii=False))
