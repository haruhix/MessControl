"""Capture real UE mixer output for the six experimental MetaSounds.

Run in the MessControl Editor with Tools/Unreal/remote_python.py. This creates
no packages and restores pause/PIE state. The capture contains, for each sound:
legacy twice, low Intensity/Speed twice, high Intensity/Speed twice. JSON labels
and separate four-second WAVs make the comparison easy to review.

This verifies DSP playback and voice termination; gameplay event routing is
verified separately with mc.Audio.ExperimentLog 1 during actual actions.
"""
import array
import builtins
import json
import math
import sys
import time
import traceback
import wave
from pathlib import Path

import unreal as u


ASSETS = (
    ("Step", "Step"),
    ("KnifeSwing", "Whoosh"),
    ("KnifeHit", "Hit"),
    ("PickaxeSwing", "Whoosh"),
    ("PickaxeHit", "Hit"),
    ("Brush", "Brush"),
)


class MetaSoundCapture:
    def __init__(self):
        root = Path(u.Paths.project_dir()).resolve()
        if root.name != "MessControl":
            raise RuntimeError("Run this only in the MessControl Editor")
        self.quick = bool(getattr(builtins, "mc_audio_capture_quick", False))
        self.pair = bool(getattr(builtins, "mc_audio_capture_pair", False))
        self.asset_sequence = (tuple(row for row in ASSETS if row[0] in ("KnifeHit", "PickaxeHit"))
                               if self.pair else ASSETS[:1] if self.quick else ASSETS)
        self.duration = len(self.asset_sequence) * 4.0 + 0.1
        self.base_name = "MetaSound_Contrast" if self.pair else "MetaSound_Probe" if self.quick else "MetaSound_AB"
        self.folder = root / "Artifacts" / "AudioExperiments"
        self.folder.mkdir(parents=True, exist_ok=True)
        self.path = self.folder / (self.base_name + ".wav")
        self.report_path = self.folder / (self.base_name + ".json")
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        self.world = None
        self.started_pie = not self.levels.is_in_play_in_editor()
        self.was_paused = False
        self.pause_changed = False
        self.recording = False
        self.callback = None
        self.phase = "waiting_pie"
        self.requested_at = time.monotonic()
        self.started_at = 0.0
        self.pending = []
        self.components = []
        self.report = {
            "status": "running", "scope": "real UE mixer asset playback",
            "quick_probe": self.quick,
            "sequence": ("legacy x2, MetaSound default x2 per section" if self.pair else
                         "legacy x2, MetaSound low x2, MetaSound high x2 per section"),
            "sections": [], "events": [], "voice_checks": [],
            "baseline_note": "Knife and pickaxe use the existing shared Whoosh/Hit palette entries.",
        }
        self.assets = {}
        self.previous_app_volume = u.SystemLibrary.get_console_variable_int_value("au.DisableAppVolume")
        # This editor settings class does not generate Python glue in UE 5.8.
        self.play_settings = u.load_object(None, "/Script/UnrealEd.Default__LevelEditorPlaySettings")
        self.previous_game_sound = self.play_settings.get_editor_property("EnableGameSound")
        for event, _ in ASSETS:
            path = "/Game/Audio/Experiments/MS_" + event
            sound = u.load_asset(path)
            if not sound or sound.get_class().get_name() != "MetaSoundSource":
                raise RuntimeError("Missing MetaSound Source: " + path)
            self.assets[event] = sound
        self.palette = u.load_asset("/Game/Data/DA_MouthSounds")
        self.legacy = {}
        if self.palette:
            for name, variation in self.palette.get_editor_property("events").items():
                sounds = variation.get_editor_property("sounds")
                if sounds:
                    self.legacy[str(name)] = (sounds[0], variation.get_editor_property("volume"))
        self.write_report()

    def start(self):
        self.play_settings.set_editor_property("EnableGameSound", True)
        editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        u.SystemLibrary.execute_console_command(editor_world, "au.DisableAppVolume 1")
        self.callback = u.register_slate_post_tick_callback(self.tick)
        if self.started_pie:
            self.levels.editor_request_begin_play()
        u.log("MC_AUDIO_CAPTURE_STARTED " + str(self.report_path))

    def write_report(self):
        self.report_path.write_text(json.dumps(self.report, indent=2, ensure_ascii=False), encoding="utf-8")

    def cleanup(self):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        for component, _, _ in self.components:
            if component:
                component.stop()
                component.destroy_component(component)
        self.components.clear()
        if self.pause_changed and self.world:
            u.GameplayStatics.set_game_paused(self.world, self.was_paused)
        self.pause_changed = False
        if self.world:
            u.SystemLibrary.execute_console_command(self.world, "au.Debug.ClearSoloAudio")
        editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        u.SystemLibrary.execute_console_command(editor_world, "au.DisableAppVolume " + str(self.previous_app_volume))
        self.play_settings.set_editor_property("EnableGameSound", self.previous_game_sound)
        if self.started_pie and self.levels.is_in_play_in_editor():
            self.levels.editor_request_end_play()

    def fail(self, reason):
        self.report["status"] = "failed"
        self.report["error"] = reason
        self.write_report()
        if self.recording and self.world:
            u.AudioMixerLibrary.stop_recording_output(
                self.world, u.AudioRecordingExportType.WAV_FILE,
                "MetaSound_AB_Failed", str(self.folder), self.submix)
            self.recording = False
        self.cleanup()
        u.log_error("MC_AUDIO_CAPTURE_FAIL " + reason)

    def setup_capture(self):
        u.SystemLibrary.execute_console_command(self.world, "au.Debug.SoloAudio")
        self.was_paused = u.GameplayStatics.is_game_paused(self.world)
        u.GameplayStatics.set_game_paused(self.world, True)
        self.pause_changed = True
        # The owned PIE world is paused. Stop its existing actor audio before
        # recording master output; preview components are UI sounds and play
        # while paused. No sound asset or editor-world component is modified.
        stopped = 0
        if self.started_pie:
            for actor in u.GameplayStatics.get_all_actors_of_class(self.world, u.Actor):
                for component in actor.get_components_by_class(u.AudioComponent):
                    if component.is_playing():
                        component.stop()
                        stopped += 1
        self.submix = None
        if not self.quick:
            self.submix = u.SoundSubmix()
            self.submix.set_editor_property("auto_disable", False)
            self.submix.set_editor_property("submix_effect_chain", [], notify_mode=u.PropertyAccessChangeNotifyMode.ALWAYS)
        self.report["submix"] = self.submix.get_path_name() if self.submix else "PIE master output"
        self.report["audio_focus"] = "au.Debug.SoloAudio; au.DisableAppVolume 1; EnableGameSound true (restored afterwards)"
        self.report["stopped_owned_actor_audio"] = stopped
        self.report["world"] = self.world.get_path_name()
        for index, (event, legacy_event) in enumerate(self.asset_sequence):
            start = index * 4.0
            baseline = self.legacy.get(legacy_event)
            section = {
                "event": event, "start_seconds": start, "end_seconds": start + 4.0,
                "legacy_asset": baseline[0].get_path_name() if baseline else None,
                "metasound_asset": self.assets[event].get_path_name(),
                "legacy_window": [start + 0.25, start + 1.25],
                "low_window": [start + 1.45, start + 2.45],
                "high_window": [start + 2.65, start + 3.65],
            }
            if self.pair:
                section.update(legacy_window=[start + 0.25, start + 1.65],
                               new_window=[start + 2.15, start + 3.65])
                section.pop("low_window")
                section.pop("high_window")
            self.report["sections"].append(section)
            if baseline:
                for offset in ((0.25, 0.95) if self.pair else (0.25, 0.75)):
                    self.pending.append((start + offset, event, "legacy", baseline[0], baseline[1], 0.0, 0.0))
            variants = (("new", (2.15, 2.85), 0.65, 0.50),) if self.pair else (
                ("low", (1.45, 1.95), 0.25, 0.15),
                ("high", (2.65, 3.15), 0.95, 0.90),
            )
            for mode, offsets, intensity, speed in variants:
                for offset in offsets:
                    self.pending.append((start + offset, event, mode, self.assets[event], 1.0, intensity, speed))
        self.pending.sort(key=lambda row: row[0])
        self.phase = "mixer_warmup"
        self.phase_at = time.monotonic()

    def play(self, row, elapsed):
        planned, event, mode, sound, volume, intensity, speed = row
        component = u.GameplayStatics.create_sound2d(
            self.world, sound, volume_multiplier=volume, auto_destroy=False)
        if not component:
            raise RuntimeError("Audio device did not create a component for " + event)
        if self.submix is not None:
            component.set_submix_send(self.submix, 1.0)
        if mode != "legacy":
            component.set_float_parameter("Intensity", intensity)
            component.set_float_parameter("Speed", speed)
        component.play()
        detail = {"event": event, "mode": mode, "planned_seconds": planned,
                  "actual_seconds": elapsed, "intensity": intensity, "speed": speed,
                  "asset": sound.get_path_name()}
        self.report["events"].append(detail)
        self.components.append((component, time.monotonic(), detail))

    def check_voices(self):
        now = time.monotonic()
        for component, created_at, detail in list(self.components):
            if not component.is_playing():
                self.report["voice_checks"].append({
                    "event": detail["event"], "mode": detail["mode"],
                    "stopped_after_seconds": now - created_at})
                component.destroy_component(component)
                self.components.remove((component, created_at, detail))
            elif detail["mode"] != "legacy" and now - created_at > 1.5:
                raise RuntimeError("MetaSound did not finish: " + detail["event"])

    def inspect_wav(self):
        with wave.open(str(self.path), "rb") as recording:
            channels, width, rate, frames = recording.getnchannels(), recording.getsampwidth(), recording.getframerate(), recording.getnframes()
            pcm = recording.readframes(frames)
        if width != 2:
            raise RuntimeError("Expected int16 PCM mixer WAV")
        samples = array.array("h", pcm)
        if sys.byteorder != "little":
            samples.byteswap()
        duration = frames / rate
        if duration < self.duration - 0.5:
            raise RuntimeError("Mixer recording is incomplete: " + str(duration))
        self.report["wav"] = {"path": str(self.path), "channels": channels, "sample_rate": rate,
                              "duration_seconds": duration, "peak": max(abs(s) for s in samples) / 32768.0}
        frame_bytes = channels * width
        for section in self.report["sections"]:
            for mode in (("legacy", "new") if self.pair else ("legacy", "low", "high")):
                start, end = section[mode + "_window"]
                segment = samples[int(start * rate) * channels:int(end * rate) * channels]
                section[mode + "_rms"] = math.sqrt(sum(float(s) * s for s in segment) / max(1, len(segment))) / 32768.0
            if (section["new_rms"] < 0.00001 if self.pair else
                    section["low_rms"] < 0.00001 or section["high_rms"] < 0.00001):
                raise RuntimeError("Silent MetaSound output: " + section["event"])
            start = int(section["start_seconds"] * rate) * frame_bytes
            end = int(section["end_seconds"] * rate) * frame_bytes
            output = self.folder / (("Contrast_" if self.pair else "AB_") + section["event"] + ".wav")
            with wave.open(str(output), "wb") as clip:
                clip.setnchannels(channels)
                clip.setsampwidth(width)
                clip.setframerate(rate)
                clip.writeframes(pcm[start:end])
            section["wav"] = str(output)

    def tick(self, _delta):
        try:
            now = time.monotonic()
            if self.phase == "waiting_pie":
                worlds = u.EditorLevelLibrary.get_pie_worlds(False)
                if worlds:
                    self.world = worlds[0]
                    self.phase = "waiting_startup"
                    self.phase_at = now
                elif now - self.requested_at > 45.0:
                    raise RuntimeError("No playable PIE world appeared within 45 seconds")
            elif self.phase == "waiting_startup" and now - self.phase_at > 2.0:
                self.setup_capture()
            elif self.phase == "mixer_warmup" and now - self.phase_at > 1.0:
                u.AudioMixerLibrary.start_recording_output(self.world, self.duration + 1.0, self.submix)
                self.recording = True
                self.started_at = now
                self.phase = "recording"
            elif self.phase == "recording":
                elapsed = now - self.started_at
                while self.pending and self.pending[0][0] <= elapsed:
                    self.play(self.pending.pop(0), elapsed)
                self.check_voices()
                if elapsed > self.duration:
                    self.export_requested_at = time.time()
                    u.AudioMixerLibrary.stop_recording_output(
                        self.world, u.AudioRecordingExportType.WAV_FILE,
                        self.base_name, str(self.folder), self.submix)
                    self.recording = False
                    self.phase = "writing"
                    self.phase_at = now
            elif self.phase == "writing":
                self.check_voices()
                if self.path.exists() and self.path.stat().st_mtime >= self.export_requested_at and now - self.phase_at > 1.0:
                    self.inspect_wav()
                    self.report["status"] = "passed"
                    self.write_report()
                    self.cleanup()
                    u.log("MC_AUDIO_CAPTURE_PASS " + str(self.path))
                elif now - self.phase_at > 15.0:
                    raise RuntimeError("Mixer did not export the WAV")
        except Exception:
            self.fail(traceback.format_exc())


previous = getattr(builtins, "mc_metasound_capture", None)
if previous and getattr(previous, "callback", None) is not None:
    previous.cleanup()
mc_metasound_capture = MetaSoundCapture()
builtins.mc_metasound_capture = mc_metasound_capture
mc_metasound_capture.start()



