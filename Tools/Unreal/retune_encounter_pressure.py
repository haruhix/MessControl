"""Save only the current encounter's pressure/size tuning; preserve art and event exclusions.

Run in Unreal Python (or -run=pythonscript when the editor is closed).
New native ability fields retain their constructor defaults after the next build.
"""
import json
import sys
from pathlib import Path

import unreal as ue


def main():
    dirty = list(ue.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(ue.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if dirty:
        raise RuntimeError('Preserve unsaved packages: ' + ', '.join(p.get_name() for p in dirty))
    path = '/Game/Gameplay/CoreLoop/DA_SingleDayDirector'
    profile = ue.load_asset(path)
    if not isinstance(profile, ue.MCGameDirectorProfile):
        raise RuntimeError('Missing saved support profile: ' + path)
    profile.set_editor_property('decision_interval', 1.25)
    days = list(profile.get_editor_property('days'))
    day = days[0]
    for name, value in dict(max_food_batch=4, food_interval=2.25, rest_seconds=1.5).items():
        day.set_editor_property(name, value)
    days[0] = day
    profile.set_editor_property('days', days)
    events = list(profile.get_editor_property('events'))
    for rule in events:
        if rule.get_editor_property('kind') == ue.MCGameDirectorEvent.FOOD and rule.get_editor_property('weight') > 0:
            rule.set_editor_property('weight', max(12.0, rule.get_editor_property('weight')))
            rule.set_editor_property('cooldown_seconds', min(3.0, rule.get_editor_property('cooldown_seconds')))
    profile.set_editor_property('events', events)
    if not ue.EditorAssetLibrary.save_loaded_asset(profile):
        raise RuntimeError('Could not save support profile')
    sys.path.insert(0, str(Path(ue.Paths.project_dir()) / 'Tools/Unreal'))
    from author_nut_rain import update_encounter_sizes
    nuts = update_encounter_sizes()
    result = dict(support=dict(path=path, difficulty_multiplier=profile.get_editor_property('difficulty_multiplier'),
                              decision_interval=profile.get_editor_property('decision_interval'),
                              food_interval=day.get_editor_property('food_interval'),
                              max_food_batch=day.get_editor_property('max_food_batch'),
                              rest_seconds=day.get_editor_property('rest_seconds')),
                  nuts=nuts)
    report = Path(ue.Paths.project_saved_dir()) / 'EncounterPressureAuthoring.json'
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    ue.log('MC_ENCOUNTER_PRESSURE_SAVED ' + json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
