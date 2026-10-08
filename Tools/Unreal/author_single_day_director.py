"""Author fixed difficulty-nine arena support, preserving legacy days."""
import unreal as ue

assert not ue.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved asset edits'
assert not ue.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
library = ue.EditorAssetLibrary
folder = '/Game/Gameplay/CoreLoop'
path = folder + '/DA_SingleDayDirector'
library.make_directory(folder)
profile = ue.load_asset(path)
if profile:
    assert isinstance(profile, ue.MCGameDirectorProfile), 'Unexpected support profile class'
else:
    factory = ue.DataAssetFactory()
    factory.set_editor_property('data_asset_class', ue.MCGameDirectorProfile)
    profile = ue.AssetToolsHelpers.get_asset_tools().create_asset('DA_SingleDayDirector', folder, ue.MCGameDirectorProfile, factory)
    assert profile

for name, value in dict(difficulty_multiplier=9.0, decision_interval=2.5, adaptation_seconds=16.0,
                        initial_cleaning_seconds=1.5, warning_seconds=2.0,
                        cleaning_worker_seconds=8.0, repair_worker_seconds=10.0).items():
    profile.set_editor_property(name, value)
days = list(profile.get_editor_property('days'))
assert days, 'Native profile constructor must supply day settings'
day = days[0]
# Native scaling produces exactly 9..9, including initialization and adaptation.
# Keep authored base values inside their existing 0..3 bounds.
for name, value in dict(minimum_difficulty=1.0, maximum_difficulty=1.0,
                        target_pressure_min=0.18, target_pressure_max=0.55, pressure_limit=0.95,
                        initial_patches=0, coffee_patches=2, max_food_batch=2, max_whole_food=4,
                        max_fragments=24, food_interval=5.0, food_work_per_player=32.0,
                        event_gap=6.0, rest_seconds=5.0).items():
    day.set_editor_property(name, value)
days[0] = day
profile.set_editor_property('days', days)
rules = []
for kind, weight, cooldown, difficulty in (
    (ue.MCGameDirectorEvent.FOOD, 6.0, 6.0, 0.0),
    (ue.MCGameDirectorEvent.COFFEE, 4.5, 10.0, 0.0),
    (ue.MCGameDirectorEvent.COFFEE_FLOOD, 2.0, 75.0, 0.75),
    (ue.MCGameDirectorEvent.COLD_COLA, 0.8, 70.0, 0.6),
    (ue.MCGameDirectorEvent.YAWN, 0.65, 45.0, 0.65),
    (ue.MCGameDirectorEvent.PEPPER, 2.0, 60.0, 0.85),
    (ue.MCGameDirectorEvent.STUCK_FOOD, 0.75, 35.0, 0.0),
    (ue.MCGameDirectorEvent.LOOSE_TOOTH, 0.6, 45.0, 0.0),
    (ue.MCGameDirectorEvent.REWARD, 0.0, 65.0, 0.0),
    (ue.MCGameDirectorEvent.BOSS, 0.0, 180.0, 0.0),
):
    rule = ue.MCGameDirectorEventRule()
    for name, value in dict(kind=kind, weight=weight, cooldown_seconds=cooldown, first_day=1,
                            minimum_difficulty=difficulty, max_per_day=0).items():
        rule.set_editor_property(name, value)
    rules.append(rule)
profile.set_editor_property('events', rules)
assert library.save_loaded_asset(profile)
ue.log('MC_SINGLE_DAY_DIRECTOR_READY: fixed difficulty 9, multiplier 9; ordinary arena support between key events without a run deadline')
