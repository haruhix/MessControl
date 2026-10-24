"""Record two native clean-respawn checks through the actual player PIE window.

Run through Tools/Unreal/remote_python.py in the requested rebuilt Editor,
idle on clean L_Mouth with -MCDevUI -MCHubTransientProfile. This reuses every
ownership, fixture and gameplay assertion from probe_clean_respawn_solo.py.
It changes no camera, pawn transform, package or expected gameplay state.

Frames are synchronous MCGameplayUIEditorLibrary.capture_pie_game_window
PNGs, sampled every two engine frames while world time advances at fixed 60.
The measured frame/game/wall timing is retained; no real-time 30 FPS claim is
made. No audio is recorded. Encode only after inspecting Frames.json.
Cancel with builtins.mc_clean_respawn_solo.finish('cancelled').
"""
import builtins
import hashlib
import json
import runpy
import statistics
import time
import traceback
from pathlib import Path

import unreal as u


SUPPORT = runpy.run_path(str(Path(__file__).with_name('probe_clean_respawn_solo.py')),
                         run_name='record_support')
Base = SUPPORT['CleanRespawnSolo']
prop = SUPPORT['prop']
valid = SUPPORT['valid']
require = SUPPORT['require']
status = SUPPORT['status']
KEY = SUPPORT['KEY']
OUTPUT = Path('E:/DEVGAME/MessControl/Artifacts/CleanRespawnVideo_20261024')

# runpy may return a copy of the module dictionary. Set only this privately
# loaded helper's output root, through its actual function globals.
Base.__init__.__globals__['OUTPUT'] = OUTPUT


class RecordedCleanRespawnSolo(Base):
    def __init__(self):
        self.recording = False
        self.frame_rows = []
        self.phase_rows = []
        self.last_engine_frame = None
        self.last_game_seconds = None
        self.recording_started_game = None
        self.recording_started_wall = None
        self.baseline_game = None
        super().__init__()
        require(self.output.drive.upper() == 'E:', 'Recording output must stay on E:')
        self.frames_dir = self.output/'frames'
        self.frames_dir.mkdir(exist_ok=False)
        self.report['video'] = dict(
            frames_report=str(self.output/'Frames.json'), frames_directory=str(self.frames_dir),
            method='MCGameplayUIEditorLibrary.capture_pie_game_window: actual PIE SWindow/HUD',
            audio_recorded=False, camera_changed=False, pawn_teleported=False,
            nominal_fixed_world_step_seconds=1.0/60.0, sample_every_engine_frames=2,
            target_game_time_sample_rate=30.0,
            timing_note='Target cadence only; Frames.json records measured game time and wall time.',
            baseline_hold_game_seconds=1.0, post_replacement_stability_hold_game_seconds=2.0)
        self.report['limitations'].append(
            'Window captures are synchronous and may run slower than real time. '
            'Encoding cadence must be checked against the recorded simulation timestamps; '
            'there is no engine audio track.')

    def stage(self, phase):
        super().stage(phase)
        game = float(u.GameplayStatics.get_time_seconds(self.world)) if valid(self.world) else None
        self.phase_rows.append(dict(order=len(self.phase_rows), phase=phase, game_seconds=game,
                                    wall_seconds=time.monotonic()-self.started,
                                    case_index=self.case['index'] if self.case else None))
        if phase == 'baseline_settle':
            self.baseline_game = game
            self.recording_started_game = game
            self.recording_started_wall = time.monotonic()
            self.recording = True
            self.report['video']['recording_started_game_seconds'] = game
        self.write()

    def capture_frame(self):
        if not self.recording or self.done or not valid(self.world):
            return
        engine_frame = int(u.SystemLibrary.get_frame_count())
        game = float(u.GameplayStatics.get_time_seconds(self.world))
        if self.last_engine_frame is not None and engine_frame-self.last_engine_frame < 2:
            return
        if self.last_game_seconds is not None and game <= self.last_game_seconds:
            return
        index = len(self.frame_rows)
        target = self.frames_dir/('%06d.png' % index)
        wall_before = time.monotonic()
        hero = self.pc.get_controlled_pawn()
        observed = status(hero) if valid(hero) else None
        alive = bool(prop(hero, 'status').is_alive()) if valid(hero) else None
        world_delta = float(u.GameplayStatics.get_world_delta_seconds(self.world))
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(target)),
                'Actual PIE SWindow frame capture failed: ' + str(target))
        data = target.read_bytes()
        require(len(data) >= 24 and data[:8] == b'\x89PNG\r\n\x1a\n',
                'Window frame is not a valid PNG: ' + str(target))
        dimensions = [int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')]
        require(not self.frame_rows or dimensions == self.frame_rows[0]['dimensions'],
                'Actual PIE window was resized during recording')
        self.frame_rows.append(dict(
            sequence_order=index, file=str(target), sha256=hashlib.sha256(data).hexdigest(),
            dimensions=dimensions, engine_frame=engine_frame, game_seconds=game,
            world_delta_seconds=world_delta,
            game_delta_since_previous=None if self.last_game_seconds is None else game-self.last_game_seconds,
            engine_frames_since_previous=None if self.last_engine_frame is None else engine_frame-self.last_engine_frame,
            wall_seconds=wall_before-self.recording_started_wall,
            capture_wall_seconds=time.monotonic()-wall_before,
            phase=self.phase, case_index=self.case['index'] if self.case else None,
            current_pawn=hero.get_path_name() if valid(hero) else None,
            current_pawn_alive=alive, current_pawn_status=observed))
        self.last_engine_frame, self.last_game_seconds = engine_frame, game
        # Retain partial footage metadata even if an external close or failure
        # interrupts the run. This does not touch any Unreal package.
        if index % 30 == 0:
            self.write_frames('recording')

    def tick(self, dt):
        if self.done:
            return
        try:
            # These gates only lengthen the base probe's observation windows.
            # They never advance respawn time or bypass a base assertion.
            require(time.monotonic()-self.started < 120.0,
                    'Owned solo recording exceeded the base 120-second deadline')
            hold = False
            if valid(self.world):
                game = float(u.GameplayStatics.get_time_seconds(self.world))
                if self.phase == 'baseline_settle':
                    hold = game-self.baseline_game < 1.0
                elif self.phase == 'check_stability':
                    # Includes 0.65 seconds of native replacement settling;
                    # keep each complete replacement visible before the next
                    # death, and keep the second one visible before stopping.
                    hold = game-self.checked_at < 2.0
            if hold:
                require(self.world in list(u.EditorLevelLibrary.get_pie_worlds(True)),
                        'Owned PIE ended or was replaced during the recording hold')
                require(not self.pc.is_reward_menu_open(),
                        'An unrelated reward interrupted the narrow respawn recording')
            if not hold:
                super().tick(dt)
            if not self.done:
                self.capture_frame()
        except Exception:
            self.finish(traceback.format_exc())

    def write_frames(self, result):
        rows = self.frame_rows
        game_deltas = [r['game_delta_since_previous'] for r in rows[1:]]
        engine_gaps = [r['engine_frames_since_previous'] for r in rows[1:]]
        game_duration = rows[-1]['game_seconds']-rows[0]['game_seconds'] if len(rows) > 1 else 0.0
        wall_duration = rows[-1]['wall_seconds']-rows[0]['wall_seconds'] if len(rows) > 1 else 0.0
        summary = dict(
            frame_count=len(rows), game_time_span_seconds=game_duration,
            wall_time_span_seconds=wall_duration,
            measured_samples_per_game_second=(len(rows)-1)/game_duration if game_duration > 0 else None,
            measured_samples_per_wall_second=(len(rows)-1)/wall_duration if wall_duration > 0 else None,
            minimum_game_delta=min(game_deltas) if game_deltas else None,
            maximum_game_delta=max(game_deltas) if game_deltas else None,
            median_game_delta=statistics.median(game_deltas) if game_deltas else None,
            engine_frame_gaps=sorted(set(engine_gaps)),
            off_target_game_intervals=sum(abs(d-2.0/60.0) > .0001 for d in game_deltas),
            off_target_engine_intervals=sum(d != 2 for d in engine_gaps))
        payload = dict(status=result, audio_recorded=False, world=self.world.get_path_name() if valid(self.world) else None,
                       method=self.report['video']['method'], timing=summary,
                       phases=self.phase_rows, frames=rows,
                       timing_note='Read game timestamps before choosing an encoding cadence. '
                                   'No missing screenshots are duplicated or fabricated.')
        (self.output/'Frames.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2,
                                                        allow_nan=False), encoding='utf8')
        self.report['video']['timing'] = summary

    def finish(self, error=None):
        if self.done:
            return
        # Persist footage evidence before base cleanup ends this owned PIE and
        # restores fixed frame rate, play settings and editor viewport state.
        try:
            if error is None:
                require(len(self.frame_rows) > 1, 'No continuous footage was captured')
            self.write_frames('failed' if error else 'captured_pending_visual_review')
        except Exception:
            error = (error+'\n' if error else '') + 'Write frame report: ' + traceback.format_exc()
        self.recording = False
        super().finish(error)


def main():
    previous = getattr(builtins, KEY, None)
    require(previous is None or getattr(previous, 'done', True),
            'A clean-respawn probe or recording is already running')
    recording = RecordedCleanRespawnSolo()
    setattr(builtins, KEY, recording)
    try:
        recording.start()
    except Exception:
        recording.finish(traceback.format_exc())
        raise
    return recording


if __name__ == '__main__':
    main()
