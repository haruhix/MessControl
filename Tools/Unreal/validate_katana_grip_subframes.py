"""Read-only dense compressed-pose validation of both katana grips.

Run in Unreal after importing the six clips. MC_KATANA_FBX_DIR selects the
export/report folder; MC_KATANA_GRIP_SAMPLE_FPS defaults to 960 so a 240 fps
bake is also checked between its keys. No assets or editor state are saved.
This verifies wrist frames relative to the mounted sword, not visible skin.
"""
import json
import math
import os
from pathlib import Path

import unreal as u


CLIP_ROOT = "/Game/Gameplay/CharacterCurrent/Animations/Katana/"
HERO_PATH = "/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay"
CLIPS = ("Carry", "DiagonalRight", "DiagonalLeft", "Horizontal", "Overhead", "Charged")
HANDS = ("hand_l", "hand_r")


def load_asset(path, expected_type):
    value = u.EditorAssetLibrary.load_asset(path)
    if not isinstance(value, expected_type):
        raise RuntimeError("Missing or incompatible asset: " + path)
    return value


def vector_values(value):
    return [float(value.x), float(value.y), float(value.z)]


def quaternion_values(value):
    return [float(value.x), float(value.y), float(value.z), float(value.w)]


def orientation_error_deg(first, second):
    a, b = quaternion_values(first), quaternion_values(second)
    length_a = math.sqrt(sum(value * value for value in a))
    length_b = math.sqrt(sum(value * value for value in b))
    if not math.isfinite(length_a * length_b) or min(length_a, length_b) < .01:
        raise ValueError("Invalid evaluated grip quaternion")
    # q and -q describe the same orientation, including across quaternion keys.
    dot = abs(sum(x * y for x, y in zip(a, b)) / (length_a * length_b))
    return math.degrees(2 * math.acos(max(0., min(1., dot))))


def transform_values(value):
    return {"position_cm": vector_values(value.translation),
            "quaternion_xyzw": quaternion_values(value.rotation),
            "scale": vector_values(value.scale3d)}


def sword_mount(pose):
    curve = lambda suffix: float(u.AnimPoseExtensions.get_curve_weight(pose, "MCWeapon" + suffix))
    if curve("Authored") < .99:
        raise ValueError("Evaluated pose lacks authored weapon curves")
    quaternion = [curve(suffix) for suffix in ("QX", "QY", "QZ", "QW")]
    length = math.sqrt(sum(value * value for value in quaternion))
    position = [curve(suffix) for suffix in ("X", "Y", "Z")]
    scale = [curve(suffix) for suffix in ("SX", "SY", "SZ")]
    if (not all(math.isfinite(value) for value in (*quaternion, *position, *scale))
            or length < .01 or min(scale) <= 0):
        raise ValueError("Invalid evaluated weapon mount")
    result = u.Transform()
    result.set_editor_properties({
        "rotation": u.Quat(*(value / length for value in quaternion)),
        "translation": u.Vector(*position),
        "scale3d": u.Vector(*scale),
    })
    return result, abs(length - 1.)


def sampled_clip(clip, options, sample_fps, source_fps, position_limit, angle_limit):
    length = float(clip.get_play_length())
    if not math.isfinite(length) or length <= 0:
        raise ValueError("Animation has no finite duration")
    # Include the endpoint exactly, even for clips not divisible by sample_fps.
    times = [index / sample_fps for index in range(math.ceil(length * sample_fps))]
    times.append(length)
    reference = {}
    metrics = {name: {"max_position_delta_cm": 0., "max_orientation_delta_deg": 0.,
                      "max_between_keys_position_delta_cm": 0.,
                      "max_between_keys_orientation_delta_deg": 0.,
                      "max_scale_delta": 0., "samples_above_tolerance": 0,
                      "worst_position": None, "worst_orientation": None}
               for name in HANDS}
    max_mount_quaternion_norm_error = 0.
    max_hand_scale_error = 0.
    between_key_samples = 0
    for index, seconds in enumerate(times):
        pose = u.AnimPoseExtensions.get_anim_pose_at_time(clip, seconds, options)
        if not u.AnimPoseExtensions.is_valid(pose):
            raise ValueError(f"Invalid compressed pose at {seconds:.8f} seconds")
        if index == 0:
            names = {str(name) for name in u.AnimPoseExtensions.get_bone_names(pose)}
            if not set(HANDS).issubset(names):
                raise ValueError("Evaluated pose lacks the actual gameplay hand bones")
        hand_poses = {name: pose.get_bone_pose(name, u.AnimPoseSpaces.WORLD) for name in HANDS}
        mount, norm_error = sword_mount(pose)
        max_mount_quaternion_norm_error = max(max_mount_quaternion_norm_error, norm_error)
        # AnimPose WORLD is skeletal component space. Both hands and the sword
        # use that same space; actor/camera transforms cancel in this comparison.
        sword = u.MathLibrary.compose_transforms(mount, hand_poses["hand_r"])
        between_keys = source_fps is not None and abs(seconds * source_fps - round(seconds * source_fps)) > 1.e-4
        between_key_samples += int(between_keys)
        for name, hand in hand_poses.items():
            relative = u.MathLibrary.make_relative_transform(hand, sword)
            if index == 0:
                reference[name] = relative
            expected_hand = u.MathLibrary.compose_transforms(reference[name], sword)
            # Measure displacement in component-space centimeters, accounting
            # for any authored mount scale rather than assuming unit scale.
            position_error = float((hand.translation - expected_hand.translation).length())
            angle_error = orientation_error_deg(hand.rotation, expected_hand.rotation)
            scale_error = max(abs(a - b) for a, b in zip(vector_values(relative.scale3d),
                                                        vector_values(reference[name].scale3d)))
            if not all(math.isfinite(value) for value in (position_error, angle_error, scale_error)):
                raise ValueError(f"Non-finite grip error for {name} at {seconds:.8f}")
            max_hand_scale_error = max(max_hand_scale_error,
                                       max(abs(value - 1.) for value in vector_values(hand.scale3d)))
            row = metrics[name]
            sample = {"time_seconds": round(seconds, 8), "between_source_keys": between_keys,
                      "position_delta_cm": position_error, "orientation_delta_deg": angle_error,
                      "relative_grip": transform_values(relative)}
            if row["worst_position"] is None or position_error > row["max_position_delta_cm"]:
                row["max_position_delta_cm"] = position_error
                row["worst_position"] = sample
            if row["worst_orientation"] is None or angle_error > row["max_orientation_delta_deg"]:
                row["max_orientation_delta_deg"] = angle_error
                row["worst_orientation"] = sample
            if between_keys:
                row["max_between_keys_position_delta_cm"] = max(row["max_between_keys_position_delta_cm"], position_error)
                row["max_between_keys_orientation_delta_deg"] = max(row["max_between_keys_orientation_delta_deg"], angle_error)
            row["max_scale_delta"] = max(row["max_scale_delta"], scale_error)
            row["samples_above_tolerance"] += int(position_error > position_limit or angle_error > angle_limit)
    passed = all(value["samples_above_tolerance"] == 0 for value in metrics.values()) and max_hand_scale_error <= .015
    return {"asset": clip.get_path_name(), "passed": passed,
            "duration_seconds": length, "sample_fps": sample_fps,
            "source_fps_from_bake": source_fps, "sample_count": len(times),
            "between_source_key_samples": between_key_samples,
            "reference_grips_at_time_zero": {name: transform_values(value) for name, value in reference.items()},
            "hands": metrics, "max_hand_scale_error": max_hand_scale_error,
            "max_mount_quaternion_norm_error": max_mount_quaternion_norm_error}


def main():
    export_root = Path(os.environ.get("MC_KATANA_FBX_DIR",
        "E:/CodexTemp/MessControl/KatanaWideArc/Exports")).resolve()
    output = Path(os.environ.get("MC_KATANA_GRIP_OUTPUT",
        str(export_root / "KatanaGripSubframes.json"))).resolve()
    if export_root.drive.upper() != "E:" or output.drive.upper() != "E:":
        raise ValueError("Katana validation files must remain on E:")
    output.parent.mkdir(parents=True, exist_ok=True)
    sample_fps = float(os.environ.get("MC_KATANA_GRIP_SAMPLE_FPS", "960"))
    position_limit = float(os.environ.get("MC_KATANA_GRIP_POSITION_CM", "1"))
    angle_limit = float(os.environ.get("MC_KATANA_GRIP_ANGLE_DEG", "5"))
    if not 120 <= sample_fps <= 1920 or not 0 < position_limit <= 1 or not 0 < angle_limit <= 5:
        raise ValueError("Use 120–1920 Hz and thresholds no looser than 1 cm / 5 degrees")
    report = {"passed": False, "evaluation_type": "COMPRESSED", "should_retarget": False,
              "optional_skeletal_mesh": HERO_PATH, "sample_fps": sample_fps,
              "position_limit_cm": position_limit, "orientation_limit_deg": angle_limit,
              "scope": "Both evaluated wrist frames relative to the actual curve-mounted sword; per-clip time-zero grip reference. Visible skin, gameplay layering and first-use readiness require separate runtime inspection.",
              "clips": []}
    try:
        source_fps = {}
        if (export_root / "KatanaBake.json").exists():
            bake = json.loads((export_root / "KatanaBake.json").read_text(encoding="utf-8"))
            source_fps = {entry["asset"]: float(entry["fps"]) for entry in bake["clips"]}
        hero = load_asset(HERO_PATH, u.SkeletalMesh)
        options = u.AnimPoseEvaluationOptions()
        options.set_editor_properties({"optional_skeletal_mesh": hero,
            "evaluation_type": u.AnimDataEvalType.COMPRESSED, "should_retarget": False,
            "evaluate_curves": True, "extract_root_motion": False,
            "incorporate_root_motion_into_pose": True})
        skeleton = hero.get_editor_property("skeleton")
        for name in CLIPS:
            asset_path = CLIP_ROOT + "A_Katana_" + name
            try:
                clip = load_asset(asset_path, u.AnimSequence)
                if clip.get_editor_property("skeleton") != skeleton:
                    raise ValueError("Clip does not use the gameplay skeleton")
                entry = sampled_clip(clip, options, sample_fps, source_fps.get(clip.get_name()), position_limit, angle_limit)
                report["clips"].append(entry)
                left = entry["hands"]["hand_l"]
                u.log(f"MC_KATANA_GRIP_SUBFRAMES {clip.get_name()} passed={entry['passed']} left_cm={left['max_position_delta_cm']:.5f} left_deg={left['max_orientation_delta_deg']:.5f}")
            except Exception as error:
                report["clips"].append({"asset": asset_path, "passed": False, "error": str(error)})
        report["passed"] = len(report["clips"]) == len(CLIPS) and all(entry["passed"] for entry in report["clips"])
    except Exception as error:
        report["error"] = str(error)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    if not report["passed"]:
        raise RuntimeError("Katana compressed subframe grip validation failed; inspect " + str(output))
    u.log("MC_KATANA_GRIP_SUBFRAMES_PASS " + str(output))


if __name__ == "__main__":
    main()
