"""Merge the unified UI locale recipes without replacing authored copy.

Run in an idle, clean MessControl Editor:
    run(apply=True)
ExecutePythonScript may instead pass MC_UNIFIED_LOCALE_OPTIONS = {"apply": True}.
The default is a plan only. No build, PIE, theme or gameplay changes are made.
All snapshots, verified package backups and coverage reports stay on E:.
"""
from __future__ import annotations

import copy
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import traceback
import uuid

PROJECT = Path('E:/DEVGAME/MessControl')
HERE = PROJECT / 'Tools/Unreal/UI'
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
from localization_catalog import (CatalogError, LANGUAGE_CULTURES, canonical_culture,
                                  has_copy, index_rows, indexed_languages, text_source)

LANGUAGES = tuple(LANGUAGE_CULTURES)
TABLE = '/Game/Gameplay/UI/Data/DT_UI_Localization'
PATCHES = ('ui_v2_screen_locale_patch.json', 'ui_v2_backend_locale_patch.json', 'ui_v2_catch_texts.json',
           'ui_v2_encounter_loading_locale_patch.json', 'ui_v2_tutorial_locale_patch.json',
           'ui_v2_hud_extras_locale_patch.json', 'ui_v2_arena_context_locale_patch.json',
           'ui_v2_emote_world_locale_patch.json', 'ui_v2_reticle_grill_locale_patch.json',
           'ui_v2_world_label_locale_patch.json', 'ui_v2_clothing_locale_patch.json', 'ui_v2_complete_commerce_locale_patch.json', 'ui_v2_complete_hud_locale_patch.json', 'ui_v2_native_commerce_locale_patch.json')
OUTPUT = 'E:/CodexWork/MessControl/Validation/UICatch_20261104/Locales'
MAP_FIELDS = ('AdditionalLanguages', 'Localizations')


def require(condition, message):
    if not condition:
        raise CatalogError(message)


def on_e(value):
    path = Path(value).resolve()
    require(path.drive.upper() == 'E:', 'Working/output paths must stay on E: ' + str(path))
    return path


def json_text(value):
    return json.dumps(value, ensure_ascii=False, indent=2)


def write_json(path, value):
    path = on_e(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json_text(value) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def culture_chain(culture):
    """Same-language parents only; never insert the native RU fallback."""
    current = canonical_culture(culture)
    result = [current]
    while '-' in current:
        current = current.rsplit('-', 1)[0]
        result.append(current)
    return result


def custom_translation(row, column):
    maps = {field: indexed_languages(row.get(field, {}), row['Name'] + '.' + field)
            for field in MAP_FIELDS}
    for culture in culture_chain(LANGUAGE_CULTURES[column]):
        for field in MAP_FIELDS:
            candidate = maps[field].get(culture)
            if candidate and has_copy(candidate[1]):
                return candidate[1], field + '[' + candidate[0] + ']'
    return '', ''


def effective_translation(row, column):
    if has_copy(row.get(column, '')):
        return row[column], column
    value, owner = custom_translation(row, column)
    if value:
        return value, owner
    # SourceText is still the authored native copy when the RU column is empty.
    # This exception is deliberately unavailable to every foreign culture.
    if column == 'RU' and has_copy(row.get('SourceText', '')):
        return row['SourceText'], 'SourceText'
    return '', ''


def native_text(row):
    return row.get('RU', '') if has_copy(row.get('RU', '')) else row.get('SourceText', '')


def parameters(value):
    return Counter(re.findall(r'\{[^{}]+\}', text_source(value)))


def plan_rows(before, patches):
    """Pure lossless merge: existing rows change only in eligible empty columns."""
    original = index_rows(before, 'current table')
    incoming = index_rows(patches, 'locale patches')
    planned = copy.deepcopy(before)
    target = index_rows(planned, 'planned table')
    changed, added, protected = [], [], []
    for identity, seed in incoming.items():
        require(all(has_copy(seed.get(column, '')) for column in LANGUAGES),
                'Incomplete seed translations: ' + seed['Name'])
        expected = parameters(native_text(seed))
        for column in LANGUAGES:
            require(parameters(seed[column]) == expected,
                    'Seed format parameters differ: ' + seed['Name'] + '.' + column)
        row = target.get(identity)
        if row is None:
            row = copy.deepcopy(seed)
            planned.append(row)
            target[identity] = row
            added.append(seed['Name'])
            continue
        for column in LANGUAGES:
            if has_copy(row.get(column, '')):
                continue
            value, owner = effective_translation(row, column)
            if value:
                protected.append(dict(key=row['Name'], column=column, existing_owner=owner))
                continue
            candidate = seed[column]
            baseline = native_text(row) or native_text(seed)
            require(parameters(candidate) == parameters(baseline),
                    'Seed is incompatible with authored native format: ' + row['Name'] + '.' + column)
            row[column] = candidate
            changed.append(dict(key=row['Name'], column=column))
    # This also asserts SourceText, Context, both maps, unknown schema fields,
    # FText histories and every untouched row remain exactly as exported.
    verify_preserved(before, planned, changed)
    require(set(target) == set(original) | set(incoming), 'Unexpected row set in merge plan')
    return planned, dict(added_rows=added, filled_cells=changed,
                        protected_effective_translations=protected)


def verify_preserved(before, after, changed):
    actual = index_rows(after, 'roundtrip table')
    allowed = {(item['key'].casefold(), item['column']) for item in changed}
    checked = 0
    for row in before:
        identity = row['Name'].casefold()
        require(identity in actual, 'Existing row disappeared: ' + row['Name'])
        for field, value in row.items():
            if (identity, field) in allowed:
                require(field in LANGUAGES and not has_copy(value),
                        'Attempted to overwrite authored copy: ' + row['Name'] + '.' + field)
                continue
            require(field in actual[identity] and actual[identity][field] == value,
                    'Preserved field/history changed: ' + row['Name'] + '.' + field)
            checked += 1
    return checked


def verify_roundtrip(before, planned, after, merge):
    checked = verify_preserved(before, after, merge['filled_cells'])
    expected = index_rows(planned, 'planned table')
    actual = index_rows(after, 'roundtrip table')
    require(set(expected) == set(actual), 'Row set changed during DataTable import')
    added = {key.casefold() for key in merge['added_rows']}
    for item in merge['filled_cells']:
        key, column = item['key'].casefold(), item['column']
        require(text_source(actual[key][column]) == text_source(expected[key][column]),
                'Filled translation did not roundtrip: ' + item['key'] + '.' + column)
    for key in added:
        for field, value in expected[key].items():
            current = actual[key].get(field)
            if field in LANGUAGES or field == 'SourceText':
                require(text_source(current) == text_source(value),
                        'New translation did not roundtrip: ' + expected[key]['Name'] + '.' + field)
            elif field == 'Name':
                require(current.casefold() == value.casefold(), 'New row identity changed')
            else:
                require(current == value, 'New row field changed: ' + expected[key]['Name'] + '.' + field)
    return checked


def coverage(rows, keys):
    indexed = index_rows(rows, 'coverage table')
    by_language = {column: dict(flat=0, custom=0, source=0, missing=0) for column in LANGUAGES}
    missing = []
    for key in keys:
        row = indexed.get(key.casefold())
        for column in LANGUAGES:
            value, owner = effective_translation(row, column) if row else ('', '')
            kind = 'missing' if not value else 'flat' if owner == column else 'source' if owner == 'SourceText' else 'custom'
            by_language[column][kind] += 1
            if not value:
                missing.append(dict(key=key, column=column))
    return dict(required_rows=len(keys), catalog_rows=len(rows),
                languages=by_language, missing=missing, foreign_RU_fallback_counted=False)


def dirty_packages(u):
    return {package.get_path_name() for package in
            list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
            list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}


def assert_idle_clean(u):
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
            'Stop PIE before applying locale patches; active play is preserved')
    dirty = dirty_packages(u)
    require(not dirty, 'Preserve unsaved editor packages before applying: ' + str(sorted(dirty)))


def read_rows(u, table):
    return json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def backup_package(project, output):
    backups = []
    base = project / 'Content' / TABLE.removeprefix('/Game/')
    for suffix in ('.uasset', '.uexp', '.ubulk'):
        source = base.with_suffix(suffix)
        if not source.is_file():
            continue
        destination = output / 'BeforePackages' / source.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        require(not destination.exists(), 'Backup path already exists: ' + str(destination))
        shutil.copy2(source, destination)
        require(sha256(source) == sha256(destination), 'Package backup hash mismatch')
        backups.append(dict(source=str(source), backup=str(destination), sha256=sha256(destination)))
    require(any(item['source'].endswith('.uasset') for item in backups), 'Saved table package was not found')
    return backups


def run(*, apply=False, output_dir=OUTPUT):
    import unreal as u
    from author_localization_catalog import native_validate

    assert_idle_clean(u)
    project = on_e(u.Paths.project_dir())
    require(project == PROJECT.resolve() and (project / 'MessControl.uproject').is_file(),
            'Expected the current MessControl checkout on E:')
    output = on_e(output_dir) / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '_' + uuid.uuid4().hex[:8])
    output.mkdir(parents=True, exist_ok=False)
    report = dict(status='planning', apply=apply, output=str(output), table=TABLE,
                  patch_files=[], package_backups=[], saved_assets=[], native_validation={},
                  runtime_or_build_started=False, source_or_theme_changed=False)
    table = None
    before = None
    mutated = False
    try:
        patches = []
        for name in PATCHES:
            path = on_e(project / 'Tools/Unreal/UI' / name)
            rows = json.loads(path.read_text(encoding='utf-8-sig'))
            require(isinstance(rows, list), 'Patch must be an array: ' + name)
            report['patch_files'].append(dict(path=str(path), rows=len(rows), sha256=sha256(path)))
            patches.extend(rows)
        index_rows(patches, 'unified locale recipes')
        table = u.load_asset(TABLE)
        require(isinstance(table, u.DataTable), 'Missing localization DataTable')
        struct = u.DataTableFunctionLibrary.get_data_table_row_struct(table)
        require(struct and struct.get_path_name() == '/Script/MessControl.MCUILocalizationRow',
                'Expected the current MCUILocalizationRow schema')
        columns = {str(value) for value in u.DataTableFunctionLibrary.get_data_table_column_names(table)}
        require(set(LANGUAGES) | set(MAP_FIELDS) | {'SourceText', 'Context'} <= columns,
                'Current localization schema is not loaded')
        require(all(set(seed) <= columns | {'Name'} for seed in patches), 'Patch contains unsupported fields')
        assert_idle_clean(u)
        before = read_rows(u, table)
        planned, merge = plan_rows(before, patches)
        report['merge'] = merge
        keys = [row['Name'] for row in patches]
        report['coverage_before'] = coverage(before, keys)
        report['coverage_planned'] = coverage(planned, keys)
        require(not report['coverage_planned']['missing'], 'Planned locale coverage is incomplete')
        write_json(output / 'BeforeRows.json', before)
        write_json(output / 'PlannedRows.json', planned)
        write_json(output / 'CoverageBefore.json', report['coverage_before'])
        write_json(output / 'CoveragePlanned.json', report['coverage_planned'])
        if not apply:
            report['status'] = 'planned'
            return report
        valid_before, problems_before = native_validate(u, table, table)
        report['native_validation']['before'] = dict(valid=valid_before, problems=problems_before)
        if before != planned:
            report['package_backups'] = backup_package(project, output)
            write_json(output / 'Report.json', report)
            assert_idle_clean(u)
            with u.ScopedEditorTransaction('Fill empty unified UI translations'):
                table.modify()
                mutated = True
                require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json_text(planned)),
                        'DataTable JSON import failed')
                after = read_rows(u, table)
                report['protected_fields_verified'] = verify_roundtrip(before, planned, after, merge)
                valid_after, problems_after = native_validate(u, table, table)
                report['native_validation']['after'] = dict(valid=valid_after, problems=problems_after)
                new_problems = set(problems_after) - set(problems_before)
                require(not new_problems, 'New native catalog validation errors: ' + '; '.join(sorted(new_problems)))
                require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
                        'PIE started during locale application')
                require(dirty_packages(u) <= {TABLE}, 'Unrelated packages became dirty; preserving them')
                require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Could not save localization table')
                report['saved_assets'].append(TABLE)
        after = read_rows(u, table)
        report['protected_fields_verified'] = verify_roundtrip(before, planned, after, merge)
        report['coverage_after'] = coverage(after, keys)
        require(not report['coverage_after']['missing'], 'Saved locale coverage is incomplete')
        write_json(output / 'AfterRows.json', after)
        write_json(output / 'CoverageAfter.json', report['coverage_after'])
        report['saved_package_sha256'] = sha256(project / 'Content/Gameplay/UI/Data/DT_UI_Localization.uasset')
        assert_idle_clean(u)
        report['status'] = 'passed'
        return report
    except Exception as exc:
        report.update(status='failed', error=str(exc), traceback=traceback.format_exc())
        if mutated and table is not None and before is not None:
            try:
                require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json_text(before)), 'Rollback import failed')
                restored = read_rows(u, table)
                require(index_rows(restored, 'rollback') == index_rows(before, 'before'), 'Rollback row payload differs')
                require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Rollback save failed')
                report['target_rollback_verified'] = True
            except Exception as rollback_error:
                report['rollback_error'] = str(rollback_error)
                report['manual_recovery_required'] = True
        raise
    finally:
        report['dirty_after'] = sorted(dirty_packages(u))
        write_json(output / 'Report.json', report)
        u.log('MC_UNIFIED_LOCALE_PATCH ' + json.dumps({key: report.get(key) for key in
              ('status', 'apply', 'output', 'error')}, ensure_ascii=False))


if __name__ == '__main__':
    run(**globals().get('MC_UNIFIED_LOCALE_OPTIONS', {}))
