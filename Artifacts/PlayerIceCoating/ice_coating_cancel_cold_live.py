import json
import time
import traceback
from pathlib import Path
import unreal as u

out = Path(u.Paths.project_dir())/'Artifacts/PlayerIceCoating'
report = json.loads((out/'RuntimeVerification.json').read_text())
assert report['passed']
worlds = list(u.EditorLevelLibrary.get_pie_worlds(False))
host = next(w for w in worlds if any(h.get_local_role() == u.NetRole.ROLE_AUTHORITY for h in u.GameplayStatics.get_all_actors_of_class(w, u.MCToothCharacter)))
pc = u.GameplayStatics.get_player_controller(host, 0)
pc.request_dev_action(u.MCDevAction.ICE_EVENT, 0)
ice = next(a for a in u.GameplayStatics.get_all_actors_of_class(host, u.MCIceEvent) if a.actor_has_tag('MC_DevKeyEvent'))
ice.set_editor_property('arrival_seconds', 1.0)
ice.set_editor_property('freeze_seconds', 60.0)
ice.set_editor_property('thaw_seconds', 20.0)
ice.set_editor_property('icicle_damage', 0.0)
state = {'stage': 'arrival', 'begin': time.monotonic(), 'stopped_at': 0.0}
handle = None

def count(w):
    return sum(1 for a in u.GameplayStatics.get_all_actors_of_class(w, u.MCIceEvent) for c in a.get_components_by_class(u.SkeletalMeshComponent) if c.component_has_tag('MC_PlayerIceCoating'))

def finish(error=None):
    u.unregister_slate_post_tick_callback(handle)
    if error:
        report.update(passed=False, error=error)
    else:
        report['samples'].append({'stage': 'cancel_while_iced_clears_host_and_client', 'worlds': [{'world': w.get_path_name(), 'coating_count': count(w)} for w in worlds]})
    (out/'RuntimeVerification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('PLAYER_ICE_COLD_CANCEL_VERIFIED', json.dumps({'passed': report['passed'], 'error': error}))

def tick(dt):
    try:
        if time.monotonic()-state['begin'] > 15:
            raise AssertionError(('Cold cancel timed out', state, [count(w) for w in worlds]))
        if state['stage'] == 'arrival':
            if ice.get_editor_property('stage') != u.MCIceEventStage.ACTIVE:
                return
            players = list(ice.get_editor_property('players'))
            if not players:
                return
            for p in players:
                p.set_editor_property('amount', .72)
            ice.set_editor_property('players', players)
            state['stage'] = 'cold'
        elif state['stage'] == 'cold':
            if not all(count(w) >= 2 for w in worlds):
                return
            pc.request_dev_action(u.MCDevAction.STOP_ICE_EVENT, 0)
            state.update(stage='stopped', stopped_at=time.monotonic())
        elif state['stage'] == 'stopped':
            if time.monotonic()-state['stopped_at'] < .6 or any(count(w) for w in worlds):
                return
            finish()
    except Exception:
        finish(traceback.format_exc())

handle = u.register_slate_post_tick_callback(tick)
print('PLAYER_ICE_COLD_CANCEL_CHECK_INSTALLED')
