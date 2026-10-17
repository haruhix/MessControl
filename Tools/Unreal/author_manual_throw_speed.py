"""Save the agreed real player throw-speed default: 1200 -> 1400 cm/s.

Read-only unless MC_MANUAL_THROW_APPLY='manual_throw_1400_v1'. Changes only
BP_PlayerCharacter's FoodCollection default, compiles it, then saves that BP.
"""
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

import unreal as u

REVISION = 'manual_throw_1400_v1'
APPLY = globals().get('MC_MANUAL_THROW_APPLY') == REVISION
PACKAGE = '/Game/Blueprints/BP_PlayerCharacter'
ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT/'Saved/ManualIntake/ThrowSpeed'
OUT.mkdir(parents=True,exist_ok=True)
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Preserve active gameplay'


def dirty():
    return {p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
            +list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}


def preserved_settings(component):
    return {key:component.get_editor_property(key) for key in
            ('max_pieces','collection_reach','throw_interval','spill_contact_impulse')}


assert not dirty(), 'Review/save unrelated editor changes first'
blueprint = u.load_asset(PACKAGE)
assert isinstance(blueprint,u.Blueprint), PACKAGE
defaults = u.get_default_object(blueprint.generated_class())
component = defaults.get_editor_property('food_collection')
assert isinstance(component,u.MCFoodCollectionComponent)
assert component.get_outer() == defaults, 'Do not modify a shared native CDO component'
before = float(component.get_editor_property('throw_speed'))
assert abs(before-1200.) < .001 or abs(before-1400.) < .001, 'Preserve other designer tuning'
unchanged = preserved_settings(component)
native = u.get_default_object(u.MCToothCharacter).get_editor_property('food_collection')
native_before = float(native.get_editor_property('throw_speed'))
file = ROOT/'Content/Blueprints/BP_PlayerCharacter.uasset'
before_sha = hashlib.sha256(file.read_bytes()).hexdigest()
report = dict(revision=REVISION,apply=APPLY,blueprint=PACKAGE,component=component.get_path_name(),
    before_speed_cm_s=before,target_speed_cm_s=1400.,native_speed_cm_s=native_before,
    preserved_collection_settings=unchanged,before_sha256=before_sha)
if not APPLY:
    (OUT/'AuthoringPlan.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    u.log('MC_MANUAL_THROW_PLAN '+str(OUT/'AuthoringPlan.json'))
else:
    backup = OUT/'Backups'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup.mkdir(parents=True,exist_ok=False)
    shutil.copy2(file,backup/file.name)
    blueprint.modify()
    defaults.modify()
    component.modify()
    component.set_editor_property('throw_speed',1400.)
    assert u.BlueprintEditorLibrary.compile_blueprint(blueprint), 'Player Blueprint compile failed'
    # A native constructor supplies only the initial default. Reacquiring the
    # compiled Blueprint CDO verifies its serialized subobject override survived.
    actual = u.get_default_object(blueprint.generated_class()).get_editor_property('food_collection')
    assert abs(float(actual.get_editor_property('throw_speed'))-1400.) < .001, 'Compile discarded the component default override'
    assert preserved_settings(actual) == unchanged, 'Other collection tuning changed'
    assert abs(float(native.get_editor_property('throw_speed'))-native_before) < .001, 'Native CDO was modified'
    assert dirty() <= {PACKAGE}, 'Compilation dirtied an unrelated package'
    assert u.EditorAssetLibrary.save_loaded_asset(blueprint,only_if_is_dirty=False)
    assert not dirty(), 'Unexpected dirty packages remain after the one Blueprint save'
    report.update(status='passed',backup=str(backup),after_sha256=hashlib.sha256(file.read_bytes()).hexdigest(),
        after_compile_speed_cm_s=float(actual.get_editor_property('throw_speed')),
        preserved_other_settings=True,native_default_unchanged=True,
        validation='Compiled CDO and saved Blueprint only; actual spawned player/Q physics are checked by the solo probe.')
    (OUT/'AuthoringApplied.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    u.log('MC_MANUAL_THROW_APPLIED '+str(OUT/'AuthoringApplied.json'))
