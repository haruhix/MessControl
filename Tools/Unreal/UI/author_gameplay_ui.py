"""Create the approved editable gameplay UI. Run inside the idle MessControl editor.

Existing trees, table rows and palette edits are preserved. New native code must
already be loaded in the editor. No level, main-menu or debug edits.
"""
import json
import hashlib
from pathlib import Path
import unreal as u

BASE = '/Game/Gameplay/UI'
ROOT = Path(u.Paths.project_dir()).resolve()
HERE = ROOT / 'Tools/Unreal/UI'
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor(), 'Authoring requires idle editor'
assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.EditorAssetLibrary
report = {'tables': [], 'widgets': [], 'preserved': []}

# A repeatable dot mask gives boss health its LED identity. Explicit linear
# theme parameters avoid Slate vertex-color conversion; text and values stay UMG.
led_description = 'Theme-tinted LED grid: 72 columns x 4 rows'
led_code = 'float2 p=frac(UV*float2(72.0,4.0))-0.5; float a=saturate((0.115-dot(p,p))*80.0); return float4(Tint.rgb,Opacity*a);'
led_path = BASE + '/Materials/M_UI_LEDGrid'
led_saved = ROOT / 'Content/Gameplay/UI/Materials/M_UI_LEDGrid.uasset'
if not led_saved.exists():
    material = u.load_asset(led_path) if lib.does_asset_exist(led_path) else assets.create_asset('M_UI_LEDGrid', BASE + '/Materials', u.Material, u.MaterialFactoryNew())
    assert material, led_path
    material.set_editor_property('material_domain', u.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    edit = u.MaterialEditingLibrary
    # Resume a failed first import without touching a saved artist material.
    edit.delete_all_material_expressions(material)
    uv = edit.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -600, 0)
    tint = edit.create_material_expression(material, u.MaterialExpressionVectorParameter, -600, 200)
    tint.set_editor_property('parameter_name', 'ThemeColor')
    tint.set_editor_property('default_value', u.LinearColor(1.0, 1.0, 1.0, 1.0))
    opacity = edit.create_material_expression(material, u.MaterialExpressionScalarParameter, -600, 400)
    opacity.set_editor_property('parameter_name', 'ThemeOpacity')
    opacity.set_editor_property('default_value', 1.0)
    dots = edit.create_material_expression(material, u.MaterialExpressionCustom, -350, 0)
    custom_inputs = []
    for input_name in ('UV', 'Tint', 'Opacity'):
        entry = u.CustomInput()
        entry.set_editor_property('input_name', input_name)
        custom_inputs.append(entry)
    dots.set_editor_property('inputs', custom_inputs)
    dots.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT4)
    dots.set_editor_property('description', led_description)
    dots.set_editor_property('code', led_code)
    rgb = edit.create_material_expression(material, u.MaterialExpressionComponentMask, -100, 0)
    alpha = edit.create_material_expression(material, u.MaterialExpressionComponentMask, -100, 180)
    for node, channels in ((rgb, (True, True, True, False)), (alpha, (False, False, False, True))):
        for key, value in zip(('r', 'g', 'b', 'a'), channels):
            node.set_editor_property(key, value)
    assert edit.connect_material_expressions(uv, '', dots, 'UV')
    assert edit.connect_material_expressions(tint, '', dots, 'Tint')
    assert edit.connect_material_expressions(opacity, '', dots, 'Opacity')
    assert edit.connect_material_expressions(dots, '', rgb, '')
    assert edit.connect_material_expressions(dots, '', alpha, '')
    assert edit.connect_material_property(rgb, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    assert edit.connect_material_property(alpha, '', u.MaterialProperty.MP_OPACITY)
    edit.recompile_material(material)
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)

# Upgrade only the exact known starter custom node. Artist-authored graphs are
# preserved; explicit color/opacity parameters bypass Slate vertex conversion.
material = u.load_asset(led_path)
expressions = u.MaterialEditingLibrary.get_material_expressions(material)
dot_node = next((x for x in expressions if isinstance(x, u.MaterialExpressionCustom) and x.get_editor_property('description') == led_description and x.get_editor_property('code') == led_code), None)
if dot_node:
    edit = u.MaterialEditingLibrary
    upgraded = False
    opacity_node = next((x for x in expressions if isinstance(x, u.MaterialExpressionScalarParameter) and str(x.get_editor_property('parameter_name')) == 'ThemeOpacity'), None)
    if not opacity_node:
        opacity_node = edit.create_material_expression(material, u.MaterialExpressionScalarParameter, -600, 400)
        opacity_node.set_editor_property('parameter_name', 'ThemeOpacity')
        opacity_node.set_editor_property('default_value', 1.0)
        assert edit.connect_material_expressions(opacity_node, '', dot_node, 'Opacity')
        upgraded = True
    tint_node = next((x for x in expressions if isinstance(x, u.MaterialExpressionVectorParameter) and str(x.get_editor_property('parameter_name')) == 'ThemeColor'), None)
    if not tint_node:
        tint_node = edit.create_material_expression(material, u.MaterialExpressionVectorParameter, -600, 200)
        tint_node.set_editor_property('parameter_name', 'ThemeColor')
        tint_node.set_editor_property('default_value', u.LinearColor(1.0, 1.0, 1.0, 1.0))
        assert edit.connect_material_expressions(tint_node, '', dot_node, 'Tint')
        upgraded = True
    if upgraded:
        edit.recompile_material(material)
        assert lib.save_loaded_asset(material, only_if_is_dirty=False)

# Import only isolated image art. Text, bars, frames and layouts remain UMG.
art_specs = [('Icons_Boosters', 'T_UI_Booster_' + name + '_v10') for name in ('MouthwashWave', 'BrushFury', 'FallGrenade')]
art_specs += [('Icons_Bosses', 'T_UI_Boss_' + name + '_v10') for name in ('Caries', 'NutKnight', 'NutMage')]
card_art_ids = ('P01', 'P02', 'P03', 'X01', 'X02', 'X04', 'N01', 'P30')
art_specs += [('Icons_Cards', 'T_UI_Card_' + name + '_v10') for name in card_art_ids]
for art_folder, texture_name in art_specs:
    texture_path = BASE + '/Art/' + texture_name
    if not lib.does_asset_exist(texture_path):
        task = u.AssetImportTask()
        task.set_editor_property('filename', str(ROOT / 'ArtSource/UI/FriendlyUISuite_20261017_v10' / art_folder / (texture_name + '.png')))
        task.set_editor_property('destination_path', BASE + '/Art')
        task.set_editor_property('destination_name', texture_name)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', False)
        task.set_editor_property('save', False)
        assets.import_asset_tasks([task])
        texture = u.load_asset(texture_path)
        assert texture, texture_path
        texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property('mip_gen_settings', u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property('lod_group', u.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property('srgb', True)
        assert lib.save_loaded_asset(texture, only_if_is_dirty=False)
    else:
        report['preserved'].append(texture_path)

def make_table(name, struct_name, source):
    path = BASE + '/Data/' + name
    if lib.does_asset_exist(path):
        table = u.load_asset(path)
        report['preserved'].append(path)
    else:
        factory = u.DataTableFactory()
        factory.set_editor_property('struct', u.load_object(None, '/Script/MessControl.' + struct_name))
        table = assets.create_asset(name, BASE + '/Data', u.DataTable, factory)
        assert table, path
        payload = (HERE / source).read_text(encoding='utf-8-sig')
        assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, payload), path
        assert lib.save_loaded_asset(table, only_if_is_dirty=False), path
    report['tables'].append({'path': path, 'rows': len(u.DataTableFunctionLibrary.get_data_table_row_names(table))})
    return table

localization = make_table('DT_UI_Localization', 'MCUILocalizationRow', 'gameplay_ui_texts.json')
boosters = make_table('DT_UI_Boosters', 'MCUIBoosterRow', 'gameplay_ui_boosters.json')
cards = make_table('DT_UI_Cards', 'MCUICardRow', 'gameplay_ui_cards.json')
# Complete only empty image fields in the new starter set; preserve authored
# copy, languages and any artist replacement already present in the table.
card_rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(cards))
card_art_changed = False
for row in card_rows:
    identity = row.get('GameplayPerkID')
    if identity in card_art_ids and row.get('Image') in (None, '', 'None'):
        row['Image'] = BASE + '/Art/T_UI_Card_' + identity + '_v10.T_UI_Card_' + identity + '_v10'
        card_art_changed = True
if card_art_changed:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(cards, json.dumps(card_rows, ensure_ascii=False))
    assert lib.save_loaded_asset(cards, only_if_is_dirty=False)

theme_path = BASE + '/Theme/DA_GameplayUITheme'
if lib.does_asset_exist(theme_path):
    theme = u.load_asset(theme_path)
    report['preserved'].append(theme_path)
else:
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class', u.MCGameplayUITheme)
    theme = assets.create_asset('DA_GameplayUITheme', BASE + '/Theme', u.MCGameplayUITheme, factory)
assert theme, theme_path
font_asset = u.load_asset('/Engine/EngineFonts/Roboto')
assert font_asset
for field in ('body_font', 'heading_font', 'display_font'):
    font = theme.get_editor_property(field)
    if not font.get_editor_property('font_object'):
        font.set_editor_property('font_object', font_asset)
        theme.set_editor_property(field, font)
theme.set_editor_property('localization_table', localization)
theme.set_editor_property('booster_table', boosters)
theme.set_editor_property('card_table', cards)
portraits = dict(theme.get_editor_property('boss_portraits'))
for key, image in (('RottenGuardian', 'Caries'), ('NutKnight', 'NutKnight'), ('NutWizard', 'NutMage')):
    if key not in portraits or not str(portraits[key]):
        portraits[key] = u.load_asset(BASE + '/Art/T_UI_Boss_' + image + '_v10')
theme.set_editor_property('boss_portraits', portraits)

specs = [
    ('PerkCard', 'WBP_PerkCard_v10', 'MCPerkCardWidget'),
    ('ReserveTooth', 'WBP_ReserveTooth_v10', 'MCReserveToothWidget'),
    ('Booster', 'WBP_Booster_v10', 'MCBoosterWidget'),
    ('GameplayAlert', 'WBP_GameplayAlert_v10', 'MCGameplayAlertWidget'),
    ('BossHealth', 'WBP_BossHealth_v10', 'MCBossHealthWidget'),
    ('Stamina', 'WBP_Stamina_v10', 'MCStaminaWidget'),
    ('Scoreboard', 'WBP_Scoreboard_v10', 'MCScoreboardWidget'),
    ('Emotes', 'WBP_Emotes_v10', 'MCEmoteWidget'),
    ('Tutorial', 'WBP_Tutorial_v10', 'MCTutorialWidget'),
    ('FreezeBar', 'WBP_FreezeBar_v10', 'MCFreezeBarWidget'),
    ('DeliveryCaption', 'WBP_DeliveryCaption_v10', 'MCDeliveryZoneCaptionWidget'),
    ('UlcerProgress', 'WBP_UlcerProgress_v10', 'MCUlcerProgressWidget'),
    ('PlayerName', 'WBP_PlayerName_v10', 'MCPlayerNameWidget'),
    ('PauseMenu', 'WBP_PauseMenu_v10', 'MCGameplayMenuWidget'),
    ('ConnectionMenu', 'WBP_ConnectionMenu_v10', 'MCGameplayMenuWidget'),
    ('PerkChoice', 'WBP_PerkChoice_v10', 'MCPerkChoiceWidget'),
    ('GameplayHUD', 'WBP_GameplayHUD_v10', 'MCGameplayHUD'),
    ('ThemePreview', 'WBP_UIThemePreview', 'MCUIThemePreviewWidget'),
]
mapping = dict(theme.get_editor_property('widget_classes'))
for role, name, native in specs:
    path = BASE + '/' + name
    parent = u.load_class(None, '/Script/MessControl.' + native)
    assert parent, native
    existed = lib.does_asset_exist(path)
    bp = u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(path, parent)
    assert bp, path
    cls = u.load_class(None, path + '.' + name + '_C')
    assert cls, path
    if role == 'BossHealth':
        defaults = u.get_default_object(cls)
        if not defaults.get_editor_property('portrait_scale_by_boss'):
            # Compensate only the approved starter PNGs' transparent margins.
            # The shared envelope and aspect fit remain identical for all rows.
            extents = {}
            art_root = ROOT / 'ArtSource/UI/FriendlyUISuite_20261017_v10/Icons_Bosses'
            measured = {entry['file']: entry for entry in json.loads((art_root / 'TechnicalQA.json').read_text(encoding='utf-8-sig'))}
            for identity, icon in (('RottenGuardian', 'Caries'), ('NutKnight', 'NutKnight'), ('NutWizard', 'NutMage')):
                file = art_root / ('T_UI_Boss_' + icon + '_v10.png')
                metric = measured[file.name]
                assert hashlib.sha256(file.read_bytes()).hexdigest() == metric['sha256']
                bounds = metric['alpha16_bounds']
                extents[identity] = max(bounds[2]-bounds[0], bounds[3]-bounds[1]) / max(metric['size'])
            target = max(extents.values())
            defaults.set_editor_property('portrait_scale_by_boss', {identity: target/extent for identity, extent in extents.items()})
            u.BlueprintEditorLibrary.compile_blueprint(bp)
            assert lib.save_loaded_asset(bp, only_if_is_dirty=False)
    # Newly authored widgets use the shared palette by inheritance, with no
    # copied palette overrides. Existing role choices remain artist-owned.
    if role not in mapping or not str(mapping[role]):
        mapping[role] = cls
    # Parents constructed later must already resolve authored child WBP classes.
    theme.set_editor_property('widget_classes', mapping)
    if not existed:
        defaults = u.get_default_object(cls)
        if role == 'ConnectionMenu':
            defaults.set_editor_property('preview_state', 'Connection')
        if role == 'GameplayHUD':
            defaults.set_editor_property('preview_scenario', u.MCGameplayUIPreviewScenario.THREE_BOSSES)
        if role == 'PerkChoice':
            defaults.set_editor_property('card_widget_class', u.load_class(None, BASE + '/WBP_PerkCard_v10.WBP_PerkCard_v10_C'))
        u.BlueprintEditorLibrary.compile_blueprint(bp)
        cls = u.load_class(None, path + '.' + name + '_C')
        mapping[role] = cls
        theme.set_editor_property('widget_classes', mapping)
        assert lib.save_loaded_asset(bp, only_if_is_dirty=False)
    report['widgets'].append({'role': role, 'path': path, 'preserved': existed})
theme.set_editor_property('widget_classes', mapping)
theme.notify_theme_changed()
assert lib.save_loaded_asset(theme, only_if_is_dirty=False)
u.MCGameplayUIEditorLibrary.ensure_gameplay_ui_preview_registered()
evidence = ROOT / 'Saved/GameplayUI/authoring.json'
evidence.parent.mkdir(parents=True, exist_ok=True)
evidence.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_UI_AUTHOR_COMPLETE ' + json.dumps(report, ensure_ascii=False))
