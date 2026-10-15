"""One owned solo PIE check of the scene-audio core; run after the native build.

Uses a transient value-copy of DA_MouthSounds and a new component on the local
controller. Existing finite SFX WAVs are technical fixtures, not delivered music.
No authored asset, controller default, map or WAV is changed. Component identity,
natural completion callbacks, playing state and a completed mixer WAV provide
evidence; SetScene's return/requested context never counts as audible proof.
"""
import array
import builtins
import json
import math
import time
import traceback
import uuid
import wave
from pathlib import Path

import unreal as u


KEY = "mc_scene_audio_solo_probe"
SETTINGS = dict(playNetMode="PIE_Standalone", playNumberOfClients=1,
                runUnderOneProcess=True, bLaunchSeparateServer=False,
                enableGameSound=True)


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return obj.get_editor_property("".join(part[:1].upper() + part[1:] for part in name.split("_")))


def path(obj):
    return obj.get_path_name() if valid(obj) else None


class SceneAudioSoloProbe:
    def __init__(self):
        self.root = Path(u.Paths.project_dir()).resolve()
        if self.root.name != "MessControl":
            raise RuntimeError("Run only in the MessControl Editor")
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        if self.levels.is_in_play_in_editor():
            raise RuntimeError("Preserve the existing PIE; this probe owns its temporary session")
        self.play = u.load_object(None, "/Script/UnrealEd.Default__LevelEditorPlaySettings")
        self.old_play = u.ToolsetLibrary.get_object_properties(self.play, list(SETTINGS))
        self.perf = u.get_default_object(u.load_class(None, "/Script/UnrealEd.EditorPerformanceSettings"))
        self.old_throttle = prop(self.perf, "bThrottleCPUWhenNotForeground")
        self.old_cvars = {name: u.SystemLibrary.get_console_variable_int_value(name)
                          for name in ("t.IdleWhenNotForeground", "au.DisableAppVolume")}
        run = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + "_" + uuid.uuid4().hex[:8]
        self.out = self.root / "Saved" / "SceneAudio_20261015" / run
        self.out.mkdir(parents=True, exist_ok=True)
        self.capture = self.out / "SceneAudio.wav"
        self.callback = self.world = self.pc = self.component = self.palette = None
        self.phase = "waiting_pie"
        self.started = self.phase_at = time.monotonic()
        self.recording = False
        self.voices = {}
        self.bindings = {}
        self.completions = {}
        self.cold_path = None
        self.report = dict(status="running", checks={}, snapshots=[], coverage_gaps=[],
                           scope="One owned standalone PIE; isolated transient palette/component; technical finite WAV fixtures",
                           audibility_basis="Master mixer capture is global output, not isolated per-layer audibility or artistic approval")
        self.write()

    def write(self):
        (self.out / "Report.json").write_text(json.dumps(self.report, indent=2, ensure_ascii=False), encoding="utf8")

    def require(self, condition, message):
        if not condition:
            raise RuntimeError(message)

    def console(self, command):
        world = self.world if valid(self.world) else u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        u.SystemLibrary.execute_console_command(world, command)

    def stage(self, name):
        self.phase, self.phase_at = name, time.monotonic()
        u.log("MC_SCENE_AUDIO_STAGE " + name)

    def start(self):
        try:
            self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(SETTINGS)), "Could not configure solo audio PIE")
            self.perf.set_editor_property("bThrottleCPUWhenNotForeground", False)
            self.console("t.IdleWhenNotForeground 0")
            self.console("au.DisableAppVolume 1")
            self.callback = u.register_slate_post_tick_callback(self.tick)
            self.levels.editor_request_begin_play()
            u.log("MC_SCENE_AUDIO_SOLO_STARTED " + str(self.out))
        except Exception:
            self.fail()

    def observe(self):
        voices = list(self.component.get_voice_components()) if valid(self.component) else []
        self.require(len(voices) <= 4, "Scene core exceeded four retained voices")
        for voice in voices:
            name = voice.get_path_name()
            self.voices[name] = voice
            if name not in self.bindings:
                self.completions[name] = 0

                def completion_callback(key):
                    def on_finished():
                        self.completions[key] += 1
                    return on_finished

                finished = completion_callback(name)
                voice.on_audio_finished.add_callable(finished)
                self.bindings[name] = finished
        self.require(sum(valid(voice) for voice in self.voices.values()) <= 4,
                     "Previously observed scene voices were dropped from inspection without destruction")
        return voices

    def snapshot(self, label):
        voices = self.observe()
        rows = [dict(component=voice.get_path_name(), source=path(prop(voice, "sound")),
                     playing=voice.is_playing(), state=str(voice.get_play_state()),
                     base_volume=prop(voice, "volume_multiplier"), completions=self.completions[voice.get_path_name()])
                for voice in voices]
        self.report["snapshots"].append(dict(stage=label, elapsed=time.monotonic() - self.started, voices=rows))
        return voices

    def select(self, scene):
        self.component.set_scene(self.palette, scene)
        self.observe()

    def sources(self):
        return {path(prop(voice, "sound")) for voice in self.observe()}

    def make_fixture(self):
        original = u.load_asset("/Game/Data/DA_MouthSounds")
        self.require(isinstance(original, u.MCSoundPalette), "Missing DA_MouthSounds")
        # Copy both data fields into /Engine/Transient; never modify the source DA.
        self.palette = u.new_object(u.MCSoundPalette, name="SceneAudioProbe_" + uuid.uuid4().hex[:8])
        self.palette.set_editor_property("events", prop(original, "events"))
        self.palette.set_editor_property("scenes", prop(original, "scenes"))
        self.require(self.palette.get_path_name().startswith("/Engine/Transient."), "Fixture palette is not transient")
        wavs = {}
        for name in ("Win", "Lose", "Complete", "DayStart"):
            sound = u.load_asset("/Game/Audio/S_" + name)
            self.require(isinstance(sound, u.SoundWave), "Missing technical WAV fixture S_" + name)
            self.require(not prop(sound, "looping"), "Technical fixture is an authored loop: " + sound.get_path_name())
            self.require(.1 < prop(sound, "duration") < 3., "Technical fixture must be a short finite WAV")
            wavs[name] = sound
        self.sources_a = {path(wavs["Win"]), path(wavs["Lose"])}
        self.sources_b = {path(wavs["Complete"]), path(wavs["DayStart"])}
        self.report["coverage_gaps"].append(
            "Cold async-load supersession is deferred: unloaded soft-reference assignment has not been verified")

        def layer(source=None):
            settings = u.MCSoundSceneLayer()
            if source is not None:
                settings.set_editor_property("sound", source)
            settings.set_editor_property("volume", .08)
            settings.set_editor_property("bRepeatOnFinish", True)
            settings.set_editor_property("fade_in_seconds", .1)
            settings.set_editor_property("fade_out_seconds", .35)
            return settings

        def scene(music=None, ambience=None):
            settings = u.MCSoundScene()
            settings.set_editor_property("music", layer(music))
            settings.set_editor_property("ambience", layer(ambience))
            return settings

        self.palette.set_editor_property("scenes", dict(
            ProbeA=scene(wavs["Win"], wavs["Lose"]),
            ProbeB=scene(wavs["Complete"], wavs["DayStart"]),
            ProbeEmpty=scene()))
        self.component = self.pc.call_method("AddComponentByClass", args=(
            u.MCSceneAudioComponent.static_class(), True, u.Transform(), False))
        self.require(isinstance(self.component, u.MCSceneAudioComponent), "Could not create registered scene component")
        self.report.update(palette_copy=self.palette.get_path_name(), source_palette=original.get_path_name(),
                           cold_async_candidate=self.cold_path, fixture_sources=sorted(self.sources_a | self.sources_b))
        u.AudioMixerLibrary.start_recording_output(self.world, 30., None)
        self.recording = True
        self.select("ProbeA")
        self.stage("repeat")

    def stop_capture(self):
        if self.recording and valid(self.world):
            u.AudioMixerLibrary.stop_recording_output(self.world, u.AudioRecordingExportType.WAV_FILE,
                                                     self.capture.stem, str(self.capture.parent), None)
        self.recording = False

    def read_capture(self):
        try:
            with wave.open(str(self.capture), "rb") as wav:
                frames, channels, width, rate = wav.getnframes(), wav.getnchannels(), wav.getsampwidth(), wav.getframerate()
                data = wav.readframes(frames)
            if width != 2 or frames < rate // 2 or len(data) != frames * channels * width:
                return None
            pcm = array.array("h", data)
            peak = max(abs(value) for value in pcm) / 32768.
            rms = math.sqrt(sum(value * value for value in pcm) / len(pcm)) / 32768.
            return dict(path=str(self.capture), seconds=frames / rate, channels=channels,
                        sample_rate=rate, peak=peak, rms=rms, bytes=self.capture.stat().st_size)
        except (OSError, EOFError, wave.Error):
            return None

    def finish(self):
        self.report["status"] = "exporting"
        self.stop_capture()
        self.stage("export")
        self.write()

    def begin_close(self):
        self.detach_callbacks()
        if valid(self.component):
            self.component.stop_scene(0.)
            self.component.destroy_component(self.pc)
        self.component = None
        if self.levels.is_in_play_in_editor():
            self.levels.editor_request_end_play()
        self.stage("closing")

    def detach_callbacks(self):
        for name, callback in self.bindings.items():
            voice = self.voices[name]
            if valid(voice):
                try:
                    voice.on_audio_finished.remove_callable(callback)
                except Exception:
                    pass
        self.bindings.clear()

    def restore(self):
        errors = []
        self.world = None  # Restore console state through the editor world after PIE teardown.
        actions = [lambda: u.ToolsetLibrary.set_object_properties(self.play, self.old_play),
                   lambda: self.perf.set_editor_property("bThrottleCPUWhenNotForeground", self.old_throttle)]
        for name, value in self.old_cvars.items():
            actions.append(lambda name=name, value=value: self.console(name + " " + str(value)))
        for action in actions:
            try:
                self.require(action() is not False, "Restore operation returned false")
            except Exception:
                errors.append(traceback.format_exc())
        self.report["restored"] = not errors
        if errors:
            self.report.update(status="failed", restore_errors=errors)
        self.report["completion_callbacks"] = self.completions
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        self.phase = "done"
        self.write()
        u.log("MC_SCENE_AUDIO_SOLO_" + self.report["status"].upper() + " " + str(self.out / "Report.json"))

    def fail(self):
        self.report.update(status="failed", error=traceback.format_exc())
        try:
            self.stop_capture()
            self.begin_close()
        except Exception:
            self.report["cleanup_error"] = traceback.format_exc()
            if self.levels.is_in_play_in_editor():
                self.levels.editor_request_end_play()
            self.stage("closing")
        self.write()
        if self.callback is None:
            self.restore()

    def tick(self, _dt):
        try:
            now, age = time.monotonic(), time.monotonic() - self.phase_at
            if self.phase == "closing":
                if not self.levels.is_in_play_in_editor():
                    self.restore()
                elif age > 10.:
                    self.report.update(status="failed", cleanup_error="Owned PIE did not finish closing within ten seconds")
                    self.restore()
                return
            self.require(now - self.started < 55., "Short scene audio probe exceeded 55 seconds")
            if self.phase == "waiting_pie":
                worlds = u.EditorLevelLibrary.get_pie_worlds(False)
                if worlds:
                    self.world = worlds[0]
                    self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                    self.require(self.pc and self.pc.is_local_controller(), "Expected local standalone controller")
                    self.stage("startup")
            elif self.phase == "startup" and age > 3.:
                if hasattr(self.pc, "is_reward_menu_open") and self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.report["initial_perk_selected_through_real_menu"] = True
                    self.stage("perk_resolution")
                elif not u.GameplayStatics.is_game_paused(self.world):
                    self.make_fixture()
            elif self.phase == "perk_resolution" and age > .5:
                if not self.pc.is_reward_menu_open() and not u.GameplayStatics.is_game_paused(self.world):
                    self.make_fixture()
                self.require(age < 12., "Initial perk choice did not resolve")
            elif self.phase == "repeat":
                voices = self.observe()
                if age > 2.8:
                    self.require(len(voices) == 2 and self.sources() == self.sources_a, "Expected both actual fixture sources")
                    self.require(all(v.is_playing() for v in voices), "Finite scene sources are not playing after their original durations")
                    self.require(all(self.completions[v.get_path_name()] >= 2 for v in voices), "No genuine finite-source completion/repetition evidence")
                    self.require(all(abs(prop(v, "volume_multiplier") - 1.) < .001 for v in voices), "Scene gain is incorrectly baked into component base volume")
                    self.report["checks"]["two_finite_sources_repeat_on_real_completion"] = True
                    self.stable_ids = {v.get_path_name() for v in voices}
                    self.counts_before = {name: self.completions[name] for name in self.stable_ids}
                    self.snapshot("repeat")
                    self.stage("unchanged")
            elif self.phase == "unchanged":
                self.select("ProbeA")  # Reassert every frame: restarting would suppress natural completion.
                if age > 1.5:
                    self.require({v.get_path_name() for v in self.observe()} == self.stable_ids, "Unchanged scene recreated voices")
                    self.require(all(self.completions[name] > self.counts_before[name] for name in self.stable_ids), "Unchanged scene repeatedly restarted a source")
                    self.report["checks"]["unchanged_scene_continues_without_restart"] = True
                    self.select("ProbeB")
                    self.require(len(self.snapshot("crossfade_entry")) == 4, "Expected simultaneous old/new voices on both layers")
                    self.stage("crossfade")
            elif self.phase == "crossfade" and age > .7:
                self.require(len(self.observe()) == 2 and self.sources() == self.sources_b, "Crossfade did not retire old voices")
                self.report["checks"]["crossfade_two_layers_bounded_four_voices"] = True
                self.cold_was_unloaded = bool(self.cold_path and u.find_object(None, self.cold_path) is None)
                if self.cold_was_unloaded:
                    self.select("ProbeCold")
                elif self.cold_path:
                    self.report["coverage_gaps"].append("Cold candidate loaded elsewhere before supersession check; cold async coverage is deferred")
                self.select("ProbeA")
                self.select("ProbeB")
                self.select("ProbeA")
                self.stage("rapid")
            elif self.phase == "rapid" and age > 1.:
                self.require(len(self.observe()) == 2 and self.sources() == self.sources_a, "Superseded selection resurfaced or leaked voices")
                self.report["checks"]["rapid_selection_keeps_latest_sources"] = True
                if self.cold_was_unloaded:
                    self.report["checks"]["cold_async_request_superseded"] = True
                self.snapshot("rapid_settled")
                self.select("ProbeEmpty")
                self.stage("empty")
            elif self.phase == "empty" and age > .7:
                self.require(not self.observe(), "Explicit empty scene did not reach silence")
                self.report["checks"]["empty_scene_releases_both_layers"] = True
                self.select("ProbeA")
                self.stage("before_stop")
            elif self.phase == "before_stop" and age > .2:
                self.require(len(self.observe()) == 2, "Stop fixture did not start")
                self.component.stop_scene(.25)
                self.stage("stop")
            elif self.phase == "stop" and age > .45:
                self.require(not self.observe(), "StopScene leaked current/fading voices")
                self.report["checks"]["stop_releases_all_voices"] = True
                self.select("ProbeA")
                self.stage("before_endplay")
            elif self.phase == "before_endplay" and age > .2:
                self.endplay_voices = list(self.observe())
                self.require(len(self.endplay_voices) == 2, "EndPlay fixture did not start")
                self.detach_callbacks()
                self.component.destroy_component(self.pc)  # Registered component destruction invokes native EndPlay.
                self.component = None
                self.stage("endplay")
            elif self.phase == "endplay" and age > .4:
                self.require(all(not valid(v) or not v.is_playing() for v in self.endplay_voices), "Component EndPlay left playing audio")
                self.report["checks"]["component_endplay_stops_owned_voices"] = True
                self.finish()
            elif self.phase == "export":
                capture = self.read_capture()
                if capture:
                    self.require(capture["peak"] > .0001, "Completed mixer WAV is silent")
                    self.report["mixer_capture"] = capture
                    self.report["checks"]["completed_mixer_output_has_nonzero_pcm"] = True
                    self.report["status"] = "passed"
                    self.begin_close()
                self.require(age < 10., "Mixer WAV export did not complete")
        except Exception:
            self.fail()


previous = getattr(builtins, KEY, None)
if previous and getattr(previous, "callback", None) is not None:
    raise RuntimeError("A scene audio probe is already active; preserve its owned session")
probe = SceneAudioSoloProbe()
setattr(builtins, KEY, probe)
probe.start()
