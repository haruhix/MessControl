"""Targeted non-boss UI polish renders; explicitly run inside an idle editor.

--manifest works without Unreal. This script never starts an editor, PIE, build,
network session or asset save. It renders actual authored Phase1 Slate trees.
EN/DE stress strings live only in three newly created, unsaved QA DataTables;
original DTs are never edited. Authored BP CDOs, theme refs/palette, process temp
paths and editor culture are preserved/restored in finally. Do not run beside
another authoring/save operation. PNG export success is not visual approval.

The optional eighteenth composite needs the preview-only HUD state PolishStress
(low reserves + long hint + alert + all booster states, no encounter preview).
An absent source hook is reported unsupported; no misleading default is drawn.
"""
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import traceback


ROOT = Path(__file__).resolve().parents[3]
BASE = '/Game/Gameplay/UI'
OUT = ROOT / 'Saved/GameplayUI/Polish/Previews'
TEMP = Path('E:/CodexTemp/UI-Polish')
QA_BASE = BASE + '/_UIPolishQA'
ROLES = {
    'GameplayHUD': 'MCGameplayHUD', 'PerkChoice': 'MCPerkChoiceWidget',
    'PauseMenu': 'MCGameplayMenuWidget', 'Scoreboard': 'MCScoreboardWidget',
    'Emotes': 'MCEmoteWidget', 'Booster': 'MCBoosterWidget',
    'Tutorial': 'MCTutorialWidget',
}
# Deliberately long QA copy, not approved shipping translations. Format arguments
# remain identical to the real row and are checked before table creation.
COPY = {
    'UI.HUD.ReserveTitle': ('RESERVE TOOTH HEALTH · EN QA', 'GESUNDHEIT DER RESERVEZÄHNE · DE QA'),
    'UI.HUD.Cleanliness': ('OVERALL MOUTH CLEANLINESS', 'SAUBERKEIT DES GESAMTEN MUNDES'),
    'UI.HUD.UntilEvent': ('TIME UNTIL THE NEXT EVENT', 'ZEIT BIS ZUM NÄCHSTEN EREIGNIS'),
    'UI.HUD.Task': ('CURRENT TEAM OBJECTIVE', 'AKTUELLE GEMEINSAME AUFGABE'),
    'UI.HUD.Tool.Brush': ('PRECISION CLEANING BRUSH', 'PRÄZISIONSREINIGUNGSBÜRSTE'),
    'UI.HUD.BrushAction': ('HOLD LMB · CLEAN THE TOOTH AND HELP YOUR TEAM', 'LMT HALTEN · ZAHN REINIGEN UND DEM TEAM HELFEN'),
    'UI.HUD.Preview.LongHint': ('HOLD E · RESCUE YOUR TEAMMATE FROM THE MIMIC AND RETURN TO SAFETY TOGETHER', 'E HALTEN · TEAMMITGLIED AUS DEM MIMIK RETTEN UND GEMEINSAM IN SICHERHEIT ZURÜCKKEHREN'),
    'UI.HUD.Preview.TeamXP': ('TEAM LEVEL 2 · 35 / 75 EXPERIENCE', 'TEAMSTUFE 2 · 35 / 75 ERFAHRUNGSPUNKTE'),
    'UI.Stamina.Title': ('REMAINING ENDURANCE', 'VERBLEIBENDE AUSDAUER'),
    'UI.Menu.Resume': ('CONTINUE THE CURRENT RUN · EN QA', 'LAUFENDEN DURCHGANG FORTSETZEN · DE QA'),
    'UI.Menu.Emotes': ('EXPRESSIONS AND TEAM COMMUNICATION', 'EMOTIONEN UND KOMMUNIKATION IM TEAM'),
    'UI.Menu.Settings': ('DISPLAY AND PERFORMANCE SETTINGS', 'ANZEIGE- UND LEISTUNGSEINSTELLUNGEN'),
    'UI.Menu.Leave': ('RETURN TO THE MAIN MENU', 'ZUM HAUPTMENÜ ZURÜCKKEHREN'),
    'UI.Menu.Exit': ('CLOSE MESS CONTROL COMPLETELY', 'MESS CONTROL VOLLSTÄNDIG BEENDEN'),
    'UI.Menu.NetworkNote': ('The online run continues while this menu is open. Your teammates still need help.', 'Der gemeinsame Online-Durchgang läuft weiter, solange dieses Menü geöffnet ist. Deine Teammitglieder benötigen weiterhin Hilfe.'),
    'UI.Menu.Resolution': ('DISPLAY RESOLUTION', 'BILDSCHIRMAUFLÖSUNG'),
    'UI.Menu.WindowMode': ('DISPLAY PRESENTATION MODE', 'BILDSCHIRMDARSTELLUNGSMODUS'),
    'UI.Menu.Quality': ('GRAPHICS QUALITY PRESET', 'GRAFIKQUALITÄTSEINSTELLUNGEN'),
    'UI.Menu.VSync': ('VERTICAL SYNCHRONIZATION', 'VERTIKALE BILDSYNCHRONISATION'),
    'UI.Menu.Apply': ('APPLY AND SAVE THESE SETTINGS', 'DIESE EINSTELLUNGEN ANWENDEN UND SPEICHERN'),
    'UI.Common.Back': ('RETURN TO THE PREVIOUS SCREEN', 'ZUM VORHERIGEN BILDSCHIRM ZURÜCK'),
}
CARD_COPY = {
    'X01': (
        ('Delicate glass enamel with a considerable price · EN QA',
         'Weapon damage increases by 30%, but maximum health decreases by 25%. '
         'This damage bonus does not improve cleaning. Coordinate with teammates before taking risks. '
         'The price remains active for the rest of this run; inspect the pinned stack summary before choosing.'),
        ('Empfindlicher Glasschmelz mit einem beträchtlichen Preis · DE QA',
         'Der Waffenschaden steigt um 30%, aber die maximale Gesundheit sinkt um 25%. '
         'Dieser Schadensbonus verbessert die Reinigung nicht. Stimme dich vor gefährlichen Aktionen mit deinem Team ab. '
         'Der Nachteil bleibt bis zum Ende dieses Durchgangs bestehen; prüfe vor der Auswahl die angeheftete Stapelübersicht.')),
    'X02': (
        ('Extraordinary strength, slower and more deliberate swings',
         'Pickaxe damage increases by 35%, but the frequency of its strikes decreases by 20%. '
         'Strong individual hits demand careful timing. Read the complete effect before selecting this permanent trade-off.'),
        ('Außergewöhnliche Schlagkraft bei langsameren, bedachten Schlägen',
         'Der Spitzhackenschaden steigt um 35%, während die Schlagfrequenz um 20% sinkt. '
         'Kräftige Einzeltreffer erfordern sorgfältiges Timing. Lies vor der Auswahl die vollständige Beschreibung dieses dauerhaften Kompromisses.')),
    'X04': (
        ('Wide cleaning brush with reduced dirt-removal efficiency',
         'The working width of the brush increases by 40%, while dirt-removal efficiency decreases by 20%. '
         'Cover more surface while keeping enough endurance for the rest of the team objective.'),
        ('Breite Reinigungsbürste mit verringerter Schmutzentfernungseffizienz',
         'Die Arbeitsbreite der Bürste steigt um 40%, während ihre Schmutzentfernungseffizienz um 20% sinkt. '
         'Reinige größere Flächen und behalte genügend Ausdauer für die übrige gemeinsame Aufgabe.')),
}
TUTORIAL_COPY = {
    'preview_title': 'Проверка длинного урока: помощь команде и безопасная очистка труднодоступных поверхностей',
    'preview_dialogue': ('Это синтетический текст проверки вёрстки, не новый сценарий обучения. '
                         'Выбери щётку, очисти подсвеченную поверхность и помоги товарищу. '
                         'Следи за выносливостью и общим прогрессом. Описание должно сохранять читаемый размер шрифта. '
                         'Длинное содержимое остаётся доступным в прокрутке; статический кадр не проверяет прокрутку до конца.\n') * 3,
    'preview_instruction': '1 — щётка. Удерживай ЛКМ у подсвеченного зуба, затем помоги товарищу и дождись общей готовности команды.',
}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def planned_cases():
    cases = []
    def add(role, state, culture='ru', size=(1280, 720), identity=None, overrides=None):
        cases.append(dict(id=identity or role + '_' + state, role=role, state=state,
                          culture=culture, size=list(size), overrides=overrides or {}))
    for role, state in (('GameplayHUD', 'Default'), ('PerkChoice', 'Risky'),
                        ('PerkChoice', 'ContractConfirm'), ('PauseMenu', 'Default'),
                        ('Scoreboard', 'LongText'), ('Emotes', 'Default')):
        add(role, state)
    for role, state in (('GameplayHUD', 'Default'), ('PerkChoice', 'Risky'), ('Scoreboard', 'LongText')):
        add(role, state, size=(1920, 1080))
    for culture in ('en-GB', 'de-DE'):
        add('GameplayHUD', 'LongText', culture)
        add('PauseMenu', 'Settings' if culture == 'de-DE' else 'Default', culture)
        add('PerkChoice', 'Risky', culture)
    add('Booster', 'AllStates', size=(640, 360))
    add('Tutorial', 'LongText', overrides=TUTORIAL_COPY)
    add('GameplayHUD', 'PolishStress', identity='HUD_LowResources_Alert_BoosterAllStates')
    require(len(cases) == 18, 'Targeted matrix must remain exactly 18 PNGs maximum')
    return cases


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True,
                                     separators=(',', ':')).encode('utf-8')).hexdigest()


def rows(u, table):
    require(isinstance(table, u.DataTable), 'Expected an existing DataTable')
    return json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def saved_ui_packages():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted((ROOT / 'Content/Gameplay/UI').rglob('*'))
            if p.is_file() and p.suffix in ('.uasset', '.uexp', '.ubulk')}


def write_report(report):
    OUT.mkdir(parents=True, exist_ok=True)
    report['updated_utc'] = datetime.now(timezone.utc).isoformat()
    path = OUT / 'report.json'
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
    os.replace(temporary, path)


def encoded(value):
    if hasattr(value, 'export_text'):
        return value.export_text()
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    return str(value)


@contextmanager
def authored_defaults(u, cls, values, report):
    require(cls.get_path_name().startswith(BASE + '/WBP_') and cls.get_name().endswith('_Phase1_C'),
            'Only authored Phase1 Blueprint CDO overrides are allowed')
    cdo = u.get_default_object(cls)
    original = {}
    record = dict(class_path=cls.get_path_name(), fields=list(values), restored=False)
    report['cdo_overrides'].append(record)
    try:
        for key, value in values.items():
            old = cdo.get_editor_property(key)
            original[key] = (old.copy() if hasattr(old, 'copy') else old, encoded(old))
            cdo.set_editor_property(key, value)
        yield
    finally:
        errors = []
        for key, (value, text) in reversed(tuple(original.items())):
            try:
                cdo.set_editor_property(key, value)
                require(encoded(cdo.get_editor_property(key)) == text, 'CDO restore mismatch: ' + key)
            except Exception as exc:
                errors.append(str(exc))
        record['restored'] = not errors
        report['cleanup_errors'].extend(errors)
        require(not errors, 'CDO restoration failed: ' + '; '.join(errors))


def localized_fixture(source_rows):
    result = json.loads(json.dumps(source_rows))
    indexed = {row['Name']: row for row in result}
    for key, translations in COPY.items():
        require(key in indexed, 'Stress localization key is absent: ' + key)
        localizations = indexed[key].setdefault('Localizations', {})
        for culture, text in zip(('en', 'de'), translations):
            require(set(re.findall(r'\{[^{}]+\}', indexed[key]['SourceText'])) ==
                    set(re.findall(r'\{[^{}]+\}', text)), 'Format arguments changed: ' + key)
            localizations[culture] = text
    return result


def card_fixture(source_rows):
    result = json.loads(json.dumps(source_rows))
    for identity, translations in CARD_COPY.items():
        matching = [row for row in result if row.get('GameplayPerkID') == identity]
        require(len(matching) == 1, 'Ambiguous/missing stress card: ' + identity)
        for culture, (title, description) in zip(('en', 'de'), translations):
            matching[0].setdefault('Localizations', {})[culture] = dict(DisplayName=title, Description=description)
    return result


def main():
    import unreal as u

    require(ROOT.drive.upper() == 'E:', 'All output must remain on E:')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Idle editor required')
    lib = u.EditorAssetLibrary
    api = u.MCGameplayUIEditorLibrary
    cases = planned_cases()
    theme = u.load_asset(BASE + '/Theme/DA_GameplayUITheme')
    require(theme is not None, 'Master theme is unavailable')
    table_specs = {
        'localization_table': ('DT_UI_Localization', 'MCUILocalizationRow'),
        'card_table': ('DT_UI_Cards', 'MCUICardRow'),
        'booster_table': ('DT_UI_Boosters', 'MCUIBoosterRow'),
    }
    original_tables = {field: u.load_asset(BASE + '/Data/' + name)
                       for field, (name, _) in table_specs.items()}
    source_rows = {field: rows(u, table) for field, table in original_tables.items()}
    # Loaded TSoftObjectPtr properties are exposed as UObjects by this bridge.
    references = {field: theme.get_editor_property(field) for field in table_specs}
    palette = theme.get_editor_property('palette').copy()
    fonts = {field: encoded(theme.get_editor_property(field))
             for field in ('body_font', 'heading_font', 'display_font', 'compact_heading_font')}
    def culture():
        return dict(language=u.InternationalizationLibrary.get_current_language(),
                    locale=u.InternationalizationLibrary.get_current_locale())
    before_culture = culture()
    before_disk = saved_ui_packages()
    previous_temp = {key: os.environ.get(key) for key in ('TEMP', 'TMP')}
    created = []
    fixture_tables = {}
    report = dict(status='running', planned_count=len(cases), renders=[], classes={},
                  cdo_overrides=[], errors=[], cleanup_errors=[], assets_saved=False,
                  shipping_translations_authored=False, engine_or_build_started=False,
                  live_gameplay_or_network_test=False, visual_approval='pending PNG inspection',
                  culture_before=before_culture,
                  font_fields_checked=list(fonts),
                  limitations=['No mouse/gamepad navigation or scroll-to-end proof.',
                               'Booster is a component-sized crop, not a viewport anchor test.',
                               'EN/DE are synthetic stress translations, not shipping localization.'])
    failure = None
    try:
        TEMP.mkdir(parents=True, exist_ok=True)
        os.environ['TEMP'] = os.environ['TMP'] = str(TEMP)
        for role, native_name in ROLES.items():
            native = u.load_class(None, '/Script/MessControl.' + native_name)
            require(native is not None, 'Native role is unavailable: ' + role)
            cls = u.MCGameplayUIFactory.resolve_class(u.Name(role), native)
            require(cls is not None and cls.get_path_name().startswith(BASE + '/WBP_')
                    and cls.get_name().endswith('_Phase1_C'), 'Refusing native/v10 fallback: ' + role)
            report['classes'][role] = cls.get_path_name()
        classes = {role: u.load_class(None, path) for role, path in report['classes'].items()}
        require(all(classes.values()), 'One or more authored classes failed to load')
        # Preflight every absent path before creating any temporary object.
        for name, _ in table_specs.values():
            require(not lib.does_asset_exist(QA_BASE + '/' + name), 'Preserving occupied QA path: ' + name)
            require(not (ROOT / 'Content/Gameplay/UI/_UIPolishQA' / (name + '.uasset')).exists(),
                    'A saved QA package already exists: ' + name)
        for field, (name, struct_name) in table_specs.items():
            factory = u.DataTableFactory()
            factory.set_editor_property('struct', u.load_object(None, '/Script/MessControl.' + struct_name))
            path = QA_BASE + '/' + name
            table = u.AssetToolsHelpers.get_asset_tools().create_asset(name, QA_BASE, u.DataTable, factory)
            require(table is not None, 'Could not create unsaved QA table: ' + path)
            created.append(path)
            fixture_tables[field] = table
            values = localized_fixture(source_rows[field]) if field == 'localization_table' else (
                card_fixture(source_rows[field]) if field == 'card_table' else source_rows[field])
            require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(
                table, json.dumps(values, ensure_ascii=False)), 'Failed to fill QA DT: ' + path)
        # Strong Python references + loaded soft paths keep the unsaved tables
        # available for both the authored root and inherited composed children.
        for field, table in fixture_tables.items():
            theme.set_editor_property(field, table)
        theme.notify_theme_changed()
        report['temporary_tables'] = {field: table.get_path_name() for field, table in fixture_tables.items()}
        report['stress_copy'] = COPY
        report['stress_cards'] = CARD_COPY
        # Resolver assertions establish inputs only. End-to-end evidence is the
        # actual authored WBP PNG below; visually inspect EN QA / DE QA markers.
        for requested, parent in (('en-GB', 'en'), ('de-DE', 'de')):
            for key, translations in COPY.items():
                actual = str(u.MCUIDataLibrary.resolve_text(fixture_tables['localization_table'], u.Name(key), u.Text(''), requested))
                require(actual == translations[0 if parent == 'en' else 1], 'Temporary translation did not resolve: ' + key)
            for identity, translations in CARD_COPY.items():
                actual = u.MCUIDataLibrary.find_card_for_gameplay_id(fixture_tables['card_table'], u.Name(identity), requested)
                expected = translations[0 if parent == 'en' else 1]
                require(str(actual.get_editor_property('display_name')) == expected[0]
                        and str(actual.get_editor_property('description')) == expected[1], 'Temporary card did not resolve: ' + identity)
        source = (ROOT / 'Source/MessControl/Private/MCGameplayHUD.cpp').read_text('utf-8-sig')
        preview = source.split('void UMCGameplayHUD::ApplyEditorPreview', 1)[1].split('void UMCGameplayHUD::ResetEditorPreview', 1)[0]
        composite_supported = 'TEXT("PolishStress")' in preview
        report['composite_source_hook_present'] = composite_supported
        for case in cases:
            role, state, language, size = case['role'], case['state'], case['culture'], case['size']
            entry = {key: value for key, value in case.items() if key != 'overrides'}
            file = OUT / (case['id'] + '_' + language + '_' + str(size[0]) + 'x' + str(size[1]) + '.png')
            entry.update(output=str(file), class_path=report['classes'][role], status='pending')
            report['renders'].append(entry)
            write_report(report)
            if state == 'PolishStress' and not composite_supported:
                entry.update(status='unsupported', error='Requires HUD PolishStress preview-only composition hook; no default PNG substituted.')
                continue
            values = {key: u.Text(value) for key, value in case['overrides'].items()}
            if role == 'PerkChoice':
                values.update(card_table=fixture_tables['card_table'], localization_table=fixture_tables['localization_table'])
            if role == 'Booster':
                values['booster_table'] = fixture_tables['booster_table']
            try:
                with authored_defaults(u, classes[role], values, report):
                    started = time.time_ns()
                    ok = api.render_gameplay_ui_preview(classes[role], str(file), u.IntPoint(*size), state, language, 1.0)
                    require(ok and file.is_file(), 'Actual authored widget render failed: ' + str(file))
                    data = file.read_bytes()
                    require(file.stat().st_mtime_ns >= started - 2_000_000_000, 'Stale PNG: ' + str(file))
                    require(data[:8] == b'\x89PNG\r\n\x1a\n' and list(struct.unpack('>II', data[16:24])) == size,
                            'Invalid/wrong-size PNG: ' + str(file))
                    entry.update(status='exported', bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                                 copy_verification='Actual authored Slate tree exported; inspect visible stress markers for localized cases.')
            except Exception as exc:
                entry.update(status='failed', error=str(exc), traceback=traceback.format_exc())
                report['errors'].append(str(exc))
                if report['cleanup_errors']:
                    raise
            write_report(report)
    except Exception as exc:
        failure = exc
        report['fatal_error'] = str(exc)
        report['fatal_traceback'] = traceback.format_exc()
    finally:
        try:
            for field, value in references.items():
                theme.set_editor_property(field, value)
            theme.set_editor_property('palette', palette)
            theme.notify_theme_changed()
            report['theme_restored'] = all(encoded(theme.get_editor_property(field)) == encoded(value)
                                           for field, value in references.items()) and encoded(theme.get_editor_property('palette')) == encoded(palette)
            report['fonts_unchanged'] = all(encoded(theme.get_editor_property(field)) == text for field, text in fonts.items())
            require(report['theme_restored'] and report['fonts_unchanged'], 'Theme restoration mismatch')
        except Exception as exc:
            report['cleanup_errors'].append('Theme: ' + str(exc))
        if report.get('theme_restored') and all(record['restored'] for record in report['cdo_overrides']):
            for path in reversed(created):
                try:
                    require(path.startswith(QA_BASE + '/'), 'Unexpected cleanup path')
                    if lib.does_asset_exist(path):
                        require(lib.delete_asset(path), 'Could not remove owned unsaved QA table: ' + path)
                    require(not lib.does_asset_exist(path), 'QA table still exists: ' + path)
                except Exception as exc:
                    report['cleanup_errors'].append(str(exc))
        try:
            report['original_tables_unchanged'] = all(fingerprint(rows(u, original_tables[field])) == fingerprint(value)
                                                       for field, value in source_rows.items())
            report['saved_packages_unchanged'] = saved_ui_packages() == before_disk
            # No global culture setter is used. Restore external changes only
            # if an unexpected renderer side effect changed the current values.
            if culture() != before_culture:
                u.InternationalizationLibrary.set_current_language(before_culture['language'], False)
                u.InternationalizationLibrary.set_current_locale(before_culture['locale'], False)
            report['culture_after'] = culture()
            report['culture_restored'] = culture() == before_culture
            require(report['original_tables_unchanged'] and report['saved_packages_unchanged']
                    and report['culture_restored'], 'DT/disk/culture preservation failed')
        except Exception as exc:
            report['cleanup_errors'].append('Preservation: ' + str(exc))
        for key, value in previous_temp.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        report['counts'] = {state: sum(case['status'] == state for case in report['renders'])
                            for state in ('exported', 'failed', 'unsupported', 'pending')}
        report['status'] = ('failed' if failure or report['errors'] or report['cleanup_errors'] else
                            'incomplete' if report['counts']['unsupported'] or report['counts']['pending'] else
                            'render_exports_complete_visual_inspection_pending')
        write_report(report)
    require(not report['cleanup_errors'], 'QA restoration failed; inspect ' + str(OUT / 'report.json'))
    if failure:
        raise failure
    require(not report['errors'], 'One or more actual widget renders failed')
    return report


if __name__ == '__main__':
    if '--manifest' in sys.argv:
        print(json.dumps(dict(count=18, cases=planned_cases()), ensure_ascii=False, indent=2))
    else:
        main()
