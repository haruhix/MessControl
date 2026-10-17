"""Update only saved teleport distance; preserve every clip and other tuning."""
import unreal as u,json,hashlib
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Artifacts/WizardBehaviorFix_20261017'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain');settings=profile.get_editor_property('settings').copy();boss=settings.get_editor_property('boss').copy()
old=boss.copy();boss.set_editor_properties(dict(MageTeleportMinDistance=1200.,MageTeleportMaxDistance=2000.))
probe=boss.copy()
for key in ['MageTeleportMinDistance','MageTeleportMaxDistance']:probe.set_editor_property(key,old.get_editor_property(key))
assert probe.export_text()==old.export_text()
settings.set_editor_property('boss',boss);profile.set_editor_property('settings',settings)
assert u.EditorAssetLibrary.save_loaded_asset(profile)
before=json.loads((out/'Before.json').read_text())
assert all(hashlib.sha256((root/p).read_bytes()).hexdigest()==sha for p,sha in before['hashes'].items())
(out/'Bindings.json').write_text(json.dumps(dict(passed=True,boss=boss.export_text(),walking=boss.get_editor_property('MageWalkAnimation').get_path_name(),teleport_min=1200,teleport_max=2000,existing_wizard_assets_unchanged=True),indent=2))
print('WIZARD_BEHAVIOR_BOUND',1200,2000,'all animations preserved')
