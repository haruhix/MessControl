"""Compose every original Blender action in RenderManifest order at its native FPS.

Writes Wizard_Original_AllAnimations.mp4, a poster, chapters and VideoManifest.json.
Source art and rendered frames are only read. This module does nothing on import.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from fractions import Fraction
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Artifacts/WizardOriginal'
SIZE = (1280, 720)
VIEWS = ('ThreeQuarter', 'Side')
FFMPEG = Path('C:/ffmpeg/ffmpeg.exe')
FFPROBE = Path('C:/ffmpeg/ffprobe.exe')
NAMES = {'idle': 'Ожидание', 'walk': 'Ходьба', 'walking': 'Ходьба',
         'run': 'Бег', 'running': 'Бег', 'melee': 'Ближняя атака',
         'attack': 'Атака', 'cast': 'Каст', 'summon': 'Призыв',
         'hit': 'Реакция на удар', 'death': 'Смерть', 'dying': 'Смерть',
         'firenut': 'Огненный орех', 'nutrain': 'Ореховый дождь',
         'castsummon': 'Призыв', 'nut_w_firenut': 'Огненный орех',
         'nut_w_nutrain': 'Ореховый дождь', 'nut_w_castsummon': 'Призыв'}


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def native_fps(manifest):
    value = manifest.get('fps', 30)
    if isinstance(value, dict):
        rate = Fraction(int(value['numerator']), int(value['denominator']))
    else:
        rate = Fraction(str(value)).limit_denominator(1000000)
    rate /= Fraction(str(manifest.get('fps_base', 1)))
    if rate <= 0:
        raise ValueError('Native FPS must be positive')
    return rate


def verify_renders(directory):
    manifest = json.loads((directory / 'RenderManifest.json').read_text(encoding='utf8'))
    if manifest.get('status') != 'complete':
        raise ValueError('Expected a complete Blender render')
    fps = native_fps(manifest)
    clips = manifest.get('clips', [])
    names = [clip['name'] for clip in clips]
    if not clips or len(names) != len(set(names)):
        raise ValueError('Expected one or more uniquely named clips')
    source_hash = manifest['source_sha256']
    if manifest.get('source_sha256_after') != source_hash:
        raise ValueError('The renderer did not preserve its source blend')
    if sha256(Path(manifest['source'])) != source_hash:
        raise ValueError('Source revision differs from the rendered preview')
    size = tuple(manifest['size'])
    if size != (640, 480):
        raise ValueError('Expected two 640x480 source views')
    records = {}
    for record in manifest['frames']:
        records.setdefault((record['clip'], record['view']), []).append(record)
    expected_keys = {(name, view) for name in names for view in VIEWS}
    if set(records) != expected_keys:
        raise ValueError('Unexpected or missing clip/view records')
    total_pngs = 0
    for clip in clips:
        count = int(clip['frame_count'])
        start = int(clip['frame_start'])
        if count <= 0 or count != int(clip['rendered_frame_count']):
            raise ValueError('Incomplete rendered timeline: ' + clip['name'])
        if 'frame_end' in clip and int(clip['frame_end']) - start + 1 != count:
            raise ValueError('Source frame range was shortened: ' + clip['name'])
        for view in VIEWS:
            frames = records[(clip['name'], view)]
            if [frame['index'] for frame in frames] != list(range(count)):
                raise ValueError('Nonconsecutive frame manifest: ' + clip['name'] + ' ' + view)
            folder = directory / clip['name'] / view
            if not folder.resolve().is_relative_to(directory):
                raise ValueError('Frame directory leaves the input directory')
            actual = sorted(folder.glob('*.png'))
            expected = ['frame_%04d.png' % index for index in range(count)]
            if [path.name for path in actual] != expected:
                raise ValueError('Nonconsecutive PNGs: ' + str(folder))
            for record, png in zip(frames, actual):
                if record['source_frame'] != start + record['index']:
                    raise ValueError('Source timeline was resampled')
                if sha256(png) != record['sha256']:
                    raise ValueError('Rendered frame SHA256 differs: ' + str(png))
                with Image.open(png) as image:
                    if image.size != size:
                        raise ValueError('Wrong source image size: ' + str(png))
            total_pngs += count
    if ('rendered_png_count' in manifest
            and int(manifest['rendered_png_count']) != total_pngs):
        raise ValueError('Rendered PNG count differs from the manifest')
    return manifest, fps, total_pngs


def fonts():
    return {name: ImageFont.truetype('C:/Windows/Fonts/' + filename, size)
            for name, filename, size in (('title', 'seguisb.ttf', 34),
                                        ('text', 'segoeui.ttf', 22),
                                        ('label', 'seguisb.ttf', 22),
                                        ('small', 'segoeui.ttf', 19))}


def action_name(clip):
    return str(clip.get('action_name', clip['name']))


def human_name(clip, labels):
    original = action_name(clip)
    return labels.get(original, labels.get(clip['name'],
                      NAMES.get(original.casefold(), NAMES.get(clip['name'].casefold(), ''))))


def fit_font(draw, text, filename, maximum, minimum=12, width=1232):
    for size in range(maximum, minimum - 1, -1):
        font = ImageFont.truetype('C:/Windows/Fonts/' + filename, size)
        if draw.textbbox((0, 0), text, font=font)[2] <= width:
            return font
    raise ValueError('Action name is too long to fit without truncation: ' + text)


def composite(directory, clip, index, ordinal, total, typefaces, fps, labels, version):
    canvas = Image.new('RGB', SIZE, '#17212d')
    draw = ImageDraw.Draw(canvas)
    title = f"Оригинальный маг · {ordinal}/{total} · {action_name(clip)}"
    draw.text((24, 16), title, font=fit_font(draw, title, 'seguisb.ttf', 34), fill='#f1f4f8')
    fps_text = format(float(fps), '.6g')
    draw.text((24, 63), f'Исходные анимации · полный темп 1× · {fps_text} кадр/с',
              font=typefaces['text'], fill='#becbda')
    draw.text((24, 107), 'Спереди, 3/4', font=typefaces['label'], fill='#e4ebf3')
    draw.text((664, 107), 'Сбоку', font=typefaces['label'], fill='#e4ebf3')
    for column, view in enumerate(VIEWS):
        with Image.open(directory / clip['name'] / view / ('frame_%04d.png' % index)) as source:
            canvas.paste(source.convert('RGB'), (column * 640, 144))
    draw = ImageDraw.Draw(canvas)
    draw.line((640, 144, 640, 624), fill='#344355', width=2)
    seconds = index / float(fps)
    timeline = (int(clip['frame_count']) - 1) / float(fps)
    draw.text((24, 641), f'Полный клип: {seconds:.2f} / {timeline:.2f} с',
              font=typefaces['label'], fill='#f1f4f8')
    label = human_name(clip, labels)
    caption = 'Оригинальные анимации' + (' · ' + label if label else '')
    if version and version != 'Original':
        caption += ' · ' + version
    draw.text((24, 675), caption,
              font=fit_font(draw, caption, 'segoeui.ttf', 19, width=752), fill='#becbda')
    draw.line((800, 687, 1240, 687), fill='#425971', width=5)
    progress = 800 + 440 * index / max(1, int(clip['frame_count']) - 1)
    draw.ellipse((progress - 6, 681, progress + 6, 693), fill='#f1f4f8')
    return canvas


def metadata_escape(value):
    return (str(value).replace('\\', '\\\\').replace('\n', ' ')
            .replace('\r', ' ').replace('=', '\\=').replace(';', '\\;').replace('#', '\\#'))


def chapter_metadata(directory, clips, fps, labels):
    chapters, cursor = [], 0
    text = [';FFMETADATA1', 'title=Оригинальный маг — все исходные анимации']
    timebase = f'{fps.denominator}/{fps.numerator}'
    for clip in clips:
        count = int(clip['frame_count'])
        original = action_name(clip)
        label = human_name(clip, labels)
        title = original + (' — ' + label if label else '')
        text += ['[CHAPTER]', 'TIMEBASE=' + timebase, 'START=' + str(cursor),
                 'END=' + str(cursor + count), 'title=' + metadata_escape(title)]
        chapters.append(dict(name=clip['name'], title=title, action=original,
                             start_frame=cursor, frames=count,
                             start_seconds=cursor / float(fps), duration_seconds=count / float(fps),
                             source_frame_start=clip['frame_start'],
                             source_frame_end=int(clip['frame_start']) + count - 1,
                             source_duration_seconds=clip.get('source_duration_seconds', (count - 1) / float(fps))))
        cursor += count
    path = directory / 'Wizard_Original_AllAnimations.ffmetadata'
    path.write_text('\n'.join(text) + '\n', encoding='utf8')
    return path, chapters, cursor


def probe(ffprobe, video, expected, fps):
    result = subprocess.run([str(ffprobe), '-v', 'error', '-count_frames', '-select_streams', 'v:0',
        '-show_entries', 'stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames,duration',
        '-show_entries', 'format=duration', '-of', 'json', str(video)],
        check=True, capture_output=True, text=True)
    evidence = json.loads(result.stdout)
    stream = evidence['streams'][0]
    if ((stream['width'], stream['height']) != SIZE
            or Fraction(stream['r_frame_rate']) != fps
            or Fraction(stream['avg_frame_rate']) != fps
            or int(stream['nb_read_frames']) != expected
            or abs(float(stream.get('duration', evidence['format']['duration'])) - expected / float(fps)) > .02):
        raise ValueError('Encoded timeline does not match source frames: ' + str(video))
    return evidence


def encode(directory, clips, typefaces, ffmpeg, ffprobe, fps, labels, version):
    metadata, chapters, count = chapter_metadata(directory, clips, fps, labels)
    basename = 'Wizard_Original_AllAnimations'
    video, log = directory / (basename + '.mp4'), directory / (basename + '.Encoder.log')
    rate = f'{fps.numerator}/{fps.denominator}'
    command = [str(ffmpeg), '-y', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo',
               '-pix_fmt', 'rgb24', '-s:v', '1280x720', '-r', rate, '-i', 'pipe:0',
               '-f', 'ffmetadata', '-i', str(metadata), '-map', '0:v:0',
               '-map_metadata', '1', '-map_chapters', '1', '-an', '-c:v', 'libx264',
               '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p',
               '-fps_mode', 'passthrough', '-movflags', '+faststart', str(video)]
    with log.open('wb') as errors:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=errors)
        try:
            for ordinal, clip in enumerate(clips, 1):
                for index in range(int(clip['frame_count'])):
                    process.stdin.write(composite(directory, clip, index, ordinal, len(clips),
                                                  typefaces, fps, labels, version).tobytes())
            process.stdin.close()
            if process.wait():
                raise RuntimeError('Video encoder failed: ' + log.read_text(encoding='utf8', errors='replace'))
        except Exception:
            process.kill()
            process.wait()
            raise
    evidence = probe(ffprobe, video, count, fps)
    poster_clip = next((clip for clip in clips if 'cast' in action_name(clip).casefold()), clips[0])
    poster_index = int((int(poster_clip['frame_count']) - 1) * .45)
    poster = directory / (basename + '.png')
    composite(directory, poster_clip, poster_index, clips.index(poster_clip) + 1, len(clips),
              typefaces, fps, labels, version).save(poster)
    return dict(video=str(video), video_sha256=sha256(video), poster=str(poster),
                poster_sha256=sha256(poster), fps=float(fps), fps_fraction=rate,
                size=list(SIZE), frame_count=count, duration_seconds=count / float(fps),
                chapters=chapters, probe=evidence, playback_speed=1,
                interpolated_frames=False, complete_source_timelines=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=OUTPUT)
    parser.add_argument('--version', default='Original', help='Optional descriptive version label')
    parser.add_argument('--labels-json', type=Path, help='Optional action/clip name to Russian label mapping')
    parser.add_argument('--ffmpeg', type=Path, default=FFMPEG)
    parser.add_argument('--ffprobe', type=Path, default=FFPROBE)
    args = parser.parse_args()
    directory = args.input_dir.resolve()
    labels = json.loads(args.labels_json.read_text(encoding='utf8')) if args.labels_json else {}
    if not isinstance(labels, dict) or any(not isinstance(value, str) for value in labels.values()):
        raise ValueError('Labels must be a JSON object of string values')
    manifest, fps, rendered_png_count = verify_renders(directory)
    video = encode(directory, manifest['clips'], fonts(), args.ffmpeg, args.ffprobe,
                   fps, labels, args.version)
    if sha256(Path(manifest['source'])) != manifest['source_sha256']:
        raise ValueError('Source revision changed during video composition')
    render_path = directory / 'RenderManifest.json'
    report = dict(status='complete', version=args.version, source_version=manifest.get('version'),
                  source=manifest['source'], source_sha256=manifest['source_sha256'], source_unchanged=True,
                  render_manifest=str(render_path), render_manifest_sha256=sha256(render_path),
                  rendered_png_count=rendered_png_count, clip_count=len(manifest['clips']),
                  view_size=manifest['size'], views=list(VIEWS), fps=float(fps),
                  fps_fraction=f'{fps.numerator}/{fps.denominator}',
                  scope='Every original Blender action in manifest order at native source speed',
                  videos=[video])
    (directory / 'VideoManifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(video['video'], flush=True)


if __name__ == '__main__':
    main()
