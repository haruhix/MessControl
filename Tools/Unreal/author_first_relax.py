"""Plan/apply only the approved first Relax fields in the saved DA_SingleDay.

Use a matching Editor build. Default run() only reports. Explicit run('apply')
saves the one profile after a verified E: backup and checks all other reflected
designer properties remain identical. No map, Blueprint, table, build or PIE.
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
CHANGES = {'OpeningMealPortions': [3, 5, 8],
           'BetweenMealPauseMinSeconds': 5.0,
           'BetweenMealPauseMaxSeconds': 10.0}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dirty():
    return sorted(p.get_path_name() for p in
        list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
        list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def run(mode='plan', output=None):
    require(mode in ('plan', 'apply'), 'Expected plan or apply')
    require(Path(u.Paths.project_dir()).resolve() == ROOT.resolve(), 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active PIE')
    require(not dirty(), 'Preserve unsaved editor changes')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '_' + uuid.uuid4().hex[:8]
    target = Path(output or ('E:/CodexWork/MessControlFirstRelax_20261020/Authoring/' + stamp)).resolve()
    require(target.drive.upper() == 'E:', 'Authoring outputs must stay on E:')
    require(not (target / 'Report.json').exists(), 'Preserve the existing authoring report')
    target.mkdir(parents=True, exist_ok=True)
    profile = u.load_asset(PROFILE)
    require(isinstance(profile, u.MCSingleDayProfile), 'Missing saved single-day profile')
    schema = json.loads(u.ToolsetLibrary.list_struct_properties(profile.get_class(), True))
    names = list(schema)
    # Schema generation normalizes native names to lower camel case. Resolve
    # those names rather than mistaking case normalization for an old DLL.
    schema_names = {name.casefold(): name for name in names}
    require(all(name.casefold() in schema_names for name in CHANGES),
            'New Relax fields are unavailable in the Editor schema: ' + repr(names))
    changes = {schema_names[name.casefold()]: value for name, value in CHANGES.items()}
    before = json.loads(u.ToolsetLibrary.get_object_properties(profile, names))
    report = {'mode': mode, 'profile': PROFILE, 'utc': datetime.now(timezone.utc).isoformat(),
              'planned_changes': changes, 'before': before, 'backups': [], 'saved': False,
              'unrelated_properties_preserved': False}

    def write():
        (target / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    write()
    if mode == 'plan':
        return report
    source = ROOT / 'Content/Gameplay/CoreLoop/DA_SingleDay.uasset'
    for suffix in ('.uasset', '.uexp', '.ubulk'):
        item = source.with_suffix(suffix)
        if item.is_file():
            backup = target / 'Backups' / item.relative_to(ROOT)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, backup)
            require(digest(item) == digest(backup), 'Profile backup hash mismatch')
            report['backups'].append({'source': str(item), 'backup': str(backup), 'sha256': digest(item)})
    write()
    profile.modify()
    profile.set_editor_properties({'opening_meal_portions': [3, 5, 8],
        'between_meal_pause_min_seconds': 5.0, 'between_meal_pause_max_seconds': 10.0})
    after = json.loads(u.ToolsetLibrary.get_object_properties(profile, names))
    require(all(after.get(key) == value for key, value in changes.items()), 'Relax settings did not apply')
    require({key: value for key, value in before.items() if key not in changes} ==
            {key: value for key, value in after.items() if key not in changes},
            'Unrelated profile properties changed; review without saving')
    report.update(after=after, unrelated_properties_preserved=True)
    write()
    require(u.EditorAssetLibrary.save_loaded_asset(profile, False), 'Could not save the single profile')
    require(not dirty(), 'Unexpected unsaved packages after profile authoring')
    report.update(saved=True, saved_sha256=digest(source))
    write()
    u.log('MC_FIRST_RELAX_PROFILE_SAVED ' + str(target / 'Report.json'))
    return report


if __name__ == '__main__':
    run()
