"""Explicit, backed-up UI catalog migration and spreadsheet editing in Unreal.

Run ``run('plan')`` first, then ``run('migrate')`` in an idle Editor with the new
native row structs loaded. ``run('export', csv_file='E:/.../Texts.csv')`` and
``run('import', csv_file=...)`` use a lossless baseline beside the CSV. This tool
never builds, starts PIE, rewires a theme or modifies gameplay definitions.

The ``MC_LOCALIZATION_OPTIONS`` global supports ExecutePythonScript/runpy.
Without explicit options the script only writes a plan, never saves assets.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import traceback
import uuid

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
from localization_catalog import (CatalogError, LANGUAGE_COLUMNS, export_spreadsheet,
                                  index_rows, merge_spreadsheet, plan_migration)

DEFAULT_TABLES = dict(localization='/Game/Gameplay/UI/Data/DT_UI_Localization',
                      cards='/Game/Gameplay/UI/Data/DT_UI_Cards',
                      boosters='/Game/Gameplay/UI/Data/DT_UI_Boosters')
EXPECTED_STRUCTS = dict(localization='MCUILocalizationRow', cards='MCUICardRow', boosters='MCUIBoosterRow')
DEFAULT_OUTPUT = 'E:/CodexWork/MessControlLocalization_20261019/Authoring'


def require(condition, message):
    if not condition:
        raise CatalogError(message)


def on_e(path):
    value = Path(path).resolve()
    require(value.drive.upper() == 'E:', 'Working/output files must stay on E: ' + str(value))
    return value


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def json_text(value):
    return json.dumps(value, ensure_ascii=False, indent=2)


def write_json(path, value):
    path = on_e(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json_text(value), encoding='utf-8')
    os.replace(temporary, path)


def package_name(path):
    require(isinstance(path, str) and path.startswith('/Game/'), 'Select an explicit /Game table: ' + str(path))
    require('..' not in path and ':' not in path, 'Invalid package path: ' + path)
    return path.split('.', 1)[0]


def dirty_packages(u):
    return {package.get_path_name() for package in
            list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
            list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}


def read_rows(u, table):
    return json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def native_validate(u, table, localization_table):
    result = u.MCUIDataLibrary.validate_content_table(table, localization_table=localization_table)
    # UE Python commonly treats bool + one out-parameter as success/failure and
    # returns only the output array (empty array is success, None is failure).
    # Retain tuple support for bridge versions that expose the C++ bool as well.
    if isinstance(result, tuple):
        return bool(result[0]), [str(value) for value in result[1]]
    if result is None:
        return False, ['Native validator returned failure; inspect Unreal output log']
    if isinstance(result, bool):
        return result, []
    problems = [str(value) for value in result]
    return not problems, problems


def verify_preserved(before, after, kind):
    editable = {*LANGUAGE_COLUMNS, 'AdditionalLanguages', 'Context'} if kind == 'localization' else {
        'DisplayNameKey', 'DescriptionKey'}
    indexed = index_rows(after, kind + ' imported')
    for row in before:
        actual = indexed.get(row['Name'].casefold())
        require(actual is not None, kind + ': an existing row disappeared: ' + row['Name'])
        for field, value in row.items():
            if field in editable:
                continue
            require(field in actual and actual[field] == value,
                    kind + ': protected native field changed: ' + row['Name'] + '.' + field)


def backup_package(root, package, output, report):
    relative = Path('Content') / package[len('/Game/'):]
    found = False
    for suffix in ('.uasset', '.uexp', '.ubulk'):
        source = (root / relative).with_suffix(suffix)
        if not source.is_file():
            continue
        found = True
        target = (output / 'Before' / relative).with_suffix(suffix)
        target.parent.mkdir(parents=True, exist_ok=True)
        require(not target.exists(), 'Fresh operation backup already exists: ' + str(target))
        shutil.copy2(source, target)
        require(sha256(source) == sha256(target), 'Backup hash mismatch: ' + str(source))
        report['backups'].append(dict(source=str(source), backup=str(target), sha256=sha256(target)))
    require(found, 'Target table has no saved package to back up: ' + package)


def apply_rows(u, root, tables, before, planned, output, report):
    changed = [kind for kind in tables if before[kind] != planned[kind]]
    report['changed_tables'] = changed
    if not changed:
        report['saved_assets'] = []
        return
    for kind in changed:
        backup_package(root, package_name(tables[kind].get_path_name()), output, report)
    # Export all participating tables before the first mutation, including hidden
    # compatibility fields and authored FText histories. Disk packages alone do
    # not describe currently loaded row serialization.
    write_json(output / 'Before' / 'Tables.json', before)
    report['before_json_sha256'] = sha256(output / 'Before' / 'Tables.json')
    write_json(output / 'Report.json', report)
    mutated = []
    try:
        with u.ScopedEditorTransaction('Migrate shared UI localization catalog'):
            for kind in changed:
                table = tables[kind]
                table.modify()
                mutated.append(kind)
                require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json_text(planned[kind])),
                        'Native DataTable import failed: ' + kind)
                verify_preserved(before[kind], read_rows(u, table), kind)
            # Validate the complete new in-memory catalog before the first save.
            # Cards and boosters can now resolve the newly added stable keys.
            for kind in tables:
                valid, problems = native_validate(u, tables[kind], tables['localization'])
                report['native_validation'][kind] = dict(valid=valid, problems=problems)
                require(valid, kind + ': native localization validation failed: ' + '; '.join(problems))
            for kind in changed:
                require(u.EditorAssetLibrary.save_loaded_asset(tables[kind], only_if_is_dirty=False),
                        'Could not save table: ' + kind)
                report['saved_assets'].append(tables[kind].get_path_name())
    except Exception:
        # No partial in-memory table is used after a failure. Saved package
        # backups remain available if a disk save itself failed mid-operation.
        restore_errors = []
        for kind in mutated:
            try:
                require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(tables[kind], json_text(before[kind])),
                        'Could not restore in-memory table: ' + kind)
            except Exception as exc:
                restore_errors.append(str(exc))
        report['in_memory_rollback_errors'] = restore_errors
        report['rollback_requires_editor_review'] = True
        raise
    for kind in changed:
        report['after_rows'][kind] = read_rows(u, tables[kind])
        package = package_name(tables[kind].get_path_name())
        file = (root / 'Content' / package[len('/Game/'):]).with_suffix('.uasset')
        report['saved_package_sha256'][package] = sha256(file)


def run(action='plan', *, output_dir=DEFAULT_OUTPUT, localization_table=DEFAULT_TABLES['localization'],
        card_table=DEFAULT_TABLES['cards'], booster_table=DEFAULT_TABLES['boosters'], csv_file=None,
        additional_rows_file=None):
    import unreal as u

    require(action in ('plan', 'migrate', 'export', 'import'), 'Unknown action: ' + str(action))
    root = on_e(u.Paths.project_dir())
    require((root / 'MessControl.uproject').is_file(), 'Expected the MessControl project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Idle editor required')
    output = on_e(output_dir) / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '_' + uuid.uuid4().hex[:8])
    output.mkdir(parents=True, exist_ok=False)
    selected = dict(localization=package_name(localization_table), cards=package_name(card_table),
                    boosters=package_name(booster_table))
    require(len(set(selected.values())) == 3, 'Select three distinct table assets')
    before_dirty = dirty_packages(u)
    report = dict(status='running', action=action, project=str(root), output=str(output),
                  selected_tables=selected, dirty_before=sorted(before_dirty), backups=[], saved_assets=[],
                  native_validation={}, after_rows={}, saved_package_sha256={}, theme_references_changed=False,
                  gameplay_definitions_changed=False, editor_or_build_started=False)
    try:
        tables = {kind: u.load_asset(path) for kind, path in selected.items()}
        for kind, table in tables.items():
            require(isinstance(table, u.DataTable), 'Missing DataTable: ' + selected[kind])
            actual = u.DataTableFunctionLibrary.get_data_table_row_struct(table)
            require(actual and actual.get_path_name() == '/Script/MessControl.' + EXPECTED_STRUCTS[kind],
                    'Wrong row struct: ' + selected[kind])
        columns = {str(value) for value in u.DataTableFunctionLibrary.get_data_table_column_names(tables['localization'])}
        require(set(LANGUAGE_COLUMNS).issubset(columns) and 'AdditionalLanguages' in columns,
                'New localization schema is not loaded. Load the rebuilt Editor before authoring.')
        if action in ('migrate', 'import'):
            require(not before_dirty.intersection(selected.values()),
                    'Preserve unsaved target table changes before authoring: ' + str(sorted(before_dirty.intersection(selected.values()))))
        before = {kind: read_rows(u, table) for kind, table in tables.items()}
        write_json(output / 'InputTables.json', before)
        if action in ('plan', 'migrate'):
            additional_rows = []
            if additional_rows_file:
                additions = on_e(additional_rows_file)
                additional_rows = json.loads(additions.read_text(encoding='utf-8-sig'))
                require(isinstance(additional_rows, list), 'Additional rows must be a JSON array')
                report['additional_rows_file'] = str(additions)
                report['additional_rows_sha256'] = sha256(additions)
            texts, cards, boosters, plan = plan_migration(before['localization'], before['cards'], before['boosters'],
                                                         card_path=selected['cards'], booster_path=selected['boosters'],
                                                         additional_localization_rows=additional_rows)
            planned = dict(localization=texts, cards=cards, boosters=boosters)
            report['plan'] = plan
            write_json(output / 'PlannedTables.json', planned)
            if action == 'migrate':
                apply_rows(u, root, tables, before, planned, output, report)
        elif action == 'export':
            require(csv_file, 'Select an explicit E: CSV output path')
            file = on_e(csv_file)
            baseline_path = file.with_suffix(file.suffix + '.baseline.json')
            require(not file.exists() and not baseline_path.exists(), 'Choose a fresh CSV filename; exports never overwrite translator edits')
            csv_text, baseline = export_spreadsheet(before['localization'])
            baseline['table'] = selected['localization']
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(csv_text, encoding='utf-8-sig', newline='')
            write_json(baseline_path, baseline)
            report.update(csv_file=str(file), baseline_file=str(baseline_path), csv_sha256=sha256(file),
                          baseline_sha256=sha256(baseline_path))
        else:
            require(csv_file, 'Select an existing E: CSV with its matching baseline')
            file = on_e(csv_file)
            baseline_path = file.with_suffix(file.suffix + '.baseline.json')
            baseline = json.loads(baseline_path.read_text(encoding='utf-8-sig'))
            require(baseline.get('table') == selected['localization'], 'CSV baseline belongs to a different table')
            merged, merge = merge_spreadsheet(before['localization'], file.read_text(encoding='utf-8-sig'), baseline)
            planned = dict(before, localization=merged)
            report['merge'] = merge
            apply_rows(u, root, tables, before, planned, output, report)
        after_dirty = dirty_packages(u)
        report['dirty_after'] = sorted(after_dirty)
        require(before_dirty.issubset(after_dirty), 'An unrelated dirty package was unexpectedly cleared')
        report['status'] = 'passed'
        return report
    except Exception as exc:
        report.update(status='failed', error=str(exc), traceback=traceback.format_exc())
        raise
    finally:
        write_json(output / 'Report.json', report)
        u.log('MC_LOCALIZATION_CATALOG ' + json_text({key: report.get(key) for key in ('status', 'action', 'output', 'error')}))


if __name__ == '__main__':
    run(**globals().get('MC_LOCALIZATION_OPTIONS', {}))
