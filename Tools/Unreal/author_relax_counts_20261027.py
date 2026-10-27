"""Plan/apply the approved five-item opening and ten-item post-nut Relax.

Default execution is read-only planning. After the matching native changes are
built, run('apply') or explicitly set MC_RELAX_COUNTS_MODE='apply' before exec.
The two existing arrays and the new six-second coffee-to-food delay are changed;
individual food drop timing, key events, boss settings, SmallMint and every other
reflected value are checked unchanged before saving this one package. No build,
map save or PIE is started. The new field must exist in the matching Editor DLL.
All evidence and verified pre-save package backups remain on E:.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import uuid

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
PROFILE = '/Game/Gameplay/CoreLoop/DA_SingleDay'
CHANGES = {'OpeningMealPortions': [5], 'AfterNutMealPortions': [10],
           'AfterNutCoffeeToFoodSeconds': 6.0}
EDITOR_CHANGES = {'opening_meal_portions': [5], 'after_nut_meal_portions': [10],
                  'after_nut_coffee_to_food_seconds': 6.0}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dirty():
    return sorted(package.get_path_name() for package in
        list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
        list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def run(mode='plan', output=None):
    require(mode in ('plan', 'apply'), 'Expected explicit plan or apply mode')
    require(Path(u.Paths.project_dir()).resolve() == ROOT.resolve(), 'Wrong project')
    commandlet = '-run=pythonscript' in u.SystemLibrary.get_command_line().lower()
    level = None if commandlet else u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not (level and level.is_in_play_in_editor()), 'Preserve active PIE')
    require(not dirty(), 'Preserve unsaved editor changes before authoring')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '_' + uuid.uuid4().hex[:8]
    target = Path(output or (
        'E:/CodexWork/Batch6_20261027/Relax/ProfileAuthoring/Runs/' + stamp)).resolve()
    require(target.drive.upper() == 'E:', 'All authoring outputs must stay on E:')
    require(not target.exists() or not any(target.iterdir()), 'Choose a fresh evidence folder')
    target.mkdir(parents=True, exist_ok=True)
    profile = u.load_asset(PROFILE)
    require(isinstance(profile, u.MCSingleDayProfile), 'Missing saved single-day profile')
    schema = json.loads(u.ToolsetLibrary.list_struct_properties(profile.get_class(), True))
    names = list(schema)
    by_lower = {name.casefold(): name for name in names}
    require(all(name.casefold() in by_lower for name in CHANGES),
            'Editor DLL does not expose the required Relax arrays and new coffee-to-food delay: '
            + repr(names))
    changes = {by_lower[name.casefold()]: value for name, value in CHANGES.items()}
    before = json.loads(u.ToolsetLibrary.get_object_properties(profile, names))
    require(all(name in before for name in names), 'Incomplete reflected profile snapshot')
    require(not dirty(), 'Loading the profile produced unsaved changes; preserve them')
    report = {
        'mode': mode, 'profile': PROFILE, 'utc': datetime.now(timezone.utc).isoformat(),
        'planned_changes': changes, 'before': before, 'backups': [], 'saved': False,
        'unrelated_properties_preserved': False,
        'validation': 'Reflected data comparison and targeted save only; no build or gameplay run.',
    }

    def write():
        (target / 'Report.json').write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    write()
    if mode == 'plan':
        u.log('MC_RELAX_COUNTS_PLAN ' + str(target / 'Report.json'))
        return report

    source = ROOT / 'Content/Gameplay/CoreLoop/DA_SingleDay.uasset'
    require(source.is_file(), 'Saved profile package is unavailable for backup')
    for suffix in ('.uasset', '.uexp', '.ubulk'):
        item = source.with_suffix(suffix)
        if item.is_file():
            backup = target / 'Backups' / item.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, backup)
            checksum = digest(item)
            require(checksum == digest(backup), 'Profile backup hash mismatch')
            report['backups'].append({
                'source': str(item), 'backup': str(backup), 'sha256': checksum})
    require(report['backups'], 'No verified profile backup was created')
    write()
    profile.modify()
    profile.set_editor_properties(EDITOR_CHANGES)
    after = json.loads(u.ToolsetLibrary.get_object_properties(profile, names))
    try:
        require(all(after.get(key) == value for key, value in changes.items()),
                'Relax counts and coffee-to-food delay did not apply exactly')
        require({key: value for key, value in before.items() if key not in changes} ==
                {key: value for key, value in after.items() if key not in changes},
                'Unrelated reflected profile properties changed; package was not saved')
        require(all(package == PROFILE for package in dirty()),
                'Unrelated package became dirty; preserve it without saving')
    except Exception as error:
        report.update(after=after, error=str(error),
                      validation='Pre-save comparison failed; no package was saved.')
        write()
        raise
    report.update(after=after, unrelated_properties_preserved=True)
    write()
    require(u.EditorAssetLibrary.save_loaded_asset(profile, False),
            'Could not save the single profile; see its verified backup')
    report.update(saved=True, saved_sha256=digest(source))
    write()
    require(json.loads(u.ToolsetLibrary.get_object_properties(profile, names)) == after,
            'Reflected profile values changed during the targeted save')
    require(not dirty(), 'Unexpected unsaved packages were preserved after profile authoring')
    report['passed'] = True
    write()
    u.log('MC_RELAX_COUNTS_PROFILE_SAVED ' + str(target / 'Report.json'))
    return report


if __name__ == '__main__':
    run(mode=globals().get('MC_RELAX_COUNTS_MODE', 'plan'),
        output=globals().get('MC_RELAX_COUNTS_OUTPUT'))
