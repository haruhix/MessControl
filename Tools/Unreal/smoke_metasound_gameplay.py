"""Focused in-editor checks of actual walking, tool hits, and brush contact.

Run through remote_python.py with no existing PIE session. All fixtures and
changes live in a temporary PIE world; no content packages are modified.
The helper never calls ExperimentalAudio.Play directly. The
MC_AUDIO_EXPERIMENT log demonstrates genuine gameplay event routing.
"""
import builtins
import json
import math
import re
import time
import traceback
from pathlib import Path

import unreal as u


class GameplayAudioSmoke:
    def __init__(self):
        root = Path(u.Paths.project_dir()).resolve()
        if root.name != "MessControl":
            raise RuntimeError("Run only in the MessControl Editor")
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        if self.levels.is_in_play_in_editor():
            raise RuntimeError("An existing PIE session is active; this smoke requires its own temporary session")
        self.path = root / "Artifacts" / "AudioExperiments" / "GameplaySmoke.json"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        command_line = u.SystemLibrary.get_command_line()
        log_argument = re.search(r'(?:^|\s)-(?:abslog|log)=(?:"([^"]+)"|(\S+))', command_line, re.IGNORECASE)
        self.log_path = Path(log_argument.group(1) or log_argument.group(2)) if log_argument else root / "Saved" / "MetaSoundPlayEditorFinal.log"
        if not self.log_path.is_absolute():
            self.log_path = Path(u.Paths.project_log_dir()) / self.log_path
        self.log_offset = self.log_path.stat().st_size if self.log_path.exists() else 0
        self.world = None
        self.hero = None
        self.callback = None
        self.phase = "waiting_pie"
        self.requested_at = time.monotonic()
        self.phase_at = self.requested_at
        self.report = {"status": "running", "scope": "owned PIE gameplay fixtures", "checks": {}}
        self.previous_enabled = u.SystemLibrary.get_console_variable_int_value("mc.Audio.Experiments")
        self.previous_log = u.SystemLibrary.get_console_variable_int_value("mc.Audio.ExperimentLog")
        self.previous_idle = u.SystemLibrary.get_console_variable_int_value("t.IdleWhenNotForeground")
        self.next_swing = 0.0
        self.statics = u.get_default_object(u.GameplayStatics)
        self.movement_speed = 0.0
        self.brush_progress = 0.0
        self.write_report()

    def write_report(self):
        self.path.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding="utf-8")

    def console(self, command):
        world = self.world or u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        u.SystemLibrary.execute_console_command(world, command)

    def start(self):
        self.console("mc.Audio.Experiments 1")
        self.console("mc.Audio.ExperimentLog 1")
        self.console("t.IdleWhenNotForeground 0")
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.levels.editor_request_begin_play()
        u.log("MC_AUDIO_GAMEPLAY_SMOKE_STARTED")

    def cleanup(self):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        if self.hero:
            self.hero.call_method("ServerSetPrimary", args=(False,))
            self.hero.set_sprint_input_held(False)
        self.console("mc.Audio.Experiments " + str(self.previous_enabled))
        self.console("mc.Audio.ExperimentLog " + str(self.previous_log))
        self.console("t.IdleWhenNotForeground " + str(self.previous_idle))
        if self.levels.is_in_play_in_editor():
            self.levels.editor_request_end_play()

    def spawn(self, cls, location):
        transform = u.Transform(location=location)
        # These spawning UFUNCTIONs are BlueprintInternalUseOnly and have no
        # Python glue. call_method uses their actual reflection declarations.
        actor = self.statics.call_method("BeginDeferredActorSpawnFromClass", args=(
            self.world, cls.static_class(), transform, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        if not actor:
            raise RuntimeError("Could not spawn fixture " + str(cls))
        return actor, transform

    def setup(self):
        self.hero = u.GameplayStatics.get_player_pawn(self.world, 0)
        if not isinstance(self.hero, u.MCToothCharacter):
            raise RuntimeError("PIE did not spawn MCToothCharacter")
        self.gs = u.GameplayStatics.get_game_state(self.world)
        self.gs.set_editor_property("dev_manual_events", True)
        self.gs.set_editor_property("physical_brushes", False)
        self.gs.set_editor_property("phase", u.MCShiftPhase.INTERMISSION)
        self.gs.set_editor_property("phase_ends_at", u.GameplayStatics.get_time_seconds(self.world) + 300.0)
        self.cube = u.load_asset("/Engine/BasicShapes/Cube")
        self.fixture_x = 10000.0
        self.floor, transform = self.spawn(u.StaticMeshActor, u.Vector(self.fixture_x, 0, 10000))
        mesh = self.floor.static_mesh_component
        mesh.set_mobility(u.ComponentMobility.MOVABLE)
        mesh.set_static_mesh(self.cube)
        mesh.set_collision_profile_name("BlockAll")
        self.statics.call_method("FinishSpawningActor", args=(self.floor, transform, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.floor.set_actor_scale3d(u.Vector(100, 6, 0.2))
        half_height = self.hero.capsule_component.get_scaled_capsule_half_height()
        self.hero_z = 10010.0 + half_height + 2.0
        self.move = self.hero.get_movement_component()
        self.hero.call_method("ServerSetPrimary", args=(False,))
        self.move.stop_movement_immediately()
        self.hero.set_actor_location_and_rotation(u.Vector(self.fixture_x - 800, 0, self.hero_z), u.Rotator(), False, True)
        self.move.set_movement_mode(u.MovementMode.MOVE_WALKING)
        self.start_x = self.hero.get_actor_location().x
        self.stage("walk")

    def stage(self, name):
        self.phase = name
        self.phase_at = time.monotonic()
        self.next_swing = 0.0
        u.log("MC_AUDIO_GAMEPLAY_STAGE " + name)

    def setup_food(self, hard):
        self.hero.call_method("ServerSetPrimary", args=(False,))
        self.hero.set_sprint_input_held(False)
        self.move.stop_movement_immediately()
        self.move.disable_movement()
        self.hero.set_actor_location_and_rotation(u.Vector(self.fixture_x, 0, self.hero_z), u.Rotator(), False, True)
        if getattr(self, "food", None):
            self.food.destroy_actor()
        self.food, transform = self.spawn(u.MCFoodActor, u.Vector(self.fixture_x + 130, 0, self.hero_z))
        row = self.food.get_editor_property("food_data")
        row.set_editor_property("resistance", u.MCFoodResistance.HARD if hard else u.MCFoodResistance.SOFT)
        row.set_editor_property("scale", u.Vector(0.6, 0.6, 0.6))
        self.food.set_editor_property("food_data", row)
        self.food.set_editor_property("item_mesh", self.cube)
        self.food.set_editor_property("item_name", "AudioSmokeFixture")
        self.food.set_editor_property("health", 500.0)
        self.statics.call_method("FinishSpawningActor", args=(self.food, transform, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.food.body.set_simulate_physics(False)
        self.food.set_actor_tick_enabled(False)
        self.food_start_health = self.food.get_editor_property("health")
        self.hero.inventory.server_select(u.MCToolSlot.PICKAXE if hard else u.MCToolSlot.KNIFE)
        selected = self.hero.inventory.get_editor_property("selected")
        if selected != (u.MCToolSlot.PICKAXE if hard else u.MCToolSlot.KNIFE):
            raise RuntimeError("Tool selection rejected after the previous swing")
        self.stage("pickaxe" if hard else "knife")

    def setup_brush(self):
        self.food.destroy_actor()
        self.food = None
        self.hero.call_method("ServerSetPrimary", args=(False,))
        self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
        if self.hero.inventory.get_editor_property("selected") != u.MCToolSlot.BRUSH:
            raise RuntimeError("Brush selection rejected after the previous swing")
        teeth = u.GameplayStatics.get_all_actors_of_class(self.world, u.MCArenaTooth)
        self.tooth = next((tooth for tooth in teeth if tooth.is_available() and tooth.get_editor_property("state").get_editor_property("tooth_id") == 3), None)
        if not self.tooth:
            raise RuntimeError("No available arena tooth in the temporary PIE world")
        settings = self.tooth.get_editor_property("settings")
        settings.set_editor_property("piano_enabled", False)
        settings.set_editor_property("initial_calculus_every_nth_tooth", 0)
        self.tooth.set_editor_property("settings", settings)
        self.tooth.set_coffee(1.0)
        self.brush_start_coffee = self.tooth.status.get_editor_property("state").get_editor_property("coffee_left")
        # Use the same real tooth and approach search as MCBrushSmoke.cpp.
        # Its coating and the tongue floor are already built for this scene.
        centre = self.tooth.get_actor_location()
        distance = math.hypot(centre.x, centre.y)
        inward = u.Vector(-centre.x / distance, -centre.y / distance, 0)
        side = u.Vector(inward.y, -inward.x, 0)
        _, extent, _ = u.SystemLibrary.get_component_bounds(self.tooth.visual)
        radius = abs(inward.x) * extent.x + abs(inward.y) * extent.y
        approach = centre + inward * radius
        face = self.tooth.get_editor_property("brush_surface").line_trace_component(centre + inward * 600, centre - inward * 200, True, False, False)
        if face:
            approach = face[0]
        self.brush_candidates = []
        half_height = self.hero.capsule_component.get_scaled_capsule_half_height()
        for gap in (65, 80, 100, 120):
            for index in (0, -1, 1, -2, 2, -3, 3, -4, 4, -5, 5, -6, 6):
                point = approach + inward * gap + side * (index * 16)
                floor = u.SystemLibrary.line_trace_single(self.world, point + u.Vector(0, 0, 1000), point - u.Vector(0, 0, 1000), u.TraceTypeQuery.ECC_VISIBILITY, False, teeth + [self.hero], u.DrawDebugTrace.NONE, True)
                if floor:
                    impact = self.statics.call_method("BreakHitResult", args=(floor,))[5]
                    point.z = impact.z + half_height + 2
                    self.brush_candidates.append(point)
        if not self.brush_candidates:
            raise RuntimeError("No floor below the brush approach candidates")
        # MCBrushTest on this saved map verified this clear approach to tooth 3.
        # Prioritize it; the geometric search remains available as a fallback.
        self.brush_candidates.insert(0, u.Vector(-1353.392, -1313.444, -51.823))
        self.brush_rotation = u.Rotator(pitch=0, yaw=math.degrees(math.atan2(-inward.y, -inward.x)), roll=0)
        self.brush_candidate_index = -1
        self.brush_contact_at = None
        self.next_brush_candidate()
        self.stage("brush")

    def next_brush_candidate(self):
        self.brush_candidate_index += 1
        if self.brush_candidate_index >= len(self.brush_candidates):
            raise RuntimeError("No reachable brush stain from the existing fixture approaches")
        self.hero.call_method("ServerSetPrimary", args=(False,))
        self.hero.set_actor_location_and_rotation(self.brush_candidates[self.brush_candidate_index], self.brush_rotation, False, True)
        self.brush_candidate_at = time.monotonic()
        u.log("MC_AUDIO_BRUSH_CANDIDATE " + str(self.brush_candidate_index) + " " + str(self.hero.get_actor_location()))

    def finish(self):
        expected = {"Step", "KnifeSwing", "KnifeHit", "PickaxeSwing", "PickaxeHit", "Brush"}
        log = self.log_path.read_bytes()[self.log_offset:].decode("utf-8", errors="replace") if self.log_path.exists() else ""
        events = re.findall(r"MC_AUDIO_EXPERIMENT event=(\w+)", log)
        self.report["gameplay_event_counts"] = {event: events.count(event) for event in sorted(expected)}
        self.report["log_path"] = str(self.log_path)
        seen = set(events)
        self.report["missing_gameplay_events"] = sorted(expected - seen)
        self.report["checks"]["brush"] = {
            "coffee_before": self.brush_start_coffee,
            "coffee_after": self.tooth.status.get_editor_property("state").get_editor_property("coffee_left"),
            "max_contact_progress": self.brush_progress,
            "fixture_target": self.tooth.get_path_name(),
            "approach_candidates_tried": self.brush_candidate_index + 1,
        }
        checks = self.report["checks"]
        passed = not self.report["missing_gameplay_events"] and checks["walking"]["peak_speed"] > 100 and checks["walking"]["travel_cm"] > 100 and checks["knife"]["damage"] > 0 and checks["pickaxe"]["damage"] > 0 and self.brush_progress > 0
        self.report["status"] = "passed" if passed else "failed"
        self.write_report()
        self.cleanup()
        u.log("MC_AUDIO_GAMEPLAY_SMOKE_" + ("PASS " if passed else "FAIL ") + str(self.path))

    def tick(self, _delta):
        try:
            now = time.monotonic()
            elapsed = now - self.phase_at
            if self.hero:
                # Day-one setup can reapply physical-brush/shift defaults after
                # BeginPlay. Keep this temporary fixture in its chosen mode.
                self.gs.set_editor_property("physical_brushes", False)
                self.gs.set_editor_property("phase", u.MCShiftPhase.INTERMISSION)
                self.gs.set_editor_property("phase_ends_at", u.GameplayStatics.get_time_seconds(self.world) + 300.0)
            if self.phase == "waiting_pie":
                worlds = u.EditorLevelLibrary.get_pie_worlds(False)
                if worlds:
                    self.world = worlds[0]
                    self.stage("startup")
                elif now - self.requested_at > 45:
                    raise RuntimeError("No PIE world after 45 seconds")
            elif self.phase == "startup" and elapsed > 3.0:
                self.setup()
            elif self.phase == "walk":
                self.hero.set_sprint_input_held(elapsed > 2.5)
                self.hero.add_movement_input(u.Vector(1, 0, 0), 1.0, False)
                self.movement_speed = max(self.movement_speed, self.hero.get_velocity().length())
                if elapsed > 5.0:
                    self.report["checks"]["walking"] = {"peak_speed": self.movement_speed, "travel_cm": self.hero.get_actor_location().x - self.start_x}
                    self.setup_food(False)
            elif self.phase in ("knife", "pickaxe"):
                if elapsed < 2.0 and elapsed > self.next_swing:
                    self.hero.call_method("ServerSwingBrush", args=())
                    self.next_swing = elapsed + (1.2 if self.phase == "pickaxe" else 0.85)
                if elapsed > 3.6:
                    self.report["checks"][self.phase] = {"health_before": self.food_start_health, "health_after": self.food.get_editor_property("health"), "damage": self.food_start_health - self.food.get_editor_property("health")}
                    if self.phase == "knife":
                        self.setup_food(True)
                    else:
                        self.setup_brush()
            elif self.phase == "brush":
                self.hero.call_method("ServerSetPrimary", args=(True,))
                self.brush_progress = max(self.brush_progress, self.hero.get_editor_property("contact_progress"))
                if self.brush_progress > 0 and self.brush_contact_at is None:
                    self.brush_contact_at = now
                if self.brush_contact_at is None and now - self.brush_candidate_at > 1.2:
                    self.next_brush_candidate()
                if self.brush_contact_at is not None and now - self.brush_contact_at > 4.0:
                    self.finish()
        except Exception:
            self.report["status"] = "failed"
            self.report["error"] = traceback.format_exc()
            self.write_report()
            self.cleanup()
            u.log_error("MC_AUDIO_GAMEPLAY_SMOKE_FAIL " + self.report["error"])


previous = getattr(builtins, "mc_audio_gameplay_smoke", None)
if previous and getattr(previous, "callback", None) is not None:
    previous.cleanup()
mc_audio_gameplay_smoke = GameplayAudioSmoke()
builtins.mc_audio_gameplay_smoke = mc_audio_gameplay_smoke
mc_audio_gameplay_smoke.start()
