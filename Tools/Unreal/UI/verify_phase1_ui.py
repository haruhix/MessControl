"""Worldless Phase1 UMG render matrix; run in the existing idle editor.

Run with runpy.run_path(..., run_name='__main__'). --manifest works without
Unreal. This script launches no editor/build, performs no gameplay/network
actions, saves no assets, and never authors or renders a private boss fixture.
Only transient CDO preview fields are overridden, then restored in finally.

Full-screen roles use real project DPI at 1280x720, 1920x1080 and 2560x1080.
Booster/status/world widgets use component-sized crops; rendering their root
directly at a viewport size would stretch them and give misleading evidence.
Every PNG and the incremental report remain on E:. A successful export is
technical evidence only; visual approval and live input/resize remain pending.

Optional process env MC_PHASE1_VERIFY_ONLY is a comma-separated list of fixture
IDs or roles. It runs a clearly reported subset, without reusing stale PNGs.
"""
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
import traceback


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Saved/GameplayUI/Phase1/Previews'
REPORT = OUT / 'render_report.json'
TEMP = Path('E:/CodexTemp/GameplayUIPhase1Verify')
SIZES = ((1280, 720), (1920, 1080), (2560, 1080))
NATIVE_ROLES = {
    'GameplayHUD': 'MCGameplayHUD', 'PauseMenu': 'MCGameplayMenuWidget',
    'ConnectionMenu': 'MCGameplayMenuWidget', 'Scoreboard': 'MCScoreboardWidget',
    'PerkChoice': 'MCPerkChoiceWidget', 'Booster': 'MCBoosterWidget',
    'Tutorial': 'MCTutorialWidget', 'Emotes': 'MCEmoteWidget',
    'FreezeBar': 'MCFreezeBarWidget', 'DeliveryCaption': 'MCDeliveryZoneCaptionWidget',
    'PlayerName': 'MCPlayerNameWidget', 'UlcerProgress': 'MCUlcerProgressWidget',
}
LONG_TUTORIAL = {
    'preview_title': 'Длинный учебный этап: совместная очистка труднодоступных участков и помощь товарищам',
    'preview_dialogue': ('Это искусственный текст для проверки вёрстки, не утверждённый перевод. '
                         'Сначала выбери щётку, подойди к подсвеченному зубу и удерживай действие очистки. '
                         'Следи за выносливостью, помогай товарищам и проверяй прогресс команды. '
                         'Длинные описания должны переноситься в пределах панели, сохраняя читаемый размер шрифта.\n'
                         'Если весь текст не помещается по высоте, содержимое должно оставаться доступным в прокрутке.'),
    'preview_instruction': ('1 — щётка. Удерживай ЛКМ, чтобы очистить подсвеченную поверхность. '
                            'Затем помоги товарищу и дождись завершения общей задачи; это предложение проверяет перенос подсказки.'),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def planned_cases():
    cases = []

    def add(role, state, identity=None, sizes=SIZES, culture='ru', overrides=None, note=''):
        for width, height in sizes:
            cases.append({'id': identity or role + '_' + state, 'role': role,
                          'state': state, 'culture': culture, 'size': [width, height],
                          'overrides': overrides or {}, 'coverage_note': note})

    for state in ('Default', 'LowResources', 'LongText', 'Swim', 'Drowning',
                  'ResultsVictory', 'ResultsDefeat'):
        add('GameplayHUD', state)
    for state in ('Default', 'Settings', 'VideoConfirm', 'Credits', 'Leave', 'Exit'):
        add('PauseMenu', state)
    add('ConnectionMenu', 'Connection', 'F2Connection')
    add('Scoreboard', 'LongText')
    for state in ('Risky', 'ContractConfirm', 'Pending'):
        add('PerkChoice', state)
    add('PerkChoice', 'LongText', overrides={'PreviewPerkIDs': '@longest_existing_positive_cards'},
        note='Three longest existing positive presentation rows; no DT content is changed.')
    for state in ('Default', 'AllStates', 'ActiveEffect', 'Armed', 'Disabled', 'Empty'):
        add('Booster', state, sizes=((640, 360),), note='Standalone component draw-size crop, not viewport anchoring.')
    add('Booster', 'Details', sizes=((640, 480),), overrides={'preview_booster_row': '@valid_existing_booster_row'})
    add('Booster', 'Details', 'Booster_DTrow_en-GB', sizes=((640, 480),), culture='en-GB',
        overrides={'preview_booster_row': '@valid_existing_booster_row'},
        note='Existing row image/name/description plus explicit-culture lookup and parent-culture fallback.')
    add('Tutorial', 'Default')
    add('Tutorial', 'LongText', overrides=LONG_TUTORIAL,
        note='Synthetic stress copy only; no shipping localization is authored.')
    add('Emotes', 'Default')
    for state in ('Danger', 'Warming'):
        add('FreezeBar', state, sizes=((500, 180),), note='Standalone status crop, not viewport anchoring.')
    add('DeliveryCaption', 'SortingError', sizes=((500, 180),), note='Standalone world-label crop, not viewport anchoring.')
    add('PlayerName', 'Help', sizes=((500, 180),), note='Standalone world-label crop, not viewport anchoring.')
    add('UlcerProgress', 'Default', 'UlcerProgress', sizes=((180, 180),), note='Standalone interaction crop, not viewport anchoring.')
    add('PauseMenu', 'Settings', 'PauseMenu_Settings_en-GB', sizes=((1920, 1080),), culture='en-GB')
    add('PerkChoice', 'LongText', 'PerkChoice_LongText_en-GB', sizes=((1920, 1080),), culture='en-GB',
        overrides={'PreviewPerkIDs': '@longest_existing_positive_cards'})
    return cases


def filtered_cases():
    selected = {item.strip() for item in os.environ.get('MC_PHASE1_VERIFY_ONLY', '').split(',') if item.strip()}
    cases = planned_cases()
    if selected:
        cases = [case for case in cases if case['id'] in selected or case['role'] in selected]
        require(cases, 'MC_PHASE1_VERIFY_ONLY matched no fixture IDs or roles')
    return cases, sorted(selected)


def write_report(report):
    require(ROOT.drive.upper() == 'E:', 'All QA output must remain on E:')
    OUT.mkdir(parents=True, exist_ok=True)
    report['updated_utc'] = utc_now()
    temporary = REPORT.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
    os.replace(temporary, REPORT)


def path_of(value):
    if value is None:
        return ''
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    if hasattr(value, 'to_string'):
        return value.to_string()
    return str(value)


def table_rows(u, table):
    require(isinstance(table, u.DataTable), 'Expected a loaded DataTable')
    text = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    rows = json.loads(text)
    require(isinstance(rows, list), 'Unexpected DataTable JSON: ' + table.get_path_name())
    return rows


def fingerprint_rows(rows):
    return hashlib.sha256(json.dumps(rows, ensure_ascii=False, sort_keys=True,
                                     separators=(',', ':')).encode('utf-8')).hexdigest()


def png_evidence(path, expected_size, started_ns):
    require(path.is_file(), 'Render returned success but no PNG exists: ' + str(path))
    metadata = path.stat()
    require(metadata.st_mtime_ns >= started_ns - 2_000_000_000, 'Output PNG is stale: ' + str(path))
    data = path.read_bytes()
    require(len(data) > 32 and data[:8] == b'\x89PNG\r\n\x1a\n', 'Invalid PNG: ' + str(path))
    size = list(struct.unpack('>II', data[16:24]))
    require(size == list(expected_size), 'PNG size mismatch: ' + str(path))
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'pixel_size': size}


@contextmanager
def preview_defaults(u, cls, values, report):
    defaults = u.get_default_object(cls)
    originals = {}
    record = {'class': cls.get_path_name(), 'fields': list(values), 'restored': False}
    report['preview_overrides'].append(record)
    try:
        for field, value in values.items():
            original = defaults.get_editor_property(field)
            if field == 'preview_booster_row':
                original = u.DataTableRowHandle(data_table=original.get_editor_property('data_table'),
                                               row_name=original.get_editor_property('row_name'))
            elif field == 'PreviewPerkIDs':
                original = list(original)
            originals[field] = original
            defaults.set_editor_property(field, value)
        yield
    finally:
        errors = []
        for field, value in reversed(tuple(originals.items())):
            try:
                defaults.set_editor_property(field, value)
                actual = defaults.get_editor_property(field)
                if hasattr(value, 'export_text'):
                    require(actual.export_text() == value.export_text(), 'Value differs after restoring ' + field)
                elif field == 'PreviewPerkIDs':
                    require(list(actual) == value, 'Array differs after restoring ' + field)
                else:
                    require(actual == value, 'Value differs after restoring ' + field)
            except Exception as exc:
                errors.append(field + ': ' + str(exc))
        record['restored'] = not errors
        if errors:
            report['cleanup_errors'].extend(errors)
        write_report(report)
        require(not errors, 'Preview CDO restore failed: ' + '; '.join(errors))


def main():
    import unreal as u

    require(ROOT.drive.upper() == 'E:', 'All QA output must remain on E:')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong Unreal project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Render QA requires idle editor')
    cases, selection = filtered_cases()
    TEMP.mkdir(parents=True, exist_ok=True)
    previous_temp = {key: os.environ.get(key) for key in ('TEMP', 'TMP')}
    report = {'status': 'in_progress', 'started_utc': utc_now(), 'selection': selection,
              'planned_count': len(cases), 'classes': {}, 'renders': [], 'tables': {},
              'preview_overrides': [], 'cleanup_errors': [], 'errors': [],
              'saved_assets': False, 'gameplay_actions': False, 'network_tests': False,
              'visual_approval': 'pending actual PNG review',
              'live_input_resize_navigation': 'not exercised by worldless static renders',
              'small_role_coverage': 'Component-sized crops only; no viewport/anchor claim.',
              'private_boss_fixtures': False}
    write_report(report)
    classes = {}
    initial_tables = {}
    fatal = None
    try:
        os.environ['TEMP'] = os.environ['TMP'] = str(TEMP)
        for role in sorted({case['role'] for case in cases}):
            native = u.load_class(None, '/Script/MessControl.' + NATIVE_ROLES[role])
            require(native is not None, 'Native role is unavailable: ' + role)
            cls = u.MCGameplayUIFactory.resolve_class(u.Name(role), native)
            require(cls is not None and cls.get_name().endswith('_Phase1_C'),
                    'Role must resolve an authored Phase1 WBP; refusing native/v10 fallback: ' + role + ' -> ' + path_of(cls))
            classes[role] = cls
            report['classes'][role] = cls.get_path_name()
        write_report(report)

        data = u.MCUIDataLibrary
        tables = {'booster': data.get_default_booster_table(), 'cards': data.get_default_card_table()}
        rows_by_kind = {}
        for kind, table in tables.items():
            rows = table_rows(u, table)
            rows_by_kind[kind] = rows
            initial_tables[kind] = (table, fingerprint_rows(rows))
            report['tables'][kind] = {'path': table.get_path_name(), 'rows': len(rows),
                                     'sha256_before': initial_tables[kind][1]}
        definitions_table = u.load_asset('/Game/Gameplay/Roguelike/DT_Perks')
        definitions = {row['Name']: row for row in table_rows(u, definitions_table)}
        candidates = []
        for row in rows_by_kind['cards']:
            identity = str(row.get('GameplayPerkID', ''))
            definition = definitions.get(identity)
            if not definition or 'Positive' not in str(definition.get('Polarity', 'Positive')):
                continue
            handle = u.DataTableRowHandle(data_table=tables['cards'], row_name=u.Name(row['Name']))
            content = data.resolve_card(handle, 'ru')
            if content.get_editor_property('found'):
                length = len(str(content.get_editor_property('description'))) + len(str(content.get_editor_property('display_name')))
                candidates.append((length, identity, row['Name']))
        longest = []
        for length, identity, row_name in sorted(candidates, reverse=True):
            if identity not in [entry['gameplay_id'] for entry in longest]:
                longest.append({'gameplay_id': identity, 'row': row_name, 'copy_length': length})
            if len(longest) == 3:
                break
        require(len(longest) == 3, 'Three valid positive card rows are required for actual LongText coverage')
        report['long_text_cards'] = longest

        booster_rows = [row for row in rows_by_kind['booster'] if str(row.get('GameplayPerkID')) == 'Fallback_Mouthwash']
        require(len(booster_rows) == 1, 'Expected one unambiguous Mouthwash presentation row')
        booster_row = booster_rows[0]
        booster_handle = u.DataTableRowHandle(data_table=tables['booster'], row_name=u.Name(booster_row['Name']))
        resolved = data.resolve_booster(booster_handle, 'en-GB')
        require(resolved.get_editor_property('found'), 'Existing booster row failed explicit-culture resolution')
        require(str(resolved.get_editor_property('display_name')).strip(), 'Booster row has no resolved title')
        report['booster_row'] = {'row': booster_row['Name'], 'gameplay_id': 'Fallback_Mouthwash',
                                 'image': str(booster_row.get('Image', '')),
                                 'culture': 'en-GB', 'resolved_title': str(resolved.get_editor_property('display_name')),
                                 'resolved_description': str(resolved.get_editor_property('description')),
                                 'used_fallback': bool(resolved.get_editor_property('used_fallback'))}
        hud_source = (ROOT / 'Source/MessControl/Private/MCGameplayHUD.cpp').read_text(encoding='utf-8-sig')
        preview_source = hud_source.split('void UMCGameplayHUD::ApplyEditorPreview', 1)[1].split('void UMCGameplayHUD::ResetEditorPreview', 1)[0]
        results_supported = all('TEXT("' + state + '")' in preview_source for state in ('ResultsVictory', 'ResultsDefeat'))
        report['results_preview_source_hook'] = results_supported
        write_report(report)

        for index, case in enumerate(cases, 1):
            role, state = case['role'], case['state']
            width, height = case['size']
            file = OUT / (case['id'] + '_' + case['culture'] + '_' + str(width) + 'x' + str(height) + '.png')
            entry = {key: value for key, value in case.items() if key != 'overrides'}
            entry.update(class_path=classes[role].get_path_name(), output=str(file), status='pending', ordinal=index)
            report['renders'].append(entry)
            write_report(report)
            if state in ('ResultsVictory', 'ResultsDefeat') and not results_supported:
                entry.update(status='unsupported', error='Current HUD ApplyEditorPreview hides ResultsPanel and has no named result-state hook; no misleading default PNG was exported.')
                write_report(report)
                continue
            values = {}
            for field, value in case['overrides'].items():
                if value == '@longest_existing_positive_cards':
                    value = [u.Name(item['gameplay_id']) for item in longest]
                elif value == '@valid_existing_booster_row':
                    value = booster_handle
                elif field in LONG_TUTORIAL:
                    value = u.Text(value)
                values[field] = value
            started = time.perf_counter()
            started_ns = time.time_ns()
            try:
                with preview_defaults(u, classes[role], values, report):
                    ok = u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(
                        classes[role], str(file), u.IntPoint(width, height), state, case['culture'], 1.0)
                    require(ok, 'Native preview renderer returned false: ' + str(file))
                    entry.update(png_evidence(file, case['size'], started_ns))
                entry.update(status='exported', seconds=round(time.perf_counter()-started, 3))
                u.log('MC_UI_PHASE1_RENDER ' + str(index) + '/' + str(len(cases)) + ' ' + file.name)
            except Exception as exc:
                entry.update(status='failed', error=str(exc), traceback=traceback.format_exc(),
                             seconds=round(time.perf_counter()-started, 3))
                report['errors'].append(case['id'] + ': ' + str(exc))
                u.log_error('MC_UI_PHASE1_RENDER_FAILED ' + case['id'] + ': ' + str(exc))
                if report['cleanup_errors']:
                    raise
            write_report(report)
    except Exception as exc:
        fatal = exc
        report['fatal_error'] = str(exc)
        report['fatal_traceback'] = traceback.format_exc()
    finally:
        for kind, (table, original_hash) in initial_tables.items():
            try:
                final_hash = fingerprint_rows(table_rows(u, table))
                report['tables'][kind].update(sha256_after=final_hash, content_unchanged=final_hash == original_hash)
                require(final_hash == original_hash, 'DT content changed unexpectedly: ' + table.get_path_name())
            except Exception as exc:
                report['cleanup_errors'].append(str(exc))
        for key, value in previous_temp.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        counts = {state: sum(entry['status'] == state for entry in report['renders'])
                  for state in ('exported', 'failed', 'unsupported', 'pending')}
        report['counts'] = counts
        report['finished_utc'] = utc_now()
        report['status'] = ('failed' if fatal is not None or counts['failed'] or report['cleanup_errors']
                            else 'incomplete' if counts['unsupported'] or counts['pending']
                            else 'render_exports_complete_visual_review_pending')
        write_report(report)
    require(not report['cleanup_errors'], 'Phase1 preview cleanup failed; inspect ' + str(REPORT))
    if fatal is not None:
        raise RuntimeError('Phase1 render matrix aborted: ' + str(fatal) + '; report: ' + str(REPORT)) from fatal
    require(not report['counts']['failed'], 'Phase1 render exports failed; inspect ' + str(REPORT))
    require(not report['counts']['unsupported'], 'Phase1 matrix is incomplete: result preview hooks are unavailable; inspect ' + str(REPORT))
    u.log('MC_UI_PHASE1_VERIFY_COMPLETE ' + json.dumps(report['counts']))
    return report


if __name__ == '__main__':
    if '--manifest' in sys.argv:
        cases, selection = filtered_cases()
        print(json.dumps({'selection': selection, 'cases': cases, 'count': len(cases)}, ensure_ascii=False, indent=2))
    else:
        main()
