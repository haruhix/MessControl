"""Render the saved nine-action V8 Wizard in an isolated Blender CLI process.

Example (run only when the source is ready):
  blender --background --factory-startup --python preview_wizard_human_v8.py

Optional arguments follow Blender's -- separator. This script never saves a
blend file and refuses to operate in a live, foreground Blender session.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import time
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/Wizard/NutWizard_Human_v8.blend'
OUTPUT = ROOT / 'Artifacts/WizardHumanV8'
FPS = 30
CLIPS = ('Idle', 'Walk', 'Melee', 'Cast', 'HeavyCast', 'Summon', 'Rain', 'Hit', 'Death')
VIEWS = {'ThreeQuarter': (3., -7., 1.45), 'Side': (-7., -.25, 1.2)}


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def write_manifest(output, manifest):
    temporary = output / 'RenderManifest.json.tmp'
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf8')
    temporary.replace(output / 'RenderManifest.json')


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--output', type=Path, default=OUTPUT)
    parser.add_argument('--action-prefix', default='MC_WizardV8_')
    parser.add_argument('--version', default='HumanV8')
    parser.add_argument('--clips', nargs='+', choices=CLIPS, default=list(CLIPS))
    parser.add_argument('--width', type=int, default=640)
    parser.add_argument('--height', type=int, default=480)
    parser.add_argument('--ortho-scale', type=float, default=3.1)
    parser.add_argument('--focus', type=float, nargs=3, default=(-.01, -.13, .85))
    parser.add_argument('--render-aa', choices=('0', '5', '8'), default='5')
    parser.add_argument('--fixed-framing', action='store_true',
                        help='Keep exactly the requested ortho scale; default enlarges both cameras together if a pose needs more room.')
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    return parser.parse_args(args)


def setup_scene(scene, rig, mesh, args):
    rig.animation_data_create()
    rig.animation_data.action = None
    rig.animation_data.use_nla = False
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    for obj in scene.objects:
        if obj.type == 'MESH' and obj != mesh:
            obj.hide_render = True
    mesh.hide_render = False
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.render_aa = args.render_aa
    shading = scene.display.shading
    shading.light = 'STUDIO'
    shading.studio_light = 'paint.sl'
    shading.color_type = 'TEXTURE'
    shading.show_shadows = True
    shading.show_cavity = True
    shading.cavity_type = 'BOTH'
    shading.show_specular_highlight = True
    shading.background_type = 'WORLD'
    if scene.world is None:
        scene.world = bpy.data.worlds.new('MC_ReviewWorld')
    scene.world.color = (.075, .085, .10)
    scene.view_settings.view_transform = 'Standard'
    scene.render.resolution_x = args.width
    scene.render.resolution_y = args.height
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.compression = 10
    scene.render.film_transparent = False
    scene.render.use_file_extension = True
    scene.render.fps = FPS
    scene.render.fps_base = 1.
    # A new floor belongs only to this background process, never to the source.
    active = bpy.context.view_layer.objects.active
    if active is not None and active.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.mesh.primitive_plane_add(size=12., location=(0, 0, -.008))
    floor = bpy.context.object
    floor.name = 'MC_ReviewFloor'
    floor.hide_render = False
    floor.color = (.38, .41, .45, 1.)
    material = bpy.data.materials.new('MC_ReviewFloorMaterial')
    material.diffuse_color = floor.color
    material.roughness = 1.
    floor.data.materials.append(material)
    focus = Vector(args.focus)
    cameras = {}
    for view, offset in VIEWS.items():
        data = bpy.data.cameras.new('MC_ReviewCamera_' + view)
        camera = bpy.data.objects.new(data.name, data)
        scene.collection.objects.link(camera)
        data.type = 'ORTHO'
        data.ortho_scale = args.ortho_scale
        data.clip_start = .01
        data.clip_end = 100.
        camera.location = focus + Vector(offset)
        camera.rotation_euler = (focus - camera.location).to_track_quat('-Z', 'Y').to_euler()
        cameras[view] = camera
    bpy.context.view_layer.update()
    return cameras


def activate(rig, action, defaults):
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis.identity()
        for name, value in defaults.get(bone.name, {}).items():
            bone[name] = value
    rig.animation_data.action = action
    if len(action.slots):
        rig.animation_data.action_slot = action.slots[0]
    bpy.context.view_layer.update()


def fit_cameras(scene, rig, mesh, cameras, clips, defaults, minimum):
    """Keep one fixed scale across all clips and both views, including extremes.

    Evaluated object bounds include the skinned pose. Conservative bounds and
    a 12% margin leave room for high palms, fingers and stride extremes.
    """
    aspect = scene.render.resolution_x / scene.render.resolution_y
    inverses = [camera.matrix_world.inverted() for camera in cameras.values()]
    scale = minimum
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for clip in clips:
        activate(rig, clip['action'], defaults)
        for frame in range(clip['frame_start'], clip['frame_end'] + 1):
            scene.frame_set(frame, subframe=0.)
            evaluated = mesh.evaluated_get(depsgraph)
            for corner in evaluated.bound_box:
                world = evaluated.matrix_world @ Vector(corner)
                for inverse in inverses:
                    local = inverse @ world
                    # Landscape AUTO sensor fit uses ortho_scale as width.
                    scale = max(scale, 2 * abs(local.x) / .88,
                                2 * abs(local.y) * aspect / .88)
    for camera in cameras.values():
        camera.data.ortho_scale = scale
    return scale


def main():
    if not bpy.app.background:
        raise RuntimeError('Use an isolated Blender --background process; live scenes must remain untouched')
    args = arguments()
    source, output = args.source.resolve(), args.output.resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    if args.width < 1 or args.height < 1 or args.width < args.height:
        raise ValueError('Expected a positive landscape render size')
    if len(set(args.clips)) != len(args.clips):
        raise ValueError('Clip list contains duplicates')
    # Refuse mixed-source reviews; use a fresh output folder for a new revision.
    for clip in args.clips:
        for view in VIEWS:
            if any((output / clip / view).glob('*.png')):
                raise FileExistsError('Existing preview frames: ' + str(output / clip / view))
    before = sha256(source)
    output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    rig, mesh = bpy.data.objects.get('rig'), bpy.data.objects.get('SM_NutWizard')
    if rig is None or mesh is None or rig.type != 'ARMATURE' or mesh.type != 'MESH':
        raise RuntimeError('Expected the saved rig and SM_NutWizard mesh')
    clips = []
    for name in args.clips:
        action_name = args.action_prefix + name
        action = bpy.data.actions.get(action_name)
        if action is None:
            raise RuntimeError('Missing saved action: ' + action_name)
        first = int(action.get('frame_start', 1))
        last = int(math.ceil(float(action.frame_range[1]) - 1e-6))
        if first != 1 or last < first or 'duration_seconds' not in action:
            raise RuntimeError('Invalid timeline/duration metadata: ' + action_name)
        clips.append(dict(name=name, action=action, action_name=action_name,
                          frame_start=first, frame_end=last,
                          source_duration_seconds=float(action['duration_seconds']),
                          timeline_seconds=(last - first) / FPS,
                          frame_count=last - first + 1))
    defaults = {bone.name: {key: bone[key] for key in bone.keys()
                           if isinstance(bone[key], (bool, int, float))}
                for bone in rig.pose.bones}
    cameras = setup_scene(scene, rig, mesh, args)
    started = time.monotonic()
    manifest = dict(status='running', version=args.version, action_prefix=args.action_prefix, source=str(source),
                    source_sha256=before, fps=FPS, size=[args.width, args.height],
                    views=list(VIEWS), engine='BLENDER_WORKBENCH', render_aa=args.render_aa, playback_speed=1,
                    scope='Saved Blender source timelines; preview before game integration',
                    focus=list(args.focus), requested_ortho_scale=args.ortho_scale,
                    clips=[], frames=[], source_saved=False)
    write_manifest(output, manifest)
    try:
        scale = args.ortho_scale if args.fixed_framing else fit_cameras(
            scene, rig, mesh, cameras, clips, defaults, args.ortho_scale)
        manifest['ortho_scale'] = scale
        for clip in clips:
            activate(rig, clip['action'], defaults)
            row = {key: value for key, value in clip.items() if key != 'action'}
            row.update(rendered_frame_count=0, encoded_duration_seconds=clip['frame_count'] / FPS)
            manifest['clips'].append(row)
            for view in VIEWS:
                (output / clip['name'] / view).mkdir(parents=True, exist_ok=True)
            for index, frame in enumerate(range(clip['frame_start'], clip['frame_end'] + 1)):
                scene.frame_set(frame, subframe=0.)
                for view, camera in cameras.items():
                    path = output / clip['name'] / view / ('frame_%04d.png' % index)
                    scene.camera = camera
                    scene.render.filepath = str(path)
                    bpy.ops.render.render(write_still=True)
                    if not path.is_file():
                        raise RuntimeError('Missing rendered PNG: ' + str(path))
                    manifest['frames'].append(dict(clip=clip['name'], view=view,
                        index=index, source_frame=frame, clip_seconds=index / FPS,
                        path=str(path), sha256=sha256(path)))
                row['rendered_frame_count'] = index + 1
            write_manifest(output, manifest)
            print('WIZARD_PREVIEW_CLIP_READY', args.version, clip['name'], clip['frame_count'], flush=True)
        after = sha256(source)
        if after != before:
            raise RuntimeError('Source blend changed during the isolated preview')
        manifest.update(status='complete', source_sha256_after=after,
                        source_unchanged=True, output_timeline_frames=sum(c['frame_count'] for c in clips),
                        rendered_png_count=len(manifest['frames']), wall_seconds=time.monotonic() - started)
    except Exception as error:
        manifest.update(status='failed', error=str(error), wall_seconds=time.monotonic() - started)
        write_manifest(output, manifest)
        raise
    write_manifest(output, manifest)
    print('WIZARD_PREVIEW_RENDER_COMPLETE', args.version, str(output), flush=True)


if __name__ == '__main__':
    main()
