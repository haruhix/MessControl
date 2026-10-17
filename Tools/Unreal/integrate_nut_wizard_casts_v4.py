"""Integrate five coordinated caster Actions and their closed channel contract.

Only MCStyleV4 animations and DA_NutRain save. The projectile origin moves to
the authored fire release. Existing art, Tank, other Wizard slots and tuning
are verified unchanged. Run in the idle Editor or PythonScript commandlet.
"""
import hashlib
import importlib.util
import json
import math
import re
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
loader = importlib.util.spec_from_file_location('mc_cast_v4_helpers', ROOT / 'Tools/Unreal/integrate_nut_wizard_style_v3.py')
helpers = importlib.util.module_from_spec(loader)
loader.loader.exec_module(helpers)
qa = helpers.qa
SPEC = dict(qa.ROLE_SPECS[1])
SPEC['clips'] = tuple(pair for pair in SPEC['clips'] if pair[1] in ('Idle', 'Cast', 'HeavyCast', 'Summon', 'Rain'))
helpers.SPEC = SPEC
qa.EXPORT_ROOT = ROOT / 'ArtSource/NutAnimationStyle/ExportsV4'
qa.OWNER_TAG, qa.OWNER_VALUE = 'MCWizardCastsV4', 'integrate_nut_wizard_casts_v4.py:1'
DEST = '/Game/Gameplay/CoreLoop/NutWizard/Animations/MCStyleV4'
REPORT = ROOT / 'Saved/Checks/WizardCastsV4Integration.json'
SOURCE_FILENAME = 'NutWizard_Casts_v4.blend'
AUTHOR_REPORT = ROOT / 'ArtSource/NutAnimationStyle/Wizard/WizardCastsV4Report.json'
REVISION_LABEL = 'V4'
GESTURE_CLIPS = {'Cast', 'HeavyCast', 'Summon', 'Rain'}
lib = u.EditorAssetLibrary


def unchanged_settings(boss):
    text = helpers.without_wizard_bindings(boss)
    text, count = re.subn(r'\bMageCastOffset=\([^)]*\)', 'MageCastOffset=(AuthoredRelease)', text)
    qa.require(count == 1, 'Could not isolate the intended cast-origin change')
    return text


def protected_hashes():
    result = {}
    destination = ROOT / 'Content' / DEST.removeprefix('/Game/')
    for directory in ('Gameplay/CoreLoop/NutWizard', 'Gameplay/CoreLoop/NutTank', 'FromBlender8'):
        for path in (ROOT / 'Content' / directory).rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.uasset', '.uexp', '.ubulk') and not path.is_relative_to(destination):
                result[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    for directory in ('Data', 'Gameplay/VFX'):
        for path in (ROOT / 'Content' / directory).rglob('*.uasset'):
            result[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def constant_curve(sequence, name, value):
    u.AnimationLibrary.add_curve(sequence, name, u.RawCurveTrackTypes.RCT_FLOAT, False)
    u.AnimationLibrary.add_float_curve_keys(sequence, name, [0., sequence.get_play_length()], [value, value])
    times, values = u.AnimationLibrary.get_float_keys(sequence, name)
    qa.require(len(times) == 2 and all(abs(float(v) - value) < .0001 for v in values), 'Native gesture curve did not serialize: ' + name)


def palm_actor(context, pose, bone, boss):
    transform = u.AnimPoseExtensions.get_bone_pose(pose, bone, u.AnimPoseSpaces.WORLD)
    point = u.MathLibrary.transform_location(transform, u.Vector(0, 8, 0))
    bounds = context['mesh'].get_bounds()
    center, extent = bounds.get_editor_property('origin'), bounds.get_editor_property('box_extent')
    height = float(boss.get_editor_property('mage_height'))
    scale, yaw = height / (2 * extent.z), math.radians(float(boss.get_editor_property('mage_model_yaw')))
    x, y = (point.x - center.x) * scale, (point.y - center.y) * scale
    return [x * math.cos(yaw) - y * math.sin(yaw), x * math.sin(yaw) + y * math.cos(yaw),
            (point.z - center.z) * scale + height * .5 - max(60., min(260., height * .45))]


def main():
    if '-run=' not in u.SystemLibrary.get_command_line().lower():
        qa.require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before cast import')
    dirty = qa.dirty_packages()
    qa.require(qa.PROFILE not in dirty, 'DA_NutRain has unsaved edits')
    protected = protected_hashes()
    profile = lib.load_asset(qa.PROFILE)
    original = profile.get_editor_property('settings').copy()
    original_boss = original.get_editor_property('boss').copy()
    settings, boss = original.copy(), original_boss.copy()
    baseline = unchanged_settings(original_boss)
    mesh = lib.load_asset('/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard')
    skeleton = mesh.get_editor_property('skeleton')
    qa.require(boss.get_editor_property('mage_skeletal_mesh') == mesh, 'The encounter uses another Wizard')
    qa.require(not {qa.package_path(mesh), qa.package_path(skeleton)}.intersection(dirty), 'Unsaved Wizard mesh or skeleton')
    source, manifest, records = qa.read_export(SPEC)
    qa.require(Path(manifest['source']).name == SOURCE_FILENAME, 'Expected final '+REVISION_LABEL+' source')
    author = json.loads(AUTHOR_REPORT.read_text(encoding='utf8'))
    qa.require(author['passed'] and not author['draft_cast_only'] and author['blend_sha256'] == manifest['source_sha256_before'], 'Expected checked final gesture bake')
    expected = {DEST + '/' + SPEC['prefix'] + suffix for _, suffix in SPEC['clips']}
    qa.require(not expected.intersection(dirty), 'Unsaved v4 animations')
    for path in lib.list_assets(DEST, recursive=True, include_folder=False):
        asset = lib.load_asset(path)
        qa.require(qa.package_path(asset) in expected and qa.owned(asset, DEST), 'Unexpected existing v4 asset')
    reference = qa.reference_snapshot(skeleton)
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_property('optional_skeletal_mesh', mesh)
    options.set_editor_property('evaluation_type', u.AnimDataEvalType.RAW)
    options.set_editor_property('extract_root_motion', False)
    context = dict(role='mage', source=source, manifest=manifest, records=records, destination=DEST,
                   mesh=mesh, skeleton=skeleton, fps=float(manifest['fps']), reference=reference, pose_options=options,
                   legacy_idle=lib.load_asset('/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Idle'), sequences={})
    report = dict(passed=False, clips=[], bindings={}, cast_origin_before_cm=qa.vec(boss.get_editor_property('mage_cast_offset')))
    importer = u.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
    try:
        u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
        for field, suffix in SPEC['clips']:
            sequence, record = qa.import_clip(SPEC, context, suffix)
            if suffix in GESTURE_CLIPS:
                constant_curve(sequence, 'MCSpellGesture', 1.)
            if suffix == 'Rain':
                constant_curve(sequence, 'MCSpellChannelEnd', .85)
                constant_curve(sequence, 'MCSpellChannelPeriod', 1.1)
                start = qa.pose_at(context, sequence, sequence.get_play_length() * .55)
                end = qa.pose_at(context, sequence, sequence.get_play_length() * .85)
                differences = [qa.transform_distance(u.AnimPoseExtensions.get_bone_pose(start, bone, u.AnimPoseSpaces.LOCAL),
                               u.AnimPoseExtensions.get_bone_pose(end, bone, u.AnimPoseSpaces.LOCAL)) for bone in reference['bones']]
                qa.require(all(d['translation_cm'] < .05 and d['rotation_degrees'] < .1 and d['scale'] < .0001 for d in differences), 'Rain channel loop is not closed')
                report['rain_loop'] = dict(start=.55, end=.85, period_seconds=1.1, forward=True, seam_checked_bones=len(differences))
            record['validation'] = qa.validate_clip(context, sequence, suffix)
            report['clips'].append(record)
            context['sequences'][suffix] = sequence
            key = 'mage_' + field + '_animation'
            boss.set_editor_property(key, sequence)
            report['bindings'][key] = sequence.get_path_name()
        fire = context['sequences']['Cast']
        pose = qa.pose_at(context, fire, fire.get_play_length() * float(boss.get_editor_property('mage_cast_release_fraction')))
        left, right = palm_actor(context, pose, 'hand_l', boss), palm_actor(context, pose, 'hand_r', boss)
        # The left hand supports the charge while the right palm launches it.
        focus = right
        qa.require(all(math.isfinite(v) for v in focus) and 20 < focus[0] < float(boss.get_editor_property('mage_height')), 'New release origin is invalid')
        boss.set_editor_property('mage_cast_offset', u.Vector(*focus))
        report['cast_origin_after_cm'] = focus
        report['release_palms_cm'] = dict(left=left, right=right)
        qa.require(unchanged_settings(boss) == baseline, 'Import changed unrelated boss settings or other Wizard bindings')
        qa.require(qa.reference_snapshot(skeleton) == reference, 'Import changed skeleton rest pose')
        settings.set_editor_property('boss', boss)
        check = settings.copy()
        check.set_editor_property('boss', original_boss)
        qa.require(check.export_text() == original.export_text(), 'Import changed other encounter settings')
        allowed = expected | {qa.PROFILE}
        qa.require(not (qa.dirty_packages() - dirty - allowed), 'Import dirtied unrelated packages')
        qa.require(protected_hashes() == protected, 'Import touched existing art or VFX')
        for sequence in context['sequences'].values():
            qa.require(lib.save_loaded_asset(sequence, only_if_is_dirty=False), 'Could not save gesture clip')
        profile.modify()
        profile.set_editor_property('settings', settings)
        saved = profile.get_editor_property('settings').get_editor_property('boss')
        qa.require(unchanged_settings(saved) == baseline, 'Serialized settings changed outside this cast revision')
        for key, path in report['bindings'].items():
            qa.require(saved.get_editor_property(key).get_path_name() == path, 'Cast binding did not serialize')
        qa.require(lib.save_loaded_asset(profile, only_if_is_dirty=False), 'Could not save cast bindings')
        qa.require(protected_hashes() == protected and qa.dirty_packages() == dirty, 'Saving touched unrelated packages')
        report.update(passed=True, saved_packages=sorted(allowed), previous_art_and_vfx_preserved=True,
                      other_boss_settings_and_wizard_slots_preserved=True, root_motion=False,
                      native_contract='Marked gestures keep both palms free and continue across server phases; Rain loops forward .55-.85/1.1 s',
                      scope='Saved assets and imported poses; full fight/network validation remains separate')
        u.log('MC_WIZARD_CASTS_'+REVISION_LABEL+'_INTEGRATION_PASS ' + json.dumps(dict(clips=len(SPEC['clips']), saved_packages=len(allowed))))
    except Exception as error:
        report['error'] = str(error)
        u.log_error('MC_WIZARD_CASTS_'+REVISION_LABEL+'_INTEGRATION_FAIL ' + str(error))
        raise
    finally:
        u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX ' + str(importer))
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(report, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
