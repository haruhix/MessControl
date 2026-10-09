import json
import time
import traceback
from pathlib import Path
import unreal as u

out = Path(u.Paths.project_dir())/'Artifacts/PlayerIceCoating'
report = json.loads((out/'RuntimeVerification.json').read_text())
assert any(sample['stage'] == 'cold_host_and_client' for sample in report['samples'])
report['passed'] = False
report['error'] = None
worlds = list(u.EditorLevelLibrary.get_pie_worlds(False))
host = next(w for w in worlds if any(h.get_local_role() == u.NetRole.ROLE_AUTHORITY for h in u.GameplayStatics.get_all_actors_of_class(w, u.MCToothCharacter)))
pc = u.GameplayStatics.get_player_controller(host, 0)
ice = next(a for a in u.GameplayStatics.get_all_actors_of_class(host, u.MCIceEvent) if a.actor_has_tag('MC_DevKeyEvent'))
heroes = [p.get_editor_property('hero') for p in ice.get_editor_property('players')]
health = {h.get_path_name(): h.get_editor_property('status').get_editor_property('state').health for h in heroes}
safe = ice.get_editor_property('tongue').get_actor_transform().transform_location(ice.get_editor_property('safe_anchor'))
for i, hero in enumerate(heroes):
    hero.get_editor_property('character_movement').stop_movement_immediately()
    hero.set_actor_location(safe+u.Vector(0, (i-.5)*70, hero.get_editor_property('capsule_component').get_scaled_capsule_half_height()+5), False, False)
for w in worlds:
    u.GameplayStatics.set_game_paused(w, False)
state = {'stage': 'thaw', 'begin': time.monotonic(), 'stage_at': time.monotonic()}
handle = None

def coatings(w):
    return [c for a in u.GameplayStatics.get_all_actors_of_class(w, u.MCIceEvent) for c in a.get_components_by_class(u.SkeletalMeshComponent) if c.component_has_tag('MC_PlayerIceCoating')]

def finish(error=None):
    report.update(passed=error is None, error=error)
    u.unregister_slate_post_tick_callback(handle)
    (out/'RuntimeVerification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('PLAYER_ICE_CLEANUP_VERIFIED', json.dumps(report))

def tick(dt):
    try:
        if time.monotonic()-state['begin'] > 15:
            raise AssertionError(('Thaw or cleanup timed out', state, [(w.get_path_name(), len(coatings(w))) for w in worlds]))
        if state['stage'] == 'thaw':
            if any(coatings(w) for w in worlds):
                return
            for hero in heroes:
                assert hero.get_editor_property('status').get_editor_property('state').health == health[hero.get_path_name()]
                actual = [hero.get_editor_property('mesh').get_material(i).get_path_name() for i in range(hero.get_editor_property('mesh').get_num_materials())]
                assert actual == report['original_materials'][hero.get_path_name()]
            report['samples'].append({'stage': 'warmth_removes_coating', 'worlds': [{'world': w.get_path_name(), 'coating_count': 0} for w in worlds], 'health_unchanged': True, 'original_body_materials_preserved': True})
            pc.request_dev_action(u.MCDevAction.STOP_ICE_EVENT, 0)
            state.update(stage='stop', stage_at=time.monotonic())
        elif state['stage'] == 'stop':
            if time.monotonic()-state['stage_at'] < .6:
                return
            assert not any(coatings(w) for w in worlds)
            report['samples'].append({'stage': 'stopped_event_has_no_coating', 'worlds': [{'world': w.get_path_name(), 'coating_count': 0} for w in worlds]})
            finish()
    except Exception:
        finish(traceback.format_exc())

handle = u.register_slate_post_tick_callback(tick)
print('PLAYER_ICE_THAW_CHECK_INSTALLED')
