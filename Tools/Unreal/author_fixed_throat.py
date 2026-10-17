"""Change only the original SK_Exit expansion; preserve the user's placement.

Default is a read-only plan. Apply with runpy init_globals:
{'MC_FIXED_THROAT_APPLY': 'fixed_throat_parameter_v2',
 'MC_FIXED_THROAT_EXPANSION': 0.4}. Without an explicit expansion it keeps the
current setting. No component transform, mesh import, derived asset,
collision authoring or original asset save is performed. Only L_Mouth is saved.
"""
import hashlib
import json
import math
import shutil
from datetime import datetime, timezone
from pathlib import Path

import unreal as u

REVISION = 'fixed_throat_parameter_v2'
APPLY = globals().get('MC_FIXED_THROAT_APPLY') == REVISION
ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT/'Saved/ManualIntake/FixedThroat'
OUT.mkdir(parents=True, exist_ok=True)
profile = json.loads((ROOT/'Tools/Unreal/manual_intake_aperture.json').read_text())
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
lib = u.EditorAssetLibrary
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name() == '/Game/Maps/L_Mouth.L_Mouth'
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Preserve active gameplay'
throats = [a for a in actors.get_all_level_actors() if isinstance(a,u.MCThroat)]
assert len(throats) == 1
throat = throats[0]
EXPANSION = float(globals().get('MC_FIXED_THROAT_EXPANSION',throat.get_editor_property('manual_authored_expansion')))
assert .05 <= EXPANSION <= 1., 'Expansion must be between .05 and 1'
mouth = throat.get_editor_property('authored_mouth')
source = mouth.get_skeletal_mesh_asset()
assert source and source.get_path_name() == '/Game/FromBlender4/SK_Exit.SK_Exit', 'Use original artist mesh only'
assert hashlib.sha256((ROOT/'Content/FromBlender4/SK_Exit.uasset').read_bytes()).hexdigest() == profile['source_saved_package_sha256'], 'Artist mesh changed: remeasure its existing morph before authoring'
assert not [a for a in actors.get_all_level_actors() if a.actor_has_tag('MCCompactThroatCollision')], 'Abandoned derivative must not be integrated'
rest = mouth.get_world_transform()
uvula = throat.get_editor_property('uvula')
uvula_rest = uvula.get_world_transform()
dirty = [p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
         +list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())]
preserve = [source,source.get_editor_property('skeleton'),uvula.static_mesh]
preserve += [s.get_editor_property('material_interface') for s in source.get_editor_property('materials')]
files = {a.get_path_name():ROOT/'Content'/(a.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
         for a in preserve if a and a.get_path_name().startswith('/Game/')}
shas = {name:hashlib.sha256(file.read_bytes()).hexdigest() for name,file in files.items()}
report = dict(revision=REVISION,apply=APPLY,mesh=source.get_path_name(),dirty_before=dirty,
    existing_transform=str(rest),fixed_expansion=EXPANSION,user_placement_preserved=True,
    preserved_package_sha256=shas,
    requirements='Existing Open morph only; preserve user component placement. No model, import or collider derivative.')


def xyz(value):
    return [value.x,value.y,value.z]


def same_rotation(a,b):
    return abs(sum(getattr(a,k)*getattr(b,k) for k in ('x','y','z','w'))) > .999999


def same_transform(a,b):
    return (math.dist(xyz(a.translation),xyz(b.translation)) < .01
            and math.dist(xyz(a.scale3d),xyz(b.scale3d)) < .0001 and same_rotation(a.rotation,b.rotation))


def transform_values(value):
    return xyz(value.translation)+xyz(value.scale3d)+[getattr(value.rotation,k) for k in ('x','y','z','w')]


uvula_authored_values = transform_values(throat.get_editor_property('authored_uvula_rest_transform'))


if not APPLY:
    (OUT/'AuthoringPlan.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    u.log('MC_FIXED_THROAT_PLAN '+str(OUT/'AuthoringPlan.json'))
else:
    assert not dirty, 'Review/save unrelated editor changes before applying'
    throat.get_editor_property('manual_authored_expansion')  # Require rebuilt DLL.
    map_file = ROOT/'Content/Maps/L_Mouth.umap'
    backup = OUT/'Backups'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup.mkdir(parents=True,exist_ok=False)
    shutil.copy2(map_file,backup/'L_Mouth.umap')
    throat.set_editor_property('manual_authored_expansion',EXPANSION)
    throat.call_method('RebuildAppearance')
    # RebuildPresentation evaluates the existing idle sway at the current
    # editor clock. Its animated bulb transform may legitimately change; the
    # authored rest transform and the upper attachment pivot must not move.
    current_uvula_rest = throat.get_editor_property('authored_uvula_rest_transform')
    assert max(abs(a-b) for a,b in zip(transform_values(current_uvula_rest),uvula_authored_values)) < .0001, 'Artist uvula rest placement changed'
    local_max = uvula.get_local_bounds()[1]
    pivot = u.Vector(0,0,local_max.z)
    expected_pivot = u.MathLibrary.transform_location(uvula.get_attach_parent().get_world_transform(),
        u.MathLibrary.transform_location(current_uvula_rest,pivot))
    actual_pivot = u.MathLibrary.transform_location(uvula.get_world_transform(),pivot)
    assert math.dist(xyz(expected_pivot),xyz(actual_pivot)) < .05, 'Artist uvula upper attachment moved'
    assert same_transform(mouth.get_world_transform(),rest), 'User mouth placement changed'
    assert mouth.get_skeletal_mesh_asset() == source and throat.open_amount() >= .99
    morph = 1-EXPANSION if throat.get_editor_property('reverse_authored_open') else EXPANSION
    assert abs(mouth.get_morph_target('Open')-morph) < .001, 'Visible opening did not use fixed expansion'
    a,b = profile['basis_area_center'],profile['open_area_center']
    center = u.Vector(*(x*(1-morph)+y*morph for x,y in zip(a,b)))
    expected = u.MathLibrary.transform_location(mouth.get_world_transform(),center)
    actual = throat.get_intake_aim_target()
    vectors = [x for x in actual if isinstance(x,u.Vector)]
    assert vectors and math.dist([vectors[0].x,vectors[0].y,vectors[0].z],[expected.x,expected.y,expected.z]) < .1
    report.update(actual_morph_weight=morph,new_transform=str(mouth.get_world_transform()),
        native_aim_center_world_cm=[expected.x,expected.y,expected.z],uvula_rest_transform_preserved=True,
        uvula_attachment_error_cm=math.dist(xyz(expected_pivot),xyz(actual_pivot)),
        uvula_world_before=str(uvula_rest),uvula_world_after=str(uvula.get_world_transform()),
        logical_open_amount=throat.open_amount(),backup=str(backup))
    assert levels.save_current_level()
    for name,file in files.items():
        assert hashlib.sha256(file.read_bytes()).hexdigest() == shas[name], 'Source package changed '+name
    report.update(status='passed',original_packages_unchanged=True)
    (OUT/'AuthoringApplied.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    u.log('MC_FIXED_THROAT_APPLIED '+str(OUT/'AuthoringApplied.json'))
