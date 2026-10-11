"""Render one Stage 2 review camera in isolated background Blender; never save art.

Reads a passed AuthorReport, binds saved Actions on BoneBrushRig using the
existing pose/default helpers, and evaluates actual half-frame poses at 60 FPS.
Run each camera separately; the resulting clip ordinals interleave both views.
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
sys.path.insert(0, str(Path(__file__).parent))
import carries_animation_helpers as pool

SOURCE = ROOT / 'ArtSource/CarriesStage2/CombatPool_v1/Carries_Stage2_CombatPool_v1.blend'
REPORT = SOURCE.parent / 'Report_v1.json'
OUTPUT = ROOT / 'Artifacts/CarriesStage2/CombatPool_v1'
CAMERAS = {'ThreeQuarter': 'MC_Carries_ThreeQuarter', 'Front': 'MC_Carries_Front', 'Side': 'MC_Carries_Side'}


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


def source_provenance(source, report_path, report):
    if report.get('passed') is not True or resolve(report_path.parent, report['blend']) != source:
        raise ValueError('Expected a passed report for the requested review blend')
    artist = report['artist_motion_source']
    artist_path = artist['path'] if isinstance(artist, dict) else artist
    artist_sha = report.get('artist_motion_source_sha256')
    if artist_sha is None and isinstance(artist, dict):
        artist_sha = artist.get('sha256')
    records = [dict(path=str(source), sha256=report['blend_sha256']),
               dict(path=str(resolve(report_path.parent, report['source'])), sha256=report['source_sha256'])]
    if not report.get('artist_motion_reference_only'):
        records.append(dict(path=str(resolve(report_path.parent, artist_path)), sha256=artist_sha))
    seen = {}
    for record in records:
        path, expected = Path(record['path']), str(record['sha256']).lower()
        if path in seen and seen[path] != expected:
            raise ValueError('Conflicting source provenance: ' + str(path))
        if path not in seen and sha256(path) != expected:
            raise ValueError('Source SHA differs from author report: ' + str(path))
        seen[path] = expected
        record['sha256'] = expected
    return records, seen


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
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--report', type=Path, default=REPORT)
    parser.add_argument('--output-dir', type=Path, default=OUTPUT)
    parser.add_argument('--view', choices=tuple(CAMERAS), required=True)
    parser.add_argument('--draft', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    source, report_path, output = args.source.resolve(), args.report.resolve(), args.output_dir.resolve()
    report = json.loads(report_path.read_text(encoding='utf8'))
    report_hash = sha256(report_path)
    sources, initial_hashes = source_provenance(source, report_path, report)
    rows = report['clips']
    names = [row['name'] for row in rows]
    if not rows or len(names) != len(set(names)):
        raise ValueError('Expected uniquely named authored clips')
    for row in rows:
        name, count = row['name'], row['frames']
        if (not isinstance(name, str) or not name or name in ('.', '..')
                or any(char in name for char in '/\\:<>"|?*')
                or not isinstance(count, int) or isinstance(count, bool) or count <= 0):
            raise ValueError('Invalid authored clip name/frame count')
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene, rig = bpy.context.scene, bpy.data.objects.get('BoneBrushRig')
    if rig is None or rig.type != 'ARMATURE':
        raise ValueError('Expected the source BoneBrushRig armature')
    rig.animation_data_create()
    stored_defaults = pool.defaults(rig)
    camera = bpy.data.objects.get(CAMERAS[args.view])
    if camera is None or camera.type != 'CAMERA':
        raise ValueError('Review camera is missing: ' + CAMERAS[args.view])
    scene.camera = camera
    if abs(scene.render.fps / scene.render.fps_base - 30.) > 1e-6:
        raise ValueError('Authored scene must use 30 FPS for half-frame 60 FPS evaluation')
    for row in rows:
        if bpy.data.actions.get(row['action']) is None:
            raise ValueError('Saved authored Action is missing: ' + row['action'])
    # Optional facial keys live on separate Key datablocks. Reset every target
    # between clips so a roar cannot leak into the next idle or stagger.
    shape_defaults = {}
    for row in rows:
        for object_name, action_name in row.get('shape_actions', {}).items():
            obj = bpy.data.objects.get(object_name)
            keys = getattr(getattr(obj, 'data', None), 'shape_keys', None)
            if keys is None or bpy.data.actions.get(action_name) is None:
                raise ValueError('Missing facial key target/action: ' + object_name + '/' + action_name)
            if object_name not in shape_defaults:
                shape_defaults[object_name] = (keys, {key.name: key.value for key in keys.key_blocks})
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.render_aa = '5'
    scene.render.resolution_x, scene.render.resolution_y = 800, 600
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.compression = 10
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / (('DraftManifest_' if args.draft else 'RenderManifest_') + args.view + '.json')
    manifest = dict(status='running', fps=60, size=[800, 600], source_fps=30,
                    source_animation_resampled=True, evaluation_subframes=True, playback_speed=1,
                    sources=sources, author_report=str(report_path), author_report_sha256=report_hash,
                    view=args.view, camera=camera.name, camera_type=camera.data.type,
                    ortho_scale=camera.data.ortho_scale, engine='BLENDER_WORKBENCH', render_aa='5',
                    scope=report.get('scope', 'Blender-only Stage 2 plasticity studies on the source custom rig'),
                    limitations=report.get('limitations', []), clips=[], frames=[], draft=args.draft)
    started = time.monotonic()
    write_manifest(manifest_path, manifest)
    try:
        for index, row in enumerate(rows):
            name = row['name'] + '_' + args.view
            ordinal = index*2 + (1 if args.view == 'ThreeQuarter' else 2)
            folder = output / ('Draft_' + args.view) if args.draft else output / name
            if not folder.resolve().is_relative_to(output):
                raise ValueError('Render folder leaves the output directory')
            if not args.draft and any(folder.glob('*.png')):
                raise ValueError('Fresh render folder is required: ' + str(folder))
            folder.mkdir(parents=True, exist_ok=True)
            pool.author.bind(rig, bpy.data.actions[row['action']], stored_defaults)
            for object_name, (keys, values) in shape_defaults.items():
                animation = keys.animation_data_create()
                animation.action = None
                animation.use_nla = False
                for track in animation.nla_tracks:
                    track.mute = True
                for key_name, value in values.items():
                    keys.key_blocks[key_name].value = value
                action_name = row.get('shape_actions', {}).get(object_name)
                animation.action = bpy.data.actions[action_name] if action_name else None
                if animation.action and animation.action.slots:
                    animation.action_slot = animation.action.slots[0]
                keys.update_tag()
            count = row['frames']*2
            clip = dict(name=name, motion=row['name'], action=row['action'], ordinal=ordinal,
                        label=row['labelRU'], note=row['useRU'], view=args.view,
                        frame_count=count, frame_start=0, frame_end=count-1,
                        timeline_seconds=(count-1)/60, duration_seconds=count/60,
                        authored_frames=row['frames'], event_frames=row.get('event_frames', {}))
            if row.get('shape_actions'):
                clip['shape_actions'] = row['shape_actions']
            manifest['clips'].append(clip)
            if args.draft:
                events = positive_events(row.get('event_frames', {}), row['frames'])
                sample_frames = sorted(events | {1., float(row['frames'])})
                if not events:
                    sample_frames = sorted({1., (row['frames']+1)/2, float(row['frames'])})
                evaluations = enumerate(sample_frames)
            else:
                evaluations = ((sample, 1+sample*.5) for sample in range(count))
            for sample, frame in evaluations:
                scene.frame_set(math.floor(frame), subframe=frame-math.floor(frame))
                if args.draft:
                    token = ('%07.3f' % frame).rstrip('0').rstrip('.').replace('.', 'p')
                    path = folder / (row['name'] + '_frame' + token + '.png')
                else:
                    path = folder / ('frame_%04d.png' % sample)
                scene.render.filepath = str(path)
                bpy.ops.render.render(write_still=True)
                manifest['frames'].append(dict(clip=name, index=sample, source_frame=sample,
                                               blender_frame=frame, view=args.view,
                                               path=str(path), sha256=sha256(path)))
            write_manifest(manifest_path, manifest)
            print('CARRIES_REVIEW_CLIP_READY', ordinal, name,
                  len(sample_frames) if args.draft else count, flush=True)
        # Exactly one final source-stability check; never save the loaded blend.
        for path, expected in initial_hashes.items():
            if sha256(path) != expected:
                raise ValueError('Source art changed during rendering: ' + str(path))
        if sha256(report_path) != report_hash:
            raise ValueError('Author report changed during rendering')
        manifest.update(status='complete', source_files_unchanged=True,
                        rendered_png_count=len(manifest['frames']), wall_seconds=time.monotonic()-started)
    except Exception as error:
        manifest.update(status='failed', error=str(error), wall_seconds=time.monotonic()-started)
        raise
    finally:
        write_manifest(manifest_path, manifest)
    print('CARRIES_REVIEW_RENDER_COMPLETE', str(manifest_path), flush=True)


if __name__ == '__main__':
    main()
