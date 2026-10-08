"""Enable the requested sequence on the existing game mode; preserve other authored tuning."""
import json
import unreal as ue

assert not ue.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved asset edits'
assert not ue.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
library = ue.EditorAssetLibrary
folder = '/Game/Gameplay/CoreLoop'
library.make_directory(folder)
path = folder + '/DA_SingleDay'
profile = ue.load_asset(path)
if not profile:
    factory = ue.DataAssetFactory()
    factory.set_editor_property('data_asset_class', ue.MCSingleDayProfile)
    profile = ue.AssetToolsHelpers.get_asset_tools().create_asset('DA_SingleDay', folder, ue.MCSingleDayProfile, factory)
    assert profile
    nut_profile = ue.load_asset(folder + '/DA_NutRain')
    assert nut_profile, 'Run author_nut_rain.py first'
    profile.set_editor_property('nut_rain_variants', [nut_profile])
    profile.set_editor_property('final_boss_class', ue.load_class(None, '/Game/Gameplay/Boss/BP_ZombieBoss.BP_ZombieBoss_C'))
    assert library.save_loaded_asset(profile)

blueprint = ue.load_asset('/Game/Blueprints/BP_MouthGameMode')
assert blueprint
defaults = ue.get_default_object(blueprint.generated_class())
defaults.set_editor_property('use_single_day_loop', True)
defaults.set_editor_property('single_day_profile', profile)
ue.BlueprintEditorLibrary.compile_blueprint(blueprint)
assert library.save_loaded_asset(blueprint)
assert ue.get_default_object(blueprint.generated_class()).get_editor_property('use_single_day_loop'), 'Single-day default was not preserved by Blueprint compilation'

table = ue.load_asset('/Game/Gameplay/Roguelike/DT_Perks')
assert table
rows = json.loads(ue.DataTableFunctionLibrary.export_data_table_to_json_string(table))
changed = False
recoveries = {
    'RecoverySelfHeal': ('Запасная пломба', 'Восстанавливает 25 здоровья вашему персонажу.'),
    'RecoveryMouthHeal': ('Скорая помощь', 'Восстанавливает 10 здоровья рту.'),
    'RecoveryCleanse': ('Свежее дыхание', 'Очищает вашего персонажа от кофе и устраняет расшатанность.'),
}
for row in rows:
    placeholder = 'Placeholder' in row.get('DisplayName', '') and not row.get('Description')
    if placeholder and row.get('bAvailableForLevelChoice', True):
        row['bAvailableForLevelChoice'] = False
        changed = True
existing = {row['Name'] for row in rows}
for name, (label, description) in recoveries.items():
    if name not in existing:
        rows.append(dict(Name=name, DisplayName=label, Description=description, Polarity='Positive',
                         Weight=1, MaxStacks=100, EffectClass='/Script/MessControl.MCRecoveryPerkEffect',
                         Icon='None', ToolUpgrade='None', Rarity='Standard', bAvailableForLevelChoice=True))
        changed = True
if changed:
    assert ue.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False))
    assert library.save_loaded_asset(table)
ue.log('MC_SINGLE_DAY_ASSETS_READY: short tutorial, first personal perks, walnut rain, Director interval, final boss')
