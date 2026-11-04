"""Guarded UI copy fix for the current Raft controls; no gameplay changes.

Idle, clean Editor: run(apply=True).
ExecutePythonScript options: MC_RAFT_INSTRUCTION_OPTIONS = {"apply": True}.
Default run() writes a plan only. It changes only RU and SourceText of the
one named row, and only when both still match the exact catalogue legacy seed.
Run before apply_unified_locale_patches.py so its nine new translations agree.
"""
from __future__ import annotations

import copy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
import traceback
import uuid

PROJECT = Path('E:/DEVGAME/MessControl')
HERE = PROJECT / 'Tools/Unreal/UI'
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
from localization_catalog import CatalogError, has_copy, index_rows, text_source
from apply_unified_locale_patches import (assert_idle_clean, backup_package, dirty_packages,
                                         on_e, read_rows, sha256, write_json)
TABLE = '/Game/Gameplay/UI/Data/DT_UI_Localization'
KEY = 'UI.Event.Cataclysm.Rafts.Instruction'
FIELDS = ('RU', 'SourceText')
NATIVE_COPY = 'ПКМ у плота — занять левое или правое место. WASD — грести относительно плота; усилия обоих игроков складываются. Пробел — спрыгнуть. Договаривайтесь о направлении и уклоняйтесь от плывущего мусора!'
OUTPUT = 'E:/CodexWork/MessControl/Validation/UICatch_20261104/Locales/RaftsCopyMigration'
_QUOTED = re.compile(r'"(?:[^"\\]|\\.)*"')
_LITERAL = re.compile(r'^\s*(NSLOCTEXT|LOCTEXT|INVTEXT)\s*\(.*\)\s*$', re.DOTALL)


def require(condition, message):
    if not condition:
        raise CatalogError(message)


def text_identity(value):
    """Raw identity arguments, excluding the replaceable source component."""
    # Reuse the catalogue parser to reject unsupported or malformed histories.
    text_source(value)
    match = _LITERAL.fullmatch(value)
    if not match:
        return ('Plain',)
    arguments = list(_QUOTED.finditer(value))
    expected = {'NSLOCTEXT': 3, 'LOCTEXT': 2, 'INVTEXT': 1}[match.group(1)]
    require(len(arguments) == expected, 'Unsupported FText identity')
    return (match.group(1), tuple(argument.group(0) for argument in arguments[:-1]))


def with_source(value, replacement):
    identity = text_identity(value)
    if identity[0] == 'Plain':
        return replacement
    last = list(_QUOTED.finditer(value))[-1]
    result = value[:last.start()] + json.dumps(replacement, ensure_ascii=False) + value[last.end():]
    require(text_identity(result) == identity and text_source(result) == replacement,
            'FText identity/source replacement failed')
    return result


def custom_ru_entries(row):
    entries = []
    for field in ('AdditionalLanguages', 'Localizations'):
        mapping = row.get(field, {})
        require(isinstance(mapping, dict), 'Unexpected custom-language payload: ' + field)
        for culture, value in mapping.items():
            name = str(culture).strip().replace('_', '-').casefold()
            if (name == 'ru' or name.startswith('ru-')) and has_copy(value):
                entries.append(dict(field=field, culture=culture, value=value))
    return entries


def plan_rows(before, legacy_seed):
    """Return an immutable-input plan; authored deviations are reported, never overwritten."""
    indexed = index_rows(before, 'current localization table')
    row = indexed.get(KEY.casefold())
    require(row is not None, 'The named source row is missing; do not invent its identity')
    require(all(field in row for field in FIELDS), 'The expected source fields are missing')
    current = {field: text_source(row[field]) for field in FIELDS}
    override = custom_ru_entries(row)
    report = dict(key=KEY, fields=list(FIELDS), legacy_seed=legacy_seed, native_copy=NATIVE_COPY,
                  observed_copy=current, custom_ru_entries=override, changed_fields=[])
    if all(current[field] == NATIVE_COPY for field in FIELDS):
        report['status'] = 'already_current'
        return copy.deepcopy(before), report
    if override:
        report['status'] = 'skipped_custom_ru_override'
        return copy.deepcopy(before), report
    if not all(current[field] == legacy_seed for field in FIELDS):
        report['status'] = 'skipped_unexpected_authored_copy'
        return copy.deepcopy(before), report
    require(legacy_seed != NATIVE_COPY, 'Catalogue no longer contains the expected legacy seed')
    planned = copy.deepcopy(before)
    target = index_rows(planned, 'planned localization table')[KEY.casefold()]
    for field in FIELDS:
        target[field] = with_source(row[field], NATIVE_COPY)
        report['changed_fields'].append(field)
    report['status'] = 'ready'
    report['protected_fields_verified'] = verify_preserved(before, planned, changed=True)
    return planned, report


def verify_preserved(before, after, *, changed):
    original = index_rows(before, 'before')
    current = index_rows(after, 'roundtrip')
    require(set(original) == set(current), 'Existing row set changed')
    checked = 0
    for identity, old in original.items():
        actual = current[identity]
        require(set(old) == set(actual), 'Existing field set changed: ' + old['Name'])
        for field, value in old.items():
            if changed and identity == KEY.casefold() and field in FIELDS:
                require(text_identity(actual[field]) == text_identity(value),
                        'FText namespace/key/history type changed: ' + field)
                require(text_source(actual[field]) == NATIVE_COPY, 'Native copy did not roundtrip: ' + field)
                continue
            require(actual[field] == value, 'Unrelated field/history changed: ' + old['Name'] + '.' + field)
            checked += 1
    return checked


def run(*, apply=False, output_dir=OUTPUT):
    import unreal as u
    from author_localization_catalog import native_validate

    assert_idle_clean(u)
    project = on_e(u.Paths.project_dir())
    require(project == PROJECT.resolve(), 'Expected the current MessControl checkout')
    catalogue = project / 'Tools/Unreal/UI/gameplay_ui_texts.json'
    rows = json.loads(catalogue.read_text(encoding='utf-8-sig'))
    seed = index_rows(rows, 'canonical locale catalogue').get(KEY.casefold())
    require(seed is not None, 'Missing exact legacy catalogue seed')
    legacy = text_source(seed.get('SourceText', ''))
    require(legacy and text_source(seed.get('RU', '')) == legacy, 'Canonical legacy RU/SourceText are not identical')
    native_source = project / 'Source/MessControl/Private/MCCataclysmEvent.cpp'
    require(NATIVE_COPY in native_source.read_text(encoding='utf-8-sig'),
            'The authorized native instruction changed; review this migration before use')
    output = on_e(output_dir) / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '_' + uuid.uuid4().hex[:8])
    output.mkdir(parents=True, exist_ok=False)
    report = dict(status='planning', apply=apply, table=TABLE, output=str(output),
                  catalogue=str(catalogue), catalogue_sha256=sha256(catalogue),
                  native_source=str(native_source), native_source_sha256=sha256(native_source),
                  package_backups=[], saved_assets=[], gameplay_modified=False, engine_or_build_started=False)
    table = None
    before = None
    mutated = False
    try:
        table = u.load_asset(TABLE)
        require(isinstance(table, u.DataTable), 'Missing localization DataTable')
        struct = u.DataTableFunctionLibrary.get_data_table_row_struct(table)
        require(struct and struct.get_path_name() == '/Script/MessControl.MCUILocalizationRow', 'Wrong row struct')
        assert_idle_clean(u)
        before = read_rows(u, table)
        planned, plan = plan_rows(before, legacy)
        report['plan'] = plan
        write_json(output / 'BeforeRows.json', before)
        write_json(output / 'PlannedRows.json', planned)
        report['protected_fields_verified'] = verify_preserved(before, planned, changed=bool(plan['changed_fields']))
        if not plan['changed_fields']:
            report['status'] = plan['status']
            write_json(output / 'AfterRows.json', before)
            return report
        if not apply:
            report['status'] = 'planned'
            return report
        valid_before, problems_before = native_validate(u, table, table)
        report['native_validation_before'] = dict(valid=valid_before, problems=problems_before)
        report['package_backups'] = backup_package(project, output)
        write_json(output / 'Report.json', report)
        assert_idle_clean(u)
        with u.ScopedEditorTransaction('Correct one Raft instruction source string'):
            table.modify()
            mutated = True
            require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(planned, ensure_ascii=False)),
                    'DataTable import failed')
            after = read_rows(u, table)
            report['protected_fields_verified'] = verify_preserved(before, after, changed=True)
            valid_after, problems_after = native_validate(u, table, table)
            report['native_validation_after'] = dict(valid=valid_after, problems=problems_after)
            new_errors = set(problems_after) - set(problems_before)
            require(not new_errors, 'New native validation errors: ' + '; '.join(sorted(new_errors)))
            require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'PIE started during migration')
            require(dirty_packages(u) <= {TABLE}, 'Unrelated packages became dirty; preserving them')
            require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Could not save target DataTable')
            report['saved_assets'].append(TABLE)
        after = read_rows(u, table)
        report['protected_fields_verified'] = verify_preserved(before, after, changed=True)
        write_json(output / 'AfterRows.json', after)
        report['saved_package_sha256'] = sha256(project / 'Content/Gameplay/UI/Data/DT_UI_Localization.uasset')
        assert_idle_clean(u)
        report['status'] = 'passed'
        return report
    except Exception as exc:
        report.update(status='failed', error=str(exc), traceback=traceback.format_exc())
        if mutated and table is not None and before is not None:
            try:
                require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(before, ensure_ascii=False)), 'Rollback import failed')
                verify_preserved(before, read_rows(u, table), changed=False)
                require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Rollback save failed')
                report['target_rollback_verified'] = True
            except Exception as rollback_error:
                report['rollback_error'] = str(rollback_error)
                report['manual_recovery_required'] = True
        raise
    finally:
        report['dirty_after'] = sorted(dirty_packages(u))
        write_json(output / 'Report.json', report)
        u.log('MC_RAFT_INSTRUCTION_COPY ' + json.dumps({name: report.get(name) for name in
              ('status', 'apply', 'output', 'error')}, ensure_ascii=False))


if __name__ == '__main__':
    run(**globals().get('MC_RAFT_INSTRUCTION_OPTIONS', {}))
