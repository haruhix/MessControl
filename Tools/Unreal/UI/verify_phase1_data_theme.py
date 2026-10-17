"""Six deferred worldless Phase1 data/theme fixtures; run in the idle editor.

Reads the three saved DTs without editing them. A single unsaved QA card DT
copies an existing row under an arbitrary name and borrows another existing
card image, proving that preview art comes from the selected row. No new
translations are authored. Authored card CDO fields and the complete master
palette are restored in finally. No asset save, culture switch, PIE or build.
"""
import hashlib
import json
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
BASE = '/Game/Gameplay/UI'
OUT = ROOT / 'Saved/GameplayUI/Phase1/ThemePreviews'
QA_TABLE = BASE + '/_Phase1QA/DT_QA_Phase1CardPreview'
QA_ROW = 'AnyArtistRow_Phase1_ActualImage'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def object_path(value):
    if value is None:
        return ''
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    value = value.to_string() if hasattr(value, 'to_string') else str(value)
    if "'" in value:
        value = value.split("'", 2)[1]
    return value


def main():
    import unreal as u

    require(ROOT.drive.upper() == 'E:', 'Outputs must remain on E:')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong Unreal project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Requires idle editor')
    temporary = Path('E:/CodexTemp/GameplayUIPhase1')
    temporary.mkdir(parents=True, exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(temporary)
    OUT.mkdir(parents=True, exist_ok=True)
    lib = u.EditorAssetLibrary
    api = u.MCGameplayUIEditorLibrary
    require(not lib.does_asset_exist(QA_TABLE), 'Preserving existing QA path: ' + QA_TABLE)
    theme = u.load_asset(BASE + '/Theme/DA_GameplayUITheme')
    require(theme is not None, 'Missing master theme')
    # Native-class NewObject initialization does not reliably propagate edited
    # native CDO fields. The authored BP renderer refreshes its custom property
    # list before creating the fixture, so this override reaches the real widget.
    card_class_path = BASE + '/WBP_PerkCard_Phase1.WBP_PerkCard_Phase1_C'
    card_class = u.load_class(None, card_class_path)
    require(card_class is not None, 'Authored Phase1 card class is not loaded')
    defaults = u.get_default_object(card_class)
    original_row_value = defaults.get_editor_property('preview_card_row')
    original_row = u.DataTableRowHandle(data_table=original_row_value.get_editor_property('data_table'),
                                      row_name=original_row_value.get_editor_property('row_name'))
    original_read_only = defaults.get_editor_property('preview_read_only')
    original_palette = theme.get_editor_property('palette').copy()
    original_palette_text = original_palette.export_text()
    original_fonts = {key: theme.get_editor_property(key).export_text()
                      for key in ('body_font', 'heading_font', 'display_font')}

    def cultures():
        return {'language': u.InternationalizationLibrary.get_current_language(),
                'locale': u.InternationalizationLibrary.get_current_locale()}

    original_cultures = cultures()
    table_names = ('DT_UI_Localization', 'DT_UI_Boosters', 'DT_UI_Cards')
    tables = {name: u.load_asset(BASE + '/Data/' + name) for name in table_names}
    require(all(isinstance(table, u.DataTable) for table in tables.values()), 'Three existing UI tables are required')

    def table_snapshot(table):
        rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        encoded = json.dumps(rows, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
        return {'path': table.get_path_name(), 'rows': len(rows), 'sha256': hashlib.sha256(encoded).hexdigest()}

    before_tables = {name: table_snapshot(table) for name, table in tables.items()}
    # Check only participating saved packages, not the whole Content tree.
    packages = [BASE + '/Data/' + name for name in table_names]
    packages += [BASE + '/Theme/DA_GameplayUITheme', BASE + '/WBP_GameplayHUD_Phase1',
                 BASE + '/WBP_PauseMenu_Phase1', BASE + '/WBP_PerkCard_Phase1']

    def disk_snapshot():
        result = {}
        for package in packages:
            path = (ROOT / 'Content' / package[len('/Game/'):]).with_suffix('.uasset')
            require(path.is_file(), 'Expected saved package is missing: ' + str(path))
            result[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        return result

    before_disk = disk_snapshot()
    report = {'tables_before': before_tables, 'booster_resolves': [], 'card_resolves': [],
              'renders': [], 'cleanup_errors': [], 'assets_saved': False,
              'new_translations_authored': False, 'visual_acceptance': 'pending',
              'culture_before': original_cultures,
              'card_render_class': card_class_path,
              'card_widget_visual_check': 'pending: must show X01 name/description and risky badge with P03 art; resolver success alone is insufficient'}
    created = False
    fixture_table = None
    failure = None

    def resolved_snapshot(value):
        return {'found': bool(value.get_editor_property('found')),
                'used_fallback': bool(value.get_editor_property('used_fallback')),
                'image': object_path(value.get_editor_property('image')),
                'display_name': str(value.get_editor_property('display_name')),
                'description': str(value.get_editor_property('description'))}

    def render(cls, name, state, culture='ru', size=(1920, 1080)):
        require(len(report['renders']) < 6, 'QA is limited to six PNGs')
        file = OUT / (name + '.png')
        ok = api.render_gameplay_ui_preview(cls, str(file), u.IntPoint(*size), state, culture, 1.0)
        report['renders'].append({'file': str(file), 'state': state, 'culture': culture,
                                  'size': list(size), 'ok': bool(ok)})
        require(ok, 'Render failed: ' + str(file))

    try:
        # Both cultures resolve existing authored copy. Missing en/en-GB is
        # expected to use the unchanged Russian source, never invented English.
        for row_name in u.DataTableFunctionLibrary.get_data_table_row_names(tables['DT_UI_Boosters']):
            handle = u.DataTableRowHandle(data_table=tables['DT_UI_Boosters'], row_name=row_name)
            for culture in ('ru', 'en-GB'):
                value = resolved_snapshot(u.MCUIDataLibrary.resolve_booster(handle, culture))
                require(value['found'], 'Existing booster did not resolve: ' + str(row_name))
                report['booster_resolves'].append(dict(value, row=str(row_name), culture=culture))

        # Keep the real X01 metadata but display the image authored in P03.
        # A hard-coded gameplay-ID art switch would visibly fail this fixture.
        card_rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(tables['DT_UI_Cards']))
        source = next((row for row in card_rows if row.get('GameplayPerkID') == 'X01'), None)
        alternate = next((row for row in card_rows if row.get('GameplayPerkID') == 'P03'), None)
        require(source is not None and alternate is not None, 'X01/P03 fixture source rows are required')
        fixture_row = dict(source)
        fixture_row['Name'] = QA_ROW
        fixture_row['Image'] = alternate['Image']
        image = u.load_asset(object_path(fixture_row['Image']))
        require(isinstance(image, u.Texture2D), 'Fixture must use a real existing Texture2D')
        expected_image = image.get_path_name()
        factory = u.DataTableFactory()
        factory.set_editor_property('struct', u.load_object(None, '/Script/MessControl.MCUICardRow'))
        # Exact absent path reserved before creation; no save is requested.
        created = True
        fixture_table = u.AssetToolsHelpers.get_asset_tools().create_asset(
            QA_TABLE.rsplit('/', 1)[1], QA_TABLE.rsplit('/', 1)[0], u.DataTable, factory)
        require(fixture_table is not None, 'Could not create unsaved QA table')
        require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(
            fixture_table, json.dumps([fixture_row], ensure_ascii=False)), 'Could not populate QA card row')
        selected = u.DataTableRowHandle(data_table=fixture_table, row_name=QA_ROW)
        defaults.set_editor_property('preview_card_row', selected)
        defaults.set_editor_property('preview_read_only', True)
        for culture in ('ru', 'en-GB'):
            value = resolved_snapshot(u.MCUIDataLibrary.resolve_card(selected, culture))
            original = resolved_snapshot(u.MCUIDataLibrary.resolve_card(
                u.DataTableRowHandle(data_table=tables['DT_UI_Cards'], row_name=source['Name']), culture))
            require(value['found'] and value['image'] == expected_image, 'Selected-row image was overridden')
            require(value['display_name'] == original['display_name'] and value['description'] == original['description'],
                    'Selected-row localized copy was overridden')
            report['card_resolves'].append(dict(value, row=QA_ROW, gameplay_id='X01', culture=culture))
            render(card_class, 'Card_ArbitraryRow_' + culture, 'Default', culture, (420, 580))

        hud = u.load_class(None, BASE + '/WBP_GameplayHUD_Phase1.WBP_GameplayHUD_Phase1_C')
        pause = u.load_class(None, BASE + '/WBP_PauseMenu_Phase1.WBP_PauseMenu_Phase1_C')
        require(hud is not None and pause is not None, 'Phase1 HUD/Pause classes are required')
        render(hud, 'Theme_Before_HUD', 'ThreeBosses')
        render(pause, 'Theme_Before_Pause', 'Default')
        changed = original_palette.copy()
        changed.set_editor_property('stamina', u.LinearColor(1.0, .025, .55, .35))
        changed.set_editor_property('button', u.LinearColor(.95, .21, .025, .35))
        theme.set_editor_property('palette', changed)
        theme.notify_theme_changed()
        report['mutation'] = {'stamina': [1.0, .025, .55, .35], 'button': [.95, .21, .025, .35]}
        render(hud, 'Theme_RGBA035_HUD', 'ThreeBosses')
        render(pause, 'Theme_RGBA035_Pause', 'Default')
    except Exception as error:
        failure = error
        report['error'] = str(error)
    finally:
        try:
            defaults.set_editor_property('preview_card_row', original_row)
            defaults.set_editor_property('preview_read_only', original_read_only)
            restored_row = defaults.get_editor_property('preview_card_row')
            report['cdo_restored'] = (restored_row.get_editor_property('data_table') == original_row.get_editor_property('data_table')
                                      and restored_row.get_editor_property('row_name') == original_row.get_editor_property('row_name')
                                      and defaults.get_editor_property('preview_read_only') == original_read_only)
            require(report['cdo_restored'], 'Authored card CDO restoration failed')
        except Exception as error:
            report['cleanup_errors'].append('Card CDO: ' + str(error))
        try:
            theme.set_editor_property('palette', original_palette)
            theme.notify_theme_changed()
            report['palette_restored'] = theme.get_editor_property('palette').export_text() == original_palette_text
            report['fonts_unchanged'] = all(theme.get_editor_property(key).export_text() == value for key, value in original_fonts.items())
            require(report['palette_restored'] and report['fonts_unchanged'], 'Master theme restoration failed')
        except Exception as error:
            report['cleanup_errors'].append('Theme: ' + str(error))
        if created and report.get('cdo_restored'):
            try:
                if lib.does_asset_exist(QA_TABLE):
                    require(lib.delete_asset(QA_TABLE), 'Could not remove exact unsaved QA table')
                report['temporary_table_removed'] = not lib.does_asset_exist(QA_TABLE)
                require(report['temporary_table_removed'], 'QA table remains loaded')
            except Exception as error:
                report['cleanup_errors'].append('QA table: ' + str(error))
        try:
            report['tables_after'] = {name: table_snapshot(table) for name, table in tables.items()}
            report['tables_unchanged'] = report['tables_after'] == before_tables
            report['saved_packages_unchanged'] = disk_snapshot() == before_disk
            report['culture_after'] = cultures()
            report['culture_unchanged'] = report['culture_after'] == original_cultures
            report['content_unchanged'] = report['tables_unchanged'] and report['saved_packages_unchanged']
            require(report['content_unchanged'] and report['culture_unchanged'], 'Data/disk/culture changed during QA')
        except Exception as error:
            report['cleanup_errors'].append('Preservation checks: ' + str(error))
        report['status'] = 'passed' if failure is None and not report['cleanup_errors'] else 'failed'
        (OUT / 'data_theme_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    require(not report['cleanup_errors'], 'QA cleanup failed: ' + '; '.join(report['cleanup_errors']))
    if failure is not None:
        raise failure
    u.log('MC_UI_PHASE1_DATA_THEME_PASS ' + json.dumps(report, ensure_ascii=False))
    return report


if __name__ == '__main__':
    main()
