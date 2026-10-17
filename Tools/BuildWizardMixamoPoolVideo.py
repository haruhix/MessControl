"""Concatenate rendered Blender clips once, at manifest FPS and full length.

Reads RenderManifest and per-clip PNGs; adds a 120px Russian review caption.
Source art and rendered frames are never modified. Importing does nothing.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw

from BuildWizardDynamicsComparisonVideo import (
    escape_metadata, fitted_font, font_for, probe, rate_from_manifest,
    resolve_path, sha256, source_records,
)
from BuildWizardMixamoReviewVideo import executable

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Artifacts/WizardMixamoPool'
BASENAME = 'Wizard_Mixamo_Pool_All'
SIZE = (640, 600)


def verify(directory, expected_clips):
    manifest_path = directory / 'RenderManifest.json'
    manifest_hash = sha256(manifest_path)
    manifest = json.loads(manifest_path.read_text(encoding='utf8'))
    if manifest.get('status') != 'complete' or tuple(manifest['size']) != (640, 480) or 'fps' not in manifest:
        raise ValueError('Expected a complete 640x480 Blender render with declared FPS')
    fps = rate_from_manifest(manifest)
    clips = manifest['clips']
    names = [clip['name'] for clip in clips]
    if len(clips) != expected_clips or len(names) != len(set(names)):
        raise ValueError('Expected exactly %d uniquely named complete clips' % expected_clips)
    sources = source_records(directory, manifest.get('sources', []))
    if not sources:
        raise ValueError('Source blend SHA provenance is required')
    records = {}
    for record in manifest['frames']:
        key = (record['clip'], int(record['index']))
        if key in records:
            raise ValueError('Duplicate frame record: ' + str(key))
        records[key] = record
    paths, expected = {}, set()
    for clip in clips:
        count = int(clip['frame_count'])
        if count <= 0 or int(clip['frame_start']) != 0 or int(clip['frame_end']) != count-1:
            raise ValueError('Incomplete source frame range: ' + clip['name'])
        folder = (directory / clip['name']).resolve()
        if not folder.is_relative_to(directory):
            raise ValueError('Frame directory leaves the input directory')
        pngs = sorted(folder.glob('*.png'))
        if [path.name for path in pngs] != ['frame_%04d.png' % index for index in range(count)]:
            raise ValueError('Nonconsecutive source PNGs: ' + str(folder))
        for index, path in enumerate(pngs):
            key = (clip['name'], index)
            expected.add(key)
            record = records.get(key)
            if (not record or record['source_frame'] != index
                    or resolve_path(directory, record['path']) != path
                    or sha256(path) != record['sha256']):
                raise ValueError('Source index/path/SHA differs: ' + str(key))
            with Image.open(path) as image:
                if image.size != (640, 480):
                    raise ValueError('Wrong source image dimensions: ' + str(path))
            paths[key] = path
    if set(records) != expected:
        raise ValueError('Unexpected or missing frame records')
    if 'rendered_png_count' in manifest and int(manifest['rendered_png_count']) != len(paths):
        raise ValueError('Declared render count differs from complete source timelines')
    return manifest, fps, sources, paths, manifest_hash


@lru_cache(maxsize=64)
def wrapped_note(note):
    draw = ImageDraw.Draw(Image.new('RGB', (1, 1)))
    for size in range(15, 9, -1):
        font = font_for('segoeui.ttf', size)
        lines, current = [], ''
        for word in note.split():
            candidate = (current + ' ' + word).strip()
            if current and draw.textbbox((0, 0), candidate, font=font)[2] > 608:
                lines.append(current)
                current = word
            else:
                current = candidate
        if current:
            lines.append(current)
        if len(lines) <= 2 and all(draw.textbbox((0, 0), line, font=font)[2] <= 608 for line in lines):
            return font, tuple(lines)
    raise ValueError('Clip note cannot fit two review-caption lines without truncation')


def composite(clip, index, ordinal, total, paths, fps):
    canvas = Image.new('RGB', SIZE, '#17212d')
    draw = ImageDraw.Draw(canvas)
    title = f'{ordinal:02d}/{total} · Маг · Mixamo как основа · Blender'
    label = str(clip.get('label') or clip['name'])
    for y, text, maximum in ((5, title, 19), (31, label, 24)):
        draw.text((16, y), text, font=fitted_font(draw, text, maximum, width=608, bold=True), fill='#f1f4f8')
    font, lines = wrapped_note(str(clip.get('note') or ''))
    for row, line in enumerate(lines):
        draw.text((16, 64 + row*18), line, font=font, fill='#becbda')
    timeline = (int(clip['frame_count']) - 1) / float(fps)
    timing = f'Темп 1× · {float(fps):.6g} кадр/с · полный клип: {index / float(fps):.2f} / {timeline:.2f} с'
    draw.text((16, 102), timing, font=fitted_font(draw, timing, 13, width=608), fill='#becbda')
    with Image.open(paths[(clip['name'], index)]) as image:
        canvas.paste(image.convert('RGB'), (0, 120))
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=OUTPUT)
    parser.add_argument('--expected-clips', type=int, default=32)
    parser.add_argument('--basename', default=BASENAME,
                        help='Output filename stem, without extension')
    parser.add_argument('--ffmpeg', type=Path)
    parser.add_argument('--ffprobe', type=Path)
    args = parser.parse_args()
    if args.expected_clips <= 0:
        raise ValueError('Expected clip count must be positive')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', args.basename):
        raise ValueError('Basename must use letters, digits, underscores or hyphens')
    basename = args.basename
    directory = args.input_dir.resolve()
    manifest, fps, sources, paths, manifest_hash = verify(directory, args.expected_clips)
    ffmpeg, ffprobe = executable('ffmpeg', args.ffmpeg), executable('ffprobe', args.ffprobe)
    chapters, cursor = [], 0
    metadata = [';FFMETADATA1', 'title=Маг: полный набор вариантов на основе Mixamo']
    for ordinal, clip in enumerate(manifest['clips'], 1):
        count, label = int(clip['frame_count']), str(clip.get('label') or clip['name'])
        note = str(clip.get('note') or '').strip()
        metadata += ['[CHAPTER]', f'TIMEBASE={fps.denominator}/{fps.numerator}',
                     f'START={cursor}', f'END={cursor+count}',
                     'title=' + escape_metadata(f'{ordinal:02d}. {label}')]
        if note:
            metadata.append('comment=' + escape_metadata(note))
        chapters.append(dict(ordinal=ordinal, name=clip['name'], label=label,
                             group=clip.get('group'), note=note or None,
                             start_frame=cursor, frame_count=count,
                             source_frame_start=0, source_frame_end=count-1,
                             start_seconds=cursor / float(fps), duration_seconds=count / float(fps),
                             source_timeline_seconds=(count-1) / float(fps)))
        cursor += count
    metadata_path = directory / (basename + '.ffmetadata')
    metadata_path.write_text('\n'.join(metadata) + '\n', encoding='utf8')
    video, log = directory / (basename + '.mp4'), directory / (basename + '.Encoder.log')
    rate = f'{fps.numerator}/{fps.denominator}'
    command = [ffmpeg, '-y', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
               '-s:v', '640x600', '-r', rate, '-i', 'pipe:0', '-f', 'ffmetadata', '-i', str(metadata_path),
               '-map', '0:v:0', '-map_metadata', '1', '-map_chapters', '1', '-an', '-c:v', 'libx264',
               '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p', '-fps_mode', 'passthrough',
               '-movflags', '+faststart', str(video)]
    with log.open('wb') as errors:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=errors)
        try:
            for ordinal, clip in enumerate(manifest['clips'], 1):
                for index in range(int(clip['frame_count'])):
                    process.stdin.write(composite(clip, index, ordinal, len(manifest['clips']), paths, fps).tobytes())
            process.stdin.close()
            if process.wait():
                raise RuntimeError('Encoder failed: ' + log.read_text(encoding='utf8', errors='replace'))
        except Exception:
            if process.poll() is None:
                process.kill()
            process.wait()
            raise
    evidence = probe(ffprobe, video, SIZE, fps, cursor)
    for source in sources:
        if sha256(Path(source['path'])) != source['sha256']:
            raise ValueError('Source blend changed during composition: ' + source['path'])
    if sha256(directory / 'RenderManifest.json') != manifest_hash:
        raise ValueError('RenderManifest changed during composition')
    poster = directory / (basename + '.png')
    first = manifest['clips'][0]
    composite(first, int((int(first['frame_count'])-1)*.45), 1, len(manifest['clips']), paths, fps).save(poster)
    report = dict(status='complete', video=str(video), video_sha256=sha256(video),
                  poster=str(poster), poster_sha256=sha256(poster), size=list(SIZE), source_view_size=[640, 480],
                  clip_count=len(manifest['clips']), frame_count=cursor, rendered_png_count=len(paths),
                  fps=float(fps), fps_fraction=rate, duration_seconds=cursor / float(fps),
                  playback_speed=1, repeated_clips=False, video_frame_interpolation=False,
                  added_holds=False, complete_source_timelines=True, chapters=chapters,
                  scope=manifest.get('scope', 'Pure Blender review; source Mixamo motion as animation base'),
                  sources=manifest['sources'], source_files_unchanged=True,
                  render_manifest=str(directory / 'RenderManifest.json'), render_manifest_sha256=manifest_hash,
                  limitations=manifest.get('limitations', []), probe=evidence)
    (directory / (basename + '.Video.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(str(video), flush=True)


if __name__ == '__main__':
    main()
