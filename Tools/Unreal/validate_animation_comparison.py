"""Brief, asynchronous secondary-animation probe in an already running PIE.

Run through Tools/Unreal/remote_python.py after the native editor build. The
caller prepares the map, nearby characters/boss, and unpaused PIE first. This
script does not start/stop PIE, move actors, save packages, or exercise F3 UI.
It restores the original local mode and writes Saved/AnimationComparisonValidation.json.

Optional exec globals: MC_ANIMATION_VALIDATION_REQUIRE_BOSS (default true in
L_VFXLab), MC_ANIMATION_VALIDATION_LABEL, MC_ANIMATION_VALIDATION_OUTPUT.
The diagnostic bounds apply to the added motion, not the full authored pose.
"""
import builtins
import json
import math
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path

import unreal as u


def property_value(obj, name):
    return obj.get_editor_property(name)


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


class AnimationComparisonValidation:
    def __init__(self, options):
        root = Path(u.Paths.project_dir()).resolve()
        if root.name != "MessControl":
            raise RuntimeError("Use the MessControl Editor")
        worlds = list(u.EditorLevelLibrary.get_pie_worlds(False))
        if len(worlds) != 1:
            raise RuntimeError("Prepare one unpaused PIE world before this focused probe")
        world = worlds[0]
        self.world_path = world.get_path_name()
        self.require_boss = bool(options.get("MC_ANIMATION_VALIDATION_REQUIRE_BOSS", "L_VFXLab" in self.world_path))
        self.require_player = bool(options.get("MC_ANIMATION_VALIDATION_REQUIRE_PLAYER", True))
        output = options.get("MC_ANIMATION_VALIDATION_OUTPUT", "Saved/AnimationComparisonValidation.json")
        self.path = root / output
        self.callback = None
        self.previous_mode = u.SystemLibrary.get_console_variable_int_value("mc.Animation.SecondaryMode")
        self.enum_modes = [u.MCSecondaryAnimationMode.NATIVE, u.MCSecondaryAnimationMode.KAWAII_PHYSICS,
                           u.MCSecondaryAnimationMode.STYLIZED_DYNAMICS]
        self.mode_names = ["Native", "KawaiiPhysics", "StylizedDynamics"]
        self.phase = -1
        self.stage_game_at = 0.0
        self.stage_frames = 0
        self.next_capture = .45
        self.started_at = time.monotonic()
        self.report = {"status": "running", "started_utc": datetime.now(timezone.utc).isoformat(),
            "label": options.get("MC_ANIMATION_VALIDATION_LABEL", "focused PIE probe"),
            "world": self.world_path, "require_player": self.require_player, "require_boss": self.require_boss,
            "original_mode": self.previous_mode, "bounds": {"added_angle_degrees": 10., "added_translation_cm": 2.},
            "scope": "reflected mode changes, evaluated skeletal poses and solver diagnostics; F3 and visual quality checked separately",
            "stages": [], "errors": []}
        self.write()

    @staticmethod
    def require(condition, message):
        if not condition:
            raise RuntimeError(message)

    def write(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")

    def world(self):
        values = [world for world in u.EditorLevelLibrary.get_pie_worlds(False) if world.get_path_name() == self.world_path]
        self.require(len(values) == 1, "The prepared PIE world ended or changed during the probe")
        return values[0]

    @staticmethod
    def comparison(world):
        # BlueprintInternalUseOnly hides this library from Python's generated module.
        library=u.get_default_object(u.load_class(None,"/Script/Engine.SubsystemBlueprintLibrary"))
        return library.call_method("GetWorldSubsystem",args=(world,u.MCAnimationComparisonSubsystem))

    def select_next(self, world, comparison):
        self.phase += 1
        if self.phase == len(self.enum_modes):
            self.finish("passed")
            return
        requested = self.enum_modes[self.phase]
        available = bool(comparison.call_method("IsModeAvailable", args=(requested,)))
        # An unavailable optional Fab plugin must keep the known Native mode.
        if not available:
            comparison.call_method("SetMode", args=(self.enum_modes[0],))
        comparison.call_method("SetMode", args=(requested,))
        expected = requested if available else self.enum_modes[0]
        self.require(comparison.call_method("GetMode") == expected, "Mode selection/fallback did not match availability")
        self.report["stages"].append({"requested": self.mode_names[self.phase], "available": available,
            "effective": self.mode_names[self.phase] if available else "Native",
            "plugin_test": "evaluated" if available else "unavailable; ignored selection verified", "snapshots": []})
        self.stage_game_at = float(u.GameplayStatics.get_time_seconds(world))
        self.stage_frames = 0
        self.next_capture = .45
        self.write()

    def capture(self, world, comparison):
        self.require(float(u.GameplayStatics.get_world_delta_seconds(world)) <= .25,
                     "Frame gap exceeds the solver reset threshold; disable editor background CPU throttling for this probe")
        current = self.report["stages"][-1]
        expected = self.enum_modes[self.phase] if current["available"] else self.enum_modes[0]
        self.require(comparison.call_method("GetMode") == expected, "Mode changed unexpectedly during the sample window")
        targets = {}
        for kind, cls in (("player", u.MCToothCharacter), ("boss", u.MCNutBoss), ("boss", u.MCBossCharacter)):
            for actor in u.GameplayStatics.get_all_actors_of_class(world, cls):
                for mesh in actor.get_components_by_class(u.SkeletalMeshComponent):
                    targets[mesh.get_path_name()] = (kind, mesh)
        rows = []
        for sample in comparison.call_method("GetSamples"):
            path = str(property_value(sample, "MeshName"))
            self.require(path in targets, "A diagnostic refers to a missing gameplay skeletal mesh: " + path)
            kind, mesh = targets[path]
            bones = int(property_value(sample, "ActiveBones"))
            evaluations = int(property_value(sample, "Evaluations"))
            angle = float(property_value(sample, "MaxAngleDegrees"))
            displacement = float(property_value(sample, "MaxTranslation"))
            self.require(property_value(sample, "Mode") == expected, "Stale mode diagnostics: " + path)
            self.require(bones >= 0 and evaluations >= 0, "Negative solver diagnostics: " + path)
            self.require(math.isfinite(angle) and math.isfinite(displacement), "Non-finite added motion: " + path)
            self.require(0. <= angle <= 10.01 and 0. <= displacement <= 2.01, "Added motion exceeded its bounds: " + path)
            checked = 0
            for index in range(mesh.get_num_bones()):
                name = mesh.get_bone_name(index)
                transform = mesh.get_socket_transform(name, u.RelativeTransformSpace.RTS_COMPONENT)
                rotation = transform.rotation
                quaternion = [float(getattr(rotation, axis)) for axis in ("x", "y", "z", "w")]
                numbers = xyz(transform.translation) + xyz(transform.scale3d) + quaternion
                self.require(all(math.isfinite(value) for value in numbers), "Non-finite rendered bone pose: " + path + "/" + str(name))
                norm = sum(value * value for value in quaternion)
                self.require(abs(norm - 1.) <= .005, "Non-normalized rendered bone rotation: " + path + "/" + str(name))
                checked += 1
            self.require(checked > 0, "Diagnostic mesh has no skeletal bones: " + path)
            rows.append({"mesh": path, "kind": kind, "active_bones": bones, "evaluations": evaluations,
                "max_angle_degrees": angle, "max_translation_cm": displacement, "finite_pose_bones": checked})
        if self.require_player:
            self.require(any(row["kind"] == "player" for row in rows), "No recently evaluated player pose; prepare a nearby visible player/bot")
        if self.require_boss:
            self.require(any(row["kind"] == "boss" for row in rows), "No recently evaluated boss pose; prepare a nearby active boss station")
        if expected != self.enum_modes[0]:
            self.require(any(row["active_bones"] > 0 and row["evaluations"] > 0 for row in rows),
                         "The available solver did not evaluate any selected bones")
        return {"game_time": float(u.GameplayStatics.get_time_seconds(world)), "post_tick_frames": self.stage_frames,
            "status_text": str(comparison.call_method("GetStatusText")), "meshes": rows}

    def check_progress(self):
        stage = self.report["stages"][-1]
        if not stage["available"] or stage["effective"] == "Native":
            return
        first, last = stage["snapshots"]
        before = {row["mesh"]: row["evaluations"] for row in first["meshes"]}
        progressed = [row["mesh"] for row in last["meshes"]
            if row["active_bones"] > 0 and row["mesh"] in before and row["evaluations"] > before[row["mesh"]]]
        self.require(progressed, "Solver counters did not advance across separate PIE frames")
        stage["evaluation_progress_meshes"] = progressed

    def cleanup(self):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        u.SystemLibrary.execute_console_command(editor_world, "mc.Animation.SecondaryMode " + str(self.previous_mode))
        self.report["mode_restored"] = u.SystemLibrary.get_console_variable_int_value("mc.Animation.SecondaryMode") == self.previous_mode

    def finish(self, status):
        self.report["status"] = status
        self.report["elapsed_wall_seconds"] = round(time.monotonic() - self.started_at, 3)
        self.cleanup()
        self.write()
        u.log("MC_ANIMATION_COMPARISON_VALIDATION_" + status.upper() + " " + str(self.path))

    def tick(self, delta_seconds):
        try:
            self.require(time.monotonic() - self.started_at < 45., "Probe timed out; PIE may be paused or background-throttled")
            world = self.world()
            comparison = self.comparison(world)
            self.require(comparison is not None, "Current native comparison subsystem is unavailable; rebuild/restart the editor")
            if self.phase < 0:
                self.select_next(world, comparison)
                return
            self.stage_frames += 1
            elapsed = float(u.GameplayStatics.get_time_seconds(world)) - self.stage_game_at
            if elapsed < self.next_capture or self.stage_frames < 4:
                return
            stage = self.report["stages"][-1]
            stage["snapshots"].append(self.capture(world, comparison))
            self.write()
            if len(stage["snapshots"]) == 1:
                self.next_capture = elapsed + .8
            else:
                self.check_progress()
                self.select_next(world, comparison)
        except Exception:
            self.report["errors"].append(traceback.format_exc())
            try:
                self.finish("failed")
            except Exception:
                self.report["status"] = "failed"
                self.report["errors"].append("Cleanup/report failure:\n" + traceback.format_exc())
                self.write()
            u.log_error("MC_ANIMATION_COMPARISON_VALIDATION_FAILED " + self.report["errors"][0])

    def start(self):
        self.callback = u.register_slate_post_tick_callback(self.tick)
        u.log("MC_ANIMATION_COMPARISON_VALIDATION_STARTED " + str(self.path))


previous = getattr(builtins, "mc_animation_comparison_validation", None)
if previous is not None and getattr(previous, "callback", None) is not None:
    previous.cleanup()
probe = AnimationComparisonValidation(globals())
builtins.mc_animation_comparison_validation = probe
probe.start()
