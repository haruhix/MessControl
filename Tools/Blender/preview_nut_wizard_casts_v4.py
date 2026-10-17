"""Read-only, close-framed spell-motion review on the original Wizard rig.

The raw montage intentionally contains no magic VFX: the cooperating palms,
spell shaping, and release must read from motion alone. A first individual spell
is encoded immediately, before the remaining spells render. Source is never saved.
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
FFMPEG = "C:/ffmpeg/ffmpeg.exe"
FFPROBE = "C:/ffmpeg/ffprobe.exe"


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=OUT / "NutWizard_Casts_v4.blend")
    parser.add_argument("--actions", default="Cast,Summon,Rain,HeavyCast")
    parser.add_argument("--stem", default="Wizard_Casts_v4_Raw")
    parser.add_argument("--resolution", type=int, default=640)
    parser.add_argument("--fps", type=int, default=24)
    parser.add_argument("--poses-only", action="store_true")
    parser.add_argument("--readable", action="store_true", help="Slow short casts 1.5x; Rain keeps native timing")
    parser.add_argument("--front", action="store_true", help="Frontal silhouette QA instead of the mild three-quarter view")
    parser.add_argument("--magic", action="store_true", help="Separate transient DCC magic concept after raw motion review")
    parser.add_argument("--camera-from", type=Path, help="Reuse the common framing of a previous review report for a corrected individual shot")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ordinary(windup, recovery):
    return lambda t: .55 * t / windup if t < windup else .55 + .45 * (t - windup) / recovery


def channel_rain(t):
    if t < 1.2:
        return .55 * t / 1.2
    if t < 5.2:
        return .55 + .30 * (((t - 1.2) / 1.1) % 1.0)
    return .85 + .15 * (t - 5.2) / 1.4


SPELLS = {
    "Cast": ("FIRE / shape, compress, release", 1.8, ordinary(.9, .9)),
    "Summon": ("SUMMON / trace, open, command", 2.3, ordinary(1.1, 1.2)),
    "Rain": ("RAIN / weave, channel, dismiss", 6.6, channel_rain),
    "HeavyCast": ("TELEPORT / inscribe, fold, appear", 1.2, ordinary(.65, .55)),
}
KEYS = (0.0, .10, .20, .30, .40, .48, .55, .59, .62, .72, .84, 1.0)


def encode(frame_dir, video, count, fps, filters):
    subprocess.run([FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
                    "-framerate", str(fps), "-i", str(frame_dir / "frame_%05d.png"),
                    "-frames:v", str(count), "-vf", ",".join(filters),
                    "-c:v", "libx264", "-threads", "2", "-crf", "18",
                    "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(video)],
                   check=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    probe = json.loads(subprocess.check_output([
        FFPROBE, "-v", "error", "-select_streams", "v:0", "-show_entries",
        "stream=width,height,nb_frames,r_frame_rate:format=duration", "-of", "json", str(video)],
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)))
    if int(probe["streams"][0]["nb_frames"]) != count:
        raise RuntimeError("Encoded motion frame count mismatch")
    return probe


def caption(text, font_size=19, position="top", enable=None):
    font = "C\\:/Windows/Fonts/arial.ttf"
    result = "drawtext=fontfile='%s':text='%s':fontcolor=white:fontsize=%d:box=1:boxcolor=black@0.65:boxborderw=7:x=14:y=%s" % (
        font, text, font_size, "14" if position == "top" else "h-30")
    if enable:
        result += ":enable='%s'" % enable
    return result


def magic_concept(scene, rig):
    """A disposable light-and-geometry layer; no rig, keys or saved source edit."""
    scene.render.engine = "BLENDER_EEVEE"
    if hasattr(scene.eevee, "taa_render_samples"):
        scene.eevee.taa_render_samples = 8
    if hasattr(scene.eevee, "use_raytracing"):
        scene.eevee.use_raytracing = False
    scene.world.use_nodes = True
    background = scene.world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (.025, .035, .065, 1)
    background.inputs["Strength"].default_value = .35
    for name, position, power, size, color in [
            ("key", (3, -4, 5), 450, 5, (1, .79, .57)),
            ("fill", (-3, -2, 3), 250, 4, (.35, .61, 1)),
            ("rim", (1, 3, 4), 550, 3, (.7, .75, 1))]:
        data = bpy.data.lights.new("V4Concept_" + name, "AREA")
        data.energy, data.size, data.color = power, size, color
        obj = bpy.data.objects.new(data.name, data)
        scene.collection.objects.link(obj)
        obj.location = position
        obj.rotation_euler = (Vector((0, 0, .9)) - obj.location).to_track_quat("-Z", "Y").to_euler()
    tree = bpy.data.node_groups.new("V4Concept_Compositor", "CompositorNodeTree")
    tree.interface.new_socket(name="Image", in_out="OUTPUT", socket_type="NodeSocketColor")
    scene.compositing_node_group = tree
    render = tree.nodes.new("CompositorNodeRLayers")
    glow = tree.nodes.new("CompositorNodeGlare")
    glow.inputs["Type"].default_value = "Fog Glow"
    glow.inputs["Quality"].default_value = "Medium"
    glow.inputs["Threshold"].default_value = .65
    glow.inputs["Size"].default_value = .2
    output = tree.nodes.new("NodeGroupOutput")
    tree.links.new(render.outputs["Image"], glow.inputs["Image"])
    tree.links.new(glow.outputs["Image"], output.inputs["Image"])

    material = bpy.data.materials.new("V4Concept_Magic")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    emission = nodes.new("ShaderNodeEmission")
    emission.inputs["Color"].default_value = (1, .25, .025, 1)
    emission.inputs["Strength"].default_value = 5
    mat_output = nodes.new("ShaderNodeOutputMaterial")
    material.node_tree.links.new(emission.outputs["Emission"], mat_output.inputs["Surface"])
    objects = []
    for index in range(18):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=6, radius=1)
        obj = bpy.context.object
        obj.name = "V4Concept_Spark_%02d" % index
        obj.data.materials.append(material)
        objects.append(obj)
    rings = []
    for index in range(4):
        bpy.ops.mesh.primitive_torus_add(major_radius=1, minor_radius=.012,
                                        major_segments=48, minor_segments=6)
        obj = bpy.context.object
        obj.name = "V4Concept_Sigil_%02d" % index
        obj.data.materials.append(material)
        rings.append(obj)
    light_data = bpy.data.lights.new("V4Concept_SpellLight", "POINT")
    light_data.energy, light_data.shadow_soft_size = 35, .4
    light = bpy.data.objects.new(light_data.name, light_data)
    scene.collection.objects.link(light)

    colors = {"Cast": (1, .19, .012), "Summon": (.72, .055, 1),
              "Rain": (.09, .46, 1), "HeavyCast": (.08, .92, .8)}

    def show(obj, location, scale, rotation=None):
        obj.hide_render = False
        obj.location = location
        obj.scale = (scale,) * 3 if isinstance(scale, (float, int)) else scale
        if rotation is not None:
            obj.rotation_euler = rotation

    def update(name, fraction, seconds):
        color = colors[name]
        emission.inputs["Color"].default_value = (*color, 1)
        light_data.color = color
        for obj in objects + rings:
            obj.hide_render = True
        right = rig.matrix_world @ rig.pose.bones["hand.r"].head
        left = rig.matrix_world @ rig.pose.bones["hand.l"].head
        center = (right + left) * .5
        light.location = center
        active = .06 < fraction < .91
        light_data.energy = 45 if active else 0
        if not active:
            return
        if name == "Cast":
            growth = min(1, fraction / .42)
            radius = (.055 + .075 * growth) * min(1, max(0, (1 - fraction) / .2))
            if fraction > .55:
                center = right + Vector((0, -2.1 * min(1, (fraction - .55) / .23), .05))
            show(objects[0], center, radius)
            for index, ring in enumerate(rings[:2]):
                show(ring, center, radius * 1.35, (seconds * 2.3 + index, seconds * 1.7, index * 1.5))
            for index, obj in enumerate(objects[1:]):
                angle = index * 2.399 + seconds * 3
                trail = Vector((math.cos(angle) * radius * 1.65, math.sin(angle) * radius * 1.65,
                                math.sin(angle * 1.7) * radius))
                if fraction > .55:
                    trail += Vector((0, index * .021, 0))
                show(obj, center + trail, radius * .095)
        elif name == "Summon":
            radius = .25 + .6 * min(1, fraction / .55)
            fade = min(1, max(0, (1 - fraction) / .2))
            for index, ring in enumerate(rings[:3]):
                show(ring, (0, -.18, .018 + index * .018), radius * (1 - index * .15) * fade,
                     (0, 0, seconds * (-1 if index % 2 else 1)))
            for index, obj in enumerate(objects):
                angle = index * math.tau / len(objects) + seconds * .9
                height = .03 + .35 * max(0, fraction - .30) * (1 + math.sin(angle * 3))
                show(obj, (math.cos(angle) * radius, -.18 + math.sin(angle) * radius, height), .023 * fade)
        elif name == "Rain":
            radius = .48 * min(1, fraction / .5)
            fade = min(1, max(0, (1 - fraction) / .15))
            for index, ring in enumerate(rings[:2]):
                show(ring, (0, -.18, 1.84 + index * .06), radius * (1 + index * .17) * fade,
                     (.10 * math.sin(seconds), .1 * math.cos(seconds), seconds * 1.7 * (-1 if index else 1)))
            if fraction >= .52:
                for index, obj in enumerate(objects):
                    angle = index * 2.399
                    travel = (seconds * 1.1 + index * .071) % 1
                    show(obj, (math.cos(angle) * .67, -.30 + math.sin(angle) * .48, 1.80 - travel * 1.55),
                         (.009 * fade, .009 * fade, .060 * fade))
        else:
            fold = 1 - min(1, abs(fraction - .55) / .55)
            radius = .62 - .27 * fold
            for index, ring in enumerate(rings[:3]):
                show(ring, (0, .0, .3 + index * .5), radius, (.12, .12, seconds * (2.4 + index * .4)))
            for index, obj in enumerate(objects):
                angle = index * 2.399 + seconds * 4
                show(obj, (math.cos(angle) * radius, math.sin(angle) * radius, .18 + index * .084), .021)

    return update


def main():
    args = arguments()
    if args.magic and args.stem == "Wizard_Casts_v4_Raw":
        args.stem = "Wizard_Casts_v4_MagicConcept"
    names = [name.strip() for name in args.actions.split(",") if name.strip()]
    if not names or set(names) - set(SPELLS):
        raise ValueError("Choose Cast, Summon, Rain, HeavyCast")
    source = args.source.resolve()
    before = digest(source)
    started = time.monotonic()
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    rig = bpy.data.objects["rig"]
    rig.animation_data_create()
    rig.animation_data.use_nla = False
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    meshes = [obj for obj in scene.objects if obj.type == "MESH" and any(
        modifier.type == "ARMATURE" and modifier.object == rig for modifier in obj.modifiers)]
    if not meshes:
        raise RuntimeError("Missing Wizard deforming mesh")
    for obj in scene.objects:
        if obj.type == "MESH":
            obj.hide_render = obj not in meshes
    for mesh in meshes:
        mesh.hide_set(False)
        mesh.hide_viewport = False
    rig.hide_set(False)
    rig.hide_viewport = False
    rig.hide_render = True
    actions = {name: bpy.data.actions["MC_Wizard_" + name] for name in names}
    base_camera_location = None
    base_ortho_scale = None

    def pose(name, fraction):
        action = actions[name]
        rig.animation_data.action = action
        if action.slots:
            rig.animation_data.action_slot = action.slots[0]
        begin, end = action.frame_range
        frame = begin + max(0, min(1, fraction)) * (end - begin)
        scene.frame_set(math.floor(frame), subframe=frame - math.floor(frame))
        bpy.context.view_layer.update()
        if base_camera_location is not None:
            camera.location = base_camera_location + Vector((0, 0, .12 if args.magic and name == "Rain" else 0))
            data.ortho_scale = base_ortho_scale * (1.13 if args.magic and name == "Rain" else 1)
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
    if not scene.world:
        scene.world = bpy.data.worlds.new("MC_V4_RawReviewWorld")
    scene.world.color = (.055, .068, .085)
    scene.render.resolution_x = scene.render.resolution_y = args.resolution
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.compression = 10
    scene.render.fps = args.fps
    scene.render.fps_base = 1
    scene.render.film_transparent = False

    # A mostly frontal, low elevation camera keeps the opposing palms and curved
    # paths visible. Fit real skinned vertices; include the finger silhouettes.
    direction = Vector((0, -5.5, 1.15) if args.front else (1.5, -5.5, 1.35)).normalized()
    rotation = (-direction).to_track_quat("-Z", "Y")
    right, up = rotation @ Vector((1, 0, 0)), rotation @ Vector((0, 1, 0))
    lo, hi = Vector((math.inf,) * 3), Vector((-math.inf,) * 3)
    for name in names:
        for fraction in KEYS:
            pose(name, fraction)
            deps = bpy.context.evaluated_depsgraph_get()
            for mesh in meshes:
                obj = mesh.evaluated_get(deps)
                geo = obj.to_mesh()
                try:
                    for vertex in geo.vertices:
                        point = obj.matrix_world @ vertex.co
                        projected = (point.dot(right), point.dot(up), point.dot(direction))
                        for i, value in enumerate(projected):
                            lo[i] = min(lo[i], value)
                            hi[i] = max(hi[i], value)
                finally:
                    obj.to_mesh_clear()
    center = (lo + hi) * .5
    focus = right * center.x + up * center.y + direction * center.z
    data = bpy.data.cameras.new("MC_V4_RawReviewCamera")
    camera = bpy.data.objects.new("MC_V4_RawReviewCamera", data)
    scene.collection.objects.link(camera)
    camera.location = focus + direction * 8
    camera.rotation_euler = rotation.to_euler()
    data.type = "ORTHO"
    data.ortho_scale = max((hi.y - lo.y) / .79, (hi.x - lo.x) / .86)
    if args.camera_from:
        previous = json.loads(args.camera_from.read_text(encoding="utf8"))
        if previous["source_sha256_before"] != before:
            raise RuntimeError("Camera reference belongs to a different source revision")
        camera.location = previous["camera"]["location"]
        data.ortho_scale = previous["camera"]["ortho_scale"]
        focus = Vector(previous["camera"]["focus"])
    base_camera_location = camera.location.copy()
    base_ortho_scale = data.ortho_scale
    data.clip_end = 200
    scene.camera = camera
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -.005))
    floor = bpy.context.object
    floor.name = "MC_V4_RawReviewFloor"
    floor.color = (.095, .11, .135, 1)
    mat = bpy.data.materials.new("MC_V4_RawReviewFloor")
    mat.diffuse_color = floor.color
    floor.data.materials.append(mat)
    if args.magic:
        mat.use_nodes = True
        shader = mat.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Base Color"].default_value = (.018, .024, .035, 1)
        shader.inputs["Roughness"].default_value = 1
    update_magic = magic_concept(scene, rig) if args.magic else None

    output = OUT / "PosesCastsV4" / args.stem
    output.mkdir(parents=True, exist_ok=True)
    report = {"source": str(source), "source_sha256_before": before,
              "scope": "Transient DCC magic concept, separate from game integration. Source is not saved." if args.magic else "Plain DCC spell motion, without game overlays, VFX, teleport displacement or network simulation. Source is not saved.",
              "camera": {"location": list(camera.location), "focus": list(focus), "ortho_scale": data.ortho_scale},
              "fps": args.fps, "resolution": args.resolution,
              "timing": "Short casts at 1.5x duration; native Rain" if args.readable else "Native default timings", "keys": [], "videos": []}
    report_path = OUT / (args.stem + "Report.json")

    def save_report(status):
        report["source_sha256_after"] = digest(source)
        report["source_preserved"] = report["source_sha256_after"] == before
        if not report["source_preserved"]:
            raise RuntimeError("Preview changed saved source")
        report["status"] = status
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        report_path.write_text(json.dumps(report, indent=2), encoding="utf8")

    if args.poses_only:
        for name in names:
            for fraction in KEYS:
                frame = pose(name, fraction)
                if update_magic:
                    update_magic(name, fraction, fraction * SPELLS[name][1])
                file = output / ("%s_%03d.png" % (name, round(fraction * 100)))
                scene.render.filepath = str(file)
                bpy.ops.render.render(write_still=True)
                report["keys"].append({"action": name, "fraction": fraction, "frame": frame, "file": str(file)})
            print("V4_RAW_POSES_READY", name, flush=True)
        save_report("poses_complete")
        return

    all_dir = ROOT / "Saved/NutAnimationStyle" / (args.stem + "Frames")
    all_dir.mkdir(parents=True, exist_ok=True)
    records, segments = [], []
    for name in names:
        title, native_seconds, sampler = SPELLS[name]
        slower = 1.5 if args.readable and name != "Rain" else 1.0
        intro, outro = .35, .45
        count = round((native_seconds * slower + intro + outro) * args.fps)
        segment_start = len(records)
        segment_dir = ROOT / "Saved/NutAnimationStyle" / (args.stem + "_" + name + "Frames")
        segment_dir.mkdir(parents=True, exist_ok=True)
        for index in range(count):
            seconds = max(0, min(native_seconds, (index / args.fps - intro) / slower))
            fraction = sampler(seconds)
            frame = pose(name, fraction)
            if update_magic:
                update_magic(name, fraction, seconds)
            file = segment_dir / ("frame_%05d.png" % index)
            scene.render.filepath = str(file)
            bpy.ops.render.render(write_still=True)
            # Store the same image twice without re-rendering: early individual
            # spell evidence and the complete montage use identical motion.
            (all_dir / ("frame_%05d.png" % len(records))).write_bytes(file.read_bytes())
            records.append({"action": name, "playback_seconds": index / args.fps,
                            "native_seconds": seconds, "fraction": max(0, min(1, fraction)), "source_frame": frame})
        video = OUT / (args.stem + "_" + name + ".mp4")
        footer = "DCC magic concept / not game VFX" if args.magic else "Raw gestures / 1.5x slower / DCC review" if slower > 1 else "Raw gestures / native timing / DCC review"
        encoded = encode(segment_dir, video, count, args.fps, [caption(title), caption(footer, 14, "bottom")])
        segments.append({"action": name, "label": title, "start_seconds": segment_start / args.fps,
                         "end_seconds": len(records) / args.fps, "frames": count, "native_duration": native_seconds,
                         "duration_multiplier": slower,
                         "camera": {"location": list(camera.location), "ortho_scale": data.ortho_scale}})
        report["videos"].append({"file": str(video), "frames": count, "duration_seconds": count / args.fps, "encoded": encoded})
        save_report("rendering")
        print("V4_RAW_SPELL_VIDEO_READY", name, str(video), flush=True)
    if len(names) > 1:
        filters = [caption(s["label"], enable="gte(t,%.5f)*lt(t,%.5f)" % (s["start_seconds"], s["end_seconds"])) for s in segments]
        filters.append(caption("DCC magic concept / not game VFX" if args.magic else "Raw gestures / no VFX / DCC review", 14, "bottom"))
        video = OUT / (args.stem + ".mp4")
        encoded = encode(all_dir, video, len(records), args.fps, filters)
        report["videos"].append({"file": str(video), "frames": len(records), "duration_seconds": len(records) / args.fps,
                                 "segments": segments, "encoded": encoded})
    (all_dir / "Frames.json").write_text(json.dumps(records, indent=2), encoding="utf8")
    report["segments"] = segments
    save_report("complete")
    print("V4_RAW_REVIEW_COMPLETE", str(report_path), flush=True)


if __name__ == "__main__":
    main()
