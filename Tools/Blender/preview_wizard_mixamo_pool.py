"""Render all saved Mixamo-derived pool Actions; never save/change source art."""
import argparse
import json
import math
import sys
import time
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import author_wizard_mixamo_pool as pool

SOURCE = pool.BLEND
OUTPUT = pool.OUTPUT


def main():
    assert bpy.app.background
    parser = argparse.ArgumentParser()
    parser.add_argument('--draft', action='store_true')
    parser.add_argument('--first', type=int, default=1)
    parser.add_argument('--last', type=int, default=32)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--report', type=Path, default=pool.REPORT)
    parser.add_argument('--output-dir', type=Path, default=OUTPUT)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    source, output = args.source.resolve(), args.output_dir.resolve()
    report = json.loads(args.report.read_text(encoding='utf8'))
    before, original_hash = pool.author.sha(source), pool.author.sha(pool.SOURCE)
    assert report['passed'] and report['blend_sha256'] == before and report['source_sha256'] == original_hash
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene, rig = bpy.context.scene, bpy.data.objects['rig']
    stored_defaults = pool.defaults(rig)
    scene.camera = bpy.data.objects['MC_ReviewCamera_ThreeQuarter']
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.render_aa = '5'
    scene.render.resolution_x, scene.render.resolution_y = 640, 480
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.compression = 10
    output.mkdir(parents=True, exist_ok=True)
    selected = report['clips'][args.first-1:args.last]
    if args.draft:
        draft = output/'Draft'
        draft.mkdir(parents=True, exist_ok=True)
        rows = []
        for row in selected:
            pool.author.bind(rig, bpy.data.actions[row['action']], stored_defaults)
            active = row['segments']
            draft_frames={int(segment['start']+.68*(segment['end']-segment['start']))
                          for index,segment in enumerate(active)
                          if index==0 or segment['part']=='upper' or segment['source_start']!=segment['source_end']}
            for accent in row.get('accents',row.get('pose_accents',[])):
                peak=max(accent['keys'],key=lambda pair:abs(pair[1]))
                draft_frames.add(int(peak[0]))
            for index,frame in enumerate(sorted(draft_frames)):
                scene.frame_set(frame)
                path = draft/('%02d_%s_%02d.png' % (row['ordinal'], row['name'], index))
                scene.render.filepath = str(path)
                bpy.ops.render.render(write_still=True)
                rows.append(dict(name=row['name'], ordinal=row['ordinal'], label=row['labelRU'], group=row['groupRU'],
                                 frame=frame, sample=index, path=str(path)))
        (draft/'DraftManifest.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf8')
        print('POOL_DRAFT_READY', len(rows), flush=True)
        return
    suffix = '' if args.first == 1 and args.last == 32 else '_%02d_%02d' % (args.first, args.last)
    manifest_path = output/('RenderManifest'+suffix+'.json')
    manifest = dict(status='running', fps=60, size=[640,480], source_fps=30,
        source_animation_resampled=True, evaluation_subframes=True, playback_speed=1,
        sources=[dict(path=str(source),sha256=before),dict(path=str(pool.SOURCE),sha256=original_hash)],
        scope=report['scope'], limitations=report['limitations'], clips=[], frames=[],
        camera=scene.camera.name, ortho_scale=scene.camera.data.ortho_scale,
        engine='BLENDER_WORKBENCH', render_aa='5', first=args.first, last=args.last)

    def save():
        temporary = manifest_path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf8')
        temporary.replace(manifest_path)

    started = time.monotonic()
    save()
    try:
        for row in selected:
            directory = output/row['name']
            assert not any(directory.glob('frame_*.png')), 'Fresh render directory required: '+str(directory)
            directory.mkdir(parents=True, exist_ok=True)
            pool.author.bind(rig, bpy.data.actions[row['action']], stored_defaults)
            count = int(row['frames'])*2
            clip = dict(name=row['name'], label=row['labelRU'], group=row['groupRU'], frame_count=count,
                        frame_start=0, frame_end=count-1, timeline_seconds=(count-1)/60,
                        note=row['useRU'], action=row['action'], authored_frames=row['frames'])
            manifest['clips'].append(clip)
            for index in range(count):
                frame = min(float(row['frames']), 1+index*.5)
                scene.frame_set(math.floor(frame), subframe=frame-math.floor(frame))
                path = directory/('frame_%04d.png'%index)
                scene.render.filepath = str(path)
                bpy.ops.render.render(write_still=True)
                manifest['frames'].append(dict(clip=row['name'], index=index, source_frame=index,
                    blender_frame=frame, path=str(path),sha256=pool.author.sha(path)))
            save()
            print('POOL_CLIP_RENDER_READY', row['ordinal'], row['name'], count, flush=True)
        assert before == pool.author.sha(source) and original_hash == pool.author.sha(pool.SOURCE)
        manifest.update(status='complete', source_files_unchanged=True, rendered_png_count=len(manifest['frames']),
                        wall_seconds=time.monotonic()-started)
    except Exception as error:
        manifest.update(status='failed',error=str(error))
        raise
    finally:
        save()
    print('POOL_RENDER_COMPLETE', str(manifest_path), flush=True)


if __name__ == '__main__':
    main()
