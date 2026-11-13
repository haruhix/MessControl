"""Check imported katana dimensions, PBR attributes and compressed attachment motion."""
import json
import math
import os
from pathlib import Path

import unreal as u


def asset(path):
    result = u.EditorAssetLibrary.load_asset(path)
    if result is None:
        raise RuntimeError(f"Missing asset {path}")
    return result


def attachment_error(clip, entry, options, blade_samples):
    errors = [0.0] * len(blade_samples)
    hand_scale_error = 0.0
    hands = ("hand_l", "hand_r", "index1_l", "index2_l", "index3_l", "thumb1_l", "thumb2_l",
             "thumb3_l", "index1_r", "index2_r", "index3_r", "thumb1_r", "thumb2_r", "thumb3_r")
    for frame, sample_data in enumerate(entry["samples"]):
        points = sample_data["prop"]
        time = frame / entry["fps"]
        pose = u.AnimPoseExtensions.get_anim_pose_at_time(clip, time, options)
        assert u.AnimPoseExtensions.is_valid(pose)
        for name in hands:
            scale = pose.get_bone_pose(name, u.AnimPoseSpaces.WORLD).scale3d.to_tuple()
            hand_scale_error = max(hand_scale_error, max(abs(axis - 1) for axis in scale))
        hand = pose.get_bone_pose("hand_r", u.AnimPoseSpaces.WORLD)
        curve = lambda name: u.AnimPoseExtensions.get_curve_weight(pose, "MCWeapon" + name)
        assert curve("Authored") > .99
        quaternion = [curve(channel) for channel in ("QX", "QY", "QZ", "QW")]
        length = math.sqrt(sum(channel * channel for channel in quaternion))
        assert length > .99
        q = u.Quat(*(channel / length for channel in quaternion))
        transform = u.Transform()
        transform.set_editor_property("rotation", q)
        transform.set_editor_property("translation", u.Vector(curve("X"), curve("Y"), curve("Z")))
        transform.set_editor_property("scale3d", u.Vector(curve("SX"), curve("SY"), curve("SZ")))
        composed = u.MathLibrary.compose_transforms(transform, hand)
        for i, (sample, expected) in enumerate(zip(blade_samples, points)):
            distance = (composed.transform_location(sample) - u.Vector(*expected)).length()
            errors[i] = max(errors[i], distance)
        for name in ("MCKatanaMouthEffort", "MCKatanaMouthAngry", "MCKatanaEyesScary"):
            difference = abs(u.AnimPoseExtensions.get_curve_weight(pose, name) - entry["curves"][name][frame])
            assert difference < .015, (clip.get_name(), frame, name, difference)
    assert hand_scale_error < .015, (clip.get_name(), hand_scale_error)
    return errors, hand_scale_error


def main():
    root = Path(os.environ["MC_KATANA_FBX_DIR"])
    imported = json.loads((root / "KatanaImport.json").read_text(encoding="utf-8"))
    source = json.loads((root / "KatanaBake.json").read_text(encoding="utf-8"))
    mesh = asset(imported["mesh"]["asset"])
    data = mesh.get_static_mesh_description(0)
    tip = max(
        (data.get_vertex_position(u.VertexID(i)) for i in range(data.get_vertex_count())),
        key=lambda point: point.z,
    )
    assert (tip - u.Vector(*source["prop"]["tip_ue_cm"])).length() < .05, tip
    values = [data.get_vertex_instance_uv(u.VertexInstanceID(i), 1)
              for i in range(data.get_vertex_instance_count())]
    metallic = sorted({round(v.x, 2) for v in values})
    roughness = sorted({round(v.y, 2) for v in values})
    assert len(metallic) >= 10 and len(roughness) >= 8
    hero = asset("/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay")
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_properties({
        "optional_skeletal_mesh": hero,
        "evaluation_type": u.AnimDataEvalType.COMPRESSED,
        "should_retarget": False,
    })
    blade_samples = (u.Vector(0, 0, 0), u.Vector(*source["prop"]["base_ue_cm"]), tip)
    checks = []
    for item, entry in zip(imported["animations"], source["clips"]):
        clip = asset(item["asset"])
        assert clip.get_name() == entry["asset"]
        error, scale_error = attachment_error(clip, entry, options, blade_samples)
        checks.append({"asset": item["asset"], "frames": entry["frames"],
                       "max_prop_error_cm": dict(zip(("pivot", "blade_base", "tip"), error)),
                       "max_hand_scale_error": scale_error})
        u.log(f"MC_KATANA_ATTACHMENT {entry['asset']} {error}")
    report = {"mesh_tip_cm": [tip.x, tip.y, tip.z], "metallic_uv_values": metallic,
              "roughness_uv_values": roughness, "animations": checks}
    (root / "KatanaAssetValidation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    worst = max(value for check in checks for value in check["max_prop_error_cm"].values())
    if worst > 1.0:
        raise ValueError(f"Compressed sword pose differs from Blender by up to {worst:.3f} cm")
    u.log("MC_KATANA_ASSET_VALIDATION_PASS")


if __name__ == "__main__":
    main()
