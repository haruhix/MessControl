"""Isolate the artist's ARP warrior and seven actions for Unreal, without saving its source.

Run with Blender 5.1 --background --factory-startup --python this.py -- [--source ...].
The installed Auto-Rig Pro exporter bakes constraints into one reusable deform rig.
Only generated files under ArtSource/NutTank are written; no Blender preferences are saved.
"""
import argparse
import hashlib
import importlib
import json
import sys
import tomllib
from pathlib import Path

import addon_utils
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
CLIPS = (
    ("NUT_Idle", "Idle", 1, 56),
    ("NUT_Walk_Combat", "Walk", 0, 41),
    ("NUT_Walk_Combat_Left", "WalkLeft", 1, 38),
    ("NUT_Walk_Combat_Right", "WalkRight", 0, 39),
    ("NUT_Atack_01", "Melee", 1, 127),
    ("NUT_Jump_atack", "Jump", 1, 114),
    ("Jump_TranseBall", "Transform", 1, 115),
)
MESH_NAMES = ("SM_NutTank", "SM_Sheld", "SM_Club")
SETTINGS = dict(
    arp_export_rig_type="UNIVERSAL", arp_engine_type="UNREAL",
    arp_rename_for_ue=True, arp_export_twist=True, arp_full_facial=True,
    arp_ue_root_motion=False, arp_export_rig_name="root", arp_units_x100=True,
    arp_global_scale=1.0, arp_ge_sel_only=True, arp_export_tex=False,
    arp_ge_add_dummy_mesh=False, arp_ge_force_rest_pose_export=True,
    arp_bake_type="ACTIONS", arp_bake_only_active=True, arp_bake_only_active_slot=True,
    arp_export_use_actlist=False, arp_export_separate_fbx=False,
    arp_simplify_fac=0.0, arp_ge_bake_sample=1.0, arp_export_triangulate=True,
    arp_apply_mods=True,
)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def rounded_vector(value):
    return [round(float(v), 6) for v in value]


def enable_arp():
    # Prefer the configured extension; the legacy module fallback is process-local.
    module_names = ["bl_ext.user_default.auto_rig_pro"]
    extension_parent = Path.home() / "AppData/Roaming/Blender Foundation/Blender/5.1/extensions/user_default"
    if (extension_parent / "auto_rig_pro/__init__.py").is_file():
        sys.path.insert(0, str(extension_parent))
        module_names.append("auto_rig_pro")
    for name in module_names:
        try:
            # ARP reads its AddonPreferences during registration. This creates the
            # entry in this factory-startup process; preferences are never saved.
            module = addon_utils.enable(name, default_set=True, persistent=False)
            if module is not None and hasattr(bpy.types.Scene, "arp_export_rig_type"):
                manifest = Path(module.__file__).with_name("blender_manifest.toml")
                version = (getattr(module, "bl_info", {}) or {}).get("version")
                if version is None and manifest.is_file():
                    version = tomllib.loads(manifest.read_text(encoding="utf8"))["version"]
                return {"module": name, "version": version}
        except Exception as error:
            print("ARP_ENABLE_ATTEMPT", name, repr(error), flush=True)
    raise RuntimeError("Installed Auto-Rig Pro could not be enabled in the isolated process")


def select(objects, active):
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for collection in bpy.data.collections:
        collection.hide_viewport = False
    for obj in bpy.context.scene.objects:
        obj.select_set(False)
    for obj in objects:
        obj.hide_set(False)
        obj.hide_viewport = False
        obj.hide_select = False
        obj.select_set(True)
    bpy.context.view_layer.objects.active = active


def fbx_inventory(path):
    """Read the produced FBX hierarchy/units, independently of ARP's temporary rigs."""
    parser = importlib.import_module("io_scene_fbx.parse_fbx")
    tree, version = parser.parse(str(path))
    sections = {element.id: element for element in tree.elems}
    objects = sections[b"Objects"].elems
    decode = lambda value: value.decode("utf8", "replace") if isinstance(value, bytes) else value
    name_of = lambda value: decode(value).split("\x00", 1)[0].split("::")[-1]
    models = {element.props[0]: element for element in objects if element.id == b"Model"}
    parents = {}
    for connection in sections[b"Connections"].elems:
        if (connection.props[0] == b"OO" and connection.props[1] in models
                and (connection.props[2] in models or connection.props[2] == 0)):
            parents[connection.props[1]] = connection.props[2]
    def properties(element):
        prop_section = next((e for e in element.elems if e.id == b"Properties70"), None)
        return {decode(e.props[0]): [decode(v) for v in e.props[4:]] for e in prop_section.elems} if prop_section else {}
    bones = []
    roots = []
    for identifier, model in models.items():
        parent = models.get(parents.get(identifier))
        item = {"name": name_of(model.props[1]), "parent": name_of(parent.props[1]) if parent else None,
                "properties": properties(model)}
        if model.props[2] == b"LimbNode":
            bones.append(item)
        elif model.props[2] == b"Null":
            roots.append(item)
    materials = [name_of(e.props[1]) for e in objects if e.id == b"Material"]
    global_properties = properties(sections[b"GlobalSettings"])
    stacks = [name_of(e.props[1]) for e in objects if e.id == b"AnimationStack"]
    hierarchy = [{"name": item["name"], "parent": item["parent"]} for item in bones]
    by_id = {e.props[0]: e for e in objects if e.props and isinstance(e.props[0], int)}
    connections = sections[b"Connections"].elems
    root_ids = {identifier for identifier, model in models.items() if model.props[2] == b"Null" and parents.get(identifier, 0) == 0}
    root_curves = []
    for connection in connections:
        if connection.props[0] != b"OP" or connection.props[2] not in root_ids:
            continue
        node_id = connection.props[1]
        for axis_link in connections:
            if axis_link.props[0] != b"OP" or axis_link.props[2] != node_id:
                continue
            curve = by_id.get(axis_link.props[1])
            if not curve or curve.id != b"AnimationCurve":
                continue
            values = next((element.props[0] for element in curve.elems if element.id == b"KeyValueFloat"), [])
            if len(values):
                root_curves.append({"property": decode(connection.props[3]), "axis": decode(axis_link.props[3]),
                                    "minimum": float(min(values)), "maximum": float(max(values)), "keys": len(values)})
    bind_poses = []
    for element in objects:
        if element.id != b"Pose":
            continue
        for node in element.elems:
            if node.id != b"PoseNode":
                continue
            identifier = next(e.props[0] for e in node.elems if e.id == b"Node")
            matrix = next(e.props[0] for e in node.elems if e.id == b"Matrix")
            model = models.get(identifier)
            if model is not None:
                bind_poses.append({"name": name_of(model.props[1]), "matrix": [round(float(v), 5) for v in matrix]})
    geometry_bounds = []
    for element in objects:
        if element.id != b"Geometry" or element.props[2] != b"Mesh":
            continue
        vertices = next((e.props[0] for e in element.elems if e.id == b"Vertices"), [])
        if len(vertices):
            lower = [float(min(vertices[axis::3])) for axis in range(3)]
            upper = [float(max(vertices[axis::3])) for axis in range(3)]
            geometry_bounds.append({"name": name_of(element.props[1]), "lower": lower, "upper": upper,
                                    "dimensions": [upper[axis] - lower[axis] for axis in range(3)]})
    return {"version": version, "bones": bones, "bone_count": len(bones), "object_roots": roots,
            "hierarchy_sha256": hashlib.sha256(json.dumps(hierarchy, sort_keys=True).encode()).hexdigest(),
            "bind_poses": bind_poses, "root_animation_curves": root_curves, "geometry_bounds_fbx_units": geometry_bounds,
            "root_animation_constant": bool(root_curves) and all(abs(item["maximum"] - item["minimum"]) < .0001 for item in root_curves),
            "materials": materials, "animation_stacks": stacks, "global_settings": global_properties}


def mesh_inventory(obj):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=bpy.context.evaluated_depsgraph_get())
    result = {"name": obj.name, "base_vertices": len(obj.data.vertices), "evaluated_vertices": len(mesh.vertices),
              "evaluated_polygons": len(mesh.polygons),
              "material_slots": [m.name if m else None for m in mesh.materials],
              "vertex_groups": [group.name for group in obj.vertex_groups],
              "modifiers": [{"type": mod.type, "name": mod.name} for mod in obj.modifiers],
              "bounds_meters": rounded_vector(evaluated.dimensions)}
    evaluated.to_mesh_clear()
    return result


def sparse_pose_samples(scene, rig, start, end):
    frames = sorted({round(start + (end - start) * fraction) for fraction in (0, .125, .25, .375, .5, .625, .75, .875, 1)})
    available = [name for name in ("root.x", "foot.l", "foot.r", "hand.l", "hand.r", "head.x") if name in rig.pose.bones]
    samples = []
    for frame in frames:
        scene.frame_set(frame)
        rig_eval = rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
        samples.append({"frame": frame, "fraction": round((frame - start) / (end - start), 6),
                        "bone_positions_meters": {name: rounded_vector((rig_eval.matrix_world @ rig_eval.pose.bones[name].matrix).translation) for name in available}})
    return samples


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default="C:/Users/user/Downloads/Telegram Desktop/Nut_Tank.blend")
    parser.add_argument("--out", default=str(ROOT / "ArtSource/NutTank"))
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    source = Path(args.source).resolve()
    out = Path(args.out).resolve()
    if out != (ROOT / "ArtSource/NutTank").resolve():
        raise RuntimeError("Generated outputs must stay in this project's ArtSource/NutTank")
    out.mkdir(parents=True, exist_ok=True)
    original_hash = digest(source)
    addon = enable_arp()
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    rig = bpy.data.objects["rig"]
    meshes = [bpy.data.objects[name] for name in MESH_NAMES]
    for obj in meshes:
        if not any(mod.type == "ARMATURE" and mod.object == rig for mod in obj.modifiers):
            raise RuntimeError("Expected artist mesh bound to rig: " + obj.name)
    rig.animation_data_create()
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    rig.animation_data.use_nla = False
    rig.animation_data.action = None
    # Reset custom IK/FK controls between sparse actions, before each bake.
    initial_props = {bone.name: {key: bone[key] for key in bone.keys() if isinstance(bone[key], (bool, int, float))} for bone in rig.pose.bones}
    for name, value in SETTINGS.items():
        if not hasattr(scene, name):
            raise RuntimeError("Unsupported ARP setting: " + name)
        setattr(scene, name, value)
    scene.frame_set(1)
    report = {"schema": 1, "status": "exporting", "source": str(source), "source_sha256_before": original_hash,
              "blender_version": bpy.app.version_string, "arp": addon, "fps": scene.render.fps / scene.render.fps_base,
              "source_unit_scale": scene.unit_settings.scale_length, "export_settings": SETTINGS,
              "source_meshes": [mesh_inventory(obj) for obj in meshes],
              "source_bones": [{"name": bone.name, "parent": bone.parent.name if bone.parent else None, "deform": bone.use_deform} for bone in rig.data.bones],
              "clips": [], "files": [], "source_pose_samples": {},
              "compatibility": "All seven clips use the same derived ARP Unreal deform hierarchy. Existing /Game/FromBlender8 skeleton compatibility is unverified and it remains untouched.",
              "timing_scope": "Sparse source poses document motion; contact/takeoff/landing gameplay fractions require visual confirmation and remain designer-tunable.",
              "import_notes": "Import SK_NutTank first into a new skeleton, then import each A_NutTank_* animation only against it. ARP bakes constraints and applies centimeter conversion; do not apply an extra 100x import scale."}
    report_path = out / "ExportReport.json"
    def save_report():
        report_path.write_text(json.dumps(report, indent=2), encoding="utf8")
    save_report()
    def export_arp(path):
        select([rig, *meshes], rig)
        result = bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
        if "FINISHED" not in result or not path.is_file() or path.stat().st_size < 10000:
            raise RuntimeError("ARP did not produce " + str(path))
        info = fbx_inventory(path)
        report["files"].append({"file": path.name, "bytes": path.stat().st_size, "sha256": digest(path), "fbx": info})
        save_report()
        print("NUT_TANK_FILE_EXPORTED", path.name, info["bone_count"], flush=True)
        return info
    scene.arp_bake_anim = False
    mesh_info = export_arp(out / "SK_NutTank.fbx")
    for action_name, label, start, end in CLIPS:
        rig.animation_data.action = None
        for bone in rig.pose.bones:
            bone.matrix_basis.identity()
            for key, value in initial_props[bone.name].items():
                bone[key] = value
        action = bpy.data.actions[action_name]
        rig.animation_data.action = action
        matching_slots = [slot for slot in action.slots if slot.target_id_type == "OBJECT"]
        if len(matching_slots) != 1:
            raise RuntimeError("Expected one object action slot: " + action_name)
        rig.animation_data.action_slot = matching_slots[0]
        scene.arp_bake_anim = True
        scene.arp_frame_range_type = "CUSTOM"
        scene.arp_export_start_frame, scene.arp_export_end_frame = start, end
        scene.frame_start, scene.frame_end = start, end
        report["source_pose_samples"][label] = sparse_pose_samples(scene, rig, start, end)
        scene.frame_set(start)
        path = out / ("A_NutTank_" + label + ".fbx")
        info = export_arp(path)
        if info["hierarchy_sha256"] != mesh_info["hierarchy_sha256"]:
            raise RuntimeError("Clip deform hierarchy differs from skeletal mesh: " + label)
        report["clips"].append({"source_action": action_name, "action_slot": matching_slots[0].identifier,
                                "file": path.name, "start_frame": start, "end_frame": end,
                                "duration_seconds": (end - start) / report["fps"], "sample_frames": end - start + 1})
        save_report()
    # Export an independent, centered static ball. Its imported size is recorded below.
    ball_source = bpy.data.objects["stylized_walnut_game_ready.001"]
    ball_eval = ball_source.evaluated_get(bpy.context.evaluated_depsgraph_get())
    ball_data = bpy.data.meshes.new_from_object(ball_eval, preserve_all_data_layers=True, depsgraph=bpy.context.evaluated_depsgraph_get())
    ball = bpy.data.objects.new("SM_NutTank_Ball", ball_data)
    scene.collection.objects.link(ball)
    matrix = ball_source.matrix_world.copy()
    points = [matrix @ vertex.co for vertex in ball.data.vertices]
    lower = Vector(tuple(min(point[axis] for point in points) for axis in range(3)))
    upper = Vector(tuple(max(point[axis] for point in points) for axis in range(3)))
    center = (lower + upper) * .5
    for vertex, point in zip(ball.data.vertices, points):
        vertex.co = point - center
    select([ball], ball)
    path = out / "SM_NutTank_Ball.fbx"
    result = bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={"MESH"},
                                    global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE",
                                    axis_forward="-Y", axis_up="Z", use_mesh_modifiers=True,
                                    mesh_smooth_type="FACE", add_leaf_bones=False, bake_anim=False,
                                    path_mode="STRIP", embed_textures=False)
    if "FINISHED" not in result or not path.is_file() or path.stat().st_size < 1000:
        raise RuntimeError("Static ball export failed")
    report["ball"] = {"source_object": ball_source.name, "file": path.name,
                       "bounds_meters": rounded_vector(upper - lower), "pivot": "geometry bounding-box center",
                       "material_slots": [mat.name if mat else None for mat in ball.data.materials]}
    report["files"].append({"file": path.name, "bytes": path.stat().st_size, "sha256": digest(path), "fbx": fbx_inventory(path)})
    report["source_sha256_after"] = digest(source)
    if report["source_sha256_after"] != original_hash:
        raise RuntimeError("Source blend hash changed")
    report["status"] = "complete"
    report["source_preserved"] = True
    save_report()
    print("NUT_TANK_EXPORT_PASS", json.dumps({"files": [item["file"] for item in report["files"]], "bone_count": mesh_info["bone_count"], "source_preserved": True}), flush=True)


if __name__ == "__main__":
    main()
