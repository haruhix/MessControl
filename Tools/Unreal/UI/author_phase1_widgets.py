"""Author Phase1 gameplay WBP assets in the already-running, idle editor.

Run with runpy.run_path(..., run_name='__main__') after the new native code and
Phase1 materials have loaded. Existing Phase1 trees are preserved by the native
helper. v10 trees, BossHealth/GameplayAlert roles, fonts and all DT content stay
unchanged. This script neither launches an editor/build nor invokes old authoring.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[3]
BASE = '/Game/Gameplay/UI'
THEME_PATH = BASE + '/Theme/DA_GameplayUITheme'
OUT = ROOT / 'Saved/GameplayUI/Phase1'
BEFORE = OUT / 'Before'
TEMP = Path('E:/CodexTemp/GameplayUIPhase1')

# Children precede composite parents: native builders resolve the updated master
# class map while constructing the editable child instances in their new trees.
SPECS = (
    ('PerkCard', 'WBP_PerkCard_Phase1', 'MCPerkCardWidget'),
    ('ReserveTooth', 'WBP_ReserveTooth_Phase1', 'MCReserveToothWidget'),
    ('Booster', 'WBP_Booster_Phase1', 'MCBoosterWidget'),
    ('Stamina', 'WBP_Stamina_Phase1', 'MCStaminaWidget'),
    ('Scoreboard', 'WBP_Scoreboard_Phase1', 'MCScoreboardWidget'),
    ('Emotes', 'WBP_Emotes_Phase1', 'MCEmoteWidget'),
    ('Tutorial', 'WBP_Tutorial_Phase1', 'MCTutorialWidget'),
    ('FreezeBar', 'WBP_FreezeBar_Phase1', 'MCFreezeBarWidget'),
    ('DeliveryCaption', 'WBP_DeliveryCaption_Phase1', 'MCDeliveryZoneCaptionWidget'),
    ('UlcerProgress', 'WBP_UlcerProgress_Phase1', 'MCUlcerProgressWidget'),
    ('PlayerName', 'WBP_PlayerName_Phase1', 'MCPlayerNameWidget'),
    ('PauseMenu', 'WBP_PauseMenu_Phase1', 'MCGameplayMenuWidget'),
    ('ConnectionMenu', 'WBP_ConnectionMenu_Phase1', 'MCGameplayMenuWidget'),
    ('PerkChoice', 'WBP_PerkChoice_Phase1', 'MCPerkChoiceWidget'),
    ('GameplayHUD', 'WBP_GameplayHUD_Phase1', 'MCGameplayHUD'),
    ('ThemePreview', 'WBP_UIThemePreview_Phase1', 'MCUIThemePreviewWidget'),
)
PALETTE_HEX = {
    'surface': 'E7F8EF', 'surface_alt': 'C3ECE1', 'ink': '082D45',
    'muted': '3D6F7A', 'marine': '134A64', 'mint': 'B5E67B',
    'cream': 'FFF5D8', 'border': '21BDCE', 'accent': '27ECF4',
    'on_marine': 'E6FFFA', 'button': 'D7F2E7', 'button_hover': 'C8F4B1',
    'button_pressed': 'A3D882', 'button_disabled': '8DB0AE',
    'button_text': '082D45', 'focus': '1FF3FF',
    'lcd_background': 'C5F578', 'lcd_ink': '183A1A',
    'stamina': '2CECF2', 'cleanliness': '32E3EE',
    'benefit': '1A6B2C', 'cost': 'E57965',
}
PRESERVED_COLORS = ('boss_health', 'boss_surface', 'boss_rim',
                    'boss_rim_highlight', 'shield', 'warning', 'backdrop')
FONT_FIELDS = ('body_font', 'heading_font', 'display_font')
DATA_FIELDS = ('localization_table', 'booster_table', 'card_table')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def class_path(value):
    if value is None:
        return ''
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    if hasattr(value, 'to_string'):
        return value.to_string()
    return str(value)


def package_path(value):
    text = class_path(value)
    # Handle both a UClass object path and a typed soft-path export literal.
    if "'" in text:
        text = text.split("'", 2)[1]
    return text.split('.', 1)[0] if text.startswith('/Game/') else ''


def rgba(value):
    return tuple(getattr(value, component) for component in ('r', 'g', 'b', 'a'))


def backup_package(package, report):
    """Copy saved package siblings once, preserving the original directory tree."""
    require(package.startswith('/Game/'), 'Backup is restricted to /Game: ' + package)
    relative = Path('Content') / package[len('/Game/'):]
    for suffix in ('.uasset', '.uexp', '.ubulk'):
        source = (ROOT / relative).with_suffix(suffix)
        if not source.is_file():
            continue
        destination = (BEFORE / relative).with_suffix(suffix)
        destination.parent.mkdir(parents=True, exist_ok=True)
        created = not destination.exists()
        if created:
            shutil.copy2(source, destination)
            require(sha256(source) == sha256(destination), 'Backup verification failed: ' + str(source))
        report['backups'].append({'source': str(source), 'backup': str(destination),
                                  'created': created, 'sha256': sha256(destination)})


def main():
    import unreal as u

    require(ROOT.drive.upper() == 'E:', 'Authoring output must remain on E:')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong Unreal project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Authoring requires idle editor')
    TEMP.mkdir(parents=True, exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(TEMP)
    OUT.mkdir(parents=True, exist_ok=True)
    BEFORE.mkdir(parents=True, exist_ok=True)
    lib = u.EditorAssetLibrary
    theme = u.load_asset(THEME_PATH)
    require(theme is not None, 'Existing master theme is required')
    report = {'widgets': [], 'backups': [], 'tables': {}, 'status': 'in_progress',
              'excluded_roles': ['BossHealth', 'GameplayAlert'],
              'old_trees_untouched': True, 'palette_srgb_hex': PALETTE_HEX,
              'render_validation': 'pending', 'solo_pie_validation': 'pending'}
    evidence = OUT / 'authoring.json'

    def save_report():
        evidence.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    def table_snapshot(field):
        # Read-only export fingerprints include row names, images and every locale.
        value = theme.get_editor_property(field)
        table = value if isinstance(value, u.DataTable) else u.load_asset(class_path(value))
        require(isinstance(table, u.DataTable), 'Existing UI DT is required: ' + field)
        rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        encoded = json.dumps(rows, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
        return {'path': table.get_path_name(), 'rows': len(rows), 'sha256': hashlib.sha256(encoded).hexdigest()}

    mapping = {u.Name(str(key)): value for key, value in theme.get_editor_property('widget_classes').items()}
    original_mapping = {str(key): class_path(value) for key, value in mapping.items()}
    for role in ('BossHealth', 'GameplayAlert'):
        require(original_mapping.get(role) not in (None, '', 'None'), 'Existing role must be preserved: ' + role)
    preserved_roles = {role: original_mapping[role] for role in ('BossHealth', 'GameplayAlert')}
    palette = theme.get_editor_property('palette')
    preserved_colors = {key: rgba(palette.get_editor_property(key)) for key in PRESERVED_COLORS}
    # Struct repr includes a wrapper memory address; export_text is the stable
    # native value, including the font object/material/outline/typeface settings.
    preserved_fonts = {key: theme.get_editor_property(key).export_text() for key in FONT_FIELDS}
    preserved_portraits = {str(key): class_path(value) for key, value in theme.get_editor_property('boss_portraits').items()}
    report['tables'] = {key: table_snapshot(key) for key in DATA_FIELDS}
    report['roles_before'] = original_mapping

    # Back up the master and all existing mapped roles before the first mutation.
    # Also capture saved v10/Phase1 targets even if they are not currently mapped.
    packages = {THEME_PATH}
    packages.update(package_path(value) for value in mapping.values() if package_path(value))
    for _, name, _ in SPECS:
        packages.add(BASE + '/' + name)
        packages.add(BASE + '/' + name.replace('_Phase1', '_v10'))
    packages.update((BASE + '/WBP_GameplayAlert_v10', BASE + '/WBP_BossHealth_v10',
                     BASE + '/WBP_BossHealth_Reference', BASE + '/WBP_UIThemePreview'))
    for package in sorted(packages):
        backup_package(package, report)
    save_report()

    def srgb_hex(hex_value):
        def linear(byte):
            value = byte / 255.0
            return value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4
        return u.LinearColor(*(linear(int(hex_value[index:index+2], 16)) for index in (0, 2, 4)), 1.0)

    classes = {}
    try:
        for key, value in PALETTE_HEX.items():
            palette.set_editor_property(key, srgb_hex(value))
        theme.set_editor_property('palette', palette)

        for role, name, native in SPECS:
            path = BASE + '/' + name
            parent = u.load_class(None, '/Script/MessControl.' + native)
            require(parent is not None, 'Native class is not loaded: ' + native)
            existed = lib.does_asset_exist(path)
            bp = u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(path, parent)
            require(bp is not None, 'Widget authoring failed: ' + path)
            cls = u.load_class(None, path + '.' + name + '_C')
            require(cls is not None and u.MathLibrary.class_is_child_of(cls, parent), 'Wrong widget parent: ' + path)
            defaults = u.get_default_object(cls)
            changed = False

            def set_default(field, value):
                nonlocal changed
                if defaults.get_editor_property(field) != value:
                    defaults.set_editor_property(field, value)
                    changed = True

            if not existed:
                set_default('preview_state', u.Name('Connection' if role == 'ConnectionMenu' else 'Default'))
                if role == 'GameplayHUD':
                    set_default('preview_scenario', u.MCGameplayUIPreviewScenario.THREE_BOSSES)
            # A first run can stop after the helper saved the tree but before
            # its role-specific defaults were saved. These two roles must resume
            # to distinct menu previews even when their Phase1 asset now exists.
            if role in ('PauseMenu', 'ConnectionMenu'):
                set_default('preview_state', u.Name('Connection' if role == 'ConnectionMenu' else 'Default'))
            if role == 'PerkChoice':
                set_default('card_widget_class', classes['PerkCard'])
            if role == 'GameplayHUD':
                for field, child_role in (('booster_widget_class', 'Booster'),
                                          ('reserve_tooth_widget_class', 'ReserveTooth'),
                                          ('stamina_widget_class', 'Stamina')):
                    set_default(field, classes[child_role])
                existing_alert = u.load_class(None, preserved_roles['GameplayAlert'])
                require(existing_alert is not None, 'Preserved alert class is unavailable')
                set_default('alert_widget_class', existing_alert)
                existing_boss = u.load_class(None, preserved_roles['BossHealth'])
                require(existing_boss is not None, 'Preserved boss class is unavailable')
                set_default('encounter_preview_widget_class', existing_boss)

            # The helper already saved a newly built tree. Save again only for
            # changed composition/preview defaults, never rebuild an existing tree.
            if changed:
                u.BlueprintEditorLibrary.compile_blueprint(bp)
                require(lib.save_loaded_asset(bp, only_if_is_dirty=False), 'Could not save defaults: ' + path)
                cls = u.load_class(None, path + '.' + name + '_C')
                require(cls is not None, 'Compiled class is missing: ' + path)
            classes[role] = cls
            mapping[u.Name(role)] = cls
            theme.set_editor_property('widget_classes', mapping)
            report['widgets'].append({'role': role, 'path': path, 'native': native,
                                      'existing_tree_preserved': existed, 'defaults_saved': changed})
            save_report()

        final_mapping = {str(key): class_path(value) for key, value in theme.get_editor_property('widget_classes').items()}
        require(all(final_mapping.get(key) == value for key, value in preserved_roles.items()), 'Excluded role mapping changed')
        current_palette = theme.get_editor_property('palette')
        require(all(rgba(current_palette.get_editor_property(key)) == value for key, value in preserved_colors.items()), 'Boss/shield/warning/backdrop color changed')
        require(all(theme.get_editor_property(key).export_text() == value for key, value in preserved_fonts.items()), 'Shared font changed')
        require({str(key): class_path(value) for key, value in theme.get_editor_property('boss_portraits').items()} == preserved_portraits, 'Boss portraits changed')
        require(all(table_snapshot(key) == value for key, value in report['tables'].items()), 'UI table content changed')
        theme.notify_theme_changed()
        require(lib.save_loaded_asset(theme, only_if_is_dirty=False), 'Could not save master theme')
        u.MCGameplayUIEditorLibrary.ensure_gameplay_ui_preview_registered()
        report.update(status='complete', roles_after=final_mapping, preserved_fonts=True,
                      preserved_boss_palette=True, preserved_tables=True, preserved_boss_portraits=True)
        save_report()
        u.log('MC_UI_PHASE1_AUTHOR_COMPLETE ' + json.dumps(report, ensure_ascii=False))
    except Exception as error:
        # No asset deletion or rollback over newly authored work. The original
        # saved theme is backed up; retain partial assets for a safe idempotent retry.
        report.update(status='failed', error=str(error))
        save_report()
        u.log_error('MC_UI_PHASE1_AUTHOR_FAILED ' + str(error))
        raise


if __name__ == '__main__':
    main()
