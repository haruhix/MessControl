"""Owned, seven-game-second listen-server/client probe on saved L_VFXLab.

Run in the idle Editor after the native build/restart. Starts two in-process
PIE worlds, selects TankCharge station 1 for both authoritative controllers,
samples real collision/visual transforms after Slate ticks at fixed 60 fps,
then ends only its PIE and restores play, frame-rate and performance settings.
No packages or native automation tests are created. Results:
Saved/Checks/NutPresentationSmoothing.json. The live probe is also available as
builtins.mc_nut_smoothing_probe; finish('cancelled') cleans it up early.
"""
import builtins
import json
import math
import time
import traceback
from pathlib import Path

import unreal as u

KEY = 'mc_nut_smoothing_probe'
SETTINGS = dict(playNetMode='PIE_ListenServer', playNumberOfClients=2,
                runUnderOneProcess=True, bLaunchSeparateServer=False,
                clientFixedFPS=[60], serverFixedFPS=0)


def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return obj.get_editor_property(''.join(s[:1].upper()+s[1:] for s in name.split('_')))


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


def distance(a, b):
    return math.sqrt(sum((x-y)**2 for x, y in zip(a, b)))


class NutSmoothingProbe:
    def __init__(self):
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        if self.levels.is_in_play_in_editor() or u.EditorLevelLibrary.get_pie_worlds(True):
            raise RuntimeError('Stop existing PIE first; the probe preserves unrelated sessions')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        if '/Game/Tests/L_VFXLab' not in self.editor_world.get_path_name():
            raise RuntimeError('Open saved /Game/Tests/L_VFXLab before running the probe')
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(SETTINGS)))
        self.engine = next(o for o in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (self.engine.get_editor_property('bUseFixedFrameRate'),
                           self.engine.get_editor_property('FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.old_idle = u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        self.old_mode = u.SystemLibrary.get_console_variable_int_value('mc.Animation.SecondaryMode')
        self.callback = None
        self.requested_pie = False
        self.done = False
        self.server = self.client = self.lab = self.station = None
        self.started = time.monotonic()
        self.capture_start = self.last_game_time = None
        self.previous = {}
        self.path = Path(u.Paths.project_saved_dir())/'Checks/NutPresentationSmoothing.json'
        self.report = dict(status='running', passed=False, station_index=1, scenario='TankCharge',
                           game_fps=60, duration_game_seconds=7., samples=[], checks={},
                           previous_play_settings=self.old_play,
                           scope='Real server/client root and PresentationRoot transforms; visual movement between network updates',
                           maximum_offset_cm=150., minimum_smoothing_frames=3)

    def write(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.report, indent=2, allow_nan=False), encoding='utf8')

    @staticmethod
    def require(value, message):
        if not value:
            raise RuntimeError(message)

    def start(self):
        self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(SETTINGS)),
                     'Could not apply two-world PIE settings')
        self.engine.set_editor_property('FixedFrameRate', 60.)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        u.SystemLibrary.execute_console_command(self.editor_world, 't.IdleWhenNotForeground 0')
        u.SystemLibrary.execute_console_command(self.editor_world, 'mc.Animation.SecondaryMode 0')
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.requested_pie = True
        self.levels.editor_request_begin_play()
        self.write()
        u.log('MC_NUT_SMOOTHING_START '+str(self.path))

    def setup(self, worlds):
        servers = [w for w in worlds if u.SystemLibrary.is_server(w)]
        clients = [w for w in worlds if not u.SystemLibrary.is_server(w)]
        if len(servers) != 1 or len(clients) != 1:
            return False
        server, client = servers[0], clients[0]
        controllers = list(u.GameplayStatics.get_all_actors_of_class(server, u.MCVFXLabPlayerController))
        local_client = u.GameplayStatics.get_player_controller(client, 0)
        if len(controllers) != 2 or not all(pc.get_controlled_pawn() for pc in controllers) or not local_client or not local_client.get_controlled_pawn():
            return False
        labs = u.GameplayStatics.get_all_actors_of_class(server, u.MCVFXLab)
        if not labs or len(prop(labs[0], 'results')) < 2:
            return False
        self.server, self.client, self.lab = server, client, labs[0]
        self.station = prop(prop(self.lab, 'results')[1], 'station')
        self.require('tankcharge' in str(prop(self.station, 'kind')).replace('_', '').lower(),
                     'Station index 1 is not TankCharge')
        # Execute the existing server path on both authoritative controllers.
        # Reflected client RPC calls from Python are intentionally not used.
        for pc in controllers:
            self.require(pc.has_authority(), 'Station selection needs server-owned controllers')
            pc.call_method('ServerSelectStation', args=(1,))
        self.lab.call_method('ResetStation', args=(1,))
        self.capture_start = float(u.GameplayStatics.get_time_seconds(server))
        self.report['worlds'] = dict(server=server.get_path_name(), client=client.get_path_name())
        self.report['server_controllers_selected'] = len(controllers)
        return True

    def sample(self, boss):
        visual = prop(boss, 'presentation_root')
        self.require(visual is not None, 'Native PresentationRoot is missing; build and restart Editor')
        root = xyz(boss.get_actor_location())
        position = xyz(visual.get_world_location())
        offset = xyz(prop(visual, 'relative_location'))
        rotation = prop(visual, 'relative_rotation')
        quat = visual.get_world_rotation().quaternion()
        finite = all(math.isfinite(v) for v in root+position+offset+
                     [rotation.pitch, rotation.yaw, rotation.roll, quat.x, quat.y, quat.z, quat.w])
        self.require(finite, 'Non-finite collision or visual transform')
        magnitude = distance(root, position)
        self.require(magnitude <= 150.01, 'Visual offset exceeded the native 150cm bound')
        authority = boss.has_authority()
        if authority:
            self.require(magnitude < .01 and distance(offset, [0., 0., 0.]) < .01,
                         'Authority acquired a client-only visual offset')
            self.require(max(abs(rotation.pitch), abs(rotation.yaw), abs(rotation.roll)) < .001,
                         'Authority acquired a client-only visual rotation')
        name = boss.get_name()
        key = ('server' if authority else 'client', name)
        previous = self.previous.get(key)
        root_delta = distance(root, previous['root']) if previous else None
        visual_delta = distance(position, previous['visual']) if previous else None
        value = dict(peer=key[0], boss=name, state=str(prop(boss, 'state')),
                     root=root, visual=position, relative_offset=offset,
                     relative_rotation=[rotation.pitch, rotation.yaw, rotation.roll],
                     world_offset_cm=magnitude, root_delta_cm=root_delta, visual_delta_cm=visual_delta)
        self.previous[key] = value
        return value

    def server_boss(self):
        # NutBoss is a private station UPROPERTY, unavailable to Python. Native
        # lab spawning owns boss -> NutRainEvent -> station on the server.
        for boss in u.GameplayStatics.get_all_actors_of_class(self.server, u.MCNutBoss):
            owner = boss.get_owner()
            if owner == self.station or (owner and owner.get_owner() == self.station):
                return boss
        return None

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 45., 'Probe wall-time limit exceeded')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.server is None:
                self.setup(worlds)
                return
            self.require(self.server in worlds and self.client in worlds, 'Owned PIE world ended during sampling')
            now = float(u.GameplayStatics.get_time_seconds(self.server))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.capture_start
            boss = self.server_boss()
            if boss:
                clients = [b for b in u.GameplayStatics.get_all_actors_of_class(self.client, u.MCNutBoss)
                           if b.get_name() == boss.get_name()]
                values = [self.sample(boss)]
                if clients:
                    values.append(self.sample(clients[0]))
                self.report['samples'].append(dict(game_seconds=elapsed, peers=values))
            if elapsed >= 7.:
                self.validate()
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def validate(self):
        frames = [p for s in self.report['samples'] for p in s['peers'] if p['peer'] == 'client']
        packets = [p for p in frames if p['root_delta_cm'] is not None and p['root_delta_cm'] > .01]
        smooth = [p for p in frames if p['root_delta_cm'] is not None and p['root_delta_cm'] < .001
                  and p['visual_delta_cm'] > .005]
        self.require(len(frames) >= 120, 'Too few real client samples')
        self.require(len(packets) >= 5, 'Too few real client root movement updates')
        self.require(len(smooth) >= 3, 'Visual did not move between unchanged collision-root frames')
        self.report['checks'] = dict(two_network_peers=True, finite_transforms=True,
                                    bounded_visual_offset=True, authority_visual_identity=True,
                                    client_root_update_frames=len(packets),
                                    client_intermediate_visual_frames=len(smooth), client_samples=len(frames))

    def finish(self, error=None):
        if self.done:
            return
        self.done = True
        restore_errors = []
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        try:
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.requested_pie and self.levels.is_in_play_in_editor():
                if self.server is None or self.server in worlds:
                    self.levels.editor_request_end_play()
                    self.report['owned_pie_end_requested'] = True
                else:
                    restore_errors.append('Owned worlds were replaced; replacement PIE preserved')
        except Exception:
            restore_errors.append(traceback.format_exc())
        actions = [
            lambda: self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(self.old_play)), 'PIE settings restore rejected'),
            lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]),
            lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]),
            lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_perf),
            lambda: u.SystemLibrary.execute_console_command(self.editor_world, 't.IdleWhenNotForeground '+str(self.old_idle)),
            lambda: u.SystemLibrary.execute_console_command(self.editor_world, 'mc.Animation.SecondaryMode '+str(self.old_mode))]
        for action in actions:
            try:
                action()
            except Exception:
                restore_errors.append(traceback.format_exc())
        if restore_errors:
            error = error or 'Setting restoration failed'
            self.report['restore_errors'] = restore_errors
        self.report.update(status='failed' if error else 'passed', passed=not bool(error), error=error,
                           restored_settings=not restore_errors, wall_seconds=time.monotonic()-self.started)
        self.write()
        u.log('MC_NUT_SMOOTHING_'+('FAIL ' if error else 'PASS ')+str(self.path))


def main():
    previous = getattr(builtins, KEY, None)
    if previous is not None and not getattr(previous, 'done', True):
        raise RuntimeError('A nut smoothing probe is already active; finish it before starting another')
    probe = NutSmoothingProbe()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
