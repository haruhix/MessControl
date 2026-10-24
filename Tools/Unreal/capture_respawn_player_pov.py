"""Capture the actual local-PC PIE SWindow of the owned player-POV sandbox.

Root starts one solo PIE on L_RespawnPlayerPOV before running this script.
Options: E:/CodexWork/RespawnPlayerPOV_20261024/CaptureOptions.json
  {"mode":"poses", "output":"E:/.../Poses01"}
  {"mode":"video", "output":"E:/.../Video01", "total":180}
Default poses: 0,18,30,38,45,52,65,79,104,179. No SceneCapture, camera override,
teleport, visibility override, overlaid text or package save. No audio recording.
Fixed 60 simulation; video samples every two engine frames. Recorded game/world/
sequence/wall timing, rather than wall-time encoding claims, is authoritative.
Finish restores sequence position/play state and engine/throttle settings, and
leaves the root-owned PIE open. Cancel via
  builtins.mc_respawn_player_pov_capture.finish('cancelled')
"""
import builtins
import hashlib
import json
import statistics
import time
import traceback
from pathlib import Path

import unreal as u


OPTIONS = Path('E:/CodexWork/RespawnPlayerPOV_20261024/CaptureOptions.json')
KEY = 'mc_respawn_player_pov_capture'
MAP = '/Game/Tests/RespawnReplacement/L_RespawnPlayerPOV'
SEQUENCE = '/Game/Tests/RespawnReplacement/LS_ReplacementPlayerPOV'
DEFAULT_POSES = [0, 18, 30, 38, 45, 52, 65, 79, 104, 179]


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


class Capture:
    def __init__(self):
        self.done = False
        self.callback = self.player = self.engine = self.perf = self.world = None
        self.old_engine = self.old_throttle = self.old_idle = self.original_time = None
        self.was_playing = False
        self.output_ready = False
        self.report, self.rows = {}, []
        self.started = time.monotonic()
        self.last_tick_engine = self.last_capture_engine = None
        self.index = 0
        self.phase = 'preflight'
        try:
            self.start()
        except Exception:
            self.finish(traceback.format_exc())
            raise

    def start(self):
        options = json.loads(OPTIONS.read_text(encoding='utf8'))
        self.mode = options.get('mode', 'poses')
        self.out = Path(options['output'])
        require(self.mode in ('poses', 'video') and self.out.is_absolute()
                and self.out.drive.upper() == 'E:', 'Expected poses/video and an absolute E: output')
        require(not any(self.out.glob('*.png')) and not (self.out/'Capture.json').exists(),
                'Choose a fresh output; existing capture files are preserved')
        self.poses = options.get('frames', DEFAULT_POSES)
        self.total = int(options.get('total', 180))
        require(self.total == 180, 'This final proposal contains exactly 180 display frames')
        require(isinstance(self.poses, list) and self.poses
                and len(set(self.poses)) == len(self.poses)
                and all(isinstance(frame, int) and 0 <= frame < 180 for frame in self.poses),
                'Pose frame list must contain unique integers in 0..179')
        worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
        require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),
                'Expected exactly the root-owned standalone authority PIE')
        self.world = worlds[0]
        # PIE package prefixes vary with instance ID; the persistent map name must match.
        require(self.world.get_path_name().split('.')[0].rsplit('/', 1)[-1].endswith('L_RespawnPlayerPOV'),
                'Only the isolated L_RespawnPlayerPOV PIE may be captured')
        require(u.GameplayStatics.get_game_mode(self.world).get_class() == u.GameModeBase.static_class(),
                'Expected neutral GameModeBase, never a production gameplay run')
        controllers = list(u.GameplayStatics.get_all_actors_of_class(self.world, u.PlayerController))
        require(len(controllers) == 1, 'Additional players are outside this solo capture')
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        require(valid(self.pc) and self.pc.is_local_controller(), 'Expected one local player controller')
        sequences = [actor for actor in u.GameplayStatics.get_all_actors_of_class(self.world, u.LevelSequenceActor)
                     if 'LoopTake' in [str(tag) for tag in actor.tags]]
        cameras = [actor for actor in u.GameplayStatics.get_all_actors_of_class(self.world, u.CineCameraActor)
                   if 'PlayerPOVCamera' in [str(tag) for tag in actor.tags]]
        require(len(sequences) == len(cameras) == 1, 'Expected the owned take and player-POV camera')
        self.actor, self.camera = sequences[0], cameras[0]
        require(self.actor.get_sequence().get_path_name().split('.')[0] == SEQUENCE,
                'Take is not LS_ReplacementPlayerPOV')
        # GetSequencePlayer is a BlueprintGetter in 5.8, not a Python method.
        self.player = self.actor.get_editor_property('sequence_player')
        require(valid(self.player), 'POV sequence player is not initialized')
        require(hasattr(u.MCGameplayUIEditorLibrary, 'capture_pie_game_window')
                and hasattr(u.MCGameplayUIEditorLibrary, 'inspect_player_view'),
                'Build the existing actual-window capture/inspector helpers first')
        self.original_time = self.player.get_current_time().time
        self.was_playing = self.player.is_playing()
        self.engine = next(obj for obj in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if obj.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (self.engine.get_editor_property('bUseFixedFrameRate'),
                           self.engine.get_editor_property('FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_throttle = self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.old_idle = u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        self.out.mkdir(parents=True, exist_ok=True)
        self.output_ready = True
        self.report = dict(status='running', map=self.world.get_path_name(), sequence=SEQUENCE,
                           mode=self.mode, frames=self.rows, audio_recorded=False, network_tested=False,
                           package_save_requested=False, production_rules_exercised=False,
                           method='MCGameplayUIEditorLibrary.capture_pie_game_window: exact local PIE SWindow/HUD/chrome',
                           camera_override_performed=False, actor_visibility_override_performed=False,
                           nominal_fixed_world_rate=60, video_sample_every_engine_frames=2,
                           nominal_video_rate=30, local_controller=self.pc.get_path_name(),
                           expected_view_target=self.camera.get_path_name(),
                           original_sequence_playing=self.was_playing)
        self.player.pause()
        self.engine.set_editor_property('FixedFrameRate', 60.0)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        u.SystemLibrary.execute_console_command(self.world, 't.IdleWhenNotForeground 0')
        self.position(0)
        self.phase = 'warmup'
        self.phase_engine = int(u.SystemLibrary.get_frame_count())
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.write()
        u.log('RESPAWN_PLAYER_POV_CAPTURE_START '+str(self.out/'Capture.json'))

    def position(self, frame):
        params = u.MovieSceneSequencePlaybackParams()
        params.set_editor_property('position_type', u.MovieScenePositionType.FRAME)
        params.set_editor_property('frame', frame if isinstance(frame, u.FrameTime)
                                   else u.FrameTime(u.FrameNumber(int(frame))))
        params.set_editor_property('update_method', u.UpdatePositionMethod.JUMP)
        self.player.set_playback_position(params)

    def inspect_view(self):
        view = json.loads(u.MCGameplayUIEditorLibrary.inspect_player_view(self.pc))
        require(view.get('Valid') and view.get('World') == self.world.get_path_name()
                and view.get('ViewTarget') == self.camera.get_path_name(),
                'Local PC did not adopt the owned POV camera cut: '+str(view))
        return view

    def capture_frame(self, engine_frame):
        view = self.inspect_view()
        now = time.monotonic()
        qt = self.player.get_current_time()
        sequence_frame = float(qt.time.frame_number.value)+float(qt.time.sub_frame)
        requested = self.poses[self.index] if self.mode == 'poses' else None
        if requested is not None:
            require(abs(sequence_frame-requested) < .01, 'Paused pose does not match the requested frame')
        name = '%04d.png' % requested if requested is not None else '%06d.png' % self.index
        target = self.out/name
        game = float(u.GameplayStatics.get_time_seconds(self.world))
        world_delta = float(u.GameplayStatics.get_world_delta_seconds(self.world))
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(target)),
                'Actual PIE SWindow capture failed')
        data = target.read_bytes()
        require(len(data) >= 24 and data[:8] == b'\x89PNG\r\n\x1a\n', 'Capture did not produce PNG')
        size = [int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')]
        require(not self.rows or size == self.rows[0]['dimensions'], 'PIE SWindow changed size during capture')
        previous = self.rows[-1] if self.rows else None
        row = dict(sequence_order=self.index, file=name, sha256=hashlib.sha256(data).hexdigest(),
                   dimensions=size, engine_frame=engine_frame, sequence_frame=sequence_frame,
                   requested_pose_frame=requested, game_seconds=game, world_delta_seconds=world_delta,
                   game_delta=None if previous is None else game-previous['game_seconds'],
                   sequence_delta=None if previous is None else sequence_frame-previous['sequence_frame'],
                   engine_frame_delta=None if previous is None else engine_frame-previous['engine_frame'],
                   wall_seconds=now-self.started, capture_wall_seconds=time.monotonic()-now,
                   view=view, camera_transform=str(self.camera.get_actor_transform()))
        self.rows.append(row)
        self.last_capture_engine = engine_frame
        self.index += 1
        if self.index % 30 == 0:
            self.write()

    def tick(self, _dt):
        if self.done:
            return
        try:
            require(time.monotonic()-self.started < 240.0, 'POV capture exceeded 240 seconds')
            require(valid(self.world) and self.world in list(u.EditorLevelLibrary.get_pie_worlds(True)),
                    'Root-owned PIE ended or was replaced; other sessions are preserved')
            frame = int(u.SystemLibrary.get_frame_count())
            if self.last_tick_engine == frame:
                return
            self.last_tick_engine = frame
            if self.phase == 'warmup':
                self.position(0)
                if frame-self.phase_engine < 24:
                    return
                self.inspect_view()
                self.player.pause()
                if self.mode == 'poses':
                    self.position(self.poses[0])
                    self.phase = 'pose_settle'
                else:
                    self.position(0)
                    self.phase = 'video_primed'
                self.phase_engine = frame
                return
            if self.phase == 'pose_settle':
                if frame-self.phase_engine < 8:
                    return
                self.capture_frame(frame)
                if self.index == len(self.poses):
                    self.player.pause()
                    self.finish()
                    return
                self.position(self.poses[self.index])
                self.phase_engine = frame
            elif self.phase == 'video_primed':
                # Frame zero is captured while paused after the warmup. Play
                # then advances two fixed-60 ticks before each subsequent PNG.
                self.capture_frame(frame)
                self.player.play()
                self.phase = 'video'
            elif self.phase == 'video':
                if frame-self.last_capture_engine < 2:
                    return
                self.capture_frame(frame)
                if self.index == self.total:
                    self.player.pause()
                    self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def write(self):
        if not self.report or not self.output_ready:
            return
        self.report['phase'] = self.phase
        self.report['frame_count'] = len(self.rows)
        if len(self.rows) > 1:
            game_span = self.rows[-1]['game_seconds']-self.rows[0]['game_seconds']
            wall_span = self.rows[-1]['wall_seconds']-self.rows[0]['wall_seconds']
            deltas = [row['game_delta'] for row in self.rows[1:]]
            self.report['timing'] = dict(game_span=game_span, wall_span=wall_span,
                measured_samples_per_game_second=(len(self.rows)-1)/game_span if game_span > 0 else None,
                measured_samples_per_wall_second=(len(self.rows)-1)/wall_span if wall_span > 0 else None,
                minimum_game_delta=min(deltas), maximum_game_delta=max(deltas),
                median_game_delta=statistics.median(deltas),
                sequence_frame_start=self.rows[0]['sequence_frame'], sequence_frame_end=self.rows[-1]['sequence_frame'],
                engine_frame_gaps=sorted({row['engine_frame_delta'] for row in self.rows[1:]}))
        (self.out/'Capture.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2,
                                                       allow_nan=False), encoding='utf8')

    def finish(self, error=None):
        if self.done:
            return
        self.done = True
        errors = []

        def restore(label, action):
            try:
                action()
            except Exception:
                errors.append(label+': '+traceback.format_exc())

        if self.callback is not None:
            restore('unregister callback', lambda: u.unregister_slate_post_tick_callback(self.callback))
            self.callback = None
        # Restore only objects from the original PIE, never a replacement run.
        owned = valid(self.world) and self.world in list(u.EditorLevelLibrary.get_pie_worlds(True))
        if valid(self.player) and owned and self.original_time is not None:
            restore('pause sequence', self.player.pause)
            restore('sequence position', lambda: self.position(self.original_time))
            if self.was_playing:
                restore('sequence playing state', self.player.play)
        if self.old_engine is not None:
            restore('frame rate', lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]))
            restore('fixed-frame enable', lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]))
        if self.old_throttle is not None:
            restore('foreground throttle', lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_throttle))
        if self.old_idle is not None and owned:
            restore('foreground idle', lambda: u.SystemLibrary.execute_console_command(
                self.world, 't.IdleWhenNotForeground '+str(self.old_idle)))
        self.report.update(status='failed' if error or errors else 'captured_pending_visual_review',
                           error=error, restoration_errors=errors, restored_settings=not errors,
                           root_owned_pie_left_open=owned, wall_seconds=time.monotonic()-self.started)
        self.write()
        u.log('RESPAWN_PLAYER_POV_CAPTURE_'+self.report['status'].upper())


previous = getattr(builtins, KEY, None)
require(previous is None or getattr(previous, 'done', True), 'A player-POV capture is already active')
setattr(builtins, KEY, Capture())
