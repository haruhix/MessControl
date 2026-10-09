import json
import time
import traceback
from pathlib import Path
import unreal as u

worlds = list(u.EditorLevelLibrary.get_pie_worlds(False))
assert len(worlds) >= 2, len(worlds)
host = next(w for w in worlds if any(h.get_local_role() == u.NetRole.ROLE_AUTHORITY for h in u.GameplayStatics.get_all_actors_of_class(w, u.MCToothCharacter)))
pc = u.GameplayStatics.get_player_controller(host, 0)
print('PLAYER_ICE_START', pc.request_dev_action(u.MCDevAction.ICE_EVENT, 0))
ice = next(a for a in u.GameplayStatics.get_all_actors_of_class(host, u.MCIceEvent) if a.actor_has_tag('MC_DevKeyEvent'))
ice.set_editor_property('freeze_seconds', 60.0)
ice.set_editor_property('thaw_seconds', 1.0)
ice.set_editor_property('circle_seconds', 60.0)
ice.set_editor_property('icicle_damage', 0.0)
heroes = [h for h in u.GameplayStatics.get_all_actors_of_class(host, u.MCToothCharacter) if h.get_controller()]
originals = {h.get_path_name(): [h.get_editor_property('mesh').get_material(i).get_path_name() for i in range(h.get_editor_property('mesh').get_num_materials())] for h in heroes}
report = {'passed': False, 'world_count': len(worlds), 'material': '/Game/Gameplay/Cold/M_PlayerIceCoating', 'original_materials': originals, 'samples': []}
state = {'stage': 'arrival', 'begin': time.monotonic(), 'stage_at': time.monotonic()}
handle = None
out = Path(u.Paths.project_dir())/'Artifacts/PlayerIceCoating'

def coatings(world):
    return [(event, comp) for event in u.GameplayStatics.get_all_actors_of_class(world, u.MCIceEvent) for comp in event.get_components_by_class(u.SkeletalMeshComponent) if comp.component_has_tag('MC_PlayerIceCoating')]

def snapshot():
    rows = []
    for w in worlds:
        parts = coatings(w)
        rows.append({'world': w.get_path_name(), 'coatings': [{'parent': c.get_attach_parent().get_path_name(), 'amount': c.get_material(0).get_scalar_parameter_value('IceAmount'), 'material': c.get_material(0).get_path_name(), 'collision': str(c.get_collision_enabled())} for _, c in parts]})
    return rows

def finish(error=None):
    report.update(passed=error is None, error=error)
    u.unregister_slate_post_tick_callback(handle)
    (out/'RuntimeVerification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('PLAYER_ICE_RUNTIME_VERIFIED', json.dumps(report))

def tick(dt):
    try:
        age = time.monotonic()-state['begin']
        if age > 120:
            raise AssertionError(('preview timed out', state, snapshot()))
        if state['stage'] == 'arrival':
            if ice.get_editor_property('stage') != u.MCIceEventStage.ACTIVE:
                return
            safe = ice.get_editor_property('tongue').get_actor_transform().transform_location(ice.get_editor_property('safe_anchor'))
            for i, hero in enumerate(heroes):
                hero.set_actor_location(safe+u.Vector(650, (i-.5)*120, hero.get_editor_property('capsule_component').get_scaled_capsule_half_height()+5), False, False)
                hero.get_editor_property('character_movement').stop_movement_immediately()
            state.update(stage='seed', stage_at=time.monotonic())
        elif state['stage'] == 'seed':
            if time.monotonic()-state['stage_at'] < .6:
                return
            players = list(ice.get_editor_property('players'))
            assert len(players) == len(heroes), (len(players), len(heroes))
            for player in players:
                player.set_editor_property('amount', .72)
            ice.set_editor_property('players', players)
            state.update(stage='cold', stage_at=time.monotonic())
        elif state['stage'] == 'cold':
            rows = snapshot()
            if not all(len(row['coatings']) >= len(heroes) and all(c['amount'] > .7 for c in row['coatings']) for row in rows):
                return
            report['samples'].append({'stage': 'cold_host_and_client', 'worlds': rows})
            for hero in heroes:
                materials = [hero.get_editor_property('mesh').get_material(i).get_path_name() for i in range(hero.get_editor_property('mesh').get_num_materials())]
                assert materials == originals[hero.get_path_name()]
            # Leave the visible high-freeze preview for a screenshot before thaw.
            for w in worlds:
                u.GameplayStatics.set_game_paused(w, True)
            state.update(stage='preview', stage_at=time.monotonic())
            (out/'PreviewReady.json').write_text(json.dumps({'heroes': [h.get_path_name() for h in heroes], 'worlds': rows}, indent=2))
            print('PLAYER_ICE_PREVIEW_READY')
        elif state['stage'] == 'preview':
            if not (out/'ContinueThaw.txt').exists():
                return
            (out/'ContinueThaw.txt').unlink()
            safe = ice.get_editor_property('tongue').get_actor_transform().transform_location(ice.get_editor_property('safe_anchor'))
            for i, hero in enumerate(heroes):
                hero.set_actor_location(safe+u.Vector(0, (i-.5)*70, hero.get_editor_property('capsule_component').get_scaled_capsule_half_height()+5), False, False)
            for w in worlds:
                u.GameplayStatics.set_game_paused(w, False)
            state.update(stage='thaw', stage_at=time.monotonic())
        elif state['stage'] == 'thaw':
            if any(coatings(w) for w in worlds):
                return
            report['samples'].append({'stage': 'warmth_removes_coating', 'worlds': snapshot()})
            print('PLAYER_ICE_STOP', pc.request_dev_action(u.MCDevAction.STOP_ICE_EVENT, 0))
            state.update(stage='stop', stage_at=time.monotonic())
        elif state['stage'] == 'stop':
            if time.monotonic()-state['stage_at'] < .5:
                return
            assert not any(coatings(w) for w in worlds)
            report['samples'].append({'stage': 'stopped_event_has_no_coating', 'worlds': snapshot()})
            finish()
    except Exception:
        finish(traceback.format_exc())

handle = u.register_slate_post_tick_callback(tick)
print('PLAYER_ICE_RUNTIME_FIXTURE_INSTALLED')
