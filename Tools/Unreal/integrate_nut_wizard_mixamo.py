"""Bind the three reviewed original performances, preserving other boss slots.

HeavyCast retains the original Rain alias. Only four owned duplicates and
DA_NutRain save. Original animation packages and skeleton remain untouched.
Run in the idle editor with the continuous one-shot Rain native guard loaded.
"""
import hashlib
import json
import math
import re
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
PROFILE = '/Game/Gameplay/CoreLoop/DA_NutRain'
BASE = '/Game/Gameplay/CoreLoop/NutWizard/Animations'
DEST = BASE + '/MCMixamo'
OWNER = 'integrate_nut_wizard_mixamo.py:1'
SLOTS = (('cast', 'Cast'), ('summon', 'Summon'), ('rain', 'Rain'), ('heavy_cast', 'HeavyCast'))
lib = u.EditorAssetLibrary


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def dirty():
    return {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
            + u.EditorLoadingAndSavingUtils.get_dirty_map_packages()}


def unaffected_boss_text(boss):
    text = boss.export_text()
    for slot, _ in SLOTS:
        name = 'Mage' + ''.join(word.title() for word in slot.split('_')) + 'Animation'
        text, count = re.subn(r'\b' + name + r'=(?:"[^"]*"|None)', name + '=None', text)
        require(count == 1, 'Could not isolate binding ' + name)
    text, count = re.subn(r'\bMageCastOffset=\([^)]*\)', 'MageCastOffset=(OriginalRelease)', text)
    require(count == 1, 'Could not isolate cast-origin adjustment')
    return text


def main():
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before integration')
    before_dirty = dirty()
    allowed = {PROFILE} | {DEST + '/A_NutWizard_' + suffix for _, suffix in SLOTS}
    require(not allowed.intersection(before_dirty), 'An integration target has unsaved edits')
    profile = lib.load_asset(PROFILE)
    original = profile.get_editor_property('settings').copy()
    previous_boss = original.get_editor_property('boss').copy()
    boss = previous_boss.copy()
    mesh = lib.load_asset('/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard')
    skeleton = mesh.get_editor_property('skeleton')
    manifest = json.loads((ROOT / 'ArtSource/NutWizard/ExportReport.json').read_text(encoding='utf8'))
    require(manifest['status'] == 'complete', 'Original source export is incomplete')
    source_blend = ROOT / 'ArtSource/NutAnimationStyle/Wizard/Original/NutWizard_Original.blend'
    require(hashlib.sha256(source_blend.read_bytes()).hexdigest() == manifest['source_sha256_before'], 'Original blend differs from the reviewed source')
    protected = {}
    for path in (ROOT / 'Content/Gameplay/CoreLoop/NutWizard').rglob('*.uasset'):
        if 'MCMixamo' not in path.parts:
            protected[path] = hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(passed=False, source=str(source_blend), source_sha256=manifest['source_sha256_before'],
                  blend_seconds=float(boss.get_editor_property('animation_blend_seconds')),
                  previous_bindings={}, bindings={}, clips=[],
                  scope='Three reviewed original source casts; HeavyCast aliases Rain; remaining five Wizard slots retained')
    sequences = []
    try:
        for slot, suffix in SLOTS:
            src = BASE + '/A_NutWizard_' + suffix
            target = DEST + '/A_NutWizard_' + suffix
            sequence = lib.load_asset(src)
            require(sequence.get_editor_property('skeleton') == skeleton, 'Incompatible original skeleton: ' + suffix)
            require(lib.get_metadata_tag(sequence, 'MCNutWizardDerivation') == 'import_nut_wizard.py:1', 'Unverified original: ' + suffix)
            require(not sequence.get_editor_property('enable_root_motion'), 'Unexpected original root motion')
            source_files = list(sequence.get_editor_property('asset_import_data').extract_filenames())
            require(len(source_files) == 1 and Path(source_files[0]).name == 'A_NutWizard_' + suffix + '.fbx', 'Wrong original source')
            if lib.does_asset_exist(target):
                duplicate = lib.load_asset(target)
                require(lib.get_metadata_tag(duplicate, 'MCWizardMixamoIntegration') == OWNER, 'Destination belongs to other work')
            else:
                duplicate = lib.duplicate_asset(src, target)
            require(isinstance(duplicate, u.AnimSequence), 'Failed to duplicate ' + suffix)
            duplicate.modify()
            if u.AnimationLibrary.does_curve_exist(duplicate, 'MCSpellGesture', u.RawCurveTrackTypes.RCT_FLOAT):
                u.AnimationLibrary.remove_curve(duplicate, 'MCSpellGesture', False)
            u.AnimationLibrary.add_curve(duplicate, 'MCSpellGesture', u.RawCurveTrackTypes.RCT_FLOAT, False)
            u.AnimationLibrary.add_float_curve_keys(duplicate, 'MCSpellGesture', [0., duplicate.get_play_length()], [1., 1.])
            for name in ('MCSpellChannelEnd', 'MCSpellChannelPeriod'):
                if u.AnimationLibrary.does_curve_exist(duplicate, name, u.RawCurveTrackTypes.RCT_FLOAT):
                    u.AnimationLibrary.remove_curve(duplicate, name, False)
            lib.set_metadata_tag(duplicate, 'MCWizardMixamoIntegration', OWNER)
            lib.set_metadata_tag(duplicate, 'MCWizardOriginalAsset', src)
            lib.set_metadata_tag(duplicate, 'MCWizardOriginalBlendSHA256', manifest['source_sha256_before'])
            times, values = u.AnimationLibrary.get_float_keys(duplicate, 'MCSpellGesture')
            require(len(times) == 2 and all(abs(float(value) - 1.) < .0001 for value in values), 'Gesture marker failed')
            require(duplicate.get_editor_property('skeleton') == skeleton and abs(duplicate.get_play_length() - sequence.get_play_length()) < .0001, 'Duplicate changed timing or skeleton')
            key = 'mage_' + slot + '_animation'
            report['previous_bindings'][key] = previous_boss.get_editor_property(key).get_path_name()
            boss.set_editor_property(key, duplicate)
            report['bindings'][key] = duplicate.get_path_name()
            report['clips'].append(dict(slot=slot, original=src, seconds=duplicate.get_play_length(),
                                        gesture_marker=1, channel='held release then authored recovery' if suffix == 'Rain' else None))
            sequences.append(duplicate)
        # Gameplay keeps a deterministic muzzle. Match it to the original
        # right-palm release instead of the replaced V6 performance's offset.
        options = u.AnimPoseEvaluationOptions()
        options.set_editor_property('optional_skeletal_mesh', mesh)
        options.set_editor_property('evaluation_type', u.AnimDataEvalType.RAW)
        options.set_editor_property('extract_root_motion', False)
        cast = sequences[0]
        pose = u.AnimPoseExtensions.get_anim_pose_at_time(cast, cast.get_play_length() * float(boss.get_editor_property('mage_cast_release_fraction')), options)
        palm = u.AnimPoseExtensions.get_bone_pose(pose, 'hand_r', u.AnimPoseSpaces.WORLD)
        point = u.MathLibrary.transform_location(palm, u.Vector(0, 8, 0))
        bounds = mesh.get_bounds()
        center, extent = bounds.get_editor_property('origin'), bounds.get_editor_property('box_extent')
        height = float(boss.get_editor_property('mage_height'))
        scale, yaw = height / (2 * extent.z), math.radians(float(boss.get_editor_property('mage_model_yaw')))
        x, y = (point.x - center.x) * scale, (point.y - center.y) * scale
        offset = [x * math.cos(yaw) - y * math.sin(yaw), x * math.sin(yaw) + y * math.cos(yaw),
                  (point.z - center.z) * scale + height * .5 - max(60., min(260., height * .45))]
        require(all(math.isfinite(value) for value in offset) and 20 < offset[0] < height, 'Invalid original release position')
        old_offset = previous_boss.get_editor_property('mage_cast_offset')
        report['cast_offset_before_cm'] = [old_offset.x, old_offset.y, old_offset.z]
        report['cast_offset_after_cm'] = offset
        boss.set_editor_property('mage_cast_offset', u.Vector(*offset))
        require(unaffected_boss_text(boss) == unaffected_boss_text(previous_boss), 'Unrelated boss settings changed')
        settings = original.copy()
        settings.set_editor_property('boss', boss)
        check = settings.copy()
        check.set_editor_property('boss', previous_boss)
        require(check.export_text() == original.export_text(), 'Unrelated encounter settings changed')
        require(not (dirty() - before_dirty - allowed), 'Unexpected dirty package')
        for sequence in sequences:
            require(lib.save_loaded_asset(sequence, only_if_is_dirty=False), 'Could not save original gesture duplicate')
        profile.modify()
        profile.set_editor_property('settings', settings)
        require(lib.save_loaded_asset(profile, only_if_is_dirty=False), 'Could not save spell bindings')
        require(all(hashlib.sha256(path.read_bytes()).hexdigest() == digest for path, digest in protected.items()), 'Original Wizard art changed')
        require(dirty() == before_dirty, 'Unrelated unsaved package state changed')
        report.update(passed=True, saved_packages=sorted(allowed), other_slots_and_tuning_preserved=True,
                      original_art_preserved=True, validation='Saved bindings, source provenance, skeleton, timing and gesture curves; local PIE follows')
        u.log('MC_WIZARD_MIXAMO_INTEGRATION_PASS ' + json.dumps(report['bindings']))
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        output = ROOT / 'Saved/Checks/WizardMixamoIntegration.json'
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
