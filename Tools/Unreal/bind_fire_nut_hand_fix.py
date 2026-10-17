"""Retune only fire-nut speed and bake its emitter from the current cast palm."""
import unreal as u,json,importlib.util
from pathlib import Path
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root=Path(u.Paths.project_dir()).resolve();out=root/'Artifacts/FireNutHandFix_20261017'
before=json.loads((out/'Before.json').read_text())
spec=importlib.util.spec_from_file_location('mc_fire_nut_binding',Path(__file__).with_name('connect_nut_bosses.py'))
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
profile=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain');settings=profile.get_editor_property('Settings').copy();boss=settings.get_editor_property('Boss').copy()
assert boss.export_text()==before['boss'],'Profile changed after inspection'
offset=helper.measure_cast_offset(boss.get_editor_property('MageSkeletalMesh'),boss.get_editor_property('MageCastAnimation'),boss.get_editor_property('MageHeight'),boss.get_editor_property('MageCastReleaseFraction'),boss.get_editor_property('MageModelYaw'))
boss.set_editor_property('MageCastOffset',offset);boss.set_editor_property('FireballSpeed',1100.)
probe=boss.copy();old=boss.copy();old.import_text(before['boss'])
for k in ['MageCastOffset','FireballSpeed']:probe.set_editor_property(k,old.get_editor_property(k))
assert probe.export_text()==old.export_text(),'Unrelated tuning changed'
settings.set_editor_property('Boss',boss);profile.set_editor_property('Settings',settings)
assert u.EditorAssetLibrary.save_loaded_asset(profile)
(out/'Binding.json').write_text(json.dumps(dict(speed=1100.,old_speed=before['speed'],offset=list(offset.to_tuple()),right_hand='hand_r',source=before['clip'],release_fraction=boss.get_editor_property('MageCastReleaseFraction'),only_speed_and_offset_changed=True),indent=2))
print('FIRE_NUT_BOUND',1100.,offset)
