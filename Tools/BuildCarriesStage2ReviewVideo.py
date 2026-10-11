"""Compose one Blender-only Stage 2 review at the rendered FPS and full length.

Manifest order is preserved; no repeated clips, added holds or interpolation.
Validation checks all indices/paths/counts, source SHA, and two PNG samples per
clip. Remaining PNGs are read once for encoding, with their dimensions checked.
Importing does not render, encode, or open an application.
"""
from __future__ import annotations

import argparse
import json
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
OUTPUT = ROOT / 'Artifacts/CarriesStage2/ArtistReview'
BASENAME = 'Carries_Stage2_AnimationReview'
TITLE = 'Босс · фаза 2 · Blender · пробы пластики'
VIEW_LABELS = {'ThreeQuarter': 'Три четверти', 'Front': 'Спереди'}
SOURCE_SIZE, SIZE = (800, 600), (800, 720)
CAPTION_WIDTH = 768


def integer(value, field):
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError('Expected integer ' + field)
    return value


def verify(directory, expected_clips):
    manifest_path = directory / 'RenderManifest.json'
    raw = manifest_path.read_bytes()
    manifest_hash = sha256(manifest_path)
    manifest = json.loads(raw.decode('utf8'))
    if (manifest.get('status') != 'complete' or tuple(manifest['size']) != SOURCE_SIZE
            or 'fps' not in manifest or manifest.get('source_files_unchanged') is False):
        raise ValueError('Expected a complete 800x600 Blender render with declared FPS')
    fps = rate_from_manifest(manifest)
    clips = manifest['clips']
    names = [clip['name'] for clip in clips]
    if (not clips or len(names) != len(set(names))
            or expected_clips is not None and len(clips) != expected_clips):
        raise ValueError('Unexpected or duplicate clip count')
    sources = source_records(directory, manifest.get('sources', []))
    if not sources:
        raise ValueError('Source blend SHA provenance is required')
    records = {}
    for record in manifest['frames']:
        key = (record['clip'], integer(record['index'], 'frame index'))
        if key in records:
            raise ValueError('Duplicate frame record: ' + str(key))
        digest = str(record['sha256'])
        if len(digest) != 64 or any(char not in '0123456789abcdefABCDEF' for char in digest):
            raise ValueError('Invalid PNG SHA record: ' + str(key))
        records[key] = record
    paths, expected, samples = {}, set(), []
    for clip in clips:
        name, count = clip['name'], integer(clip['frame_count'], 'frame_count')
        if (count <= 0 or clip['frame_start'] != 0 or clip['frame_end'] != count-1
                or clip.get('view') not in VIEW_LABELS):
            raise ValueError('Invalid source frame range or camera view: ' + name)
        if not name or name in ('.', '..') or any(char in name for char in '/\\:'):
            raise ValueError('Unsafe clip folder name: ' + str(name))
        folder = (directory / name).resolve()
        if not folder.is_relative_to(directory):
            raise ValueError('Clip directory leaves the review folder')
        pngs = sorted(folder.glob('*.png'))
        if [path.name for path in pngs] != ['frame_%04d.png' % index for index in range(count)]:
            raise ValueError('Nonconsecutive PNG count: ' + name)
        for index, path in enumerate(pngs):
            key = (name, index)
            record = records.get(key)
            if not record or resolve_path(directory, record['path']) != path:
                raise ValueError('Source frame record/path differs: ' + str(key))
            expected.add(key)
            paths[key] = path
        for index in sorted({0, count-1}):
            path = paths[name, index]
            if sha256(path) != str(records[name, index]['sha256']).lower():
                raise ValueError('Sample PNG SHA differs: ' + str(path))
            with Image.open(path) as image:
                if image.size != SOURCE_SIZE:
                    raise ValueError('Sample PNG dimensions differ: ' + str(path))
            samples.append(dict(clip=name, index=index, path=str(path)))
    if set(records) != expected:
        raise ValueError('Unexpected or missing frame records')
    if 'rendered_png_count' in manifest and manifest['rendered_png_count'] != len(paths):
        raise ValueError('Declared rendered PNG count differs')
    return manifest, fps, sources, paths, manifest_hash, samples


@lru_cache(maxsize=64)
def wrapped_note(note):
    draw = ImageDraw.Draw(Image.new('RGB', (1, 1)))
    for size in range(15, 9, -1):
        font = font_for('segoeui.ttf', size)
        lines, current = [], ''
        for word in note.split():
            candidate = (current + ' ' + word).strip()
            if current and draw.textbbox((0, 0), candidate, font=font)[2] > CAPTION_WIDTH:
                lines.append(current)
                current = word
            else:
                current = candidate
        if current:
            lines.append(current)
        if len(lines) <= 2 and all(draw.textbbox((0, 0), line, font=font)[2] <= CAPTION_WIDTH for line in lines):
            return font, tuple(lines)
    raise ValueError('Clip note cannot fit two caption lines without truncation')


def clip_title(clip):
    return str(clip.get('label') or clip['name']) + ' · ' + VIEW_LABELS[clip['view']]


def composite(clip, index, ordinal, total, paths, fps):
    canvas = Image.new('RGB', SIZE, '#17212d')
    draw = ImageDraw.Draw(canvas)
    title = f'{ordinal:02d}/{total} · {TITLE}'
    for y, text, maximum in ((5, title, 20), (31, clip_title(clip), 25)):
        draw.text((16, y), text, font=fitted_font(draw, text, maximum, width=CAPTION_WIDTH, bold=True), fill='#f1f4f8')
    font, lines = wrapped_note(str(clip.get('note') or ''))
    for row, line in enumerate(lines):
        draw.text((16, 64+row*18), line, font=font, fill='#becbda')
    duration = (clip['frame_count']-1) / float(fps)
    timing = f'Темп 1× · {float(fps):.6g} кадр/с · полный клип: {index / float(fps):.2f} / {duration:.2f} с'
    draw.text((16, 102), timing, font=fitted_font(draw, timing, 13, width=CAPTION_WIDTH), fill='#becbda')
    with Image.open(paths[clip['name'], index]) as image:
        if image.size != SOURCE_SIZE:
            raise ValueError('PNG dimensions differ during encoding: ' + str(paths[clip['name'], index]))
        canvas.paste(image.convert('RGB'), (0, 120))
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=OUTPUT)
    parser.add_argument('--expected-clips', type=int, help='Optional exact count; default accepts any positive count')
    parser.add_argument('--ffmpeg', type=Path)
    parser.add_argument('--ffprobe', type=Path)
    args = parser.parse_args()
    if args.expected_clips is not None and args.expected_clips <= 0:
        raise ValueError('Expected clip count must be positive')
    directory = args.input_dir.resolve()
    manifest, fps, sources, paths, manifest_hash, samples = verify(directory, args.expected_clips)
    ffmpeg, ffprobe = executable('ffmpeg', args.ffmpeg), executable('ffprobe', args.ffprobe)
    chapters, cursor = [], 0
    metadata = [';FFMETADATA1', 'title=' + escape_metadata(TITLE)]
    for ordinal, clip in enumerate(manifest['clips'], 1):
        count, label = clip['frame_count'], clip_title(clip)
        note = str(clip.get('note') or '').strip()
        # Resolve caption lengths before starting the encoder.
        wrapped_note(note)
        metadata += ['[CHAPTER]', f'TIMEBASE={fps.denominator}/{fps.numerator}',
                     f'START={cursor}', f'END={cursor+count}',
                     'title=' + escape_metadata(f'{ordinal:02d}. {label}')]
        if note:
            metadata.append('comment=' + escape_metadata(note))
        chapters.append(dict(ordinal=ordinal, name=clip['name'], label=label,
                             view=clip['view'], note=note or None, start_frame=cursor,
                             frame_count=count, source_frame_start=0, source_frame_end=count-1,
                             start_seconds=cursor / float(fps), duration_seconds=count / float(fps)))
        cursor += count
    metadata_path = directory / (BASENAME + '.ffmetadata')
    metadata_path.write_text('\n'.join(metadata)+'\n', encoding='utf8')
    video, log = directory / (BASENAME+'.mp4'), directory / (BASENAME+'.Encoder.log')
    rate = f'{fps.numerator}/{fps.denominator}'
    command = [ffmpeg, '-y', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
               '-s:v', '800x720', '-r', rate, '-i', 'pipe:0', '-f', 'ffmetadata', '-i', str(metadata_path),
               '-map', '0:v:0', '-map_metadata', '1', '-map_chapters', '1', '-an', '-c:v', 'libx264',
               '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p', '-fps_mode', 'passthrough',
               '-movflags', '+faststart', str(video)]
    with log.open('wb') as errors:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=errors)
        try:
            for ordinal, clip in enumerate(manifest['clips'], 1):
                for index in range(clip['frame_count']):
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
    poster = directory / (BASENAME+'.png')
    first = manifest['clips'][0]
    composite(first, int((first['frame_count']-1)*.45), 1, len(manifest['clips']), paths, fps).save(poster)
    report = dict(status='complete', title=TITLE, video=str(video), video_sha256=sha256(video),
                  poster=str(poster), poster_sha256=sha256(poster), chapters=chapters,
                  chapter_metadata=str(metadata_path), size=list(SIZE), source_view_size=list(SOURCE_SIZE),
                  clip_count=len(manifest['clips']), frame_count=cursor, rendered_png_count=len(paths),
                  fps=float(fps), fps_fraction=rate, duration_seconds=cursor / float(fps), playback_speed=1,
                  repeated_clips=False, added_holds=False, video_frame_interpolation=False,
                  complete_source_timelines=True, source_files_unchanged=True, sources=sources,
                  render_manifest=str(directory / 'RenderManifest.json'), render_manifest_sha256=manifest_hash,
                  validation=dict(all_frame_indices_paths_counts_checked=True,
                                  png_sha_samples=samples, png_sha_validation='first and last frame per clip',
                                  all_frame_dimensions_checked_during_encode=True),
                  scope=manifest.get('scope', 'Blender-only Stage 2 pose studies on the source custom rig'),
                  limitations=manifest.get('limitations', []), probe=evidence)
    (directory / 'Video.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(str(video), flush=True)


if __name__ == '__main__':
    main()
