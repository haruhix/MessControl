"""Compose four Blender cast variants at the rendered FPS, with optional repeats.

Reuses comparison-manifest verification without changing source art or PNGs.
source_frame is the evaluation index; blender_frame records the authored subframe.
Importing this module does nothing.
"""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

from BuildWizardDynamicsComparisonVideo import (
    CASE_HEADER, HEADER, ROW_HEIGHT, escape_metadata, fitted_font, layout,
    probe, sha256, verify,
)
from BuildWizardMixamoReviewVideo import executable

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Artifacts/WizardMixamoVariants'
BASENAME = 'Wizard_Mixamo_Variants'
CASES = ('Original', 'Quick', 'Heavy', 'Ritual')
LABELS = {'Original': 'Исходный Mixamo', 'Quick': 'Быстрый выброс',
          'Heavy': 'Тяжёлый заряд', 'Ritual': 'Ритуальный каст'}


def composite(clip, index, repetition, repeats, cases, paths, fps):
    _, size = layout(4)
    canvas = Image.new('RGB', size, '#17212d')
    draw = ImageDraw.Draw(canvas)
    title = 'Маг · варианты каста на основе Mixamo'
    caption = 'Предпросмотр в Blender · один ракурс · одинаковая камера'
    seconds = (int(clip['frame_count']) - 1) / float(fps)
    timing = (f'Темп 1× · {float(fps):.6g} кадр/с · повтор {repetition}/{repeats} · '
              f'{index / float(fps):.2f} / {seconds:.2f} с')
    for y, text, maximum, bold in ((8, title, 30, True), (43, caption, 19, False), (67, timing, 17, False)):
        draw.text((24, y), text, font=fitted_font(draw, text, maximum, width=size[0]-48, bold=bold),
                  fill='#f1f4f8' if bold else '#becbda')
    for position, case in enumerate(cases):
        x, y = position % 2 * 640, HEADER + position // 2 * ROW_HEIGHT
        label = LABELS[case['name']]
        draw.text((x + 16, y + 1), label,
                  font=fitted_font(draw, label, 18, width=608, bold=True), fill='#f1f4f8')
        with Image.open(paths[(case['name'], clip['name'], index)]) as image:
            panel = image.convert('RGB')
            canvas.paste(panel, (x + (640-panel.width)//2, y + CASE_HEADER))
    draw.line((640, HEADER, 640, size[1]), fill='#344355', width=2)
    draw.line((0, HEADER + ROW_HEIGHT, size[0], HEADER + ROW_HEIGHT), fill='#344355', width=2)
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=OUTPUT)
    parser.add_argument('--repeats', type=int, default=2)
    parser.add_argument('--provider', help='Defaults to the actual provider in RenderManifest')
    parser.add_argument('--ffmpeg', type=Path)
    parser.add_argument('--ffprobe', type=Path)
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10:
        raise ValueError('Review repetitions must be between 1 and 10')
    directory = args.input_dir.resolve()
    manifest_path = directory / 'RenderManifest.json'
    manifest_hash = sha256(manifest_path)
    initial = json.loads(manifest_path.read_text(encoding='utf8'))
    provider = (args.provider or initial.get('provider') or '').strip()
    if not provider:
        raise ValueError('RenderManifest provider or explicit --provider is required')
    if (len(initial.get('clips', [])) != 1 or initial['clips'][0]['name'] != 'Cast'
            or set(case['name'] for case in initial.get('cases', [])) != set(CASES)):
        raise ValueError('Expected one Cast clip and Original/Quick/Heavy/Ritual cases')
    for record in initial.get('frames', []):
        expected = int(initial['clips'][0]['frame_start']) + int(record['index'])
        if record['source_frame'] != expected:
            raise ValueError('source_frame must be the evaluation index; use blender_frame for authored subframes')
    manifest, fps, cases, paths, png_count = verify(directory, provider, list(CASES), LABELS)
    clip = manifest['clips'][0]
    count = int(clip['frame_count'])
    total = count * args.repeats
    _, size = layout(4)
    ffmpeg, ffprobe = executable('ffmpeg', args.ffmpeg), executable('ffprobe', args.ffprobe)
    metadata = [';FFMETADATA1', 'title=Маг: варианты каста на основе Mixamo',
                'comment=' + escape_metadata(provider)]
    chapters = []
    for repetition in range(1, args.repeats + 1):
        start = (repetition-1) * count
        title = 'Повтор ' + str(repetition)
        metadata += ['[CHAPTER]', f'TIMEBASE={fps.denominator}/{fps.numerator}',
                     f'START={start}', f'END={start+count}', 'title=' + title]
        chapters.append(dict(title=title, start_frame=start, frame_count=count,
                             start_seconds=start / float(fps), duration_seconds=count / float(fps)))
    metadata_path = directory / (BASENAME + '.ffmetadata')
    metadata_path.write_text('\n'.join(metadata) + '\n', encoding='utf8')
    video, log = directory / (BASENAME + '.mp4'), directory / (BASENAME + '.Encoder.log')
    rate = f'{fps.numerator}/{fps.denominator}'
    command = [ffmpeg, '-y', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
               '-s:v', f'{size[0]}x{size[1]}', '-r', rate, '-i', 'pipe:0',
               '-f', 'ffmetadata', '-i', str(metadata_path), '-map', '0:v:0',
               '-map_metadata', '1', '-map_chapters', '1', '-an', '-c:v', 'libx264',
               '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p',
               '-fps_mode', 'passthrough', '-movflags', '+faststart', str(video)]
    with log.open('wb') as errors:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=errors)
        try:
            for repetition in range(1, args.repeats + 1):
                for index in range(count):
                    process.stdin.write(composite(clip, index, repetition, args.repeats, cases, paths, fps).tobytes())
            process.stdin.close()
            if process.wait():
                raise RuntimeError('Encoder failed: ' + log.read_text(encoding='utf8', errors='replace'))
        except Exception:
            if process.poll() is None:
                process.kill()
            process.wait()
            raise
    evidence = probe(ffprobe, video, size, fps, total)
    for case in cases:
        for source in case['sources']:
            if sha256(Path(source['path'])) != source['sha256']:
                raise ValueError('Source changed during composition: ' + source['path'])
    if sha256(manifest_path) != manifest_hash:
        raise ValueError('RenderManifest changed during composition')
    poster = directory / (BASENAME + '.png')
    composite(clip, int((count-1)*.45), 1, args.repeats, cases, paths, fps).save(poster)
    report = dict(status='complete', video=str(video), video_sha256=sha256(video),
                  poster=str(poster), poster_sha256=sha256(poster), size=list(size),
                  source_view_size=manifest['size'], layout='two by two', camera='ThreeQuarter',
                  provider=provider, scope=manifest.get('scope', 'Pure Blender preview; Mixamo-derived authored cast variants'),
                  cases=cases, case_metadata=manifest['cases'], source_clip=clip,
                  rendered_png_count=png_count, source_frames_per_case=count,
                  review_repetitions=args.repeats, frame_count=total, fps=float(fps), fps_fraction=rate,
                  duration_seconds=total / float(fps), one_pass_seconds=count / float(fps),
                  playback_speed=1, video_frame_interpolation=False,
                  repeat_method='Replay every actual rendered frame in the same order at unchanged FPS',
                  frame_index_semantics='source_frame=evaluation index; blender_frame=authored scene subframe',
                  source_files_unchanged=True, render_manifest=str(manifest_path), render_manifest_sha256=manifest_hash,
                  limitations=manifest.get('limitations', []), chapters=chapters, probe=evidence)
    (directory / (BASENAME + '.Video.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(str(video), flush=True)


if __name__ == '__main__':
    main()
