"""Capture the imported two-unit animation gallery without saving any packages.

Run in an idle, rendering Editor after Style v2 import:
    py "E:/DEVGAME/MessControl/Tools/Unreal/preview_nut_style_v2.py"

The show-only capture renders transient actors on a separate offstage platform.
This is an asset-pose preview, not gameplay/physics/network validation. It uses
saved phase settings but deliberately omits native procedural overlays and VFX.
Capture has a 30-second wall-time limit; a slow Editor returns partial frames.
An installed C:/ffmpeg/ffmpeg.exe assembles a silent MP4 asynchronously if found.
"""
import builtins
import json
import math
import os
import subprocess
import time
from pathlib import Path

import unreal as u


TAG = "MCNutStyleV2AssetPreview"
OUT = Path(u.Paths.project_dir()).resolve() / "ArtSource/NutAnimationStyle/UnrealPreview"
REPORT = Path(u.Paths.project_saved_dir()).resolve() / "Checks/NutStyleV2Preview.json"
FPS = 24
WIDTH, HEIGHT = 960, 480
WALL_LIMIT = 30.
FULL_GALLERY = os.environ.get("MC_NUT_STYLE_V2_FULL_GALLERY", "0") == "1"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def smooth(value):
    value = max(0., min(1., value))
    return value * value * (3. - 2. * value)


def dirty_packages():
    return sorted({package.get_name() for package in
                   list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                   list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())})


def vector_list(value):
    return [float(value.x), float(value.y), float(value.z)]


def measure_cast_reach(mesh, clips, boss):
    """Read-only two-bone reach and .85 blend estimate before native overlays."""
    bounds = mesh.get_bounds()
    center = vector_list(bounds.get_editor_property("origin"))
    scale = float(boss.get_editor_property("mage_height")) / (2 * bounds.get_editor_property("box_extent").z)
    yaw = math.radians(float(boss.get_editor_property("mage_model_yaw")))
    c, s = math.cos(yaw), math.sin(yaw)
    height_offset = float(boss.get_editor_property("mage_height")) * .5 - 90
    emitter = vector_list(boss.get_editor_property("mage_cast_offset"))
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", mesh)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("evaluation_type", u.AnimDataEvalType.RAW)
    result = {"preserved_emitter_actor_local_cm": emitter, "model_scale": scale, "samples": [],
              "scope": "Base imported pose at release/channel; unmodified (0,8,0) hand tip. Native torso/aim/release rotations are omitted; .85 IK estimate retains end rotation as UE solver does."}
    def component_point(actor_point):
        x, y = actor_point[0] / scale, actor_point[1] / scale
        return [c * x + s * y + center[0], -s * x + c * y + center[1],
                (actor_point[2] - height_offset) / scale + center[2]]
    for label, clip in clips.items():
        release = float(boss.get_editor_property("mage_cast_release_fraction"))
        phases = (release, release + .035, release + .07) if label == "Rain" else (release,)
        for phase in phases:
            pose = u.AnimPoseExtensions.get_anim_pose_at_time(clip, clip.get_play_length() * phase, options)
            transforms = {name: u.AnimPoseExtensions.get_bone_pose(pose, name, u.AnimPoseSpaces.WORLD)
                          for name in ("arm_stretch_r", "forearm_stretch_r", "hand_r")}
            positions = {name: vector_list(transform.get_editor_property("translation")) for name, transform in transforms.items()}
            upper, middle, end = (positions[name] for name in ("arm_stretch_r", "forearm_stretch_r", "hand_r"))
            lengths = [math.dist(upper, middle), math.dist(middle, end)]
            tip = vector_list(u.MathLibrary.transform_direction(transforms["hand_r"], u.Vector(0, 8, 0)))
            for aim_yaw in (-55., 0., 55.):
                radians = math.radians(aim_yaw)
                actor_target = [emitter[0] * math.cos(radians) - emitter[1] * math.sin(radians),
                                emitter[0] * math.sin(radians) + emitter[1] * math.cos(radians), emitter[2]]
                target = component_point(actor_target)
                wrist_target = [a - b for a, b in zip(target, tip)]
                distance = math.dist(upper, wrist_target)
                maximum = sum(lengths)
                minimum = abs(lengths[0] - lengths[1])
                ratio = min(1., maximum / max(.001, distance))
                solved_wrist = [a + (b - a) * ratio for a, b in zip(upper, wrist_target)]
                blended_palm = [a + .85 * (b - a) + t for a, b, t in zip(end, solved_wrist, tip)]
                result["samples"].append({"clip": clip.get_path_name(), "phase": phase, "aim_yaw_degrees": aim_yaw,
                    "bone_positions_component_cm": positions, "limb_lengths_cm": lengths,
                    "wrist_target_component_cm": wrist_target, "distance_from_upper_arm_cm": distance,
                    "maximum_reach_cm": maximum, "reach_margin_cm": maximum - distance,
                    "reachable_without_stretch": minimum <= distance <= maximum,
                    "raw_palm_error_actor_cm": math.dist([a + b for a, b in zip(end, tip)], target) * scale,
                    "estimated_palm_error_after_85_percent_ik_actor_cm": math.dist(blended_palm, target) * scale})
    return result


def reach_only():
    boss = u.load_asset("/Game/Gameplay/CoreLoop/DA_NutRain").get_editor_property("settings").get_editor_property("boss")
    mesh = u.load_asset("/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard")
    clips = {label: u.load_asset("/Game/Gameplay/CoreLoop/NutWizard/Animations/MCStyleV2/A_NutWizard_" + label)
             for label in ("Cast", "HeavyCast", "Summon", "Rain")}
    result = measure_cast_reach(mesh, clips, boss)
    output = Path(u.Paths.project_saved_dir()) / "Checks/NutStyleV2CastReach.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2), encoding="utf8")
    u.log("MC_NUT_STYLE_V2_CAST_REACH " + str(output))
    return result


def cleanup_owned():
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), "Stop PIE before removing the preview")
    running = getattr(builtins, "_mc_nut_style_v2_review", None)
    if running:
        running.finish("Capture interrupted by explicit cleanup")
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    subsystem = u.get_editor_subsystem(u.EditorActorSubsystem)
    removed = []
    for actor in u.GameplayStatics.get_all_actors_of_class(world, u.Actor):
        if TAG in [str(tag) for tag in actor.get_editor_property("tags")]:
            removed.append(actor.get_path_name())
            require(subsystem.destroy_actor(actor), "Could not remove owned transient preview actor")
    u.log("MC_NUT_STYLE_V2_PREVIEW_CLEANUP " + json.dumps(removed))
    return removed


class Review:
    def __init__(self):
        require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),
                "Stop PIE before capturing the two imported rigs")
        require(not getattr(builtins, "_mc_nut_style_v2_review", None),
                "A Nut Style capture is already running")
        self.world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(self.world is not None, "A rendering Editor world is required")
        self.actors = u.get_editor_subsystem(u.EditorActorSubsystem)
        self.spawned = []
        self.models = {}
        self.clips = {}
        self.active_clips = {}
        self.pose_options = {}
        self.handle = None
        self.target = None
        self.encoder = None
        self.encoder_log = None
        self.frame = 0
        self.warm = 0
        self.started = time.monotonic()
        self.before_dirty = dirty_packages()
        self.report = {"passed": False, "cosmetic_asset_preview": True,
                       "scope": "Imported base clips, saved phase timing and static Roll mesh; no gameplay physics, native overlays or VFX",
                       "frames": [], "output": str(OUT), "fps": FPS, "width": WIDTH, "height": HEIGHT,
                       "review_content": "All abilities" if FULL_GALLERY else "Complete Tank Charge and corrected Wizard Cast twice",
                       "pose_checks": [],
                       "dirty_packages_before": self.before_dirty}
        self.boss = u.load_asset("/Game/Gameplay/CoreLoop/DA_NutRain").get_editor_property("settings").get_editor_property("boss")
        self.report["cast_reach"] = reach_only()
        self.tank_origin = u.Vector(100000, 300, -100000)
        self.mage_origin = u.Vector(100000, -300, -100000)
        try:
            self.setup()
            self.timelines = self.make_timelines()
            self.duration = max(sum(item[1] for item in timeline) for timeline in self.timelines.values())
            self.frame_count = math.ceil(self.duration * FPS)
            self.report["duration_seconds"] = self.duration
            self.report["planned_frames"] = self.frame_count
            self.report["timelines"] = self.timelines
            builtins._mc_nut_style_v2_review = self
            self.handle = u.register_slate_post_tick_callback(self.tick)
            self.write_report()
            u.log("MC_NUT_STYLE_V2_PREVIEW_STARTED " + str(OUT))
        except Exception as error:
            self.finish(str(error))
            raise

    def spawn(self, kind, point=None):
        actor = self.actors.spawn_actor_from_class(kind, point or self.tank_origin, u.Rotator(), transient=True)
        require(actor is not None, "Could not create a transient " + kind.__name__)
        self.spawned.append(actor)
        actor.set_editor_property("tags", [u.Name(TAG)])
        actor.set_actor_enable_collision(False)
        actor.set_actor_tick_enabled(False)
        return actor

    def setup(self):
        OUT.mkdir(parents=True, exist_ok=True)
        # A unique frame directory preserves previous captured reviews.
        self.frames_dir = OUT / time.strftime("Frames_%Y%m%d_%H%M%S")
        self.frames_dir.mkdir(parents=True, exist_ok=False)
        self.report["frames_directory"] = str(self.frames_dir)
        for role, folder, mesh_name, origin, height_key in (
                ("tank", "NutTank", "SK_NutTank", self.tank_origin, "tank_height"),
                ("mage", "NutWizard", "SK_NutWizard", self.mage_origin, "mage_height")):
            base = "/Game/Gameplay/CoreLoop/" + folder
            mesh = u.load_asset(base + "/" + mesh_name)
            require(isinstance(mesh, u.SkeletalMesh), "Missing imported skeletal mesh: " + base)
            actor = self.spawn(u.SkeletalMeshActor, origin)
            component = actor.get_editor_property("skeletal_mesh_component")
            component.set_skeletal_mesh_asset(mesh)
            component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
            component.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE, True)
            component.set_update_animation_in_editor(True)
            bounds = mesh.get_bounds()
            center = bounds.get_editor_property("origin")
            height = float(self.boss.get_editor_property(height_key))
            scale = height / max(1., bounds.get_editor_property("box_extent").z * 2.)
            yaw = float(self.boss.get_editor_property(role + "_model_yaw"))
            rotation = u.Rotator(pitch=0, yaw=yaw, roll=0)
            offset = u.MathLibrary.rotate_angle_axis(center * scale, yaw, u.Vector(0, 0, 1))
            actor.set_actor_rotation(rotation, True)
            actor.set_actor_location(origin + u.Vector(0, 0, height * .5) - offset, False, True)
            actor.set_actor_scale3d(u.Vector(scale, scale, scale))
            self.models[role] = (actor, component, scale)
            pose_options = u.AnimPoseEvaluationOptions()
            pose_options.set_editor_property("optional_skeletal_mesh", mesh)
            pose_options.set_editor_property("extract_root_motion", False)
            pose_options.set_editor_property("evaluation_type", u.AnimDataEvalType.RAW)
            self.pose_options[role] = pose_options
            suffixes = ("Idle", "Walk", "Melee", "Jump", "Transform", "ChargeTell", "ChargeLoop", "ChargeRecovery") if role == "tank" else (
                "Idle", "Walk", "Cast", "HeavyCast", "Summon", "Rain", "Melee", "Hit", "Death")
            prefix = "A_NutTank_" if role == "tank" else "A_NutWizard_"
            for suffix in suffixes:
                clip = u.load_asset(base + "/Animations/MCStyleV2/" + prefix + suffix)
                require(isinstance(clip, u.AnimSequence) and clip.get_editor_property("skeleton") == mesh.get_editor_property("skeleton"),
                        "Missing/incompatible imported Style v2 clip: " + prefix + suffix)
                self.clips[(role, suffix)] = clip
        ball_mesh = u.load_asset("/Game/Gameplay/CoreLoop/NutTank/SM_NutTank_Ball")
        require(isinstance(ball_mesh, u.StaticMesh), "Existing tank ball mesh is missing")
        self.ball = self.spawn(u.StaticMeshActor, self.tank_origin)
        self.ball_component = self.ball.get_editor_property("static_mesh_component")
        self.ball_component.set_static_mesh(ball_mesh)
        self.ball_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        center, extent, _ = u.SystemLibrary.get_component_bounds(self.ball_component)
        self.ball_center = center - self.tank_origin
        self.ball_height = float(self.boss.get_editor_property("tank_ball_height"))
        self.ball_scale = self.ball_height / max(1., extent.z * 2)
        self.ball.set_actor_hidden_in_game(True)
        self.ball_component.set_visibility(False)

        floor = self.spawn(u.StaticMeshActor, u.Vector(100000, 0, -100001))
        floor_component = floor.get_editor_property("static_mesh_component")
        floor_component.set_static_mesh(u.load_asset("/Engine/BasicShapes/Plane"))
        floor_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        floor.set_actor_scale3d(u.Vector(11, 11, 1))
        self.capture = self.spawn(u.SceneCapture2D)
        self.capture_component = self.capture.get_editor_property("capture_component2d")
        eye, aim = u.Vector(100700, -650, -99480), u.Vector(100000, 0, -99895)
        self.capture.set_actor_location_and_rotation(eye, u.MathLibrary.find_look_at_rotation(eye, aim), False, True)
        self.target = u.RenderingLibrary.create_render_target2d(self.world, WIDTH, HEIGHT,
                        u.TextureRenderTargetFormat.RTF_RGBA8, u.LinearColor(.035, .04, .055, 1), False, False)
        # BaseColor provides the actual material textures without exposure/lighting
        # from the user's current map and is cheap enough for the bounded capture.
        for key, value in dict(texture_target=self.target, capture_source=u.SceneCaptureSource.SCS_BASE_COLOR,
                capture_every_frame=False, capture_on_movement=False, always_persist_rendering_state=True,
                projection_type=u.CameraProjectionMode.ORTHOGRAPHIC, ortho_width=1050.,
                primitive_render_mode=u.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():
            self.capture_component.set_editor_property(key, value)
        for actor in (self.models["tank"][0], self.models["mage"][0], self.ball, floor):
            self.capture_component.show_only_actor_components(actor)
        self.capture_component.set_editor_property("show_flag_settings", [
            u.EngineShowFlagsSetting(show_flag_name=name, enabled=False)
            for name in ("MotionBlur", "Fog", "VolumetricFog", "Atmosphere", "GlobalIllumination", "LumenReflections", "ScreenSpaceReflections")])

    def make_timelines(self):
        value = lambda name: float(self.boss.get_editor_property(name))
        charge_length = min(1100., value("charge_distance")) / max(1., value("charge_speed"))
        if not FULL_GALLERY:
            charge_duration = value("charge_windup") + charge_length + 1.4
            cast_duration = value("fireball_windup") + .9
            duration = max(6., .4 + charge_duration, .8 + cast_duration * 2)
            return {"tank": [("Idle", .4), ("Charge", charge_duration),
                             ("Idle", max(.001, duration - .4 - charge_duration))],
                    "mage": [("Idle", .4), ("Fireball", cast_duration), ("Idle", .4),
                             ("Fireball", cast_duration), ("Idle", max(.001, duration - .8 - cast_duration * 2))]}
        return {
            "tank": [("Idle", 1.), ("Walk", 1.2), ("Melee", value("melee_windup") + .9),
                     ("Charge", value("charge_windup") + charge_length + 1.4),
                     ("Jump", value("jump_windup") + value("jump_flight_seconds") + 1.7),
                     ("Roll", value("roll_windup") + value("roll_duration") + 1.4)],
            "mage": [("Idle", 1.), ("Walk", 1.2), ("Fireball", value("fireball_windup") + .9),
                     ("Summon", 1.1 + 1.2), ("Rain", value("rain_windup") + value("rain_active_seconds") + 1.4),
                     ("Melee", value("melee_windup") + .9), ("Hit", .4), ("Death", value("death_seconds"))],
        }

    def phase(self, role, time_value):
        timeline = self.timelines[role]
        label, duration = timeline[-1]
        for candidate, candidate_duration in timeline:
            label, duration = candidate, candidate_duration
            if time_value < duration:
                break
            time_value -= duration
        age = min(time_value, duration)
        value = lambda name: float(self.boss.get_editor_property(name))
        fraction = age / max(.001, duration)
        ball_weight, ball_age = 0., 0.
        if label in ("Idle", "Walk"):
            clip = self.clips[(role, label)]
            fraction = (age % clip.get_play_length()) / clip.get_play_length()
        elif label in ("Melee", "Fireball", "Summon", "Rain"):
            release = value("tank_melee_impact_fraction") if role == "tank" else (
                value("mage_melee_impact_fraction") if label == "Melee" else value("mage_cast_release_fraction"))
            windup = value("melee_windup") if label == "Melee" else value("fireball_windup") if label == "Fireball" else 1.1 if label == "Summon" else value("rain_windup")
            active = value("rain_active_seconds") if label == "Rain" else 0.
            if age < windup:
                fraction = release * age / windup
            elif age < windup + active:
                fraction = release + .035 * (1 + math.sin((age - windup) * 3.5))
            else:
                fraction = release + (1 - release) * (age - windup - active) / max(.001, duration - windup - active)
        elif label == "Charge":
            tell, recovery = value("charge_windup"), 1.4
            if age < tell:
                label, fraction = "ChargeTell", age / tell
            elif age < duration - recovery:
                label = "ChargeLoop"
                clip = self.clips[(role, label)]
                fraction = ((age - tell) * value("tank_charge_loop_play_rate") % clip.get_play_length()) / clip.get_play_length()
            else:
                label, fraction = "ChargeRecovery", (age - duration + recovery) / recovery
        elif label == "Jump":
            tell, active = value("jump_windup"), value("jump_flight_seconds")
            takeoff, impact = value("tank_jump_takeoff_fraction"), value("tank_jump_impact_fraction")
            fraction = takeoff * age / tell if age < tell else (
                takeoff + (impact - takeoff) * (age - tell) / active if age < tell + active else
                impact + (1 - impact) * (age - tell - active) / 1.7)
        elif label == "Roll":
            tell, active = value("roll_windup"), value("roll_duration")
            label = "Transform"
            if age < tell:
                fraction, ball_weight = age / tell, smooth((age - tell + .18) / .18)
            elif age < tell + active:
                fraction, ball_weight, ball_age = 1., 1., age - tell
            else:
                fraction = 1 - (age - tell - active) / 1.4
                ball_weight = 1 - smooth((age - tell - active) / .18)
                ball_age = active
        label = "Cast" if label == "Fireball" else label
        return label, max(0., min(1., fraction)), ball_weight, ball_age

    def apply_frame(self, frame):
        timeline_time = frame / FPS
        record = {"frame": frame + 1, "seconds": timeline_time, "poses": {}}
        for role in ("tank", "mage"):
            label, fraction, ball_weight, ball_age = self.phase(role, timeline_time)
            actor, component, scale = self.models[role]
            clip = self.clips[(role, label)]
            if self.active_clips.get(role) != clip:
                component.play_animation(clip, False)
                component.set_play_rate(0.)
                self.active_clips[role] = clip
            component.set_position(clip.get_play_length() * fraction, False)
            # OverrideAnimationData forces a zero-delta tick and bone refresh.
            # PlayAnimation/SetPosition above also update the current instance,
            # which SetAnimationMode alone does not do when already SingleNode.
            component.override_animation_data(clip, False, False, clip.get_play_length() * fraction, 0.)
            if self.warm >= 4 and frame in {0, round(1.3 * FPS), round(2.5 * FPS), round(4. * FPS)}:
                actual = component.get_socket_transform("hand_r", u.RelativeTransformSpace.RTS_COMPONENT)
                reference_pose = u.AnimPoseExtensions.get_anim_pose_at_time(
                    clip, clip.get_play_length() * fraction, self.pose_options[role])
                expected = u.AnimPoseExtensions.get_bone_pose(reference_pose, "hand_r", u.AnimPoseSpaces.WORLD)
                error_cm = math.dist(vector_list(actual.get_editor_property("translation")),
                                     vector_list(expected.get_editor_property("translation")))
                self.report["pose_checks"].append({"frame": frame + 1, "role": role, "clip": clip.get_path_name(),
                    "phase": fraction, "hand_component_error_cm": error_cm, "passed": error_cm < 1.})
                require(error_cm < 1., "Live preview hand differs from imported clip by %.3f cm: %s/%s" % (error_cm, role, label))
            if role == "tank":
                actor.set_actor_scale3d(u.Vector(scale, scale, scale) * max(.0001, 1 - ball_weight))
                self.ball_component.set_visibility(ball_weight > .001)
                self.ball.set_actor_hidden_in_game(ball_weight <= .001)
                ball_scale = self.ball_scale * max(.0001, ball_weight)
                pitch = math.degrees(ball_age * float(self.boss.get_editor_property("roll_speed")) / max(1., self.ball_height * .5)) % 360
                rotation = u.Rotator(pitch=pitch, yaw=0, roll=0)
                self.ball.set_actor_rotation(rotation, True)
                self.ball.set_actor_scale3d(u.Vector(ball_scale, ball_scale, ball_scale))
                offset = u.MathLibrary.rotate_angle_axis(self.ball_center * ball_scale, -pitch, u.Vector(0, 1, 0))
                self.ball.set_actor_location(self.tank_origin + u.Vector(0, 0, self.ball_height * .5) - offset, False, True)
            record["poses"][role] = {"clip": clip.get_path_name(), "fraction": fraction, "ball_weight": ball_weight}
        self.capture_component.capture_scene()
        return record

    def tick(self, delta_seconds):
        try:
            if self.encoder:
                result = self.encoder.poll()
                if result is None:
                    if time.monotonic() - self.encode_started > 20:
                        self.encoder.terminate()
                        self.finish("FFmpeg exceeded its 20-second assembly limit")
                    return
                self.report["video"] = str(self.video_path) if result == 0 and self.video_path.is_file() else None
                self.finish(None if result == 0 else "FFmpeg failed; captured PNG frames are retained")
                return
            if time.monotonic() - self.started > WALL_LIMIT:
                self.report["partial"] = self.frame < self.frame_count
                self.start_encoding()
                return
            if self.warm < 4:
                self.apply_frame(0)
                self.warm += 1
                return
            record = self.apply_frame(self.frame)
            filename = "%04d.png" % (self.frame + 1)
            u.RenderingLibrary.export_render_target(self.world, self.target, str(self.frames_dir), filename)
            require((self.frames_dir / filename).is_file(), "Capture did not write " + filename)
            self.report["frames"].append(record)
            self.frame += 1
            if self.frame == 1:
                self.report["first_frame"] = str(self.frames_dir / filename)
                self.write_report()
                u.log("MC_NUT_STYLE_V2_FIRST_FRAME " + str(self.frames_dir / filename))
            if self.frame >= self.frame_count:
                self.start_encoding()
        except Exception as error:
            self.finish(str(error))
            u.log_error("MC_NUT_STYLE_V2_PREVIEW_FAIL " + str(error))

    def remove_actors(self):
        if self.target:
            if hasattr(self, "capture_component"):
                self.capture_component.set_editor_property("texture_target", None)
            u.RenderingLibrary.release_render_target2d(self.target)
            self.target = None
        for actor in reversed(self.spawned):
            if u.SystemLibrary.is_valid(actor):
                self.actors.destroy_actor(actor)
        self.spawned = []

    def start_encoding(self):
        self.report["captured_frames"] = self.frame
        self.report["capture_wall_seconds"] = time.monotonic() - self.started
        self.remove_actors()
        self.write_report()
        ffmpeg = Path("C:/ffmpeg/ffmpeg.exe")
        if self.frame == 0 or not ffmpeg.is_file():
            self.finish(None if self.frame else "No frames were captured within the wall-time limit")
            return
        self.video_path = OUT / (self.frames_dir.name.replace("Frames_", "NutStyleV2_UE_") + ".mp4")
        self.encoder_log = (self.frames_dir / "FFmpeg.log").open("wb")
        # Run without a visible helper window; the Editor remains responsive.
        self.encoder = subprocess.Popen([str(ffmpeg), "-y", "-hide_banner", "-loglevel", "error",
            "-framerate", str(FPS), "-i", str(self.frames_dir / "%04d.png"),
            "-vf", "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Tank / Charge                         Wizard / Cast':fontcolor=white:fontsize=22:box=1:boxcolor=black@0.65:boxborderw=7:x=(w-tw)/2:y=16,drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Unreal imported poses - no gameplay physics or overlays':fontcolor=white:fontsize=14:box=1:boxcolor=black@0.65:boxborderw=5:x=(w-tw)/2:y=h-26",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", "-movflags", "+faststart", str(self.video_path)],
            stdout=self.encoder_log, stderr=subprocess.STDOUT, creationflags=0x08000000)
        self.encode_started = time.monotonic()

    def write_report(self):
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(self.report, indent=2), encoding="utf8")

    def finish(self, error=None):
        if self.handle is not None:
            u.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        self.remove_actors()
        if self.encoder_log:
            self.encoder_log.close()
        self.report["captured_frames"] = self.frame
        self.report["passed"] = error is None and self.frame == getattr(self, "frame_count", -1)
        self.report["error"] = error
        self.report["partial"] = self.frame < getattr(self, "frame_count", 0)
        self.report["dirty_packages_after"] = dirty_packages()
        self.report["dirty_packages_preserved"] = self.before_dirty == self.report["dirty_packages_after"]
        self.report["maps_or_assets_saved"] = False
        self.write_report()
        builtins._mc_nut_style_v2_review = None
        u.log("MC_NUT_STYLE_V2_PREVIEW_" + ("PASS" if self.report["passed"] else "FAIL") +
              " " + json.dumps({"frames": self.frame, "partial": self.report["partial"], "video": self.report.get("video"), "error": error}))


def main():
    cleanup_owned()
    return Review()


if __name__ == "__main__":
    main()
