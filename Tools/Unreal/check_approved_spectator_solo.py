"""Check the actual death spectator in an already running solo PIE game.

Run only after LiveCoding/UHT succeeded, with the ordinary game started,
menus closed, and at least one reserve tooth available. This script creates
one transient MCPlaytestBotController and its normal game pawn. It kills the
local hero through the existing F3 action, then exercises the reflected
spectator input API. It does not launch PIE or a network session.

The check consumes one reserve tooth through the real respawn flow. All
changes are in PIE memory: no map, package, or asset is saved. Only the
created bot is removed. The dead hero's own RespawnAt is held for this short
capture, then advanced so the real reserve-tooth respawn cleans it up; GameMode
defaults are left untouched. Actual Shot SHOWUI frames
and timestamped evidence are written under Saved/ApprovedTasks. Hardware
mouse input still needs a brief manual check; this script supplies input
through the same approved public orbit/zoom API.

Keep PIE running until the report says complete/failed. To stop early:
    builtins.mc_approved_spectator_check.begin_cleanup('Stopped by operator')
"""
import builtins
import datetime
import json
import math
import time
import uuid
from pathlib import Path

import unreal as u


def vector(value):
    return [float(value.x), float(value.y), float(value.z)]


def rotation(value):
    return [float(value.pitch), float(value.yaw), float(value.roll)]


def angle_delta(a, b):
    return (a - b + 180.0) % 360.0 - 180.0


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


class SpectatorSoloCheck:
    def __init__(self):
        self.callback = None
        self.world = self.pc = self.mode = self.hero = None
        self.bot = self.target = self.target_camera = self.target_boom = None
        self.error = None
        self.phase = 'setup'
        self.started = time.monotonic()
        self.phase_at = self.started
        self.frame_index = 0
        self.next_frame_at = 0.0
        self.orbit_started = False
        self.zoom_started = False
        self.return_started = False
        self.initial_camera = self.target_snapshot = None
        run_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '_' + uuid.uuid4().hex[:8]
        self.output = Path(u.Paths.project_saved_dir(), 'ApprovedTasks', 'SpectatorSolo_' + run_id).resolve()
        self.frames = self.output / 'Frames'
        self.frames.mkdir(parents=True, exist_ok=False)
        self.report = {
            'status': 'running', 'phase': self.phase, 'scenario': 'actual solo PIE death spectating',
            'input': 'reflected ApplySpectatorOrbitInput / ZoomSpectatorCamera; no synthetic video',
            'capture': 'actual player viewport Shot SHOWUI', 'requested_capture_fps': 8,
            'checks': {}, 'frames': [], 'samples': [], 'cleanup': {},
            'limitation': 'Hardware mouse delivery and multiplayer are not tested.'}

    def call(self, obj, name, *args):
        try:
            return obj.call_method(name, args=args)
        except Exception as error:
            raise RuntimeError('Reflected call unavailable/failed: ' + name + ': ' + str(error)) from error

    def spawn(self, cls, pose, owner=None):
        lib = u.get_default_object(u.GameplayStatics)
        actor = self.call(lib, 'BeginDeferredActorSpawnFromClass', self.world, cls, pose,
                          u.SpawnActorCollisionHandlingMethod.ADJUST_IF_POSSIBLE_BUT_DONT_SPAWN_IF_COLLIDING,
                          owner, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT)
        if not valid(actor):
            raise RuntimeError('No collision-safe space for ' + str(cls))
        self.call(lib, 'FinishSpawningActor', actor, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT)
        return actor

    def camera_sample(self):
        manager = u.GameplayStatics.get_player_camera_manager(self.world, 0)
        return {'location': vector(manager.get_camera_location()), 'rotation': rotation(manager.get_camera_rotation()),
                'fov': float(manager.get_fov_angle())}

    def observed_camera_sample(self):
        pose = self.target_camera.get_relative_transform()
        return {'location': vector(pose.translation), 'rotation': rotation(pose.rotation.rotator()),
                'scale': vector(pose.scale3d),
                'fov': float(self.target_camera.get_editor_property('FieldOfView')),
                'arm_length': float(self.target_boom.get_editor_property('TargetArmLength')),
                'target_offset': vector(self.target_boom.get_editor_property('TargetOffset')),
                'body_rotation': rotation(self.target.get_actor_rotation()),
                'controller_rotation': rotation(self.bot.get_control_rotation())}

    def start(self):
        worlds = u.EditorLevelLibrary.get_pie_worlds(False)
        if len(worlds) != 1:
            raise RuntimeError('Exactly one already running solo PIE world is required; found ' + str(len(worlds)))
        self.world = worlds[0]
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        self.mode = u.GameplayStatics.get_game_mode(self.world)
        state = u.GameplayStatics.get_game_state(self.world)
        if not valid(self.pc) or not valid(self.mode):
            raise RuntimeError('Local authoritative game controller and GameMode required')
        if state.get_editor_property('bLobbyWaiting') or state.get_editor_property('bTutorialActive'):
            raise RuntimeError('Start the ordinary game and leave the lobby/tutorial before this check')
        if self.call(self.pc, 'IsSpectating') or self.pc.is_look_input_ignored():
            raise RuntimeError('Start alive with all menus/F3/reward choices closed')
        if self.call(state, 'AvailableArenaTeeth') < 1:
            raise RuntimeError('One available reserve tooth is required for real respawn cleanup')
        if u.GameplayStatics.get_all_actors_of_class(self.world, u.MCPlaytestSession):
            raise RuntimeError('An existing bot session must finish first; this check will not alter its bots')
        self.hero = self.pc.get_controlled_pawn()
        if not valid(self.hero) or not self.hero.get_editor_property('Status').is_alive():
            raise RuntimeError('The local hero must be alive')
        # Zero input probes establish that UHT exposed both APIs before any gameplay changes.
        self.call(self.pc, 'ApplySpectatorOrbitInput', u.Vector2D(0.0, 0.0))
        self.call(self.pc, 'ZoomSpectatorCamera', 0.0)
        self.report['checks']['reflected_input_api'] = True
        self.bot = self.spawn(u.MCPlaytestBotController, u.Transform())
        identity = self.bot.get_editor_property('PlayerState')
        # BlueprintGetter functions are reflected but intentionally omitted from
        # Python's named-method glue. call_method handles that engine API.
        if not valid(identity) or self.call(identity, 'IsSpectator') or self.call(identity, 'IsOnlyASpectator'):
            raise RuntimeError('The new playtest bot lacks a nonspectator PlayerState')
        self.report['checks']['living_target_player_state'] = identity.get_path_name()
        pawn_class = self.mode.get_editor_property('DefaultPawnClass')
        base = self.hero.get_actor_location()
        last_error = None
        for offset in (u.Vector(240, 0, 20), u.Vector(-240, 0, 20), u.Vector(0, 240, 20), u.Vector(0, -240, 20)):
            try:
                self.target = self.spawn(pawn_class, u.Transform(location=base + offset), self.bot)
                break
            except RuntimeError as error:
                last_error = error
        if not valid(self.target):
            raise RuntimeError('Could not spawn the target beside the hero: ' + str(last_error))
        self.bot.possess(self.target)
        if self.target.get_editor_property('PlayerState') != identity:
            raise RuntimeError('The observed pawn did not inherit its controller PlayerState')
        self.target_camera = self.target.get_editor_property('Camera')
        self.target_boom = self.target.get_editor_property('CameraBoom')
        self.report['target'] = self.target.get_path_name()
        self.phase = 'settle_target'
        self.phase_at = time.monotonic()
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.write()

    def take_frame(self, now):
        if now < self.next_frame_at:
            return
        # Wait for the previous real screenshot request rather than overwrite it.
        if self.report['frames'] and not Path(self.report['frames'][-1]['path']).is_file():
            return
        path = self.frames / ('%04d.png' % self.frame_index)
        u.SystemLibrary.execute_console_command(self.world, 'Shot SHOWUI filename="' + path.as_posix() + '" -nosuffix', self.pc)
        game_seconds = float(u.GameplayStatics.get_time_seconds(self.world))
        self.report['frames'].append({'index': self.frame_index, 'path': str(path), 'file': str(path),
                                      'wall_seconds': now - self.started,
                                      'game_seconds': game_seconds, 'time': game_seconds, 'phase': self.phase})
        self.frame_index += 1
        self.next_frame_at = now + 1.0 / 8.0

    def tick(self, dt):
        try:
            now = time.monotonic()
            if not valid(self.world) or not valid(self.pc):
                self.finish('PIE ended before the check completed')
                return
            if now - self.started > 35.0:
                self.finish('Check/cleanup timed out')
                return
            age = now - self.phase_at
            if self.phase == 'settle_target' and age >= 0.8:
                if not self.target.get_editor_property('Status').is_alive():
                    raise RuntimeError('The target must stay alive')
                self.target_snapshot = self.observed_camera_sample()
                self.call(self.pc, 'RequestDevAction', u.MCDevAction.KILL_SELF, -1)
                if self.hero.get_editor_property('Status').is_alive():
                    raise RuntimeError('The real KillSelf action did not kill the hero')
                state = u.GameplayStatics.get_game_state(self.world)
                server_now = float(state.get_server_world_time_seconds())
                native_respawn_at = float(self.hero.get_editor_property('RespawnAt'))
                if native_respawn_at <= server_now:
                    raise RuntimeError('The real death flow did not schedule this hero for respawn')
                held_respawn_at = server_now+20.
                self.hero.set_editor_property('RespawnAt', held_respawn_at)
                self.report['respawn_fixture'] = dict(native_respawn_at=native_respawn_at,
                    capture_hold_respawn_at=held_respawn_at,
                    reason='Hold only this dead hero during camera recording; no GameMode/CDO setting changes')
                self.phase = 'wait_spectator'
                self.phase_at = now
            elif self.phase == 'wait_spectator':
                if self.call(self.pc, 'IsSpectating') and self.call(self.pc, 'GetSpectatorTarget') == self.target:
                    self.report['checks']['entered_death_spectator'] = True
                    self.phase = 'capture'
                    self.phase_at = now
                    self.initial_camera = self.camera_sample()
                    self.next_frame_at = now
                elif age > 5.0:
                    raise RuntimeError('KillSelf was rejected or the living bot was not selected')
            elif self.phase == 'capture':
                if not self.call(self.pc, 'IsSpectating') or self.call(self.pc, 'GetSpectatorTarget') != self.target:
                    raise RuntimeError('Spectator target changed during the short check')
                if age >= 0.6 and not self.orbit_started:
                    self.call(self.pc, 'ApplySpectatorOrbitInput', u.Vector2D(260.0, 75.0))
                    self.orbit_started = True
                if age >= 2.0 and not self.return_started:
                    view = self.camera_sample()
                    yaw_change = abs(angle_delta(view['rotation'][1], self.initial_camera['rotation'][1]))
                    self.report['checks']['orbit_camera_yaw_degrees'] = yaw_change
                    if yaw_change < 5.0:
                        raise RuntimeError('Orbit input did not rotate the local camera')
                    self.call(self.pc, 'ApplySpectatorOrbitInput', u.Vector2D(-500.0, -120.0))
                    self.return_started = True
                if age >= 3.5 and not self.zoom_started:
                    self.before_zoom = self.camera_sample()
                    self.before_zoom_distance = math.dist(self.before_zoom['location'], vector(self.call(self.target, 'GetCameraFocusLocation')))
                    self.call(self.pc, 'ZoomSpectatorCamera', 2.0)
                    self.zoom_started = True
                self.take_frame(now)
                self.report['samples'].append({'wall_seconds': now - self.started, 'view': self.camera_sample()})
                if age >= 5.5:
                    after = self.observed_camera_sample()
                    self.report['observed_camera_before'] = self.target_snapshot
                    self.report['observed_camera_after'] = after
                    differences = {}
                    for key, value in after.items():
                        previous = self.target_snapshot[key]
                        difference = max(abs(a-b) for a,b in zip(value, previous)) if isinstance(value, list) else abs(value-previous)
                        if difference > 0.02:
                            differences[key] = difference
                    self.report['checks']['observed_camera_unchanged'] = not differences
                    if differences:
                        raise RuntimeError('Observed camera/body/control rotation changed: ' + str(differences))
                    self.report['checks']['zoom_view_displacement_cm'] = math.dist(self.before_zoom['location'], self.camera_sample()['location'])
                    zoom_distance = math.dist(self.camera_sample()['location'], vector(self.call(self.target, 'GetCameraFocusLocation')))
                    self.report['checks']['zoom_distance_reduction_cm'] = self.before_zoom_distance - zoom_distance
                    if self.report['checks']['zoom_distance_reduction_cm'] < 10.0:
                        raise RuntimeError('Zoom input did not move the local camera')
                    self.begin_cleanup()
            elif self.phase == 'cleanup':
                pawn = self.pc.get_controlled_pawn()
                if valid(pawn) and pawn != self.hero and pawn.get_editor_property('Status').is_alive() and not self.call(self.pc, 'IsSpectating'):
                    self.report['cleanup']['real_respawn_completed'] = True
                    self.report['cleanup']['view_target_restored'] = self.pc.get_view_target() == pawn
                    if not self.report['cleanup']['view_target_restored']:
                        raise RuntimeError('The post-respawn view target was not restored to the new hero')
                    if self.report['frames'] and not Path(self.report['frames'][-1]['path']).is_file() and age < 7.0:
                        return
                    self.finish(self.error)
                elif age > 7.0:
                    self.finish(self.error or 'Real respawn cleanup failed within seven seconds')
            self.write()
        except Exception as error:
            if self.phase == 'cleanup':
                self.finish(str(error))
            else:
                self.begin_cleanup(str(error))

    def begin_cleanup(self, error=None):
        if error:
            self.error = error
        if self.phase == 'cleanup':
            return
        self.phase = 'cleanup'
        self.phase_at = time.monotonic()
        if valid(self.hero) and not self.hero.get_editor_property('Status').is_alive():
            state = u.GameplayStatics.get_game_state(self.world)
            respawn_at = float(state.get_server_world_time_seconds())+.1
            self.hero.set_editor_property('RespawnAt', respawn_at)
            self.report['cleanup']['real_respawn_requested_at'] = respawn_at
        else:
            self.report['cleanup']['death_not_started'] = True
            self.finish(self.error)

    def write(self):
        self.report['phase'] = self.phase
        (self.output / 'Report.json').write_text(json.dumps(self.report, indent=2, ensure_ascii=False), encoding='utf-8')

    def finish(self, error=None):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        cleanup_errors = []
        for name, operation in (
            ('unpossess own bot', lambda: self.bot.un_possess() if valid(self.bot) else None),
            ('destroy own target', lambda: self.target.destroy_actor() if valid(self.target) else None),
            ('destroy own bot', lambda: self.bot.destroy_actor() if valid(self.bot) else None)):
            try:
                operation()
            except Exception as cleanup_error:
                cleanup_errors.append(name + ': ' + str(cleanup_error))
        missing = [frame['path'] for frame in self.report['frames'] if not Path(frame['path']).is_file()]
        self.report['cleanup']['errors'] = cleanup_errors
        self.report['cleanup']['game_mode_defaults_untouched'] = True
        self.report['missing_frames'] = missing
        if not error and (missing or len(self.report['frames']) < 8):
            error = 'Actual viewport capture is incomplete'
        if not error and cleanup_errors:
            error = '; '.join(cleanup_errors)
        self.report['status'] = 'failed' if error else 'complete'
        self.report['error'] = error
        self.report['wall_seconds'] = time.monotonic() - self.started
        self.report['clip_encode_command'] = ['ffmpeg', '-framerate', '8', '-i', str(self.frames / '%04d.png'),
                                               '-c:v', 'libx264', '-pix_fmt', 'yuv420p', str(self.output / 'SpectatorSolo.mp4')]
        self.phase = 'done'
        self.write()
        u.log('MC_APPROVED_SPECTATOR ' + self.report['status'] + ' ' + str(error or self.output))


previous = getattr(builtins, 'mc_approved_spectator_check', None)
if previous is not None and previous.callback is not None:
    raise RuntimeError('The previous spectator check is still running')
check = SpectatorSoloCheck()
builtins.mc_approved_spectator_check = check
try:
    check.start()
except Exception as start_error:
    check.finish(str(start_error))
    raise
print('MC_APPROVED_SPECTATOR_STARTED ' + str(check.output))
