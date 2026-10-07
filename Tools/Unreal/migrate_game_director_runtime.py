"""Initialize runtime-selection fields once after compiling the new profile schema.

Run through the Unreal Editor Python API. This script preserves existing day
durations, caps, rest timings and coffee tuning; it never authors event quotas.
"""
import unreal as ue

PATH = '/Game/Data/DA_GameDirector'
VERSION_TAG = 'MC.GameDirector.RuntimeSelectionVersion'
VERSION = '1'
NEW_DAY_FIELDS = (
    'target_pressure_min', 'target_pressure_max',
    'minimum_difficulty', 'maximum_difficulty', 'initial_patches',
)
PRESERVED_DAY_FIELDS = (
    'day_seconds', 'coffee_patches', 'max_food_batch', 'max_whole_food',
    'max_fragments', 'food_interval', 'food_work_per_player', 'pressure_limit',
    'event_gap', 'rest_seconds', 'final_cleanup_seconds',
)

library = ue.EditorAssetLibrary
asset = library.load_asset(PATH)
if not isinstance(asset, ue.MCGameDirectorProfile):
    raise RuntimeError('Create DA_GameDirector before migrating runtime selection')

defaults = ue.get_default_object(ue.MCGameDirectorProfile)
days = list(asset.get_editor_property('days'))
default_days = list(defaults.get_editor_property('days'))
if len(days) != 7 or len(default_days) != 7:
    raise RuntimeError('Inspect the profile: migration requires exactly seven saved days')

if library.get_metadata_tag(asset, VERSION_TAG) == VERSION:
    ue.log('MC_DIRECTOR_RUNTIME_PROFILE_PRESERVED: migration already applied')
else:
    before = [tuple(day.get_editor_property(name) for name in PRESERVED_DAY_FIELDS)
              for day in days]
    for index, day in enumerate(days):
        for name in NEW_DAY_FIELDS:
            day.set_editor_property(name, default_days[index].get_editor_property(name))
    asset.set_editor_property('days', days)
    asset.set_editor_property('events', list(defaults.get_editor_property('events')))
    for name in ('decision_interval', 'adaptation_seconds'):
        asset.set_editor_property(name, defaults.get_editor_property(name))

    saved_days = list(asset.get_editor_property('days'))
    after = [tuple(day.get_editor_property(name) for name in PRESERVED_DAY_FIELDS)
             for day in saved_days]
    if after != before:
        raise RuntimeError('Migration unexpectedly changed existing day tuning')
    library.set_metadata_tag(asset, VERSION_TAG, VERSION)
    if not library.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Could not save the migrated Director profile')
    ue.log('MC_DIRECTOR_RUNTIME_PROFILE_MIGRATED: seven days, ten runtime event rules')

rules = list(asset.get_editor_property('events'))
if len(rules) != 10 or len({str(rule.get_editor_property('kind')) for rule in rules}) != 10:
    raise RuntimeError('Inspect the profile: expected ten unique runtime event rules')
ue.log('MC_DIRECTOR_RUNTIME_PROFILE_READY: targets=' + ','.join(
    f"{day.get_editor_property('target_pressure_min'):.3f}.."
    f"{day.get_editor_property('target_pressure_max'):.3f}"
    for day in asset.get_editor_property('days')))
