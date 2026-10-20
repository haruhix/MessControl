"""Author the post-nut Relax and explicit Jellies slot in DA_SingleDay.

Default is read-only planning. Apply requires a matching Editor, no PIE/dirty
packages, a verified E: backup, and preserves all unrelated designer values.
For the old standard saved Nut / Nut-Ice shapes, materialize the Ice/Fog tail
that Initialize previously supplied only in its runtime copy, before inserting
Jellies. The exact Nut-Jellies shape from the first authoring is repaired too.
Custom sequences are never extended with a guessed compatibility tail.
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
    commandlet = '-run=pythonscript' in u.SystemLibrary.get_command_line().lower()
    level = None if commandlet else u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not (level and level.is_in_play_in_editor()), 'Preserve active PIE')
    require(not dirty(), 'Preserve unsaved editor changes')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '_' + uuid.uuid4().hex[:8]
    target = Path(output or ('E:/CodexWork/MessControlNextRelax_20261020/Authoring/' + stamp)).resolve()
    require(target.drive.upper() == 'E:', 'All authoring outputs must stay on E:')
    require(not (target / 'Report.json').exists(), 'Preserve the existing report')
    target.mkdir(parents=True, exist_ok=True)
    profile = u.load_asset(PROFILE)
    require(isinstance(profile, u.MCSingleDayProfile), 'Missing single-day profile')
    schema = json.loads(u.ToolsetLibrary.list_struct_properties(profile.get_class(), True))
    names = list(schema)
    by_lower = {name.casefold(): name for name in names}
    changed = {'keyevents', 'afternutmealportions', 'afternutmealdropseconds', 'smallmint'}
    require(changed.issubset(by_lower), 'Editor DLL does not expose the new Relax schema')
    before = json.loads(u.ToolsetLibrary.get_object_properties(profile, names))
    events = list(profile.get_editor_property('key_events'))
    nuts = [index for index, event in enumerate(events)
            if event.get_editor_property('kind') == u.MCSingleDayKeyEventKind.NUT_ENCOUNTER]
    require(nuts, 'Preserve custom profiles without a nut encounter')
    nut_index = nuts[0]
    jelly_indices = [index for index, event in enumerate(events)
                     if event.get_editor_property('kind') == u.MCSingleDayKeyEventKind.JELLIES]
    require(not jelly_indices or jelly_indices == [nut_index + 1],
            'A separately authored Jellies slot exists; do not reorder custom content')
    # Match only the prior standard compatibility shapes. Inserting Jellies
    # first would stop Initialize recognizing them, silently dropping Ice/Fog.
    # Track every original row so its identity, variants and tuning can be
    # verified unchanged even when new rows move its saved array index.
    original_kinds = [event.get_editor_property('kind') for event in events]
    origins = list(range(len(events)))
    nut, ice, fog, jellies = (u.MCSingleDayKeyEventKind.NUT_ENCOUNTER,
        u.MCSingleDayKeyEventKind.ICE_EVENT, u.MCSingleDayKeyEventKind.FOG_BRAWL,
        u.MCSingleDayKeyEventKind.JELLIES)
    compatibility_tail = []
    if not profile.get_editor_property('legacy_timed_finale'):
        if original_kinds in ([nut], [nut, jellies]):
            compatibility_tail = [('IceEvent', ice), ('FogBrawl', fog)]
        elif original_kinds == [nut, ice]:
            compatibility_tail = [('FogBrawl', fog)]
    for event_id, kind in compatibility_tail:
        slot = u.MCSingleDayKeyEvent()
        # These are exactly AddDefaulted + EventId/Kind in Initialize, leaving
        # XP/support/variants at the reflected struct's ordinary defaults.
        slot.set_editor_properties({'event_id': event_id, 'kind': kind})
        events.append(slot)
        origins.append(None)
    if not jelly_indices:
        jelly = u.MCSingleDayKeyEvent()
        jelly.set_editor_properties({'event_id': 'Jellies', 'kind': jellies,
                                     'completion_experience': 25, 'director_support_seconds': 120.0})
        events.insert(nut_index + 1, jelly)
        origins.insert(nut_index + 1, None)
    report = {'mode': mode, 'profile': PROFILE, 'before': before, 'backups': [],
              'planned_portions': [5, 8, 9], 'insert_jellies_after': nut_index,
              'already_authored': bool(jelly_indices), 'saved': False,
              'materialized_compatibility_tail': [
                  {'event_id': event_id, 'kind': str(kind),
                   'saved_index': len(original_kinds) + int(not jelly_indices) + offset,
                   'source': 'AMCSingleDayDirector::Initialize runtime compatibility'}
                  for offset, (event_id, kind) in enumerate(compatibility_tail)],
              'planned_keyevent_kinds': [str(event.get_editor_property('kind')) for event in events],
              'existing_keyevent_index_map': [
                  {'before': origin, 'after': index}
                  for index, origin in enumerate(origins) if origin is not None]}

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
    require(report['backups'], 'No saved profile backup created')
    write()
    mint = u.MCSmallMintCandySettings()
    mint.set_editor_properties({'health_per_player': 180.0, 'diameter': 135.0,
        'fall_seconds': 1.4, 'fall_height': 850.0,
        'first_wave_min_seconds': 3.0, 'first_wave_max_seconds': 5.0,
        'wave_rest_min_seconds': 3.0, 'wave_rest_max_seconds': 5.0,
        'wave_radius': 2600.0, 'wave_travel_seconds': 2.8, 'foot_ice_health': 80.0})
    profile.modify()
    profile.set_editor_properties({'key_events': events, 'after_nut_meal_portions': [5, 8, 9],
                                  'after_nut_meal_drop_seconds': 1.0, 'small_mint': mint})
    after = json.loads(u.ToolsetLibrary.get_object_properties(profile, names))
    unchanged = [name for name in names if name.casefold() not in changed]
    require({name: before.get(name) for name in unchanged} ==
            {name: after.get(name) for name in unchanged}, 'Unrelated designer properties changed; review before saving')
    key = by_lower['keyevents']
    old_slots = before[key]
    new_slots = after[key]
    require(isinstance(old_slots, list) and isinstance(new_slots, list), 'Cannot verify serialized key-event rows')
    require(len(new_slots) == len(origins), 'Unexpected key-event row count after authoring')
    stripped = [slot for slot, origin in zip(new_slots, origins) if origin is not None]
    require(stripped == old_slots, 'Existing key-event identities or tuning changed')
    for slot in report['materialized_compatibility_tail']:
        slot['properties'] = new_slots[slot['saved_index']]
    require(after[by_lower['afternutmealportions']] == [5, 8, 9], 'Portion authoring did not apply')
    report.update(after=after, unrelated_properties_preserved=True, existing_keyevents_preserved=True)
    write()
    require(u.EditorAssetLibrary.save_loaded_asset(profile, False), 'Could not save the single profile')
    require(not dirty(), 'Unexpected unsaved packages after authoring')
    report.update(saved=True, saved_sha256=digest(source))
    write()
    u.log('MC_NEXT_RELAX_PROFILE_SAVED ' + str(target / 'Report.json'))
    return report


if __name__ == '__main__':
    run()
