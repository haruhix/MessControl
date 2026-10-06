"""Verify audio Status/Compare commands in one temporary MessControl PIE.

Run through remote_python.py after building the comparison commands. This checks
the actual console commands, four labelled slots, mode preservation, reflection
Play after changing a Sounds soft reference, restart, and EndPlay cancellation.
The editor stays open; no packages are saved. PCM audition is a separate check.
"""
import builtins
import json
import re
import time
import traceback
from pathlib import Path

import unreal as u


EVENTS = ("Step", "KnifeSwing", "KnifeHit", "PickaxeSwing", "PickaxeHit", "Brush")
COMPARE_PATTERN = re.compile(
    r"MC_AUDIO_COMPARE event=(\w+) phase=(old|new) sample=(\d+) "
    r"path=(\S+) class=(\S+) intensity=(\S+) speed=(\S+) "
    r"volume=(\S+) pitch=(\S+)")
PLAY_PATTERN = re.compile(
    r"MC_AUDIO_EXPERIMENT event=(\w+) intensity=(\S+) speed=(\S+) "
    r"owner=(\S+) net=(\d+) path=(\S+) class=(\S+)")


def asset_path(event):
    return "/Game/Audio/Experiments/MS_" + event + ".MS_" + event


class AudioComparisonVerification:
    def __init__(self):
        root = Path(u.Paths.project_dir()).resolve()
        if root.name != "MessControl":
            raise RuntimeError("Run only in the MessControl Editor")
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        if self.levels.is_in_play_in_editor():
            raise RuntimeError("This verification requires its own PIE session")
        command = u.SystemLibrary.get_command_line()
        match = re.search(r'(?:^|\s)-(?:abslog|log)=(?:"([^"]+)"|(\S+))', command, re.IGNORECASE)
        if match:
            self.log_path = Path(match.group(1) or match.group(2))
            if not self.log_path.is_absolute():
                self.log_path = Path(u.Paths.project_log_dir()) / self.log_path
        else:
            candidates = list((root / "Saved").glob("MetaSound*.log"))
            self.log_path = max(candidates, key=lambda path: path.stat().st_mtime) if candidates else root / "Saved" / "Logs" / "MessControl.log"
        self.path = root / "Artifacts" / "AudioExperiments" / "ComparisonVerification.json"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.world = None
        self.hero = None
        self.audio = None
        self.original_sounds = None
        self.callback = None
        self.phase = "waiting_pie"
        self.requested_at = time.monotonic()
        self.phase_at = self.requested_at
        self.game_at = 0.0
        self.log_offset = 0
        self.previous_mode = u.SystemLibrary.get_console_variable_int_value("mc.Audio.Experiments")
        self.previous_log = u.SystemLibrary.get_console_variable_int_value("mc.Audio.ExperimentLog")
        self.previous_idle = u.SystemLibrary.get_console_variable_int_value("t.IdleWhenNotForeground")
        self.expected_mode = self.previous_mode
        self.report = {
            "status": "running", "scope": "owned PIE comparison routing and cancellation",
            "audio_output_recorded": False, "log_path": str(self.log_path),
            "original_mode": self.previous_mode, "checks": {},
        }
        self.write_report()

    def write_report(self):
        self.path.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding="utf-8")

    @staticmethod
    def require(condition, reason):
        if not condition:
            raise RuntimeError(reason)

    def console(self, command):
        world = self.world or u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        u.SystemLibrary.execute_console_command(world, command)

    def set_mode(self, mode):
        self.console("mc.Audio.Experiments " + str(mode))
        self.expected_mode = mode

    def start(self):
        self.console("mc.Audio.ExperimentLog 1")
        self.console("t.IdleWhenNotForeground 0")
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.levels.editor_request_begin_play()
        u.log("MC_AUDIO_COMPARISON_VERIFICATION_STARTED")

    def cleanup(self):
        # Remove the callback before touching UObjects, including on exceptions.
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        if self.levels.is_in_play_in_editor():
            try:
                if self.audio and self.original_sounds is not None:
                    self.audio.set_editor_property("sounds", self.original_sounds)
            finally:
                self.levels.editor_request_end_play()
        self.world = None
        self.hero = None
        self.audio = None
        self.console("mc.Audio.Experiments " + str(self.previous_mode))
        self.console("mc.Audio.ExperimentLog " + str(self.previous_log))
        self.console("t.IdleWhenNotForeground " + str(self.previous_idle))

    def log_since_stage(self):
        self.require(self.log_path.exists(), "The active editor log is missing")
        return self.log_path.read_bytes()[self.log_offset:].decode("utf-8", errors="replace")

    def stage(self, name):
        self.phase = name
        self.phase_at = time.monotonic()
        self.game_at = u.GameplayStatics.get_time_seconds(self.world) if self.world else 0.0
        self.log_offset = self.log_path.stat().st_size if self.log_path.exists() else 0
        u.log("MC_AUDIO_COMPARISON_CHECK stage=" + name)

    @staticmethod
    def comparison_rows(log):
        keys = ("event", "phase", "sample", "path", "class", "intensity", "speed", "volume", "pitch")
        rows = []
        for match in COMPARE_PATTERN.finditer(log):
            row = dict(zip(keys, match.groups()))
            row["sample"] = int(row["sample"])
            for key in ("intensity", "speed", "volume", "pitch"):
                row[key] = float(row[key])
            rows.append(row)
        return rows

    def check_sequence(self, name, rows):
        expected = [("old", 1), ("old", 2), ("new", 1), ("new", 2)]
        self.require([(row["phase"], row["sample"]) for row in rows] == expected,
                     name + " did not produce exactly OLDx2 then NEWx2")
        for row in rows:
            self.require(row["event"] == name, "A cancelled comparison continued")
            path = "/Game/Audio/S_Hit.S_Hit" if row["phase"] == "old" else asset_path(name)
            sound_class = "SoundWave" if row["phase"] == "old" else "MetaSoundSource"
            self.require(row["path"] == path and row["class"] == sound_class,
                         "Unexpected comparison source: " + str(row))
            self.require(row["intensity"] == .65 and row["speed"] == .5,
                         "Comparison did not use the declared graph inputs")

    def setup(self):
        self.hero = u.GameplayStatics.get_player_pawn(self.world, 0)
        self.require(isinstance(self.hero, u.MCToothCharacter), "PIE did not spawn MCToothCharacter")
        self.gs = u.GameplayStatics.get_game_state(self.world)
        self.hero.call_method("ServerSetPrimary", args=(False,))
        self.hero.get_movement_component().stop_movement_immediately()
        self.hero.get_movement_component().disable_movement()
        self.audio = self.hero.experimental_audio
        self.require(self.audio is not None, "The hero has no experimental audio component")
        self.stage("status")
        self.console("mc.Audio.Status")

    def begin_comparison(self, event, mode, name):
        self.set_mode(mode)
        self.stage(name)
        self.console("mc.Audio.Compare " + event)

    def play_reflected(self, expected_path):
        # Play is the existing BlueprintCallable UFUNCTION. Its logged resolved
        # path verifies the cache without reading private implementation fields.
        result = self.audio.call_method("Play", args=("KnifeHit", self.hero.get_actor_location(), .65, .5))
        self.require(result is True, "Reflected Play returned false for " + expected_path)

    def check_play_path(self, expected_path):
        rows = list(PLAY_PATTERN.finditer(self.log_since_stage()))
        self.require(len(rows) == 1, "Expected exactly one reflected Play event")
        event, intensity, speed, owner, net, path, sound_class = rows[0].groups()
        self.require(event == "KnifeHit" and path == expected_path and sound_class == "MetaSoundSource",
                     "Sounds map replacement left a stale cached sound: " + path)
        return {"event": event, "path": path, "class": sound_class, "intensity": float(intensity),
                "speed": float(speed), "owner": owner, "net": int(net)}

    def finish(self):
        self.report["status"] = "passed"
        self.report["mode_restored_to"] = self.previous_mode
        self.cleanup()
        self.write_report()
        u.log("MC_AUDIO_COMPARISON_VERIFICATION_PASS " + str(self.path))

    def tick(self, _delta):
        try:
            now = time.monotonic()
            elapsed = now - self.phase_at
            self.require(u.SystemLibrary.get_console_variable_int_value("mc.Audio.Experiments") == self.expected_mode,
                         "A comparison/status command changed mc.Audio.Experiments")
            if now - self.requested_at > 90:
                raise RuntimeError("Comparison verification exceeded 90 seconds")
            if self.hero:
                self.gs.set_editor_property("dev_manual_events", True)
                self.gs.set_editor_property("physical_brushes", False)
                self.gs.set_editor_property("phase", u.MCShiftPhase.INTERMISSION)
                self.gs.set_editor_property("phase_ends_at", u.GameplayStatics.get_time_seconds(self.world) + 300)
            game_elapsed = u.GameplayStatics.get_time_seconds(self.world) - self.game_at if self.world else 0
            if self.phase == "waiting_pie":
                worlds = u.EditorLevelLibrary.get_pie_worlds(False)
                if worlds:
                    self.world = worlds[0]
                    self.stage("startup")
            elif self.phase == "startup" and elapsed > 2.5:
                self.setup()
            elif self.phase == "status" and elapsed > .2:
                log = self.log_since_stage()
                paths = {}
                for event in EVENTS:
                    expected = event + " → " + asset_path(event) + " [MetaSoundSource]"
                    self.require(expected in log, "Status omitted the current source for " + event)
                    paths[event] = {"path": asset_path(event), "class": "MetaSoundSource"}
                self.original_sounds = dict(self.audio.get_editor_property("sounds"))
                self.report["checks"]["status"] = {"mode_unchanged": self.expected_mode, "sources": paths}
                self.begin_comparison("KnifeHit", 0, "knife")
            elif self.phase == "knife" and game_elapsed > 4.3:
                log = self.log_since_stage()
                rows = self.comparison_rows(log)
                self.check_sequence("KnifeHit", rows)
                self.require("Сравнение завершено" in log, "KnifeHit comparison did not finish")
                self.report["checks"]["knife"] = {"mode_before_after": [0, 0], "phases": rows}
                self.begin_comparison("PickaxeHit", 1, "pickaxe")
            elif self.phase == "pickaxe" and game_elapsed > 4.3:
                log = self.log_since_stage()
                rows = self.comparison_rows(log)
                self.check_sequence("PickaxeHit", rows)
                self.require("Сравнение завершено" in log, "PickaxeHit comparison did not finish")
                self.report["checks"]["pickaxe"] = {"mode_before_after": [1, 1], "phases": rows}
                changed = dict(self.original_sounds)
                key = next(key for key in changed if str(key) == "KnifeHit")
                replacement = u.load_asset("/Game/Audio/Experiments/MS_Brush")
                self.require(replacement is not None, "Missing replacement MetaSound")
                changed[key] = replacement
                self.audio.set_editor_property("sounds", changed)
                self.stage("swap")
                self.play_reflected(asset_path("Brush"))
            elif self.phase == "swap" and elapsed > .4:
                swapped = self.check_play_path(asset_path("Brush"))
                self.report["checks"]["soft_reference_swap"] = {"replacement": swapped}
                self.audio.set_editor_property("sounds", self.original_sounds)
                self.stage("restore")
                self.play_reflected(asset_path("KnifeHit"))
            elif self.phase == "restore" and elapsed > .4:
                restored = self.check_play_path(asset_path("KnifeHit"))
                self.report["checks"]["soft_reference_swap"]["restored"] = restored
                self.stage("restart")
                self.console("mc.Audio.Compare KnifeHit")
            elif self.phase == "restart" and game_elapsed > .1:
                self.console("mc.Audio.Compare PickaxeHit")
                self.phase = "restarted"
                self.phase_at = now
                self.game_at = u.GameplayStatics.get_time_seconds(self.world)
            elif self.phase == "restarted" and game_elapsed > 4.3:
                rows = self.comparison_rows(self.log_since_stage())
                self.require(len(rows) == 5 and rows[0]["event"] == "KnifeHit" and rows[0]["phase"] == "old" and rows[0]["sample"] == 1,
                             "Restart retained an old comparison slot")
                self.check_sequence("PickaxeHit", rows[1:])
                self.report["checks"]["repeat_cancels_previous"] = {"phases": rows}
                self.stage("endplay")
                self.console("mc.Audio.Compare KnifeHit")
            elif self.phase == "endplay" and game_elapsed > .1:
                self.audio.set_editor_property("sounds", self.original_sounds)
                self.levels.editor_request_end_play()
                self.world = None
                self.hero = None
                self.audio = None
                self.phase = "after_endplay"
                self.phase_at = now
            elif self.phase == "after_endplay" and elapsed > 4.3:
                log = self.log_since_stage()
                rows = self.comparison_rows(log)
                self.require(len(rows) == 1 and rows[0]["event"] == "KnifeHit" and rows[0]["phase"] == "old" and rows[0]["sample"] == 1,
                             "EndPlay retained comparison callbacks")
                self.require(not self.levels.is_in_play_in_editor(), "The temporary PIE session is still active")
                self.require("BeginTearingDown" in log, "No owned-world teardown was recorded")
                self.report["checks"]["endplay_cancels_previous"] = {"phases": rows, "pie_ended": True, "no_further_slots_after_seconds": 4.3}
                self.finish()
        except Exception:
            self.report["status"] = "failed"
            self.report["error"] = traceback.format_exc()
            try:
                self.report["last_stage_log"] = self.log_since_stage()[-12000:]
            except Exception:
                pass
            try:
                self.cleanup()
            except Exception:
                self.report["cleanup_error"] = traceback.format_exc()
            self.write_report()
            u.log_error("MC_AUDIO_COMPARISON_VERIFICATION_FAIL " + self.report["error"])


previous = getattr(builtins, "mc_audio_comparison_verification", None)
if previous and getattr(previous, "callback", None) is not None:
    previous.cleanup()
mc_audio_comparison_verification = AudioComparisonVerification()
builtins.mc_audio_comparison_verification = mc_audio_comparison_verification
mc_audio_comparison_verification.start()
