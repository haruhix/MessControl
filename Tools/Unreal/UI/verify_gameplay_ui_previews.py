"""Render actual authored UMG fixtures and run isolated localization checks.

No game world, gameplay effects, travel, networking or settings changes.
"""
import json
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/GameplayUI/Previews'
OUT.mkdir(parents=True, exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
api = u.MCGameplayUIEditorLibrary
checks = api.validate_gameplay_ui_localization()
report = {'localization': {'ok': checks is not None, 'report': list(checks) if checks is not None else []}, 'renders': [], 'tables': {}}
evidence = ROOT / 'Saved/GameplayUI/preview_validation.json'
def save_report():
    evidence.write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
save_report()
for name in ('DT_UI_Boosters', 'DT_UI_Cards'):
    table = u.load_asset('/Game/Gameplay/UI/Data/' + name)
    assert table, name
    problems = u.MCUIDataLibrary.validate_content_table(table)
    report['tables'][name] = {'ok': problems is not None, 'problems': list(problems) if problems is not None else []}

cases = [
    ('WBP_GameplayHUD_v10', 'OneBoss', 1920, 1080),
    ('WBP_GameplayHUD_v10', 'TwoBosses', 1920, 1080),
    ('WBP_GameplayHUD_v10', 'ThreeBosses', 1920, 1080),
    ('WBP_GameplayHUD_v10', 'ThreeBosses', 1280, 720),
    ('WBP_GameplayHUD_v10', 'ThreeBosses', 2560, 1440),
    ('WBP_GameplayHUD_v10', 'LowResources', 1920, 1080),
    ('WBP_GameplayHUD_v10', 'LongText', 2560, 1080),
    ('WBP_PerkChoice_v10', 'Default', 1920, 1080),
    ('WBP_PerkChoice_v10', 'Risky', 1920, 1080),
    ('WBP_PerkChoice_v10', 'ContractConfirm', 1920, 1080),
    ('WBP_PerkChoice_v10', 'Pending', 1920, 1080),
    ('WBP_PauseMenu_v10', 'Default', 1920, 1080),
    ('WBP_PauseMenu_v10', 'Settings', 1920, 1080),
    ('WBP_PauseMenu_v10', 'VideoConfirm', 1920, 1080),
    ('WBP_PauseMenu_v10', 'Credits', 1920, 1080),
    ('WBP_PauseMenu_v10', 'Leave', 1920, 1080),
    ('WBP_PauseMenu_v10', 'Exit', 1920, 1080),
    ('WBP_ConnectionMenu_v10', 'Connection', 1920, 1080),
    ('WBP_Scoreboard_v10', 'LongText', 1920, 1080),
    ('WBP_Booster_v10', 'Details', 640, 360),
    ('WBP_Emotes_v10', 'Default', 1920, 1080),
    ('WBP_Tutorial_v10', 'Default', 1920, 1080),
    ('WBP_UIThemePreview', 'Default', 960, 720),
    ('WBP_FreezeBar_v10', 'Default', 500, 180),
    ('WBP_DeliveryCaption_v10', 'Default', 500, 180),
    ('WBP_UlcerProgress_v10', 'Default', 180, 180),
    ('WBP_PlayerName_v10', 'Default', 500, 180),
]
for name, state, width, height in cases:
    path = '/Game/Gameplay/UI/' + name
    cls = u.load_class(None, path + '.' + name + '_C')
    assert cls, path
    file = OUT / (name + '_' + state + '_' + str(width) + '.png')
    ok = api.render_gameplay_ui_preview(cls, str(file), u.IntPoint(width, height), state, 'ru', 1.0)
    report['renders'].append({'widget': name, 'state': state, 'size': [width, height], 'file': str(file), 'ok': ok})
    save_report()
    assert ok, str(file)

# Exercise the actual authoring contract: arbitrary row name, editable image and
# description, and en-GB -> en fallback. The sample table is transient, and the
# Blueprint default row selection is restored without saving it.
booster_table = u.load_asset('/Game/Gameplay/UI/Data/DT_UI_Boosters')
fixture_factory = u.DataTableFactory()
fixture_factory.set_editor_property('struct', u.load_object(None, '/Script/MessControl.MCUIBoosterRow'))
fixture_table = u.AssetToolsHelpers.get_asset_tools().create_asset('DT_UIBoosterValidationOnly', '/Game/Gameplay/UI/Validation', u.DataTable, fixture_factory)
assert fixture_table
fixture_data = [{
    'Name': 'AnyArtistRow', 'GameplayPerkID': 'Fallback_Mouthwash',
    'Image': '/Game/Gameplay/UI/Art/T_UI_Booster_MouthwashWave_v10.T_UI_Booster_MouthwashWave_v10',
    'DisplayName': 'Проверка редактируемой строки', 'Description': 'Текст из отдельной таблицы бустеров.',
    'Localizations': {'en': {'DisplayName': 'Rinse wave', 'Description': 'A deliberately longer editable description, with a replaceable image and a translation stored in this same booster row. The compact HUD uses its title while Details preview shows the full description.'}},
}]
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(fixture_table, json.dumps(fixture_data, ensure_ascii=False))
fixture_row = u.DataTableRowHandle(data_table=fixture_table, row_name='AnyArtistRow')
resolved = u.MCUIDataLibrary.resolve_booster(fixture_row, 'en-GB')
assert resolved.get_editor_property('found')
assert str(resolved.get_editor_property('display_name')) == 'Rinse wave'
booster_class = u.load_class(None, '/Game/Gameplay/UI/WBP_Booster_v10.WBP_Booster_v10_C')
defaults = u.get_default_object(booster_class)
original_row = defaults.get_editor_property('preview_booster_row')
original_table = original_row.get_editor_property('data_table')
original_name = original_row.get_editor_property('row_name')
try:
    defaults.set_editor_property('preview_booster_row', fixture_row)
    file = OUT / 'WBP_Booster_v10_custom-row_en-GB.png'
    assert api.render_gameplay_ui_preview(booster_class, str(file), u.IntPoint(640, 480), 'Details', 'en-GB', 0.0)
    report['booster_authoring'] = {'arbitrary_row': 'AnyArtistRow', 'culture': 'en-GB', 'resolved_title': 'Rinse wave', 'file': str(file), 'ok': True}
finally:
    defaults.set_editor_property('preview_booster_row', u.DataTableRowHandle(data_table=original_table, row_name=original_name))
    assert u.EditorAssetLibrary.delete_asset('/Game/Gameplay/UI/Validation/DT_UIBoosterValidationOnly')
save_report()
u.log('MC_UI_PREVIEW_REPORT ' + json.dumps(report, ensure_ascii=False, default=str))
