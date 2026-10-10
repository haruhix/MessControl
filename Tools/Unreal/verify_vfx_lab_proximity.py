"""Phased live-PIE proximity probe; no maps, source assets or RPCs are authored.

CLI example: python Tools/Unreal/verify_vfx_lab_proximity.py --phase init
Then clear/place, allow at least two seconds of normal PIE ticks, and capture.
Place uses authority pawn locations ONLY as a distance probe, not network input.
For transport: before-input, actual client Slate key, then after-input. The
caller supplies Slate input; this script never calls Server/Client RPC methods.
Snapshots contain primitive data only; no PIE world/actor references persist.
"""
import argparse
import json
import math
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=('init', 'clear', 'place', 'gallery', 'capture', 'before-input', 'after-input', 'restore'), default='capture')
    parser.add_argument('--label', default='capture')
    parser.add_argument('--player', type=int, default=0, help='Human controller index; local first, remote second. Place supports authority-local only.')
    parser.add_argument('--station', type=int, default=0)
    parser.add_argument('--sample', type=int, default=0)
    parser.add_argument('--gap', type=float, default=1000, help='Distance outside the platform edge in cm')
    parser.add_argument('--height', type=float, default=120, help='Height above platform top in cm')
    parser.add_argument('--side', choices=('-x', '+x', '-y', '+y'), default='-x')
    parser.add_argument('--key', choices=('PageDown', 'PageUp', 'R', 'P', 'W'), default='PageDown')
    parser.add_argument('--require-active', type=int, action='append', default=[])
    parser.add_argument('--require-inactive', type=int, action='append', default=[])
    parser.add_argument('--require-sample-visible', type=int, action='append', default=[])
    return parser.parse_args(argv)


def remote(argv):
    # Same project-scoped loopback connection used by remote_python.py.
    sys.path.insert(0, 'E:/UE/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
    import remote_execution as ue_remote
    session = ue_remote.RemoteExecution()
    try:
        session.start()
        deadline = time.monotonic() + 8
        nodes = []
        while time.monotonic() < deadline:
            nodes = [n for n in session.remote_nodes if n.get('project_name') == 'MessControl']
            if nodes:
                break
            time.sleep(.25)
        if len(nodes) != 1:
            raise RuntimeError('Expected one running MessControl editor, found ' + str(len(nodes)))
        session.open_command_connection(nodes[0]['node_id'])
        path = Path(__file__).resolve().as_posix()
        command = f"exec(compile(open({path!r},encoding='utf-8').read(),{path!r},'exec'),{{'__name__':'__main__','__file__':{path!r},'PROBE_ARGV':{argv!r}}})"
        print(json.dumps(session.run_command(command, unattended=True, raise_on_failure=True), ensure_ascii=False))
    finally:
        session.stop()


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name) if obj is not None else default
    except Exception:
        return default


def vector(point):
    return [float(point.x), float(point.y), float(point.z)]


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def bounds_distance(point, center, extent):
    return math.sqrt(sum(max(0., abs(point[i] - center[i]) - extent[i]) ** 2 for i in range(3)))


def human_controllers(u, world):
    cls = u.load_class(None, '/Script/MessControl.MCVFXLabPlayerController')
    values = u.GameplayStatics.get_all_actors_of_class(world, cls)
    return sorted(values, key=lambda pc: (not pc.call_method('IsLocalController'), pc.get_name()))


def pawn_of(pc):
    return pc.call_method('K2_GetPawn')


def player_id_of(state):
    if state is None:
        return None
    return int(state.call_method('GetPlayerId'))


def platform_bounds(u, station):
    floor = prop(station, 'Floor')
    if floor is not None:
        center, extent = floor.get_actor_bounds(True)
    else:
        return vector(station.get_actor_location()), [0., 0., 0.]
    return vector(center), vector(extent)


def snapshot(u):
    reports = []
    for world in u.EditorLevelLibrary.get_pie_worlds(False):
        catalogues = u.GameplayStatics.get_all_actors_of_class(world, u.load_class(None, '/Script/MessControl.MCVFXLab'))
        if not catalogues:
            continue
        lab = catalogues[0]
        values = json.loads(u.ToolsetLibrary.get_object_properties(lab, ['Results', 'bRunning']))
        result_rows = values.get('Results', values.get('results'))
        if result_rows is None:
            raise RuntimeError('Catalogue Results missing from ToolsetLibrary properties: ' + ', '.join(values))
        activation = prop(lab, 'ActivationDistance')
        release = prop(lab, 'ReleaseDistance')
        if activation is None or release is None:
            raise RuntimeError('Load the current proximity native classes before probing')
        actors = u.GameplayStatics.get_all_actors_of_class(world, u.Actor)
        stations = [a for a in actors if a.actor_has_tag('MCVFXLabStation')]
        names = {str(a.tags[1]) if len(a.tags) > 1 else a.get_name(): a for a in stations}
        humans = []
        for pc in human_controllers(u, world):
            pawn = pawn_of(pc)
            ps = prop(pc, 'PlayerState')
            manager = prop(pc, 'PlayerCameraManager')
            camera = manager.get_camera_location() if manager is not None else None
            humans.append(dict(controller=pc.get_name(), player_id=player_id_of(ps),
                local=pc.call_method('IsLocalController'), station_index=prop(pc, 'StationIndex'),
                pawn=pawn.get_path_name() if pawn else None, pawn_class=pawn.get_class().get_name() if pawn else None,
                position=vector(pawn.get_actor_location()) if pawn else None,
                viewer=vector(pc.call_method('GetLabViewerLocation')) if pawn else None,
                velocity=vector(pawn.get_velocity()) if pawn else None,
                camera=vector(camera) if camera else None))
        owner_counts = {a.get_path_name(): 0 for a in stations}
        for actor in actors:
            owner = actor.get_owner()
            visited = set()
            while owner is not None and owner.get_path_name() not in visited:
                path = owner.get_path_name()
                visited.add(path)
                if path in owner_counts:
                    owner_counts[path] += 1
                    break
                owner = owner.get_owner()
        rows = []
        for row in result_rows:
            station = names.get(row['name'])
            if station is None:
                raise RuntimeError('Missing static station actor ' + row['name'])
            center, extent = platform_bounds(u, station)
            points = [h['viewer'] for h in humans if h['viewer'] is not None]
            nearest = min((bounds_distance(p, center, extent) for p in points), default=None)
            rows.append(dict(name=row['name'], running=row['bRunning'], station_running=bool(prop(station, 'bRunning')),
                tick=station.is_actor_tick_enabled(), owned_actors=owner_counts[station.get_path_name()],
                cycles=row['cycles'], passed=row['passed'], failed=row['failed'], status=row['status'],
                center=center, extent=extent, human_distance_cm=nearest))
        samples = []
        local_views = [h['camera'] or h['position'] for h in humans if h['local'] and (h['camera'] or h['position'])]
        for actor in actors:
            if not actor.actor_has_tag('FabVFXLabSample') or not isinstance(actor, u.NiagaraActor):
                continue
            component = actor.get_component_by_class(u.NiagaraComponent)
            position = vector(actor.get_actor_location())
            samples.append(dict(name=actor.get_name(), active=component.is_active(), tick=component.is_component_tick_enabled(),
                visible=component.is_visible(),
                local_distance_cm=min((distance(p, position) for p in local_views), default=None)))
        samples.sort(key=lambda item: item['name'])
        reports.append(dict(world=world.get_path_name(), authority=lab.has_authority(),
            seconds=u.GameplayStatics.get_time_seconds(world), enabled=values['bRunning'],
            activation_cm=float(activation), release_cm=float(release), humans=humans,
            running=sum(r['running'] for r in rows), catalogue_count=len(rows), rows=rows, samples=samples))
    if not reports:
        raise RuntimeError('Start L_VFXLab PIE first')
    return dict(captured_at=datetime.now(timezone.utc).isoformat(), worlds=reports)


def verify(current, previous, args):
    errors = []
    for report in current['worlds']:
        if report['catalogue_count'] != 51:
            errors.append(report['world'] + ': expected 51 static catalogue rows')
        prior = next((w for w in (previous or {}).get('worlds', []) if w['world'] == report['world']), {})
        old = {r['name']: r for r in prior.get('rows', [])}
        for index, row in enumerate(report['rows']):
            if not row['running'] and row['tick']:
                errors.append(row['name'] + ': paused actor still ticks in ' + report['world'])
            if report['authority']:
                nearest = row['human_distance_cm']
                # Movement may cross an activation boundary between captures.
                # Either state is valid in the hysteresis band; explicit
                # require-active/inactive steps test its retained state.
                expected = row['running']
                if not report['enabled'] or nearest is None or nearest > max(report['activation_cm'], report['release_cm']):
                    expected = False
                elif nearest <= report['activation_cm']:
                    expected = True
                if row['running'] != expected or row['station_running'] != row['running']:
                    errors.append(row['name'] + ': running does not match human platform proximity')
                if not row['running'] and row['owned_actors']:
                    errors.append(row['name'] + ': paused station retained gameplay actors')
                if index in args.require_active and not row['running']:
                    errors.append(row['name'] + ': required nearby station did not activate')
                if index in args.require_inactive and row['running']:
                    errors.append(row['name'] + ': required distant station remained active')
                prior_row = old.get(row['name'])
                reset_capture = args.phase == 'after-input' and args.key == 'R'
                if prior_row and not row['running'] and not prior_row['running'] and not reset_capture:
                    if any(row[key] != prior_row[key] for key in ('cycles', 'passed', 'failed')):
                        errors.append(row['name'] + ': paused counters changed without a reset')
        for index, sample in enumerate(report['samples']):
            far = sample['local_distance_cm'] is None or sample['local_distance_cm'] > report['release_cm']
            if (not report['enabled'] or far) and sample['active']:
                errors.append(sample['name'] + ': distant/paused local Fab sample active')
            if (not report['enabled'] or far) and (sample['tick'] or sample['visible']):
                errors.append(sample['name'] + ': distant/paused local Fab sample visible or ticking')
            if report['authority'] and index in args.require_sample_visible and not sample['visible']:
                errors.append(sample['name'] + ': required nearby Fab sample was not made visible')
    return errors


def authority_world(u):
    return next(w for w in u.EditorLevelLibrary.get_pie_worlds(False)
        if u.GameplayStatics.get_all_actors_of_class(w, u.GameModeBase))


def observed_key(before, after, args):
    errors = []
    server_before = next(w for w in before['worlds'] if w['authority'])
    server_after = next(w for w in after['worlds'] if w['authority'])
    human_before = server_before['humans'][args.player]
    human_after = next(h for h in server_after['humans'] if h['controller'] == human_before['controller'])
    player_id = human_before['player_id']
    clients = [(w, h) for w in after['worlds'] if not w['authority'] for h in w['humans'] if h['local'] and h['player_id'] == player_id]
    if not clients:
        errors.append('No matching remote client: this capture cannot establish client RPC transport')
    if args.key in ('PageDown', 'PageUp'):
        expected = (human_before['station_index'] + (1 if args.key == 'PageDown' else -1)) % 51
        if human_after['station_index'] != expected:
            errors.append('Client navigation did not change authority StationIndex')
        for _, human in clients:
            if human['station_index'] != expected or distance(human['position'], human_after['position']) > 5:
                errors.append('Client navigation index/position did not converge with authority')
    elif args.key == 'P':
        expected = not server_before['enabled']
        if any(w['enabled'] != expected for w in after['worlds']):
            errors.append('Pause/resume state did not replicate to every world')
        if not expected and any(w['running'] for w in after['worlds']):
            errors.append('Paused catalogue still has running stations')
    elif args.key == 'R':
        index = human_after['station_index']
        if server_before['rows'][index]['cycles'] == 0:
            errors.append('No completed cycle before R; a zero-to-zero capture cannot establish reset')
        if any(w['rows'][index]['cycles'] != 0 for w in after['worlds']):
            errors.append('Current-station reset was not observed as zero cycles on both worlds')
    elif args.key == 'W':
        for world, human in clients:
            old_world = next(w for w in before['worlds'] if w['world'] == world['world'])
            old_human = next(h for h in old_world['humans'] if h['local'] and h['player_id'] == player_id)
            if distance(old_human['position'], human['position']) < 25:
                errors.append('Real client W movement was not observed')
            if distance(human['position'], human_after['viewer']) > 10:
                errors.append('Remote spectator viewer report did not converge with authority after W movement')
    return errors


def editor_main(u, args):
    output = Path(u.Paths.project_saved_dir()) / 'Codex' / 'vfx_lab_proximity_latest.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    state = json.loads(output.read_text(encoding='utf-8')) if output.exists() and args.phase != 'init' else dict(
        schema=1, scope='Human platform proximity, authoritative cleanup/ticks, local Fab distance; external Slate input only for RPC evidence',
        snapshots=[], placements=[], input_baselines={}, input_checks=[])
    before = snapshot(u)
    previous = state['snapshots'][-1]['snapshot'] if state['snapshots'] else None
    errors = []
    if args.phase in ('clear', 'place', 'gallery', 'restore'):
        world = authority_world(u)
        humans = human_controllers(u, world)
        if args.phase == 'clear':
            targets = [(pc, [-200000., -200000., 1000.]) for pc in humans if pc.call_method('IsLocalController')]
        elif args.phase == 'gallery':
            samples = [a for a in u.GameplayStatics.get_all_actors_of_class(world, u.NiagaraActor)
                if a.actor_has_tag('FabVFXLabSample')]
            samples.sort(key=lambda actor: actor.get_name())
            sample = samples[args.sample]
            point = vector(sample.get_actor_location())
            point[0] -= args.gap
            point[2] += args.height
            targets = [(humans[args.player], point)]
        elif args.phase == 'place':
            server = next(w for w in before['worlds'] if w['authority'])
            row = server['rows'][args.station]
            point = list(row['center'])
            axis = 0 if args.side.endswith('x') else 1
            point[axis] += (1 if args.side.startswith('+') else -1) * (row['extent'][axis] + args.gap)
            point[2] += row['extent'][2] + args.height
            targets = [(humans[args.player], point)]
        else:
            initial = next(w for w in state['snapshots'][0]['snapshot']['worlds'] if w['authority'])
            targets = [(pc, next(h['position'] for h in initial['humans'] if h['controller'] == pc.get_name())) for pc in humans if pc.call_method('IsLocalController')]
        for pc, point in targets:
            pawn = pawn_of(pc)
            if pawn is None or not pawn.has_authority():
                raise RuntimeError('Distance probes require an authority human pawn')
            if not pc.call_method('IsLocalController'):
                raise RuntimeError('Remote viewers use client position reports. Move the remote client through real Slate input; authority pawn placement does not update its viewer cache.')
            pawn.set_actor_location(u.Vector(*point), False, True)
            state['placements'].append(dict(phase=args.phase, controller=pc.get_name(), position=point,
                evidence='Authority distance placement only; not a client movement/RPC test'))
    elif args.phase == 'before-input':
        state['input_baselines'][args.key] = before
    else:
        errors = verify(before, previous, args)
        if args.phase == 'after-input':
            baseline = state['input_baselines'].get(args.key)
            if baseline is None:
                raise RuntimeError('Capture before-input for this key before actual Slate input')
            input_errors = observed_key(baseline, before, args)
            errors += input_errors
            state['input_checks'].append(dict(key=args.key, player=args.player, errors=input_errors,
                input_source='Actual Slate keyboard input supplied externally by caller; no Python RPC calls'))
    state['snapshots'].append(dict(phase=args.phase, label=args.label, errors=errors, snapshot=before))
    state['latest_errors'] = errors
    output.write_text(json.dumps(state, indent=2), encoding='utf-8')
    print('VFX_LAB_PROXIMITY ' + json.dumps(dict(phase=args.phase, label=args.label, errors=errors,
        worlds=[dict(authority=w['authority'], catalogue=w['catalogue_count'], running=w['running'], humans=len(w['humans'])) for w in before['worlds']], output=str(output))))
    if errors:
        raise AssertionError('; '.join(errors))


if __name__ == '__main__':
    argv = globals().get('PROBE_ARGV', sys.argv[1:])
    args = arguments(argv)
    try:
        import unreal as u
    except ImportError:
        remote(argv)
    else:
        editor_main(u, args)
