"""Import Walk/Melee/Hit cartoon poses into the existing Tank skeleton.

Run in the idle Editor after the native TankHitAnimation slot is compiled.
Only three new animations and DA_NutRain are saved; all other tuning and
Wizard bindings are retained. Saved/Checks stores the previous bindings.
"""
import importlib.util
import json
import re
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
loader = importlib.util.spec_from_file_location('mc_tank_cartoon_helpers', ROOT / 'Tools/Unreal/integrate_nut_style_v2.py')
qa = importlib.util.module_from_spec(loader)
loader.loader.exec_module(qa)
qa.EXPORT_ROOT = ROOT / 'ArtSource/NutAnimationStyle'
qa.OWNER_TAG, qa.OWNER_VALUE = 'MCTankCartoonV3', 'integrate_nut_tank_cartoon_v3.py:1'
qa.CONTACT_CLIPS['tank'].add('Hit')
SPEC = dict(qa.ROLE_SPECS[0], source='TankCartoonV3', clips=(('walk', 'Walk'), ('melee', 'Melee'), ('hit', 'Hit')))
DEST = '/Game/Gameplay/CoreLoop/NutTank/Animations/MCStyleV3'
REPORT = ROOT / 'Saved/Checks/TankCartoonV3Integration.json'
MARK_AUTHORED = True
lib = u.EditorAssetLibrary


def unchanged_settings(boss):
    text = boss.export_text()
    for field, _ in SPEC['clips']:
        name = 'Tank' + ''.join(part.title() for part in field.split('_')) + 'Animation'
        text, count = re.subn(r'\b' + name + r'=(?:"[^"]*"|None)', name + '=None', text)
        qa.require(count == 1, 'Cannot isolate Tank binding: ' + name)
    return text


def main():
    qa.require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before import')
    dirty = qa.dirty_packages()
    qa.require(qa.PROFILE not in dirty, 'DA_NutRain has unsaved edits')
    profile = lib.load_asset(qa.PROFILE)
    original = profile.get_editor_property('settings').copy()
    old_boss = original.get_editor_property('boss').copy()
    settings, boss = original.copy(), old_boss.copy()
    baseline = unchanged_settings(old_boss)
    mesh = lib.load_asset('/Game/Gameplay/CoreLoop/NutTank/SK_NutTank')
    skeleton = mesh.get_editor_property('skeleton')
    qa.require(boss.get_editor_property('tank_skeletal_mesh') == mesh, 'Encounter uses another Tank mesh')
    qa.require(not {qa.package_path(mesh), qa.package_path(skeleton)}.intersection(dirty), 'Unsaved Tank mesh/skeleton')
    source, manifest, records = qa.read_export(SPEC)
    expected = {DEST + '/' + SPEC['prefix'] + suffix for _, suffix in SPEC['clips']}
    qa.require(not expected.intersection(dirty), 'Unsaved cartoon clips')
    for path in lib.list_assets(DEST, recursive=True, include_folder=False):
        qa.require(qa.package_path(lib.load_asset(path)) in expected and qa.owned(lib.load_asset(path), DEST), 'Unexpected existing asset: ' + path)
    reference = qa.reference_snapshot(skeleton)
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_property('optional_skeletal_mesh', mesh)
    options.set_editor_property('evaluation_type', u.AnimDataEvalType.RAW)
    options.set_editor_property('extract_root_motion', False)
    context = dict(role='tank', source=source, manifest=manifest, records=records, destination=DEST,
                   mesh=mesh, skeleton=skeleton, fps=float(manifest['fps']), reference=reference, pose_options=options,
                   legacy_idle=lib.load_asset('/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Idle'), sequences={})
    report = dict(passed=False, clips=[], bindings={}, previous_bindings={})
    importer = u.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
    try:
        u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
        for field, suffix in SPEC['clips']:
            key = 'tank_' + field + '_animation'
            previous = boss.get_editor_property(key)
            report['previous_bindings'][key] = previous.get_path_name() if previous else None
            sequence, record = qa.import_clip(SPEC, context, suffix)
            if MARK_AUTHORED:
                u.AnimationLibrary.add_curve(sequence, 'MCAuthoredCartoon', u.RawCurveTrackTypes.RCT_FLOAT, False)
                u.AnimationLibrary.add_float_curve_keys(sequence, 'MCAuthoredCartoon', [0., sequence.get_play_length()], [1., 1.])
                _, values = u.AnimationLibrary.get_float_keys(sequence, 'MCAuthoredCartoon')
                qa.require(len(values) == 2 and all(abs(float(v) - 1.) < .0001 for v in values), 'Cartoon curve missing')
            record['validation'] = qa.validate_clip(context, sequence, suffix)
            report['clips'].append(record)
            context['sequences'][suffix] = sequence
            boss.set_editor_property(key, sequence)
            report['bindings'][key] = sequence.get_path_name()
        qa.require(unchanged_settings(boss) == baseline, 'Unrelated boss tuning changed')
        qa.require(qa.reference_snapshot(skeleton) == reference, 'Skeleton reference pose changed')
        settings.set_editor_property('boss', boss)
        check = settings.copy()
        check.set_editor_property('boss', old_boss)
        qa.require(check.export_text() == original.export_text(), 'Other encounter settings changed')
        allowed = expected | {qa.PROFILE}
        qa.require(not (qa.dirty_packages() - dirty - allowed), 'Importer dirtied unrelated packages')
        for sequence in context['sequences'].values():
            qa.require(lib.save_loaded_asset(sequence, only_if_is_dirty=False), 'Cannot save cartoon clip')
        profile.modify()
        profile.set_editor_property('settings', settings)
        qa.require(lib.save_loaded_asset(profile, only_if_is_dirty=False), 'Cannot save Tank bindings')
        qa.require(qa.dirty_packages() == dirty, 'Unexpected dirty packages after save')
        report.update(passed=True, saved_packages=sorted(allowed), other_settings_preserved=True,
                      root_motion=False, scope='Imported Tank clips and saved bindings; broad fight/network testing deferred')
        u.log('MC_TANK_CARTOON_V3_PASS')
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX ' + str(importer))
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(report, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
