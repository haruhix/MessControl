"""Connect nine expressive Wizard clips; preserve Tank, equipment and encounter tuning.

Run with the idle Editor or PythonScript commandlet. MCStyleV2 and all original
meshes/skeletons remain available. Only nine MCStyleV3 clips and DA_NutRain save.
"""
import hashlib
import difflib
import importlib.util
import json
import re
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
MODULE = ROOT / 'Tools/Unreal/integrate_nut_style_v2.py'
loader = importlib.util.spec_from_file_location('mc_wizard_v3_import_helpers', MODULE)
qa = importlib.util.module_from_spec(loader)
loader.loader.exec_module(qa)
qa.EXPORT_ROOT = ROOT / 'ArtSource/NutAnimationStyle/ExportsV3'
qa.OWNER_TAG = 'MCWizardStyleV3Integration'
qa.OWNER_VALUE = 'integrate_nut_wizard_style_v3.py:1'
SPEC = qa.ROLE_SPECS[1]
qa.BINDING_FIELDS = tuple('mage_' + field + '_animation' for field, _ in SPEC['clips'])
DEST = '/Game/Gameplay/CoreLoop/NutWizard/Animations/MCStyleV3'
REPORT = ROOT / 'Saved/Checks/WizardStyleV3Integration.json'
lib = u.EditorAssetLibrary


def without_wizard_bindings(boss):
    # UE's None setter can retain an unloaded soft-reference path. Compare the
    # full serialized struct while masking only the nine intended bindings.
    text = boss.export_text()
    for field, _ in SPEC['clips']:
        name = 'Mage' + ''.join(part.title() for part in field.split('_')) + 'Animation'
        text, count = re.subn(r'\b' + name + r'=(?:"[^"]*"|None)', name + '=None', text)
        qa.require(count == 1, 'Could not isolate serialized Wizard binding: ' + name)
    return text


def preserved_art_hashes():
    content = ROOT / 'Content'
    result = {}
    for directory in ('Gameplay/CoreLoop/NutTank', 'Gameplay/CoreLoop/NutWizard', 'FromBlender8'):
        for file in (content / directory).rglob('*'):
            if file.is_file() and file.suffix.lower() in ('.uasset', '.uexp', '.ubulk') and 'MCStyleV3' not in file.parts:
                result[str(file)] = hashlib.sha256(file.read_bytes()).hexdigest()
    for relative in ('Data/DA_Equipment.uasset', 'Maps/L_Mouth.umap',
                     'Gameplay/VFX/NS_WatergunMist.uasset', 'Gameplay/VFX/Spray/M_WatergunDroplets.uasset'):
        file = content / relative
        if file.is_file():
            result[str(file)] = hashlib.sha256(file.read_bytes()).hexdigest()
    return result


def main():
    if '-run=' not in u.SystemLibrary.get_command_line().lower():
        qa.require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before Wizard import')
    before_dirty = qa.dirty_packages()
    qa.require(qa.PROFILE not in before_dirty, 'Keep unsaved DA_NutRain edits before changing Wizard bindings')
    profile = lib.load_asset(qa.PROFILE)
    qa.require(isinstance(profile, u.MCNutRainProfile), 'DA_NutRain is missing')
    original_settings = profile.get_editor_property('settings').copy()
    original_boss = original_settings.get_editor_property('boss').copy()
    settings, boss = original_settings.copy(), original_boss.copy()
    baseline = without_wizard_bindings(original_boss)
    protected = preserved_art_hashes()
    base = '/Game/Gameplay/CoreLoop/NutWizard'
    mesh = lib.load_asset(base + '/SK_NutWizard')
    qa.require(isinstance(mesh, u.SkeletalMesh) and boss.get_editor_property('mage_skeletal_mesh') == mesh,
               'The saved encounter uses another Wizard mesh')
    skeleton = mesh.get_editor_property('skeleton')
    qa.require(not {qa.package_path(mesh), qa.package_path(skeleton)}.intersection(before_dirty), 'Wizard mesh/skeleton has unsaved edits')
    source, manifest, records = qa.read_export(SPEC)
    qa.require(Path(manifest['source']).name == 'NutWizard_Style_v3.blend', 'The export is not the expressive Wizard revision')
    fps = float(manifest['fps'])
    expected = {DEST + '/' + SPEC['prefix'] + suffix for _, suffix in SPEC['clips']}
    qa.require(not expected.intersection(before_dirty), 'New Wizard clips have unsaved edits')
    for path in lib.list_assets(DEST, recursive=True, include_folder=False):
        asset = lib.load_asset(path)
        qa.require(isinstance(asset, u.AnimSequence) and qa.owned(asset, DEST) and qa.package_path(asset) in expected,
                   'Unexpected asset in the new Wizard folder: ' + path)
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_property('optional_skeletal_mesh', mesh)
    options.set_editor_property('extract_root_motion', False)
    options.set_editor_property('evaluation_type', u.AnimDataEvalType.RAW)
    reference = qa.reference_snapshot(skeleton)
    context = dict(role='mage', source=source, manifest=manifest, records=records, destination=DEST,
                   mesh=mesh, skeleton=skeleton, fps=fps, reference=reference, pose_options=options,
                   legacy_idle=lib.load_asset(base + '/Animations/A_NutWizard_Idle'), sequences={})
    report = dict(passed=False, clips=[], bindings={}, cast_anchor={}, preserved_unrelated_dirty_packages=sorted(before_dirty))
    old_importer = u.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
    try:
        u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
        for field, suffix in SPEC['clips']:
            sequence, record = qa.import_clip(SPEC, context, suffix)
            record['validation'] = qa.validate_clip(context, sequence, suffix)
            report['clips'].append(record)
            context['sequences'][suffix] = sequence
            key = 'mage_' + field + '_animation'
            boss.set_editor_property(key, sequence)
            report['bindings'][key] = sequence.get_path_name()
        for suffix in ('Cast', 'Summon', 'Rain'):
            anchor = qa.measure_cast_anchor(context, context['sequences'][suffix], boss)
            qa.require(anchor['distance_to_emitter_cm'] < 12., 'Wizard release palm misses preserved emitter: ' + suffix)
            report['cast_anchor'][suffix] = anchor
        qa.require(qa.reference_snapshot(skeleton) == reference, 'Import changed the Wizard skeleton reference pose')
        after_tuning = without_wizard_bindings(boss)
        if after_tuning != baseline:
            report['tuning_comparison_difference'] = list(difflib.unified_diff(baseline.split(','), after_tuning.split(','), lineterm=''))
        qa.require(after_tuning == baseline, 'Wizard binding changed Tank or other boss tuning')
        settings.set_editor_property('boss', boss)
        check_settings = settings.copy()
        check_settings.set_editor_property('boss', original_boss)
        qa.require(check_settings.export_text() == original_settings.export_text(), 'Encounter settings changed outside Wizard bindings')
        allowed = expected | {qa.PROFILE}
        qa.require(not (qa.dirty_packages() - before_dirty - allowed), 'Import dirtied an unrelated package')
        qa.require(preserved_art_hashes() == protected, 'Import changed previous art/equipment/map packages')
        for sequence in context['sequences'].values():
            qa.require(lib.save_loaded_asset(sequence, only_if_is_dirty=False), 'Could not save Wizard clip')
        profile.modify()
        profile.set_editor_property('settings', settings)
        checked = profile.get_editor_property('settings').get_editor_property('boss')
        for key, path in report['bindings'].items():
            qa.require(checked.get_editor_property(key).get_path_name() == path, 'Wizard binding did not serialize: ' + key)
        qa.require(without_wizard_bindings(checked) == baseline, 'Profile serialization changed other boss settings')
        qa.require(lib.save_loaded_asset(profile, only_if_is_dirty=False), 'Could not save Wizard bindings')
        qa.require(preserved_art_hashes() == protected, 'Save changed existing art/equipment/map packages')
        qa.require(qa.dirty_packages() == before_dirty, 'Import changed unrelated unsaved package state')
        report.update(passed=True, saved_packages=sorted(allowed), original_art_preserved=True,
                      tank_and_equipment_preserved=True, encounter_tuning_preserved=True,
                      skeleton_reference_pose_preserved=True, root_motion=False,
                      teleport='HeavyCast; no projectile palm alignment',
                      scope='Saved packages and imported base poses; full gameplay/network checks are separate')
        u.log('MC_WIZARD_STYLE_V3_INTEGRATION_PASS ' + json.dumps({'clips': 9, 'saved_packages': 10}))
    except Exception as error:
        report['error'] = str(error)
        u.log_error('MC_WIZARD_STYLE_V3_INTEGRATION_FAIL ' + str(error))
        raise
    finally:
        u.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX ' + str(old_importer))
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(report, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
