"""Export the artist wizard and independent motion references without saving its source.

Blender --background --factory-startup --threads 2 --python this.py
The shared Tank helper only supplies ARP export/FBX inspection conventions. This
script writes ArtSource/NutWizard exclusively and never changes the live scene.
Procedural clips are motion references: authoritative attack phase timings own
playback rates, holds and blending in the game.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_nut_tank import SETTINGS, digest, enable_arp, fbx_inventory, mesh_inventory, select, sparse_pose_samples

ROOT = Path(__file__).resolve().parents[2]
ARTIST_ACTIONS = (("NUT_W_Firenut", "Cast"), ("NUT_W_CastSummon", "Summon"),
                  ("NUT_W_NutRain", "Rain"), ("NUT_W_NutRain", "HeavyCast"))
PROCEDURAL = (("Idle", 2.4, True), ("Walk", 1.0, True),
              ("Hit", .7, False), ("Death", 1.6, False))


def reset(rig, basis, properties):
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = basis[bone.name].copy()
        for key, value in properties[bone.name].items():
            bone[key] = value
    rig.update_tag()
    bpy.context.view_layer.update()


def bind_action(rig, action):
    rig.animation_data.action = action
    slots = [slot for slot in action.slots if slot.target_id_type == "OBJECT"]
    if len(slots) != 1:
        raise RuntimeError("Expected one object slot: " + action.name)
    rig.animation_data.action_slot = slots[0]


def key_pose(bone, frame):
    for prop in ("location", "rotation_euler" if bone.rotation_mode not in ("QUATERNION", "AXIS_ANGLE") else
                 "rotation_quaternion" if bone.rotation_mode == "QUATERNION" else "rotation_axis_angle", "scale"):
        bone.keyframe_insert(prop, frame=frame, group=bone.name)


def make_procedural(rig, label, seconds, loop, fps, base_basis, base_properties):
    """Key original ARP controls, then let ARP bake the artist's IK constraints."""
    reset(rig, base_basis, base_properties)
    action = bpy.data.actions.new("MC_NutWizard_" + label)
    rig.animation_data.action = action
    end = round(seconds * fps) + 1
    controls = [name for name in ("c_root.x", "c_head.x", "c_neck.x", "c_arm_fk.l", "c_arm_fk.r",
                                 "c_forearm_fk.l", "c_forearm_fk.r", "c_hand_ik.l", "c_hand_ik.r",
                                 "c_foot_ik.l", "c_foot_ik.r") if name in rig.pose.bones]
    base_matrices = {name: rig.pose.bones[name].matrix.copy() for name in controls}
    # Complete neutral control keys isolate this sparse authored action from any
    # previous artist action. The moving controls receive smooth per-frame keys.
    for bone in rig.pose.bones:
        key_pose(bone, 1)
        key_pose(bone, end)
        for key in base_properties[bone.name]:
            bone.keyframe_insert('[' + json.dumps(key) + ']', frame=1, group=bone.name)
            bone.keyframe_insert('[' + json.dumps(key) + ']', frame=end, group=bone.name)
    for frame in range(1, end + 1):
        phase = (frame - 1) / (end - 1)
        angle = math.tau * phase
        for bone in rig.pose.bones:
            bone.matrix_basis = base_basis[bone.name].copy()
        offsets, rotations = {}, {}
        if label == "Idle":
            offsets["c_root.x"] = (0, 0, .018 * math.sin(angle))
            rotations["c_head.x"] = (.025 * math.sin(angle), .03 * math.sin(angle), .035 * math.sin(angle))
            for side, sign in (("l", 1), ("r", -1)):
                offsets["c_hand_ik." + side] = (0, sign * .012 * math.sin(angle), .012 * math.sin(angle))
        elif label == "Walk":
            offsets["c_root.x"] = (.015 * math.sin(angle), 0, .022 * (1 - math.cos(2 * angle)))
            rotations["c_root.x"] = (0, .035 * math.sin(angle), .04 * math.sin(angle))
            for side, sign in (("l", 1), ("r", -1)):
                step = math.sin(angle) * sign
                offsets["c_foot_ik." + side] = (0, .17 * step, .12 * max(0, step))
                offsets["c_hand_ik." + side] = (0, -.08 * step, .01 * math.cos(angle))
            rotations["c_head.x"] = (0, 0, -.025 * math.sin(angle))
        elif label == "Hit":
            kick = math.sin(math.pi * min(1, phase / .3)) * math.exp(-phase * 2.3) if phase < .3 else 0
            recover = math.sin(math.pi * phase) * .08
            offsets["c_root.x"] = (0, .075 * kick, -.035 * kick)
            rotations["c_root.x"] = (-.18 * kick - recover, 0, .055 * kick)
            rotations["c_head.x"] = (.22 * kick, 0, -.075 * kick)
            offsets["c_hand_ik.r"] = (.02 * kick, .04 * kick, .07 * kick)
        else:
            collapse = min(1, phase / .8)
            collapse = collapse * collapse * (3 - 2 * collapse)
            offsets["c_root.x"] = (0, .15 * collapse, -.27 * collapse)
            rotations["c_root.x"] = (-1.0 * collapse, .12 * collapse, .16 * collapse)
            rotations["c_head.x"] = (.30 * collapse, -.2 * collapse, .2 * collapse)
            for side, sign in (("l", 1), ("r", -1)):
                offsets["c_hand_ik." + side] = (.16 * sign * collapse, -.06 * collapse, -.2 * collapse)
        bpy.context.view_layer.update()
        for name in controls:
            matrix = base_matrices[name].copy()
            if name in rotations:
                rx, ry, rz = rotations[name]
                matrix = matrix @ Matrix.Rotation(rx, 4, "X") @ Matrix.Rotation(ry, 4, "Y") @ Matrix.Rotation(rz, 4, "Z")
            if name in offsets:
                matrix.translation += Vector(offsets[name])
            rig.pose.bones[name].matrix = matrix
            key_pose(rig.pose.bones[name], frame)
    slot = rig.animation_data.action_slot
    for layer in action.layers:
        for strip in layer.strips:
            bag = strip.channelbag(slot)
            if bag:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = "LINEAR"
    action.use_fake_user = True
    return {"action": action, "label": label, "start": 1, "end": end,
            "loop": loop, "provenance": "procedural ARP control motion reference", "phase_ready": True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default="C:/Users/user/Downloads/Telegram Desktop/Nut_Wizard.blend")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    source = Path(args.source).resolve()
    out = ROOT / "ArtSource/NutWizard"
    out.mkdir(parents=True, exist_ok=True)
    before = digest(source)
    addon = enable_arp()
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    rig = bpy.data.objects["rig"]
    meshes = [obj for obj in scene.objects if obj.type == "MESH" and any(mod.type == "ARMATURE" and mod.object == rig for mod in obj.modifiers)]
    if not meshes or "SM_NutWizard" not in {obj.name for obj in meshes}:
        raise RuntimeError("Saved wizard does not contain the artist's skinned wizard")
    rig.animation_data_create()
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    rig.animation_data.use_nla = False
    properties = {bone.name: {key: bone[key] for key in bone.keys() if isinstance(bone[key], (int, float, bool))} for bone in rig.pose.bones}
    neutral_basis = {bone.name: Matrix.Identity(4) for bone in rig.pose.bones}
    rest_signature = [{"name": bone.name, "parent": bone.parent.name if bone.parent else None,
                       "matrix": [list(row) for row in bone.matrix_local]} for bone in rig.data.bones]
    for name, value in SETTINGS.items():
        setattr(scene, name, value)
    reset(rig, neutral_basis, properties)
    bind_action(rig, bpy.data.actions["NUT_W_Firenut"])
    scene.frame_set(1)
    bpy.context.view_layer.update()
    baseline = {bone.name: bone.matrix_basis.copy() for bone in rig.pose.bones}
    baseline_props = {bone.name: {key: bone[key] for key in bone.keys() if isinstance(bone[key], (int, float, bool))} for bone in rig.pose.bones}
    report = {"schema": 1, "status": "exporting", "source": str(source), "source_sha256_before": before,
              "blender_version": bpy.app.version_string, "arp": addon, "fps": scene.render.fps / scene.render.fps_base,
              "export_settings": SETTINGS, "mesh_file": "SK_NutWizard.fbx", "source_meshes": [mesh_inventory(obj) for obj in meshes],
              "available_artist_actions": [{"name": action.name, "frames": list(action.frame_range), "slots": [slot.identifier for slot in action.slots]} for action in bpy.data.actions],
              "source_bones": [{"name": bone.name, "parent": bone.parent.name if bone.parent else None, "deform": bone.use_deform} for bone in rig.data.bones],
              "files": [], "clips": [], "source_pose_samples": {}, "textures": [],
              "casting_anchor": {"bone": "hand.r", "local_offset_cm": [0, 8, 0], "scope": "Casting palm; source has no separate staff object/bone"},
              "playback_contract": "Runtime uses normalized authoritative phases, variable rates and hold/blend overlays; reference durations never gate attacks or motion."}
    report_path = out / "ExportReport.json"
    def save_report():
        report_path.write_text(json.dumps(report, indent=2), encoding="utf8")
    # Preserve packed source bytes, rather than rendering textures through color management.
    texture_names = {"stylized_walnut_game_ready_texture_0.PNG": "Walnut_BaseColor.png", "hand_eye": "HandEye_BaseColor.png"}
    for image in bpy.data.images:
        if image.name not in texture_names:
            continue
        packed = image.packed_file
        filename = out / "Textures" / texture_names[image.name]
        filename.parent.mkdir(exist_ok=True)
        if not packed:
            raise RuntimeError("Expected artist texture packed in supplied blend: " + image.name)
        filename.write_bytes(packed.data)
        report["textures"].append({"name": image.name, "file": str(filename.relative_to(out)).replace("\\", "/"),
                                   "packed": True, "bytes": filename.stat().st_size, "sha256": digest(filename),
                                   "original_external_path": image.filepath})
    save_report()
    def export(path):
        select([rig, *meshes], rig)
        result = bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
        if "FINISHED" not in result or not path.is_file() or path.stat().st_size < 10000:
            raise RuntimeError("ARP export failed: " + str(path))
        info = fbx_inventory(path)
        report["files"].append({"file": path.name, "bytes": path.stat().st_size, "sha256": digest(path), "fbx": info})
        save_report()
        print("NUT_WIZARD_FILE_EXPORTED", path.name, info["bone_count"], flush=True)
        return info
    reset(rig, neutral_basis, properties)
    scene.arp_bake_anim = False
    reference = export(out / report["mesh_file"])
    records = []
    for name, label in ARTIST_ACTIONS:
        action = bpy.data.actions[name]
        start, end = map(round, action.frame_range)
        records.append({"action": action, "label": label, "start": start, "end": end,
                        "loop": False, "provenance": "artist", "phase_ready": True})
    for label, seconds, loop in PROCEDURAL:
        records.append(make_procedural(rig, label, seconds, loop, report["fps"], baseline, baseline_props))
    for record in records:
        reset(rig, neutral_basis, properties)
        bind_action(rig, record["action"])
        scene.arp_bake_anim = True
        scene.arp_frame_range_type = "CUSTOM"
        scene.arp_export_start_frame, scene.arp_export_end_frame = record["start"], record["end"]
        scene.frame_start, scene.frame_end = record["start"], record["end"]
        report["source_pose_samples"][record["label"]] = sparse_pose_samples(scene, rig, record["start"], record["end"])
        scene.frame_set(record["start"])
        filename = "A_NutWizard_" + record["label"] + ".fbx"
        info = export(out / filename)
        if info["hierarchy_sha256"] != reference["hierarchy_sha256"]:
            raise RuntimeError("Incompatible generated clip hierarchy: " + filename)
        report["clips"].append({"source_action": record["action"].name, "file": filename,
                                "start_frame": record["start"], "end_frame": record["end"],
                                "duration_seconds": (record["end"] - record["start"]) / report["fps"],
                                "loop": record["loop"], "provenance": record["provenance"], "phase_ready": record["phase_ready"]})
        save_report()
    after_rest = [{"name": bone.name, "parent": bone.parent.name if bone.parent else None,
                   "matrix": [list(row) for row in bone.matrix_local]} for bone in rig.data.bones]
    report["source_sha256_after"] = digest(source)
    report["source_preserved"] = before == report["source_sha256_after"]
    report["original_source_preserved"] = report["source_preserved"]
    report["source_reference_pose_unchanged"] = rest_signature == after_rest
    base_bind = {node["name"]: node["matrix"] for node in reference["bind_poses"]}
    comparisons = []
    for item in report["files"][1:]:
        current = {node["name"]: node["matrix"] for node in item["fbx"]["bind_poses"]}
        maximum = max(abs(a - b) for name in base_bind for a, b in zip(base_bind[name], current[name]))
        comparisons.append({"file": item["file"], "maximum_bind_matrix_difference": maximum})
    report["bind_pose_comparisons"] = comparisons
    report["derived_reference_pose_unchanged"] = all(item["maximum_bind_matrix_difference"] < .0001 for item in comparisons)
    report["root_animation_constant"] = all(item["fbx"]["root_animation_constant"] for item in report["files"][1:])
    report["hierarchy_consistent"] = len({item["fbx"]["hierarchy_sha256"] for item in report["files"]}) == 1
    if not all(report[key] for key in ("source_preserved", "source_reference_pose_unchanged", "derived_reference_pose_unchanged", "root_animation_constant", "hierarchy_consistent")):
        raise RuntimeError("Source, hierarchy, reference pose or root invariants failed")
    report["status"] = "complete"
    save_report()
    print("NUT_WIZARD_EXPORT_PASS", json.dumps({"files": len(report["files"]), "clips": len(report["clips"]), "bones": reference["bone_count"], "source_preserved": True}), flush=True)


if __name__ == "__main__":
    main()
