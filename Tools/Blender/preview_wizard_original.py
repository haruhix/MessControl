"""Render original retargeted Wizard actions without authoring or saving the source."""
from __future__ import annotations

import argparse
import importlib.util
import math
import sys
import time
from pathlib import Path
from types import SimpleNamespace

import bpy

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('C:/Users/user/Downloads/Telegram Desktop/Nut_Wizard.blend')
OUTPUT = ROOT / 'Artifacts/WizardOriginal'
ORDER = (('Firenut', 'NUT_W_Firenut'), ('NutRain', 'NUT_W_NutRain'), ('CastSummon', 'NUT_W_CastSummon'))
spec = importlib.util.spec_from_file_location('review_util', Path(__file__).with_name('preview_wizard_human_v8.py'))
util = importlib.util.module_from_spec(spec)
spec.loader.exec_module(util)


def bind_original(rig, action, defaults):
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = defaults[bone.name]['basis'].copy()
        for name, value in defaults[bone.name]['properties'].items():
            bone[name] = value
    rig.animation_data.action = action
    if action.slots:
        rig.animation_data.action_slot = action.slots[0]
    rig.update_tag()
    bpy.context.view_layer.update()


def main():
    if not bpy.app.background:
        raise RuntimeError('Run in an isolated background process')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    source, output = args.source.resolve(), args.output.resolve()
    before = util.sha256(source)
    if any(output.rglob('frame_*.png')):
        raise FileExistsError('Use a fresh preview directory')
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    source_fps, source_fps_base = scene.render.fps, scene.render.fps_base
    fps = source_fps / source_fps_base
    rig, mesh = bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    defaults = {b.name: dict(basis=b.matrix_basis.copy(),
        properties={k: b[k] for k in b.keys() if isinstance(b[k], (bool,int,float))})
        for b in rig.pose.bones}
    clips = []
    for name, action_name in ORDER:
        action = bpy.data.actions[action_name]
        first, last = math.floor(action.frame_range[0]), math.ceil(action.frame_range[1])
        clips.append(dict(name=name, action=action, action_name=action_name,
            frame_start=first, frame_end=last, frame_count=last-first+1,
            source_duration_seconds=(last-first)/fps, timeline_seconds=(last-first)/fps))
    settings = SimpleNamespace(width=640, height=480, render_aa='5',
                               focus=(-.01,-.13,.85), ortho_scale=3.1)
    cameras = util.setup_scene(scene, rig, mesh, settings)
    scene.render.fps, scene.render.fps_base = source_fps, source_fps_base
    # The original's static, unkeyed control values remain intact. The generic
    # V8 helper resets bases to identity, so use this source-preserving binder.
    util.activate = bind_original
    scale = util.fit_cameras(scene, rig, mesh, cameras, clips, defaults, settings.ortho_scale)
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    unattached = [a.name for a in bpy.data.actions if a.name.startswith('mixamo.com')]
    manifest = dict(status='running', version='Original', source=str(source),
        source_sha256=before, fps=fps, native_fps=source_fps, native_fps_base=source_fps_base,
        size=[640,480], views=list(cameras), engine='BLENDER_WORKBENCH', render_aa='5',
        playback_speed=1, ortho_scale=scale, source_saved=False,
        scope='Three original retargeted NUT_W actions. No animation or rig edits. Unattached Mixamo actions are not rendered.',
        unattached_mixamo_actions=unattached, clips=[], frames=[])
    util.write_manifest(output, manifest)
    try:
        for clip in clips:
            bind_original(rig, clip['action'], defaults)
            row = {k:v for k,v in clip.items() if k!='action'}
            row.update(rendered_frame_count=0, encoded_duration_seconds=clip['frame_count']/fps)
            manifest['clips'].append(row)
            for view in cameras:
                (output/clip['name']/view).mkdir(parents=True, exist_ok=True)
            # Start at the first source frame before advancing to avoid saved
            # frame values leaking into the first clip after bounds inspection.
            for index, frame in enumerate(range(clip['frame_start'],clip['frame_end']+1)):
                scene.frame_set(frame, subframe=0)
                for view, camera in cameras.items():
                    path = output/clip['name']/view/('frame_%04d.png'%index)
                    scene.camera, scene.render.filepath = camera, str(path)
                    bpy.ops.render.render(write_still=True)
                    if not path.is_file():
                        raise RuntimeError('Missing render: '+str(path))
                    manifest['frames'].append(dict(clip=clip['name'], view=view,
                        index=index, source_frame=frame, clip_seconds=index/fps,
                        path=str(path), sha256=util.sha256(path)))
                row['rendered_frame_count'] = index+1
            util.write_manifest(output, manifest)
            print('ORIGINAL_CLIP_READY', clip['action_name'], clip['frame_count'], flush=True)
        after = util.sha256(source)
        if after != before:
            raise RuntimeError('Original source was changed')
        manifest.update(status='complete', source_sha256_after=after, source_unchanged=True,
            output_timeline_frames=sum(c['frame_count'] for c in clips),
            rendered_png_count=len(manifest['frames']), wall_seconds=time.monotonic()-started)
    except Exception as error:
        manifest.update(status='failed',error=str(error),wall_seconds=time.monotonic()-started)
        util.write_manifest(output, manifest)
        raise
    util.write_manifest(output, manifest)
    print('WIZARD_ORIGINAL_RENDER_COMPLETE', str(output), flush=True)


if __name__ == '__main__':
    main()
