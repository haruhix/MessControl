"""Migrate the saved sequence to the first authored fragment; preserve unrelated tuning."""
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
support_profile = ue.load_asset(folder + '/DA_SingleDayDirector')
assert support_profile, 'Run author_single_day_director.py first'
variants = list(profile.get_editor_property('nut_rain_variants')) or [nut_profile]
slots = list(profile.get_editor_property('key_events'))
if not slots:
    slots = [ue.MCSingleDayKeyEvent()]
first = slots[0]
first.set_editor_property('event_id', 'NutEncounter')
first.set_editor_property('kind', ue.MCSingleDayKeyEventKind.NUT_ENCOUNTER)
first.set_editor_property('nut_rain_variants', variants)
first.set_editor_property('completion_experience', 25)
first.set_editor_property('director_support_seconds', 120.0)
slots[0] = first
profile.set_editor_property('key_events', slots)
profile.set_editor_property('legacy_timed_finale', False)
profile.set_editor_property('run_target_min_minutes', 25.0)
profile.set_editor_property('run_target_max_minutes', 35.0)
for name, value in dict(after_training_pause_seconds=5.0, opening_meal_items=12,
                        opening_meal_drop_seconds=1.0, before_nuts_pause_seconds=4.0).items():
    profile.set_editor_property(name, value)
assert library.save_loaded_asset(profile)

blueprint = ue.load_asset('/Game/Blueprints/BP_MouthGameMode')
assert blueprint
defaults = ue.get_default_object(blueprint.generated_class())
defaults.set_editor_property('use_single_day_loop', True)
defaults.set_editor_property('single_day_profile', profile)
defaults.set_editor_property('director_profile', support_profile)
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
ue.log('MC_SINGLE_DAY_ASSETS_READY: tutorial, personal perks, authored NutEncounter, open-ended Director support; no automatic Zombie finale')
