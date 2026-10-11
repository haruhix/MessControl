"""Compose synchronized native-FPS dynamics variants from rendered PNGs.

Accepts the comparison RenderManifest schema, any clip count and two/three/four selected
cases. Provider attribution is explicit; importing this module does nothing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from fractions import Fraction
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Artifacts/WizardDynamicsComparison'
FFMPEG = Path('C:/ffmpeg/ffmpeg.exe')
FFPROBE = Path('C:/ffmpeg/ffprobe.exe')
BASENAME = 'Wizard_Dynamics_Comparison'
LABELS = {'baseline': 'Исходная анимация', 'mild': 'Слабая динамика',
          'medium': 'Средняя динамика', 'strong': 'Сильная динамика'}
HEADER, ROW_HEIGHT, CASE_HEADER = 92, 504, 24


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def rate_from_manifest(manifest):
    value = manifest.get('fps', 30)
    rate = (Fraction(int(value['numerator']), int(value['denominator']))
            if isinstance(value, dict) else Fraction(str(value)).limit_denominator(1000000))
    rate /= Fraction(str(manifest.get('fps_base', 1)))
    if rate <= 0:
        raise ValueError('Native FPS must be positive')
    return rate


def resolve_path(directory, value):
    path = Path(value)
    return (path if path.is_absolute() else directory / path).resolve()


def source_records(directory, records):
    result = []
    for record in records:
        path = resolve_path(directory, record['path'])
        expected = str(record['sha256']).lower()
        if sha256(path) != expected:
            raise ValueError('Source SHA256 differs: ' + str(path))
        result.append(dict(path=str(path), sha256=expected))
    return result


def verify(directory, provider, selected, label_overrides):
    manifest = json.loads((directory / 'RenderManifest.json').read_text(encoding='utf8'))
    if manifest.get('status') != 'complete':
        raise ValueError('Expected a complete comparison render')
    if manifest.get('provider') and manifest['provider'] != provider:
        raise ValueError('--provider must match the actual provider in RenderManifest')
    fps = rate_from_manifest(manifest)
    size = tuple(manifest['size'])
    if size not in ((640, 480), (480, 480)):
        raise ValueError('Expected 640x480 or 480x480 source images')
    available = manifest['cases']
    lookup = {case['name']: case for case in available}
    if len(lookup) != len(available):
        raise ValueError('Case names must be unique')
    selected = selected if selected is not None else [case['name'] for case in available]
    if len(selected) not in (2, 3, 4) or len(set(selected)) != len(selected):
        raise ValueError('Select exactly two, three or four unique cases with --cases')
    if any(name not in lookup for name in selected):
        raise ValueError('Selected case is absent from the manifest')
    shared_sources = source_records(directory, manifest.get('sources', []))
    cases = []
    for name in selected:
        original = lookup[name]
        folder = resolve_path(directory, original.get('directory', name))
        if not folder.is_relative_to(directory):
            raise ValueError('Case directory leaves the input directory: ' + name)
        specific = original.get('sources')
        sources = source_records(directory, specific) if specific is not None else shared_sources
        if not sources:
            raise ValueError('Each case requires source SHA provenance: ' + name)
        label = label_overrides.get(name, original.get('label', LABELS.get(name, name)))
        cases.append(dict(name=name, label=str(label), directory=str(folder),
                          sources=sources, source_scope='case sources' if specific is not None
                          else 'shared manifest sources'))
    clips = manifest['clips']
    if not clips or len({clip['name'] for clip in clips}) != len(clips):
        raise ValueError('Expected one or more uniquely named clips')
    records = {}
    for record in manifest['frames']:
        key = (record['case'], record['clip'], int(record['index']))
        if key in records:
            raise ValueError('Duplicate frame record: ' + str(key))
        records[key] = record
    paths, png_count = {}, 0
    for clip in clips:
        count, start = int(clip['frame_count']), int(clip['frame_start'])
        if count <= 0 or int(clip['frame_end']) - start + 1 != count:
            raise ValueError('Incomplete source frame range: ' + clip['name'])
        for case in cases:
            folder = Path(case['directory']) / clip['name']
            if not folder.resolve().is_relative_to(directory):
                raise ValueError('Clip directory leaves the input directory')
            expected = ['frame_%04d.png' % index for index in range(count)]
            actual = sorted(folder.glob('*.png'))
            if [path.name for path in actual] != expected:
                raise ValueError('Incomplete consecutive PNGs: ' + str(folder))
            for index, png in enumerate(actual):
                key = (case['name'], clip['name'], index)
                if key not in records:
                    raise ValueError('Missing frame record: ' + str(key))
                record = records[key]
                if int(record['source_frame']) != start + index:
                    raise ValueError('Case source timeline was resampled: ' + str(key))
                if resolve_path(directory, record['path']) != png.resolve():
                    raise ValueError('Manifest frame path differs: ' + str(key))
                if sha256(png) != record['sha256']:
                    raise ValueError('Rendered frame SHA256 differs: ' + str(png))
                with Image.open(png) as image:
                    if image.size != size:
                        raise ValueError('Wrong source image size: ' + str(png))
                paths[key] = png
                png_count += 1
            manifested = [key[2] for key in records if key[:2] == (case['name'], clip['name'])]
            if sorted(manifested) != list(range(count)):
                raise ValueError('Unexpected source frame records: ' + case['name'] + ' ' + clip['name'])
    return manifest, fps, cases, paths, png_count


@lru_cache(maxsize=80)
def font_for(filename, size):
    return ImageFont.truetype('C:/Windows/Fonts/' + filename, size)


def fitted_font(draw, text, maximum, width=1232, bold=False):
    filename = 'seguisb.ttf' if bold else 'segoeui.ttf'
    for size in range(maximum, 9, -1):
        font = font_for(filename, size)
        if draw.textbbox((0, 0), text, font=font)[2] <= width:
            return font
    raise ValueError('Label too long to fit without truncation: ' + text)


def layout(case_count):
    columns = 3 if case_count == 3 else 2
    rows = (case_count + columns - 1) // columns
    return columns, (640 * columns, HEADER + ROW_HEIGHT * rows)


def composite(clip, index, ordinal, total, cases, paths, fps, provider):
    columns, size = layout(len(cases))
    canvas = Image.new('RGB', size, '#17212d')
    draw = ImageDraw.Draw(canvas)
    name = str(clip.get('label', clip.get('action_name', clip['name'])))
    title = f'Сравнение динамики · {ordinal}/{total} · {name}'
    draw.text((24, 8), title, font=fitted_font(draw, title, 30, width=size[0]-48, bold=True), fill='#f1f4f8')
    attribution = 'Провайдер: ' + provider
    draw.text((24, 43), attribution, font=fitted_font(draw, attribution, 19, width=size[0]-48), fill='#becbda')
    timeline = (int(clip['frame_count']) - 1) / float(fps)
    prefix = 'Реальная игра · полный темп 1×' if clip.get('source_kind') == 'Actual current UE gameplay engine ticks' else 'Полный исходный темп 1×'
    timing = (f'{prefix} · {float(fps):.6g} кадр/с · '
              f'Клип: {index / float(fps):.2f} / {timeline:.2f} с')
    note = str(clip.get('note') or '').strip()
    if note:
        timing += ' · ' + note
    draw.text((24, 67), timing, font=fitted_font(draw, timing, 17, width=size[0]-48), fill='#becbda')
    for position, case in enumerate(cases):
        column, row = position % columns, position // columns
        x, y = column * 640, HEADER + row * ROW_HEIGHT
        label = case['label'] + ' · ' + case['name']
        draw.text((x + 16, y + 1), label,
                  font=fitted_font(draw, label, 18, width=608, bold=True), fill='#f1f4f8')
        with Image.open(paths[(case['name'], clip['name'], index)]) as image:
            panel = image.convert('RGB')
            canvas.paste(panel, (x + (640 - panel.width) // 2, y + CASE_HEADER))
    for column in range(1, columns):
        draw.line((640*column, HEADER, 640*column, size[1]), fill='#344355', width=2)
    if len(cases) == 4:
        draw.line((0, HEADER + ROW_HEIGHT, 1280, HEADER + ROW_HEIGHT), fill='#344355', width=2)
    return canvas


def escape_metadata(value):
    return (str(value).replace('\\', '\\\\').replace('\r', ' ').replace('\n', ' ')
            .replace('=', '\\=').replace(';', '\\;').replace('#', '\\#'))


def chapter_metadata(directory, clips, fps, provider):
    chapters, cursor = [], 0
    text = [';FFMETADATA1', 'title=Сравнение динамики мага',
            'comment=' + escape_metadata('Провайдер: ' + provider)]
    for clip in clips:
        count = int(clip['frame_count'])
        title = str(clip.get('label', clip.get('action_name', clip['name'])))
        text += ['[CHAPTER]', f'TIMEBASE={fps.denominator}/{fps.numerator}',
                 'START=' + str(cursor), 'END=' + str(cursor + count),
                 'title=' + escape_metadata(title)]
        note = str(clip.get('note') or '').strip()
        if note:
            text.append('comment=' + escape_metadata(note))
        chapters.append(dict(name=clip['name'], title=title, start_frame=cursor,
                             frames=count, start_seconds=cursor / float(fps),
                             duration_seconds=count / float(fps),
                             source_frame_start=int(clip['frame_start']),
                             source_frame_end=int(clip['frame_end']),
                             source_timeline_seconds=(count - 1) / float(fps), note=note or None))
        cursor += count
    path = directory / (BASENAME + '.ffmetadata')
    path.write_text('\n'.join(text) + '\n', encoding='utf8')
    return path, chapters, cursor


def probe(ffprobe, video, size, fps, count):
    result = subprocess.run([str(ffprobe), '-v', 'error', '-count_frames', '-select_streams', 'v:0',
        '-show_entries', 'stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames,duration',
        '-show_entries', 'format=duration', '-of', 'json', str(video)],
        check=True, capture_output=True, text=True)
    evidence = json.loads(result.stdout)
    stream = evidence['streams'][0]
    duration = float(stream.get('duration', evidence['format']['duration']))
    if ((stream['width'], stream['height']) != size
            or Fraction(stream['r_frame_rate']) != fps
            or Fraction(stream['avg_frame_rate']) != fps
            or int(stream['nb_read_frames']) != count
            or abs(duration - count / float(fps)) > .02):
        raise ValueError('Encoded timeline differs from the synchronized source frames')
    return evidence


def encode(directory, clips, cases, paths, fps, provider, ffmpeg, ffprobe):
    metadata, chapters, count = chapter_metadata(directory, clips, fps, provider)
    video, log = directory / (BASENAME + '.mp4'), directory / (BASENAME + '.Encoder.log')
    _, size = layout(len(cases))
    rate = f'{fps.numerator}/{fps.denominator}'
    command = [str(ffmpeg), '-y', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo',
               '-pix_fmt', 'rgb24', '-s:v', f'{size[0]}x{size[1]}', '-r', rate, '-i', 'pipe:0',
               '-f', 'ffmetadata', '-i', str(metadata), '-map', '0:v:0',
               '-map_metadata', '1', '-map_chapters', '1', '-an', '-c:v', 'libx264',
               '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p',
               '-fps_mode', 'passthrough', '-movflags', '+faststart', str(video)]
    with log.open('wb') as errors:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=errors)
        try:
            for ordinal, clip in enumerate(clips, 1):
                for index in range(int(clip['frame_count'])):
                    process.stdin.write(composite(clip, index, ordinal, len(clips),
                                                  cases, paths, fps, provider).tobytes())
            process.stdin.close()
            if process.wait():
                raise RuntimeError('Encoder failed: ' + log.read_text(encoding='utf8', errors='replace'))
        except Exception:
            process.kill()
            process.wait()
            raise
    evidence = probe(ffprobe, video, size, fps, count)
    poster_clip = next((clip for clip in clips if 'cast' in clip['name'].casefold()), clips[0])
    poster = directory / (BASENAME + '.png')
    index = int((int(poster_clip['frame_count']) - 1) * .45)
    composite(poster_clip, index, clips.index(poster_clip) + 1, len(clips),
              cases, paths, fps, provider).save(poster)
    return dict(video=str(video), video_sha256=sha256(video), poster=str(poster),
                poster_sha256=sha256(poster), metadata=str(metadata), metadata_sha256=sha256(metadata),
                size=list(size), frame_count=count, fps=float(fps), fps_fraction=rate,
                duration_seconds=count / float(fps), chapters=chapters, probe=evidence,
                playback_speed=1, interpolated_frames=False, synchronized_cases=True,
                complete_source_timelines=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=OUTPUT)
    parser.add_argument('--provider', required=True, help='Actual dynamics provider; use a prototype label when applicable')
    parser.add_argument('--cases', nargs='+', help='Two, three or four case names, in panel order')
    parser.add_argument('--labels-json', type=Path, help='Optional JSON case-name to display-label mapping')
    parser.add_argument('--ffmpeg', type=Path, default=FFMPEG)
    parser.add_argument('--ffprobe', type=Path, default=FFPROBE)
    args = parser.parse_args()
    provider = args.provider.strip()
    if not provider:
        raise ValueError('An explicit actual provider label is required')
    directory = args.input_dir.resolve()
    labels = json.loads(args.labels_json.read_text(encoding='utf8')) if args.labels_json else {}
    if not isinstance(labels, dict) or any(not isinstance(value, str) for value in labels.values()):
        raise ValueError('Labels must be a JSON object of string values')
    manifest, fps, cases, paths, png_count = verify(directory, provider, args.cases, labels)
    render_path = directory / 'RenderManifest.json'
    render_hash = sha256(render_path)
    video = encode(directory, manifest['clips'], cases, paths, fps, provider, args.ffmpeg, args.ffprobe)
    checked = set()
    for case in cases:
        for source in case['sources']:
            key = (source['path'], source['sha256'])
            if key not in checked and sha256(Path(source['path'])) != source['sha256']:
                raise ValueError('Source changed during composition: ' + source['path'])
            checked.add(key)
    if sha256(render_path) != render_hash:
        raise ValueError('RenderManifest changed during composition')
    capture_path = directory / 'Capture.json'
    capture_evidence = None
    if capture_path.is_file():
        capture = json.loads(capture_path.read_text(encoding='utf8'))
        capture_evidence = dict(path=str(capture_path), sha256=sha256(capture_path),
                                status=capture.get('status'), capture=capture.get('capture'),
                                gameplay_failed_runs=capture.get('gameplay_failed_runs'),
                                restore_errors=capture.get('restore_errors'),
                                frame_count=len(capture.get('frames', [])),
                                limitations=capture.get('limitations', []))
    report = dict(status='complete', provider=provider, manifest_provider=manifest.get('provider'),
                  render_manifest=str(render_path), render_manifest_sha256=render_hash,
                  sources=manifest.get('sources', []), source_files_unchanged=True,
                  cases=cases, clip_count=len(manifest['clips']), rendered_png_count=png_count,
                  source_view_size=manifest['size'], camera='ThreeQuarter', fps=float(fps),
                  fps_fraction=f'{fps.numerator}/{fps.denominator}',
                  scope=manifest.get('scope', 'Synchronized rendered variants, every source frame at native speed'),
                  capture_evidence=capture_evidence,
                  limitations=list(dict.fromkeys(manifest.get('limitations', []) +
                      (capture_evidence or {}).get('limitations', []))),
                  layout='three columns' if len(cases) == 3 else 'two columns', videos=[video])
    (directory / 'VideoManifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(video['video'], flush=True)


if __name__ == '__main__':
    main()
