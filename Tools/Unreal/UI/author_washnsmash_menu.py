"""Editable Wash n Smash front end over the real L_MainMenu 3D camera.

No new C++ class, build, level edit, PIE, network or microphone action is used.
The working authored MainMenu is duplicated; every settings/connection page and
native button adapter survives. Existing custom target trees are preserved.
The root agent separately authors the real 3D scene and native menu GameMode.
"""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import re
import runpy

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
BASE = '/Game/Gameplay/UI'
SOURCE = BASE + '/WBP_MainMenu'
TARGET = BASE + '/WBP_WashNSmashMenu'
THEME = BASE + '/Theme/DA_GameplayUITheme'
TEXTS = BASE + '/Data/DT_UI_Localization'
CANONICAL = ROOT / 'Tools/Unreal/UI/gameplay_ui_texts.json'
PATCH = ROOT / 'Tools/Unreal/UI/washnsmash_menu_text_patch.json'
OWNER_KEY = 'MC_WashNSmashMenu'
OWNER_VALUE = 'EditableNativeMenu_v1'
LANGUAGES = ('RU', 'EN', 'DE', 'FR', 'ES', 'PT_BR', 'ZH_HANS', 'ZH_HANT', 'JA', 'KO')
POLISH_KEYS = ('UI.MainMenu.Action.Tutorial', 'UI.MainMenu.Action.Settings',
              'UI.MainMenu.Action.Credits', 'UI.MainMenu.Action.Exit',
              'UI.MainMenu.Tagline', 'UI.MainMenu.NavigationHint')


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def dirty():
    return sorted(p.get_path_name() for p in
        list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
        list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def object_path(package):
    return package + '.' + package.rsplit('/', 1)[-1]


def widget(package, name):
    return u.find_object(None, object_path(package) + ':WidgetTree.' + name)


def widgets(package):
    prefix = object_path(package) + ':WidgetTree.'
    return {obj.get_name(): obj for obj in u.ObjectIterator(u.Widget)
            if obj.get_path_name().startswith(prefix)}


def class_path(value):
    if value and hasattr(value, 'get_path_name'):
        return value.get_path_name()
    match = re.search(r'/(?:Game|Script)/[^\s\'"<>\)]+', str(value))
    return match.group(0) if match else ''


def enum(cls, name):
    snake = re.sub(r'(?<!^)(?=[A-Z])', '_', name).upper()
    for candidate in dict.fromkeys((name, snake, name.upper())):
        if hasattr(cls, candidate):
            return getattr(cls, candidate)
    normalized = re.sub(r'[^a-z0-9]', '', name.lower())
    matches = [member for member in dir(cls)
               if re.sub(r'[^a-z0-9]', '', member.lower()) == normalized]
    if len(matches) == 1:
        return getattr(cls, matches[0])
    # UE's legacy EHorizontalAlignment/EVerticalAlignment preserve their
    # H_ALIGN_/V_ALIGN_ enumerator prefixes in Python. Match only a unique
    # uppercase enum member, never similarly named helper methods.
    matches = [member for member in dir(cls) if member.isupper()
               and re.sub(r'[^a-z0-9]', '', member.lower()).endswith(normalized)]
    if len(matches) == 1:
        return getattr(cls, matches[0])
    raise RuntimeError('Missing reflected enum value: ' + str(cls) + '.' + name)


def prop(obj, name, value, native=None):
    errors = []
    for candidate in dict.fromkeys((name, native or name)):
        try:
            obj.set_editor_property(candidate, value)
            return
        except Exception as error:
            errors.append(str(error))
    raise RuntimeError('Cannot author ' + name + ': ' + ' | '.join(errors))


def inventory(package):
    result = []
    for name, obj in widgets(package).items():
        parent = obj.get_parent()
        result.append(dict(name=name, native_class=obj.get_class().get_path_name(),
                           parent=parent.get_name() if parent else None))
    return sorted(result, key=lambda value: value['name'])


def backup(root, file, out, report):
    if not file.exists():
        return
    target = out / 'BeforeFiles' / file.relative_to(root)
    target.parent.mkdir(parents=True, exist_ok=True)
    data = file.read_bytes()
    target.write_bytes(data)
    require(target.read_bytes() == data, 'Backup verification failed: ' + str(file))
    report['backups'].append(dict(source=str(file), backup=str(target), sha256=hashlib.sha256(data).hexdigest()))


def compile_and_save(asset):
    result = u.BlueprintEditorLibrary.compile_blueprint(asset)
    require(result is not False, 'Blueprint compile failed: ' + asset.get_path_name())
    require('ERROR' not in str(asset.get_editor_property('status')).upper(), 'Blueprint status is Error')
    require(u.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save ' + asset.get_path_name())


def style_binding(name, color, target, font_size=0, bold=False):
    result = u.MCUIStyleBinding()
    prop(result, 'widget_name', name, 'WidgetName')
    prop(result, 'color', enum(u.MCUIColorToken, color), 'Color')
    prop(result, 'target', enum(u.MCUIStyleTarget, target), 'Target')
    prop(result, 'font_size', font_size, 'FontSize')
    prop(result, 'bold', bold, 'bBold')
    return result


def brush(radius):
    result = u.SlateBrush()
    prop(result, 'draw_as', enum(u.SlateBrushDrawType, 'RoundedBox'), 'DrawAs')
    outline = result.get_editor_property('outline_settings')
    prop(outline, 'rounding_type', enum(u.SlateBrushRoundingType, 'FixedRadius'), 'RoundingType')
    prop(outline, 'corner_radii', u.Vector4(radius, radius, radius, radius), 'CornerRadii')
    prop(result, 'outline_settings', outline, 'OutlineSettings')
    # No texture/material is assigned: this remains an ordinary editable shape.
    return result


def patch_localization(table, out, report):
    text_tools = runpy.run_path(str(ROOT / 'Tools/Unreal/UI/localization_catalog.py'))
    text_source = text_tools['text_source']
    additions = json.loads(PATCH.read_text(encoding='utf-8-sig'))
    expected = {'UI.MainMenu.BrandTitle', 'UI.WashMenu.JoinFriends', 'UI.WashMenu.Play', 'UI.MainMenu.Action.PlayHub', *POLISH_KEYS}
    require({row['Name'] for row in additions} == expected, 'Unexpected text patch scope')
    before = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    planned = copy.deepcopy(before)
    by_name = {row['Name']: row for row in planned}
    require('UI.MainMenu.BrandTitle' in by_name, 'The stable brand localization row is missing')
    for addition in additions:
        name = addition['Name']
        if name not in by_name:
            row = copy.deepcopy(addition)
            row.setdefault('Localizations', {})
            row.setdefault('AdditionalLanguages', {})
            planned.append(row)
            by_name[name] = row
        elif name != 'UI.WashMenu.JoinFriends':
            # UE exports authored FText histories as NSLOCTEXT literals. Keep
            # their identities verbatim when the displayed copy already fits.
            fields = ('SourceText',) + LANGUAGES if name == 'UI.MainMenu.BrandTitle' else LANGUAGES
            for key in fields:
                if text_source(by_name[name].get(key)) != addition[key]:
                    by_name[name][key] = addition[key]
        # Preserve any already-authored JoinFriends translations on a repeat run.
    if planned != before:
        require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(planned, ensure_ascii=False)), 'Localization import failed')
        require(u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Could not save localization table')
    after = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    untouched_before = {row['Name']: row for row in before if row['Name'] not in expected}
    untouched_after = {row['Name']: row for row in after if row['Name'] not in expected}
    require(untouched_after == untouched_before, 'An unrelated localization row changed')
    brand = next(row for row in after if row['Name'] == 'UI.MainMenu.BrandTitle')
    require(all(text_source(brand.get(code)) == 'Wash n Smash' for code in LANGUAGES), 'Brand is not updated in all ten language columns')
    resolved_brand = {culture: str(u.MCUIDataLibrary.resolve_text(table,
        u.Name('UI.MainMenu.BrandTitle'), u.Text(''), culture))
        for culture in text_tools['LANGUAGE_CULTURES'].values()}
    require(all(value == 'Wash n Smash' for value in resolved_brand.values()), 'Runtime culture lookup did not resolve the brand')
    play_patch = next(row for row in additions if row['Name'] == 'UI.WashMenu.Play')
    resolved_play = {}
    for code, culture in text_tools['LANGUAGE_CULTURES'].items():
        for key in ('UI.WashMenu.Play', 'UI.MainMenu.Action.PlayHub'):
            value = str(u.MCUIDataLibrary.resolve_text(table, u.Name(key), u.Text(''), culture))
            require(value == play_patch[code], 'Play label failed runtime lookup: ' + key + '/' + culture)
            resolved_play[key + '/' + culture] = value
    resolved_polish = {}
    for addition in additions:
        if addition['Name'] not in POLISH_KEYS:
            continue
        for code, culture in text_tools['LANGUAGE_CULTURES'].items():
            value = str(u.MCUIDataLibrary.resolve_text(table, u.Name(addition['Name']), u.Text(''), culture))
            require(value == addition[code], 'Menu copy failed runtime lookup: ' + addition['Name'] + '/' + culture)
            resolved_polish[addition['Name'] + '/' + culture] = value
    original_catalog = json.loads(CANONICAL.read_text(encoding='utf-8-sig'))
    catalog = copy.deepcopy(original_catalog)
    indexed = {row['Name']: row for row in catalog}
    require('UI.MainMenu.BrandTitle' in indexed, 'Canonical brand row missing')
    for addition in additions:
        name = addition['Name']
        if name not in indexed:
            catalog.append(copy.deepcopy(addition))
        elif name != 'UI.WashMenu.JoinFriends':
            fields = ('SourceText',) + LANGUAGES if name == 'UI.MainMenu.BrandTitle' else LANGUAGES
            indexed[name].update({key: addition[key] for key in fields})
    if catalog != original_catalog:
        CANONICAL.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    report['localization'] = dict(changed_rows=sorted(expected),
        preserved_unrelated_rows=len(untouched_before), brand_languages=list(LANGUAGES),
        runtime_brand=resolved_brand, runtime_play=resolved_play, runtime_polish=resolved_polish,
        canonical=str(CANONICAL))


def repair_menu_layout(asset, left_padding, report):
    """Repair this owned menu's existing slot properties and static Play binding."""
    controls = widgets(TARGET)
    backdrop, card = controls['MenuBackdrop'], controls['MenuCard']
    scale, size = backdrop.get_content(), card.get_content()
    require(isinstance(scale, u.ScaleBox) and isinstance(scale.slot, u.BorderSlot), 'Unexpected backdrop content slot; preserve it for review')
    require(isinstance(card.slot, u.ScaleBoxSlot) and isinstance(size.slot, u.BorderSlot), 'Unexpected card slots; preserve them for review')
    before = dict(backdrop_slot_horizontal=str(scale.slot.get_editor_property('horizontal_alignment')),
                  card_slot_horizontal=str(card.slot.get_editor_property('horizontal_alignment')))
    asset.modify()
    for slot in (scale.slot, card.slot, size.slot):
        slot.modify()
    # UBorder setters do not update the saved child UBorderSlot. RebuildWidget
    # uses that slot's alignment, so write the slot, which also updates Border.
    scale.slot.set_horizontal_alignment(enum(u.HorizontalAlignment, 'Left'))
    scale.slot.set_vertical_alignment(enum(u.VerticalAlignment, 'Center'))
    scale.slot.set_padding(u.Margin(left_padding, 30., 0., 30.))
    card.slot.set_horizontal_alignment(enum(u.HorizontalAlignment, 'Left'))
    card.slot.set_vertical_alignment(enum(u.VerticalAlignment, 'Center'))
    size.slot.set_padding(u.Margin(24., 24., 24., 24.))
    cls = u.load_class(None, object_path(TARGET) + '_C')
    defaults = u.get_default_object(cls)
    defaults.modify()
    bindings = dict(defaults.get_editor_property('text_bindings'))
    bindings['MenuMain_Action1Label'] = 'UI.WashMenu.Play'
    for index, key in ((0, 'Tutorial'), (3, 'Settings'), (4, 'Credits'), (5, 'Exit')):
        bindings['MenuMain_Action' + str(index) + 'Label'] = 'UI.MainMenu.Action.' + key
    bindings['MenuMain_Text1'] = 'UI.MainMenu.Tagline'
    bindings['MenuNavigationHint'] = 'UI.MainMenu.NavigationHint'
    defaults.set_editor_property('text_bindings', bindings)
    style = list(defaults.get_editor_property('style_bindings'))
    updated = set()
    for binding in style:
        name = str(binding.get_editor_property('widget_name'))
        if name in ('MenuTitle', 'MenuMain_Action1'):
            binding.set_editor_property('color', enum(u.MCUIColorToken, 'Cream' if name == 'MenuTitle' else 'Mint'))
            updated.add(name)
    require(updated == {'MenuTitle', 'MenuMain_Action1'}, 'Owned menu style contract is incomplete')
    defaults.set_editor_property('style_bindings', style)
    compile_and_save(asset)
    report['layout_repair'] = dict(before=before,
        backdrop_slot_horizontal=str(scale.slot.get_editor_property('horizontal_alignment')),
        card_slot_horizontal=str(card.slot.get_editor_property('horizontal_alignment')),
        play_label_binding='UI.WashMenu.Play', runtime_cache_key='UI.MainMenu.Action.PlayHub',
        brand_color_token='Cream', play_color_token='Mint',
        hierarchy_preserved=True, actual_play_action='CreateLobby')


def run(action='plan', *, output='E:/CodexWork/MessControlWashNSmashMenu_20261021/Authoring01',
        activate=True, panel_tint=(.018, .009, .024, .58), panel_width=440., panel_height=800.,
        left_padding=88., brand_font_size=62):
    require(action in ('plan', 'apply', 'repair'), 'Unknown action')
    require(Path(u.Paths.project_dir()).resolve() == ROOT, 'Wrong project checkout')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active Play')
    require(not dirty(), 'Preserve unsaved editor changes before authoring')
    require(len(panel_tint) == 4 and all(0. <= float(value) <= 1. for value in panel_tint), 'Invalid panel tint')
    require(400. <= panel_width <= 600. and 680. <= panel_height <= 900., 'Preserve readable menu bounds')
    require(0. <= left_padding <= 160. and 40 <= brand_font_size <= 80, 'Invalid presentation values')
    theme = u.load_asset(THEME)
    source = u.load_asset(SOURCE)
    table = u.load_asset(TEXTS)
    require(theme and source and table, 'The authored source menu/theme/table is missing')
    source_class = u.load_class(None, object_path(SOURCE) + '_C')
    require(source_class and isinstance(u.get_default_object(source_class), u.MCMainMenuWidget), 'Source menu does not use existing native actions')
    mapping = dict(theme.get_editor_property('widget_classes'))
    prior_role = next((value for key, value in mapping.items() if str(key).lower() == 'mainmenu'), None)
    require(not activate or class_path(prior_role) in ('', object_path(SOURCE) + '_C', object_path(TARGET) + '_C'),
            'An unrelated custom MainMenu role is preserved; review it before activation')
    existing = u.load_asset(TARGET) if u.EditorAssetLibrary.does_asset_exist(TARGET) else None
    require(not existing or u.EditorAssetLibrary.get_metadata_tag(existing, OWNER_KEY) == OWNER_VALUE,
            'Existing unrelated target preserved; select a reviewed destination')
    require(action != 'repair' or existing, 'Repair requires the existing owned menu')
    report = dict(action=action, source=SOURCE, target=TARGET, theme=THEME, activate=activate,
        existing_target_preserved=bool(existing), source_tree=inventory(SOURCE), backups=[], previews=[],
        composition=dict(panel_width=panel_width, panel_height=panel_height, left_padding=left_padding,
                         panel_tint=list(panel_tint), brand_font_size=brand_font_size),
        real_scene='The menu is transparent outside its left panel. L_MainMenu camera/actors are authored separately.',
        preserved_actions=['PlayToHub/Steam', 'JoinLobby', 'Tutorial', 'Settings', 'Credits', 'Exit'])
    if action == 'plan':
        u.log(json.dumps(report, ensure_ascii=False, indent=2))
        return report
    out = Path(output).resolve()
    require(out.drive.upper() == 'E:' and (not out.exists() or not any(out.iterdir())), 'Choose a fresh E: output folder')
    out.mkdir(parents=True, exist_ok=True)
    for package in (SOURCE, TARGET, THEME, TEXTS):
        backup(ROOT, ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset'), out, report)
    backup(ROOT, CANONICAL, out, report)
    (out / 'Plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    asset = existing or u.EditorAssetLibrary.duplicate_asset(SOURCE, TARGET)
    require(asset, 'Could not duplicate the existing authored menu')
    if not existing:
        controls = widgets(TARGET)
        required = ['MenuBackdrop', 'MenuCard', 'MenuTitle', 'MenuPages', 'MenuMain_Page'] + ['MenuMain_Action' + str(index) for index in range(6)]
        require(all(name in controls for name in required), 'Native named control contract is incomplete')
        backdrop, card, title, main = (controls[name] for name in ('MenuBackdrop', 'MenuCard', 'MenuTitle', 'MenuMain_Page'))
        require(isinstance(backdrop, u.Border) and isinstance(card, u.Border) and isinstance(main, u.VerticalBox), 'Custom source layout requires review')
        size = card.get_content()
        require(isinstance(size, u.SizeBox), 'Preserve custom card content instead of replacing it')
        backdrop.set_brush(brush(0.))
        backdrop.set_brush_color(u.LinearColor(1., 1., 1., 0.))
        backdrop.set_horizontal_alignment(enum(u.HorizontalAlignment, 'Left'))
        backdrop.set_vertical_alignment(enum(u.VerticalAlignment, 'Center'))
        backdrop.set_padding(u.Margin(left_padding, 30., 0., 30.))
        card.set_brush(brush(18.))
        card.set_brush_color(u.LinearColor(*panel_tint))
        card.set_padding(u.Margin(24., 24., 24., 24.))
        size.set_width_override(panel_width)
        size.set_height_override(panel_height)
        title.set_text('Wash n Smash')
        title.set_auto_wrap_text(True)
        title.set_editor_property('wrap_text_at', panel_width)
        title.set_editor_property('justification', enum(u.TextJustify, 'Left'))
        original_children = list(main.get_all_children())
        ordered = [controls['MenuMain_Action' + str(index)] for index in (1, 2, 0, 3, 4, 5)]
        # TemplateTextIndex is incremented before authoring: the saved source
        # has Text1 as its tagline and Text2 as the old lobby explanation.
        subtitle = controls.get('MenuMain_Text1')
        tail = [child for child in original_children if child not in ordered and child != subtitle]
        for child in original_children:
            child.remove_from_parent()
        for child in ([subtitle] if subtitle else []) + ordered + tail:
            main.add_child_to_vertical_box(child).set_padding(u.Margin(0., 0., 0., 7.))
        if controls.get('MenuMain_Text2'):
            controls['MenuMain_Text2'].set_visibility(enum(u.SlateVisibility, 'Collapsed'))
        if subtitle:
            subtitle.set_render_opacity(.76)
            subtitle.set_editor_property('wrap_text_at', panel_width)
        for index, button in enumerate(ordered):
            require(isinstance(button, u.MCMainMenuActionButton), 'Action adapter was replaced')
            button.set_background_color(u.LinearColor(1., 1., 1., 1.))
            inset = button.get_content()
            require(isinstance(inset, u.Border), 'Preserve custom button content instead of guessing')
            inset.set_padding(u.Margin(22., 17. if index == 0 else 11., 22., 17. if index == 0 else 11.))
        cls = u.load_class(None, object_path(TARGET) + '_C')
        defaults = u.get_default_object(cls)
        asset.modify()
        defaults.modify()
        defaults.set_editor_property('theme', theme)
        defaults.set_editor_property('use_native_layout', False)
        text_bindings = {str(key): str(value) for key, value in dict(defaults.get_editor_property('text_bindings')).items()}
        text_bindings['MenuTitle'] = 'UI.MainMenu.BrandTitle'
        text_bindings['MenuMain_Action2Label'] = 'UI.WashMenu.JoinFriends'
        defaults.set_editor_property('text_bindings', text_bindings)
        style = list(defaults.get_editor_property('style_bindings'))
        replacement_names = {'MenuBackdrop', 'MenuCard', 'MenuTitle', 'MenuStatus', 'MenuNavigationHint'}
        if subtitle:
            replacement_names.add(subtitle.get_name())
        replacement_names.update(button.get_name() for button in ordered)
        replacement_names.update('MenuMain_Action' + str(index) + 'Label' for index in range(6))
        style = [binding for binding in style if str(binding.get_editor_property('widget_name')) not in replacement_names]
        style += [style_binding('MenuTitle', 'Cream', 'Text', brand_font_size, True),
                  style_binding('MenuStatus', 'Accent', 'Text', 15),
                  style_binding('MenuNavigationHint', 'OnMarine', 'Text', 12)]
        if subtitle:
            style.append(style_binding(subtitle.get_name(), 'OnMarine', 'Text', 16))
        for index, button in enumerate(ordered):
            style += [style_binding(button.get_name(), 'Mint' if index == 0 else 'Button', 'Button'),
                      style_binding(button.get_name() + 'Label', 'Ink' if index == 0 else 'ButtonText', 'Text', 29 if index == 0 else 21, True)]
        defaults.set_editor_property('style_bindings', style)
        u.EditorAssetLibrary.set_metadata_tag(asset, OWNER_KEY, OWNER_VALUE)
        compile_and_save(asset)
    if action == 'repair' or not existing:
        repair_menu_layout(asset, left_padding, report)
    patch_localization(table, out, report)
    target_class = u.load_class(None, object_path(TARGET) + '_C')
    require(target_class and isinstance(u.get_default_object(target_class), u.MCMainMenuWidget), 'Compiled target lost its native menu parent')
    if activate:
        theme.modify()
        mapping = {key: value for key, value in mapping.items() if str(key).lower() != 'mainmenu'}
        mapping['MainMenu'] = target_class
        theme.set_editor_property('widget_classes', mapping)
        theme.notify_theme_changed()
        require(u.EditorAssetLibrary.save_loaded_asset(theme, only_if_is_dirty=False), 'Could not save the MainMenu role')
    report['target_tree'] = inventory(TARGET)
    source_names = {(entry['name'], entry['native_class']) for entry in report['source_tree']}
    target_names = {(entry['name'], entry['native_class']) for entry in report['target_tree']}
    require(source_names == target_names, 'An existing control was lost or replaced')
    for culture in ('ru', 'en'):
        for width, height in ((1920, 1080), (1280, 720)):
            file = out / f'Menu_{culture}_{width}x{height}.png'
            require(u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(target_class, str(file),
                u.IntPoint(width, height), 'Main', culture, .5), 'Could not render ' + str(file))
            report['previews'].append(str(file))
    require(not dirty(), 'Unexpected unsaved packages were preserved')
    report['validation'] = 'Blueprint compile/save, exact control preservation, native action adapters and UI-only RU/EN renders. Root separately checks the real scene and actions in solo PIE.'
    (out / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MC_WASHNSMASH_MENU_AUTHOR_COMPLETE ' + str(out / 'Authoring.json'))
    return report
