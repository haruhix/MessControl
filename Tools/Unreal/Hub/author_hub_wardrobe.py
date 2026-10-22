"""Backed-up authoring of the editable Hub wardrobe and empty vending-machine UI.

Default run('plan') saves only E: evidence. Explicit run('apply') requires the
rebuilt native classes and a clean idle Editor. No build, PIE, map switching,
SaveAll, purchases, currencies, source-mesh changes or gameplay data edits.
Optional station placements use explicit inspected location/rotation/extent;
Hub must already be open. Existing station transforms and WBP trees survive.
"""
from __future__ import annotations

from datetime import datetime, timezone
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys
import traceback
import uuid

ROOT = Path(__file__).resolve().parents[3]
HUB = '/Game/Maps/Hub'
THEME = '/Game/Gameplay/UI/Theme/DA_GameplayUITheme'
TEXTS = '/Game/Gameplay/UI/Data/DT_UI_Localization'
OWNER_TAG = 'MC.HubWardrobeShop.AuthoringV1'
SPECS = (
    ('Wardrobe', '/Game/Gameplay/UI/WBP_HubWardrobe', 'MCHubWardrobeWidget'),
    ('Shop', '/Game/Gameplay/UI/WBP_HubShop', 'MCHubShopWidget'),
)
COPY = (
    ('UI.Wardrobe.Title', 'ГАРДЕРОБ', 'WARDROBE'),
    ('UI.Wardrobe.Subtitle', 'Внешний вид', 'Appearance'),
    ('UI.Wardrobe.Skin', 'Скин', 'Skin'),
    ('UI.Wardrobe.DefaultSkin', 'Стандартный', 'Default'),
    ('UI.Wardrobe.EmptySlot', 'Пусто', 'Empty'),
    ('UI.Wardrobe.BagAndBelt', 'Цвет сумки и пояса', 'Bag and belt color'),
    ('UI.Wardrobe.Hint', 'RGB · 0–255. Изменяется только внешний вид.', 'RGB · 0–255. Appearance only.'),
    ('UI.Wardrobe.PaletteHint', 'Выбери оттенок на полоске и цвет на палитре. Изменяется только внешний вид.',
        'Choose a hue on the strip, then a color on the palette. Appearance only.'),
    ('UI.Wardrobe.Apply', 'ПРИМЕНИТЬ', 'APPLY'),
    ('UI.Wardrobe.Cancel', 'Назад', 'Back'),
    ('UI.Wardrobe.OpenPrompt', 'E · ГАРДЕРОБ', 'E · WARDROBE'),
    ('UI.Shop.Title', 'МАГАЗИН', 'SHOP'),
    ('UI.Shop.Empty', 'Ассортимент пока пуст', 'The catalog is currently empty'),
    ('UI.Shop.EmptySlot', 'Пусто', 'Empty'),
    ('UI.Shop.Close', 'Назад', 'Back'),
    ('UI.Shop.OpenPrompt', 'E · МАГАЗИН', 'E · SHOP'),
)
PROTECTED_THEME_FIELDS = ('palette', 'body_font', 'heading_font', 'display_font',
    'compact_heading_font', 'corner_radius', 'outline_width', 'localization_table',
    'booster_table', 'card_table', 'boss_portraits')


def require(value, message):
    if not value:
        raise RuntimeError(message)


def on_e(value):
    path = Path(value).resolve()
    require(path.drive.upper() == 'E:', 'All working/output files must remain on E: ' + str(path))
    return path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    path = on_e(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def dirty(u):
    return sorted({p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())})


def object_path(value):
    if value is None:
        return ''
    text = value.get_path_name() if hasattr(value, 'get_path_name') else str(value)
    if "'" in text:
        text = text.split("'", 2)[1]
    return text


def value_snapshot(value):
    if hasattr(value, 'export_text'):
        return value.export_text()
    if hasattr(value, 'items'):
        return {str(key): value_snapshot(item) for key, item in value.items()}
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return object_path(value)


def backup(package, output, report):
    require(package.startswith('/Game/') and '..' not in package, 'Invalid package: ' + package)
    existed = False
    for suffix in ('.uasset', '.umap', '.uexp', '.ubulk'):
        source = (ROOT / 'Content' / package[len('/Game/'):]).with_suffix(suffix)
        if not source.is_file():
            continue
        existed = True
        target = output / 'BeforePackages' / source.relative_to(ROOT)
        require(not target.exists(), 'Never overwrite a package backup: ' + str(target))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        require(digest(source) == digest(target), 'Package backup hash mismatch: ' + str(source))
        report['backups'].append(dict(package=package, source=str(source), backup=str(target), sha256=digest(target)))
    if not existed:
        report['initially_absent'].append(package)


def read_rows(u, table):
    return json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def localization_plan(before):
    # Reuse the established lossless row normalizer only for NEW rows. Never
    # run the broad card/booster migration as part of Hub authoring.
    from localization_catalog import index_rows, migrate_localization_row
    known = index_rows(before, 'existing localization catalog')
    additions = [migrate_localization_row(dict(Name=key, RU=ru, EN=en, SourceText=ru,
        Context='Hub wardrobe/empty vending-machine UI')) for key, ru, en in COPY if key.casefold() not in known]
    return copy.deepcopy(before) + additions, [row['Name'] for row in additions]


def append_localization(u, table, before, planned, report):
    from author_localization_catalog import native_validate
    from localization_catalog import index_rows
    if before == planned:
        return
    try:
        with u.ScopedEditorTransaction('Append Hub wardrobe and empty-shop labels'):
            table.modify()
            require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,
                json.dumps(planned, ensure_ascii=False)), 'Could not append localization rows')
            actual = index_rows(read_rows(u, table), 'appended localization catalog')
            for row in before:
                require(actual.get(row['Name'].casefold()) == row,
                    'An existing localization row changed: ' + row['Name'])
            valid, problems = native_validate(u, table, table)
            report['localization_validation'] = dict(valid=valid, problems=problems)
            require(valid, 'Native localization validation failed: ' + '; '.join(problems))
            require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Could not save localization catalog')
            report['saved_assets'].append(TEXTS)
    except Exception:
        # Restore loaded rows through Unreal, never overwrite a loaded package
        # externally. Preserve any dirty rollback for explicit editor review.
        restored = u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,
            json.dumps(before, ensure_ascii=False))
        report['localization_rollback_restored_in_memory'] = bool(restored)
        report['rollback_requires_editor_review'] = True
        raise


def validate_placement(placement):
    if placement is None:
        return
    require(isinstance(placement, dict), 'Station placement must be an explicit object')
    for name in ('location', 'rotation', 'extent'):
        values = placement.get(name)
        require(isinstance(values, (list, tuple)) and len(values) == 3
            and all(isinstance(v, (int, float)) and math.isfinite(v) for v in values),
            'Explicit finite station ' + name + ' must contain three numbers')
    require(all(25 <= value <= 2000 for value in placement['extent']), 'Station half extents must be 25–2000 cm')


def author_station(u, kind, native, placement, report):
    if placement is None:
        return
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(world and world.get_path_name().split('.', 1)[0] == HUB, 'Open Hub explicitly before adding a station')
    cls = u.load_class(None, '/Script/MessControl.' + native)
    require(cls, 'Build the Editor with the new station class first: ' + native)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    existing = list(u.GameplayStatics.get_all_actors_of_class(world, cls))
    require(len(existing) <= 1, 'Review duplicate ' + kind + ' stations before authoring')
    if existing:
        report['stations'].append(dict(kind=kind, actor=existing[0].get_path_name(), existing_station_preserved=True))
        return
    pitch, yaw, roll = placement['rotation']
    station = actors.spawn_actor_from_class(cls, u.Vector(*placement['location']),
        u.Rotator(pitch=pitch, yaw=yaw, roll=roll))
    require(station, 'Could not create explicit ' + kind + ' station')
    station.set_actor_label('Hub_' + kind + 'Station')
    station.tags = list(station.tags) + [u.Name(OWNER_TAG)]
    volume = station.get_editor_property('interaction_volume')
    volume.set_box_extent(u.Vector(*placement['extent']), False)
    require(volume.get_collision_enabled() == u.CollisionEnabled.NO_COLLISION, 'Station unexpectedly has gameplay collision')
    report['stations'].append(dict(kind=kind, actor=station.get_path_name(), placement=placement, created=True))


def verify_palette_tree(u, blueprint):
    # Use the engine's reflected read-only tree export, not protected WidgetTree.
    cls = u.load_class(None, '/Script/UMGToolSet.UMGToolSet')
    require(cls, 'Enabled UMGToolSet is required to verify the authored palette tree')
    tree = u.get_default_object(cls).call_method('GetWidgets', args=(blueprint,))
    items = {entry.widget.get_name(): entry.widget for entry in tree.widgets
        if entry.widget is not None and u.SystemLibrary.is_valid(entry.widget)}
    for name in ('WardrobeColorControls', 'WardrobeColorPalette', 'WardrobeHueStrip', 'WardrobeColorSwatch'):
        require(name in items, 'Authored palette control is missing: '+name)
    for name in ('WardrobeColorPalette', 'WardrobeHueStrip'):
        require(items[name].get_class().get_path_name() == '/Script/MessControl.MCWardrobeColorPalette',
            'Wrong authored palette control class: '+name)
    require(not any(name in items for name in ('WardrobeRSlider', 'WardrobeGSlider', 'WardrobeBSlider')),
        'Old independent RGB controls remain in the authored tree')
    require('HUE' in str(items['WardrobeHueStrip'].get_editor_property('Mode')).upper(),
        'The authored hue strip has the wrong mode')
    return [dict(name=name, native_class=item.get_class().get_path_name()) for name, item in items.items()]


def run(action='plan', *, output='E:/CodexWork/HubWardrobeRewards_20261022/UIAuthoring',
        station=None, shop_station=None, localize=True, refresh_palette=False):
    import unreal as u

    require(action in ('plan', 'apply'), 'Select plan or apply explicitly')
    require(on_e(u.Paths.project_dir()) == ROOT and (ROOT / 'MessControl.uproject').is_file(), 'Wrong Unreal project')
    level = u.get_editor_subsystem(u.LevelEditorSubsystem)
    require(not level.is_in_play_in_editor(), 'Stop PIE before authoring')
    require(not dirty(u), 'Preserve unsaved editor packages before authoring: ' + str(dirty(u)))
    validate_placement(station)
    validate_placement(shop_station)
    if station is not None or shop_station is not None:
        world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(world and world.get_path_name().split('.', 1)[0] == HUB, 'Hub must already be open; this tool never switches maps')
    output = on_e(output) / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '_' + uuid.uuid4().hex[:8])
    output.mkdir(parents=True, exist_ok=False)
    report = dict(action=action, status='running', output=str(output), backups=[], initially_absent=[],
        saved_assets=[], widgets=[], stations=[], no_build=True, no_pie=True,
        no_gameplay_definitions_changed=True, deferred_validation=['Widget rendering and short solo interaction',
        'Multiplayer color replication and reconnect are not tested by authoring'])
    ui_tools = ROOT / 'Tools/Unreal/UI'
    if str(ui_tools) not in sys.path:
        sys.path.insert(0, str(ui_tools))
    try:
        theme = u.load_asset(THEME)
        require(isinstance(theme, u.MCGameplayUITheme), 'Existing shared UI theme is required')
        mapping = dict(theme.get_editor_property('widget_classes'))
        before_mapping = {str(key): object_path(value) for key, value in mapping.items()}
        preserved = {field: value_snapshot(theme.get_editor_property(field)) for field in PROTECTED_THEME_FIELDS}
        report['roles_before'] = before_mapping
        for role, path, native in SPECS:
            current = before_mapping.get(role, '')
            expected = path + '.' + path.rsplit('/', 1)[-1] + '_C'
            require(current in ('', 'None', expected), 'Existing ' + role + ' role points elsewhere; preserve it for review: ' + current)
        table = u.load_asset(TEXTS) if localize else None
        if localize:
            from localization_catalog import LANGUAGE_COLUMNS
            require(isinstance(table, u.DataTable), 'Existing localization table is required')
            structure = u.DataTableFunctionLibrary.get_data_table_row_struct(table)
            require(structure and structure.get_path_name() == '/Script/MessControl.MCUILocalizationRow', 'Wrong localization row struct')
            columns = {str(name) for name in u.DataTableFunctionLibrary.get_data_table_column_names(table)}
            require(set(LANGUAGE_COLUMNS).issubset(columns), 'Build the current ten-language row schema first')
        before_rows = read_rows(u, table) if localize else []
        planned_rows, added_keys = localization_plan(before_rows) if localize else ([], [])
        report['new_text_keys'] = added_keys
        report['locales'] = dict(authored=['ru', 'en'], other_supported_locales='Existing native fallback; no invented translations')
        write_json(output / 'BeforeLocalization.json', before_rows)
        report['refresh_owned_wardrobe_palette_tree'] = bool(refresh_palette)
        write_json(output / 'Plan.json', dict(report, specs=SPECS, station=station, shop_station=shop_station))
        if action == 'plan':
            report['status'] = 'planned'
            return report
        require(hasattr(u.MCGameplayUIEditorLibrary, 'create_editable_gameplay_widget'), 'Build the Editor UI helpers first')
        if refresh_palette:
            require(hasattr(u.MCGameplayUIEditorLibrary, 'refresh_hub_wardrobe_palette_presentation'),
                'Build the narrow owned Wardrobe palette refresh helper first')
        parents = {role: u.load_class(None, '/Script/MessControl.' + native) for role, _, native in SPECS}
        require(all(parents.values()), 'Build the new wardrobe and shop widget classes first')
        packages = [THEME] + [path for _, path, _ in SPECS]
        if localize:
            packages.append(TEXTS)
        if station is not None or shop_station is not None:
            packages.append(HUB)
        for package in packages:
            backup(package, output, report)
        write_json(output / 'Report.json', report)
        for role, path, native in SPECS:
            existed = u.EditorAssetLibrary.does_asset_exist(path)
            bp = u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(path, parents[role])
            require(bp, 'Editable widget authoring failed: ' + path)
            refreshed = bool(role == 'Wardrobe' and existed and refresh_palette)
            if refreshed:
                require(any(item['package'] == path for item in report['backups']),
                    'Verified package backup is required before replacing the owned RGB tree')
                require(u.MCGameplayUIEditorLibrary.refresh_hub_wardrobe_palette_presentation(bp),
                    'Owned Wardrobe palette tree refresh failed; preserve dirty package for review')
            cls_path = path + '.' + path.rsplit('/', 1)[-1] + '_C'
            cls = u.load_class(None, cls_path)
            require(cls and u.MathLibrary.class_is_child_of(cls, parents[role]), 'Unexpected widget parent: ' + path)
            if not existed:
                defaults = u.get_default_object(cls)
                defaults.set_editor_property('use_native_layout', False)
                defaults.set_editor_property('preview_culture', 'ru')
            u.BlueprintEditorLibrary.compile_blueprint(bp)
            require('ERROR' not in str(bp.get_editor_property('status')).upper(), 'Widget Blueprint compilation failed: ' + path)
            require(u.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False), 'Could not save widget: ' + path)
            if role == 'Wardrobe' and (refreshed or not existed):
                report['wardrobe_palette_tree_after'] = verify_palette_tree(u, bp)
                write_json(output / 'WardrobePaletteTree.json', report['wardrobe_palette_tree_after'])
            cls = u.load_class(None, cls_path)
            mapping[u.Name(role)] = cls
            report['widgets'].append(dict(role=role, path=path, native=native,
                existing_tree_preserved=existed and not refreshed, owned_palette_tree_refreshed=refreshed))
            report['saved_assets'].append(path)
            write_json(output / 'Report.json', report)
        # Creation requires a clean Designer; modify the shared class map only after
        # both WBP trees have been compiled and explicitly saved.
        theme.set_editor_property('widget_classes', mapping)
        after_mapping = {str(key): object_path(value) for key, value in theme.get_editor_property('widget_classes').items()}
        require(all(after_mapping.get(key) == value for key, value in before_mapping.items() if key not in ('Wardrobe', 'Shop')),
            'An unrelated UI role changed')
        require(all(value_snapshot(theme.get_editor_property(field)) == value for field, value in preserved.items()),
            'An unrelated shared theme property changed')
        theme.notify_theme_changed()
        require(u.EditorAssetLibrary.save_loaded_asset(theme, only_if_is_dirty=False), 'Could not save shared UI roles')
        report['saved_assets'].append(THEME)
        report['roles_after'] = after_mapping
        if localize:
            append_localization(u, table, before_rows, planned_rows, report)
        author_station(u, 'Wardrobe', 'MCHubWardrobeStation', station, report)
        author_station(u, 'Shop', 'MCHubShopStation', shop_station, report)
        if any(item.get('created') for item in report['stations']):
            require(level.save_current_level(), 'Could not save explicit Hub station placement')
            report['saved_assets'].append(HUB)
        require(not dirty(u), 'Unexpected dirty packages remain; preserved for inspection: ' + str(dirty(u)))
        report['status'] = 'complete'
        return report
    except Exception as exc:
        report.update(status='failed', error=str(exc), traceback=traceback.format_exc())
        # Keep partial packages and in-memory edits for diagnosis/idempotent
        # recovery. Never delete assets or overwrite loaded packages externally.
        raise
    finally:
        write_json(output / 'Report.json', report)
        u.log('MC_HUB_WARDROBE_SHOP_AUTHOR ' + json.dumps({key: report.get(key) for key in ('status', 'action', 'output', 'error')}))


if __name__ == '__main__':
    run(**globals().get('MC_HUB_WARDROBE_OPTIONS', {}))
