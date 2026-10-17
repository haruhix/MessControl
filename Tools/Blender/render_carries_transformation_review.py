"""Render a saved transformation scene timeline in isolated background Blender.

Evaluate the complete scene, including shape keys, object transforms and bones.
Never bind/replace Actions, alter NLA, or save source art. At authored 30 FPS,
each output PNG evaluates frame 1 + index*0.5 for a real 60 FPS review.
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

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'Artifacts/CarriesTransformation/Review'
CAMERAS = {'ThreeQuarter': 'MC_Transform_ThreeQuarter'}


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def resolve(directory, value):
    path = Path(value)
    return (path if path.is_absolute() else directory / path).resolve()


def write_manifest(path, value):
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf8')
    temporary.replace(path)


def positive_events(value, end):
    if isinstance(value, dict):
        return {frame for child in value.values() for frame in positive_events(child, end)}
    if isinstance(value, (list, tuple)):
        return {frame for child in value for frame in positive_events(child, end)}
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        frame = float(value)
        if math.isfinite(frame) and 0 < frame <= end:
            return {frame}
    return set()


def main():
    if not bpy.app.background:
        raise RuntimeError('Use an isolated background Blender process')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=OUTPUT)
    parser.add_argument('--view', choices=tuple(CAMERAS), default='ThreeQuarter')
    parser.add_argument('--draft', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    source, report_path, output = args.source.resolve(), args.report.resolve(), args.output_dir.resolve()
    raw = report_path.read_bytes()
    report, report_hash = json.loads(raw.decode('utf8')), hashlib.sha256(raw).hexdigest()
    if (report.get('passed') is not True or resolve(report_path.parent, report['blend']) != source
            or report.get('fps') != 30):
        raise ValueError('Expected a passed 30 FPS author report for the requested blend')
    initial_hashes = {source: str(report['blend_sha256']).lower()}
    for record in report['sources']:
        path, digest = resolve(report_path.parent, record['path']), str(record['sha256']).lower()
        if path in initial_hashes and initial_hashes[path] != digest:
            raise ValueError('Conflicting source SHA: ' + str(path))
        initial_hashes[path] = digest
    if not report['sources']:
        raise ValueError('Source art provenance is required')
    for path, digest in initial_hashes.items():
        if sha256(path) != digest:
            raise ValueError('Source SHA differs from author report: ' + str(path))
    sources = [dict(path=str(path), sha256=digest) for path, digest in initial_hashes.items()]
    rows = report['clips']
    if len(rows) != 1 or rows[0].get('action') != 'SCENE_TIMELINE':
        raise ValueError('Expected one complete SCENE_TIMELINE transformation clip')
    row = rows[0]
    name, authored_frames = row['name'], row['frames']
    if (not isinstance(name, str) or not name or name in ('.', '..')
            or any(char in name for char in '/\\:<>"|?*') or name[-1] in '. '
            or not isinstance(authored_frames, int) or isinstance(authored_frames, bool)
            or authored_frames <= 0):
        raise ValueError('Invalid scene timeline name/frame count')
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    if abs(scene.render.fps / scene.render.fps_base - 30.) > 1e-6:
        raise ValueError('Saved scene must use 30 FPS for half-frame evaluation')
    camera = bpy.data.objects.get(CAMERAS[args.view])
    if camera is None or camera.type != 'CAMERA':
        raise ValueError('Saved transformation camera is missing: ' + CAMERAS[args.view])
    scene.camera = camera
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.render_aa = '5'
    scene.render.resolution_x, scene.render.resolution_y = 800, 600
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.compression = 10
    clip_name = name + '_' + args.view
    folder = output / ('Draft_' + args.view) if args.draft else output / clip_name
    if not folder.resolve().is_relative_to(output):
        raise ValueError('Render folder leaves the output directory')
    if not args.draft and any(folder.glob('*.png')):
        raise ValueError('Fresh render folder is required: ' + str(folder))
    folder.mkdir(parents=True, exist_ok=True)
    count = authored_frames*2
    clip = dict(name=clip_name, motion=name, action='SCENE_TIMELINE', ordinal=1,
                label=row['labelRU'], note=row['useRU'], view=args.view,
                frame_count=count, frame_start=0, frame_end=count-1,
                timeline_seconds=(count-1)/60, duration_seconds=count/60,
                authored_frames=authored_frames, event_frames=row.get('event_frames', {}))
    manifest_path = output / ('DraftManifest_' + args.view + '.json' if args.draft else 'RenderManifest.json')
    manifest = dict(status='running', fps=60, size=[800, 600], source_fps=30,
                    source_animation_resampled=True, evaluation_subframes=True, playback_speed=1,
                    scene_timeline_evaluated=True, sources=sources,
                    author_report=str(report_path), author_report_sha256=report_hash,
                    view=args.view, camera=camera.name, camera_type=camera.data.type,
                    ortho_scale=camera.data.ortho_scale, engine='BLENDER_WORKBENCH', render_aa='5',
                    scope=report.get('scope', 'Blender-only phase 1 to 2 to 3 scene transformation study'),
                    limitations=report.get('limitations', []), clips=[clip], frames=[], draft=args.draft)
    if args.draft:
        sample_frames = sorted(positive_events(row.get('event_frames', {}), authored_frames)
                               | {1., float(authored_frames)})
        clip['draft_sample_count'] = len(sample_frames)
        evaluations = enumerate(sample_frames)
    else:
        evaluations = ((index, 1+index*.5) for index in range(count))
    started = time.monotonic()
    write_manifest(manifest_path, manifest)
    try:
        for index, frame in evaluations:
            # This evaluates all scene animation together; no per-rig rebind.
            scene.frame_set(math.floor(frame), subframe=frame-math.floor(frame))
            if args.draft:
                token = ('%07.3f' % frame).rstrip('0').rstrip('.').replace('.', 'p')
                path = folder / (name + '_frame' + token + '.png')
            else:
                path = folder / ('frame_%04d.png' % index)
            scene.render.filepath = str(path)
            bpy.ops.render.render(write_still=True)
            manifest['frames'].append(dict(clip=clip_name, index=index, source_frame=index,
                                           blender_frame=frame, view=args.view,
                                           path=str(path), sha256=sha256(path)))
            if args.draft or (index+1) % 60 == 0:
                write_manifest(manifest_path, manifest)
                print('CARRIES_TRANSFORMATION_RENDER_PROGRESS', index+1,
                      clip.get('draft_sample_count', count), flush=True)
        # One final check for each source file; source art is never saved.
        for path, digest in initial_hashes.items():
            if sha256(path) != digest:
                raise ValueError('Source changed during scene rendering: ' + str(path))
        if sha256(report_path) != report_hash:
            raise ValueError('Author report changed during rendering')
        manifest.update(status='complete', source_files_unchanged=True,
                        rendered_png_count=len(manifest['frames']), wall_seconds=time.monotonic()-started)
    except Exception as error:
        manifest.update(status='failed', error=str(error), wall_seconds=time.monotonic()-started)
        raise
    finally:
        write_manifest(manifest_path, manifest)
    print('CARRIES_TRANSFORMATION_RENDER_COMPLETE', str(manifest_path), flush=True)


if __name__ == '__main__':
    main()
