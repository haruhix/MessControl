"""Compose the restored wizard casts from actual Native gameplay PNGs at native FPS.

Reads a complete capture_nut_dynamics_nightly RenderManifest. Source images stay
640x480; a 92px caption is added above them. Importing this module does nothing.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

from BuildWizardDynamicsComparisonVideo import (
    escape_metadata, fitted_font, probe, rate_from_manifest, resolve_path,
    sha256, source_records,
)

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Artifacts/WizardMixamoIntegration'
BASENAME = 'Wizard_Mixamo_Gameplay'
SIZE = (640, 572)
LABELS = {'WizardCast': 'Огненный орех', 'WizardSummon': 'Призыв',
          'WizardRain': 'Ореховый дождь', 'WizardTeleport': 'Телепорт · база Rain'}


def executable(name, explicit):
    candidate = str(explicit) if explicit else shutil.which(name)
    candidate = candidate or str(Path('C:/ffmpeg') / (name + '.exe'))
    resolved = shutil.which(candidate) or candidate
    if not Path(resolved).is_file():
        raise FileNotFoundError('Executable unavailable; pass --' + name + ': ' + resolved)
    return resolved


def verify(directory):
    manifest_path = directory / 'RenderManifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf8'))
    if manifest.get('status') != 'complete' or tuple(manifest['size']) != (640, 480):
        raise ValueError('Expected a complete actual 640x480 gameplay capture')
    cases = [case for case in manifest['cases'] if case['name'] == 'Native']
    if len(cases) != 1:
        raise ValueError('Expected exactly one Native case')
    clips = manifest['clips']
    names = [clip['name'] for clip in clips]
    if (not clips or len(names) != len(set(names)) or any(name not in LABELS for name in names)
            or not {'WizardCast', 'WizardSummon', 'WizardRain'}.issubset(names)):
        raise ValueError('Expected Cast, Summon, Rain, and optionally Teleport')
    fps = rate_from_manifest(manifest)
    sources = source_records(directory, cases[0].get('sources', manifest.get('sources', [])))
    if not sources:
        raise ValueError('Saved package SHA provenance is required')
    records = {}
    for record in manifest['frames']:
        if record['case'] != 'Native':
            continue
        key = (record['clip'], int(record['index']))
        if key in records:
            raise ValueError('Duplicate Native frame: ' + str(key))
        records[key] = record
    case_directory = resolve_path(directory, cases[0].get('directory', 'Native'))
    paths, expected_keys = {}, set()
    for clip in clips:
        count, start = int(clip['frame_count']), int(clip['frame_start'])
        if (count <= 0 or int(clip['frame_end']) - start + 1 != count
                or clip.get('source_kind') != 'Actual current UE gameplay engine ticks'):
            raise ValueError('Incomplete/non-gameplay source timeline: ' + clip['name'])
        folder = (case_directory / clip['name']).resolve()
        if not folder.is_relative_to(directory):
            raise ValueError('Frame directory leaves the input directory')
        pngs = sorted(folder.glob('*.png'))
        if [path.name for path in pngs] != ['frame_%04d.png' % index for index in range(count)]:
            raise ValueError('Nonconsecutive source PNGs: ' + str(folder))
        for index, path in enumerate(pngs):
            key = (clip['name'], index)
            expected_keys.add(key)
            record = records.get(key)
            if (not record or int(record['source_frame']) != start + index
                    or resolve_path(directory, record['path']) != path
                    or sha256(path) != record['sha256']):
                raise ValueError('Source frame path/index/SHA differs: ' + str(key))
            with Image.open(path) as image:
                if image.size != (640, 480):
                    raise ValueError('Source image dimensions differ: ' + str(path))
            paths[key] = path
    if set(records) != expected_keys:
        raise ValueError('Unexpected Native frame records')
    capture_path = directory / 'Capture.json'
    capture = json.loads(capture_path.read_text(encoding='utf8'))
    if (capture.get('status') != 'complete' or capture.get('restore_errors')
            or capture.get('package_sources_unchanged') is not True):
        raise ValueError('Capture is incomplete or source/settings restoration failed')
    return manifest, fps, sources, paths, capture, sha256(manifest_path), sha256(capture_path)


def composite(clip, index, ordinal, total, paths, fps):
    canvas = Image.new('RGB', SIZE, '#17212d')
    draw = ImageDraw.Draw(canvas)
    title = 'Маг · Mixamo / оригинал · игровой просмотр'
    label = f"{ordinal}/{total} · {LABELS[clip['name']]}"
    timeline = (int(clip['frame_count']) - 1) / float(fps)
    timing = f'Native · 1× · {float(fps):.6g} кадр/с · {index / float(fps):.2f} / {timeline:.2f} с'
    for y, text, size, bold in ((8, title, 23, True), (39, label, 22, True), (68, timing, 17, False)):
        draw.text((16, y), text, font=fitted_font(draw, text, size, width=608, bold=bold),
                  fill='#f1f4f8' if bold else '#becbda')
    with Image.open(paths[(clip['name'], index)]) as image:
        canvas.paste(image.convert('RGB'), (0, 92))
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=OUTPUT)
    parser.add_argument('--ffmpeg', type=Path)
    parser.add_argument('--ffprobe', type=Path)
    args = parser.parse_args()
    directory = args.input_dir.resolve()
    manifest, fps, sources, paths, capture, manifest_hash, capture_hash = verify(directory)
    ffmpeg, ffprobe = executable('ffmpeg', args.ffmpeg), executable('ffprobe', args.ffprobe)
    chapters, cursor = [], 0
    metadata = [';FFMETADATA1', 'title=Маг: оригинальные заклинания в игре']
    for clip in manifest['clips']:
        count = int(clip['frame_count'])
        label = LABELS[clip['name']]
        note = str(clip.get('note') or '').strip()
        metadata += ['[CHAPTER]', f'TIMEBASE={fps.denominator}/{fps.numerator}',
                     f'START={cursor}', f'END={cursor + count}', 'title=' + escape_metadata(label)]
        if note:
            metadata.append('comment=' + escape_metadata(note))
        chapters.append(dict(name=clip['name'], title=label, note=note or None,
                             start_frame=cursor, frame_count=count,
                             start_seconds=cursor / float(fps), duration_seconds=count / float(fps)))
        cursor += count
    metadata_path = directory / (BASENAME + '.ffmetadata')
    metadata_path.write_text('\n'.join(metadata) + '\n', encoding='utf8')
    video = directory / (BASENAME + '.mp4')
    log = directory / (BASENAME + '.Encoder.log')
    rate = f'{fps.numerator}/{fps.denominator}'
    command = [ffmpeg, '-y', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
               '-s:v', '640x572', '-r', rate, '-i', 'pipe:0', '-f', 'ffmetadata', '-i', str(metadata_path),
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
            process.kill()
            process.wait()
            raise
    evidence = probe(ffprobe, video, SIZE, fps, cursor)
    for source in sources:
        if sha256(Path(source['path'])) != source['sha256']:
            raise ValueError('Saved source changed during composition: ' + source['path'])
    if sha256(directory / 'RenderManifest.json') != manifest_hash or sha256(directory / 'Capture.json') != capture_hash:
        raise ValueError('Capture evidence changed during composition')
    first = manifest['clips'][0]
    poster = directory / (BASENAME + '.png')
    composite(first, int((int(first['frame_count']) - 1) * .45), 1, len(manifest['clips']), paths, fps).save(poster)
    report = dict(status='complete', video=str(video), video_sha256=sha256(video),
                  poster=str(poster), poster_sha256=sha256(poster), size=list(SIZE),
                  source_view_size=[640, 480], case='Native', frame_count=cursor,
                  fps=float(fps), fps_fraction=rate, duration_seconds=cursor / float(fps),
                  playback_speed=1, interpolated_frames=False, complete_captured_timelines=True,
                  scope=manifest.get('scope'), chapters=chapters, sources=sources,
                  render_manifest=str(directory / 'RenderManifest.json'), render_manifest_sha256=manifest_hash,
                  capture=str(directory / 'Capture.json'), capture_sha256=capture_hash,
                  gameplay_failed_runs=capture.get('gameplay_failed_runs'), restore_errors=capture.get('restore_errors'),
                  limitations=capture.get('limitations', []), probe=evidence)
    (directory / (BASENAME + '.Video.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(str(video), flush=True)


if __name__ == '__main__':
    main()
