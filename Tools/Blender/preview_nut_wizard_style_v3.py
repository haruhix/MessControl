"""Render an expressive Wizard review without saving or changing its source.

Run with Blender --background --python this_file -- [--poses-only]. Main clips
use current native default timings, including HeavyCast as the Teleport pose.
The separate optional montage is DCC performance exploration, not new gameplay.
"""
import argparse
import hashlib
import json
import math
import subprocess
import sys
import time
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "ArtSource/NutAnimationStyle/Wizard"


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=OUT / "NutWizard_Style_v3.blend")
    parser.add_argument("--poses-only", action="store_true")
    parser.add_argument("--production-only", action="store_true")
    parser.add_argument("--optional-only", action="store_true")
    parser.add_argument("--resolution", type=int, default=640)
    parser.add_argument("--fps", type=int, default=24)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def attack(preparation, recovery):
    def sample(t):
        if t < preparation:
            return .55 * t / preparation
        return .55 + .45 * (t - preparation) / recovery
    return sample


def rain(t):
    if t < 1.2:
        return .55 * t / 1.2
    if t < 5.2:
        return .55 + .035 * (1 + math.sin((t - 1.2) * 3.5))
    return .55 + .45 * (t - 5.2) / 1.4


def production_segments():
    # Native defaults: GetAttackWindup + ExecuteAttack recovery / Rain channel.
    return [
        ("Idle", "Poised archmage", 4.0, lambda t: t / 4.0),
        ("Walk", "Walk", 1.6, lambda t: t / 1.6),
        ("Cast", "FireNut", 1.8, attack(.9, .9)),
        ("Summon", "Summon", 2.3, attack(1.1, 1.2)),
        ("Rain", "NutRain - four second channel", 6.6, rain),
        ("Melee", "Close-range strike", 1.55, attack(.65, .9)),
        ("HeavyCast", "Teleport", 1.2, attack(.65, .55)),
        ("Hit", "Hit reaction", .4, lambda t: t / .4),
        ("Death", "Defeat", 2.4, lambda t: t / 2.4),
    ]


KEY_POSES = {
    "Idle": (.15, .65), "Walk": (.25, .75),
    "Cast": (.20, .40, .55, .70), "Summon": (.25, .45, .55, .70),
    "Rain": (.25, .50, .55, .58, .62), "Melee": (.35, .55, .70),
    "HeavyCast": (.25, .55, .63, .70), "Hit": (.35,), "Death": (.40, .65, 1.0),
    "Taunt": (.40, .65), "Victory": (.40, .70),
    "SpawnFlourish": (.40, .70), "IdleOrbit": (.25, .65),
    "IdleMeditate": (.25, .65), "SpellFizzle": (.45, .75),
}
OPTIONAL = [
    ("Taunt", "Taunt"), ("Victory", "Victory"),
    ("SpawnFlourish", "Entrance flourish"), ("IdleOrbit", "Orbiting idle"),
    ("IdleMeditate", "Meditation"), ("SpellFizzle", "Spell fizzle"),
]


def main():
    args = parse_args()
    if args.production_only and args.optional_only:
        raise ValueError("Choose either --production-only or --optional-only")
    if args.resolution < 256 or args.resolution % 2 or args.fps < 12:
        raise ValueError("Use an even resolution >= 256 and fps >= 12")
    started = time.monotonic()
    source = args.source.resolve()
    source_hash = digest(source)
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    rig = bpy.data.objects.get("rig")
    if rig is None or rig.type != "ARMATURE":
        raise RuntimeError("Expected the original Wizard armature named rig")
    rig.animation_data_create()
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    rig.animation_data.use_nla = False
    meshes = [obj for obj in scene.objects if obj.type == "MESH" and
              any(mod.type == "ARMATURE" and mod.object == rig for mod in obj.modifiers)]
    if not meshes:
        raise RuntimeError("No Wizard meshes deform with the source armature")
    for obj in scene.objects:
        if obj.type == "MESH":
            obj.hide_render = obj not in meshes
    for mesh in meshes:
        mesh.hide_set(False)
        mesh.hide_viewport = False
    rig.hide_render = True
    rig.hide_set(False)
    rig.hide_viewport = False

    production = [] if args.optional_only else production_segments()
    optional = [] if args.production_only else OPTIONAL
    names = [s[0] for s in production] + [s[0] for s in optional]
    actions = {}
    for name in names:
        action = bpy.data.actions.get("MC_Wizard_" + name)
        if action is None:
            raise RuntimeError("Missing review action MC_Wizard_" + name)
        actions[name] = action

    def pose(name, fraction):
        action = actions[name]
        rig.animation_data.action = action
        if action.slots:
            rig.animation_data.action_slot = action.slots[0]
        begin, end = action.frame_range
        if end <= begin:
            raise RuntimeError("Action has no animated range: " + action.name)
        frame = begin + min(1.0, max(0.0, fraction)) * (end - begin)
        scene.frame_set(math.floor(frame), subframe=frame - math.floor(frame))
        bpy.context.view_layer.update()
        return frame

    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.render_aa = "8"
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.studio_light = "paint.sl"
    shading.color_type = "TEXTURE"
    shading.show_shadows = True
    shading.show_cavity = True
    shading.cavity_type = "BOTH"
    shading.show_specular_highlight = True
    shading.background_type = "WORLD"
    if scene.world is None:
        scene.world = bpy.data.worlds.new("MC_V3_PreviewWorld")
    scene.world.color = (.075, .10, .14)
    scene.render.resolution_x = scene.render.resolution_y = args.resolution
    scene.render.resolution_percentage = 100
    scene.render.fps = args.fps
    scene.render.fps_base = 1.0
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.compression = 15
    scene.render.film_transparent = False

    # Fit a fixed three-quarter camera to real deformed geometry across the
    # selected actions. This gives large gestures consistent framing throughout.
    direction = Vector((3.0, -5.2, 2.3)).normalized()
    rotation = (-direction).to_track_quat("-Z", "Y")
    right = rotation @ Vector((1, 0, 0))
    up = rotation @ Vector((0, 1, 0))
    lo = Vector((math.inf, math.inf, math.inf))
    hi = Vector((-math.inf, -math.inf, -math.inf))
    world_lo = Vector((math.inf, math.inf, math.inf))
    world_hi = Vector((-math.inf, -math.inf, -math.inf))
    for name in names:
        fractions = set(KEY_POSES[name]) | {0, .10, .30, .50, .80, 1.0}
        for fraction in sorted(fractions):
            pose(name, fraction)
            depsgraph = bpy.context.evaluated_depsgraph_get()
            for mesh in meshes:
                evaluated = mesh.evaluated_get(depsgraph)
                geometry = evaluated.to_mesh()
                try:
                    for vertex in geometry.vertices:
                        point = evaluated.matrix_world @ vertex.co
                        projected = Vector((point.dot(right), point.dot(up), point.dot(direction)))
                        for i in range(3):
                            lo[i] = min(lo[i], projected[i])
                            hi[i] = max(hi[i], projected[i])
                            world_lo[i] = min(world_lo[i], point[i])
                            world_hi[i] = max(world_hi[i], point[i])
                finally:
                    evaluated.to_mesh_clear()
    projected_center = (lo + hi) * .5
    focus = right * projected_center.x + up * projected_center.y + direction * projected_center.z
    camera_data = bpy.data.cameras.new("MC_V3_PreviewCamera")
    camera = bpy.data.objects.new("MC_V3_PreviewCamera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = focus + direction * 8
    camera.rotation_euler = rotation.to_euler()
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = max(2.75, (hi.y - lo.y) / .78, (hi.x - lo.x) / .88)
    camera_data.lens = 50
    camera_data.clip_end = 200
    scene.camera = camera
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -.005))
    floor = bpy.context.object
    floor.name = "MC_V3_PreviewFloor"
    floor.color = (.13, .16, .21, 1)
    floor_material = bpy.data.materials.new("MC_V3_PreviewFloor")
    floor_material.diffuse_color = floor.color
    floor.data.materials.append(floor_material)

    pose_dir = OUT / "PosesV3"
    pose_dir.mkdir(parents=True, exist_ok=True)
    key_records = []
    for name in names:
        for fraction in KEY_POSES[name]:
            frame = pose(name, fraction)
            output = pose_dir / ("%s_%03d.png" % (name, round(fraction * 100)))
            scene.render.filepath = str(output)
            bpy.ops.render.render(write_still=True)
            key_records.append({"action": name, "fraction": fraction,
                                "source_frame": frame, "file": str(output)})
        print("V3_KEY_POSES", name, flush=True)

    report = {
        "source": str(source), "source_sha256_before": source_hash,
        "fps": args.fps, "resolution": [args.resolution, args.resolution],
        "render_engine": "Blender Workbench / studio textures / 8 samples",
        "camera": {"location": list(camera.location), "focus": list(focus),
                   "ortho_scale": camera_data.ortho_scale,
                   "projected_bounds": [list(lo), list(hi)],
                   "world_bounds": [list(world_lo), list(world_hi)]},
        "keys": key_records, "videos": [],
        "timing": "Current native C++ defaults. FireNut .9+.9 s; Summon 1.1+1.2 s; Rain 1.2+4+1.4 s; Melee .65+.9 s; Teleport .65+.55 s. Cast contact at 55% of source action. Hit/Death are review playback durations. Saved difficulty overrides are not simulated.",
        "scope": "DCC poses only. No runtime overlays, teleport translation, game VFX, targeting, collision or network simulation. Optional performances are DCC exploration and are not additional abilities.",
    }
    report_path = OUT / "WizardStyleV3PreviewReport.json"

    def write_report():
        report["source_sha256_after"] = digest(source)
        report["source_preserved"] = report["source_sha256_after"] == source_hash
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        if not report["source_preserved"]:
            raise RuntimeError("Saved Blender source changed during a read-only preview")
        report_path.write_text(json.dumps(report, indent=2), encoding="utf8")

    write_report()
    if args.poses_only:
        print("V3_PREVIEW_POSES_COMPLETE", str(report_path), flush=True)
        return

    def render_video(entries, stem, optional_montage=False):
        frame_dir = ROOT / "Saved/NutAnimationStyle" / (stem + "Frames")
        frame_dir.mkdir(parents=True, exist_ok=True)
        segments, records = [], []
        for name, title, seconds, sampler in entries:
            start = len(records)
            count = round(seconds * args.fps)
            for index in range(count):
                t = index / args.fps
                fraction = sampler(t)
                if name in {"Hit", "Death"} or optional_montage:
                    fraction = index / max(1, count - 1)
                frame = pose(name, fraction)
                output = frame_dir / ("frame_%05d.png" % len(records))
                scene.render.filepath = str(output)
                bpy.ops.render.render(write_still=True)
                records.append({"action": name, "seconds": t,
                                "fraction": max(0.0, min(1.0, fraction)), "source_frame": frame})
            segments.append({"action": name, "label": title, "requested_seconds": seconds,
                             "start_seconds": start / args.fps,
                             "end_seconds": len(records) / args.fps,
                             "frame_count": count})
            print("V3_VIDEO_SEGMENT", stem, name, len(records), flush=True)
        font = "C\\:/Windows/Fonts/arial.ttf"
        filters = ["drawtext=fontfile='%s':text='%s':fontcolor=white:fontsize=23:box=1:boxcolor=black@0.55:boxborderw=9:x=18:y=16:enable='gte(t,%.5f)*lt(t,%.5f)'" %
                   (font, s["label"], s["start_seconds"], s["end_seconds"]) for s in segments]
        footer = "Archmage v3 / DCC performance exploration" if optional_montage else "Archmage v3 / native default timings / DCC preview"
        filters.append("drawtext=fontfile='%s':text='%s':fontcolor=white@0.85:fontsize=15:box=1:boxcolor=black@0.45:boxborderw=6:x=18:y=h-32" % (font, footer))
        video = OUT / (stem + ".mp4")
        subprocess.run(["C:/ffmpeg/ffmpeg.exe", "-hide_banner", "-loglevel", "error", "-y",
                        "-framerate", str(args.fps), "-i", str(frame_dir / "frame_%05d.png"),
                        "-vf", ",".join(filters), "-frames:v", str(len(records)),
                        "-c:v", "libx264", "-threads", "2", "-crf", "19",
                        "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(video)], check=True,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        encoded = json.loads(subprocess.check_output([
            "C:/ffmpeg/ffprobe.exe", "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=width,height,nb_frames,r_frame_rate:format=duration",
            "-of", "json", str(video)], creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)))
        stream = encoded["streams"][0]
        if int(stream["nb_frames"]) != len(records) or (stream["width"], stream["height"]) != (args.resolution, args.resolution):
            raise RuntimeError("Encoded video does not contain the complete rendered frame sequence")
        frame_manifest = frame_dir / "Frames.json"
        frame_manifest.write_text(json.dumps(records, indent=2), encoding="utf8")
        report["videos"].append({"file": str(video), "frames": len(records),
                                 "duration_seconds": len(records) / args.fps,
                                 "segments": segments, "frames_manifest": str(frame_manifest),
                                 "encoded_stream": stream,
                                 "playback": "accelerated performance montage" if optional_montage else "native default phases"})
        write_report()
        print("V3_VIDEO_COMPLETE", str(video), flush=True)

    if production:
        render_video(production, "Wizard_AllAbilities_v3")
    if optional:
        render_video([(name, title, 1.7, lambda t: t / 1.7) for name, title in optional],
                     "Wizard_Performances_v3", optional_montage=True)
    report["status"] = "complete"
    write_report()
    print("V3_PREVIEW_COMPLETE", str(report_path), flush=True)


if __name__ == "__main__":
    main()
