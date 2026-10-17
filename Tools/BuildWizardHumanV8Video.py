"""Compose the nine saved Blender Human V8 clips at original 30 fps.

Outputs a 1280x720 paired-camera overview, chapters, a Cast extract, posters,
and frame-count/SHA256 provenance. No source art or game files are modified.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Artifacts/WizardHumanV8'
FPS = 30
SIZE = (1280, 720)
ORDER = ('Idle', 'Walk', 'Melee', 'Cast', 'HeavyCast', 'Summon', 'Rain', 'Hit', 'Death')
VIEWS = ('ThreeQuarter', 'Side')
NAMES = dict(Idle='Ожидание', Walk='Ходьба', Melee='Ближняя атака', Cast='Каст',
             HeavyCast='Телепорт', Summon='Призыв', Rain='Ореховый дождь',
             Hit='Реакция на удар', Death='Смерть')
FFMPEG = Path('C:/ffmpeg/ffmpeg.exe')
FFPROBE = Path('C:/ffmpeg/ffprobe.exe')


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def verify_renders(directory):
    path = directory / 'RenderManifest.json'
    manifest = json.loads(path.read_text(encoding='utf8'))
    if manifest.get('status') != 'complete' or manifest.get('fps') != FPS:
        raise ValueError('Expected a complete original 30 fps Blender render')
    if tuple(c['name'] for c in manifest['clips']) != ORDER:
        raise ValueError('Expected all nine actions in the declared order')
    if manifest.get('source_sha256_after') != manifest['source_sha256']:
        raise ValueError('The renderer did not preserve its source blend')
    if sha256(Path(manifest['source'])) != manifest['source_sha256']:
        raise ValueError('Source revision differs from the rendered preview')
    size = tuple(manifest['size'])
    if size not in ((640, 480), (480, 480)):
        raise ValueError('Expected 640x480 or 480x480 source views')
    for clip in manifest['clips']:
        count = int(clip['frame_count'])
        if count != int(clip['rendered_frame_count']):
            raise ValueError('Incomplete rendered timeline: ' + clip['name'])
        for view in VIEWS:
            frames = [f for f in manifest['frames'] if f['clip'] == clip['name'] and f['view'] == view]
            if [f['index'] for f in frames] != list(range(count)):
                raise ValueError('Nonconsecutive frame manifest: ' + clip['name'] + ' ' + view)
            expected = ['frame_%04d.png' % index for index in range(count)]
            actual = sorted((directory / clip['name'] / view).glob('*.png'))
            if [p.name for p in actual] != expected:
                raise ValueError('Nonconsecutive PNGs: ' + clip['name'] + ' ' + view)
            for record, png in zip(frames, actual):
                if record['source_frame'] != clip['frame_start'] + record['index']:
                    raise ValueError('Source timeline was resampled')
                if sha256(png) != record['sha256']:
                    raise ValueError('Rendered frame SHA256 differs: ' + str(png))
                with Image.open(png) as image:
                    if image.size != size:
                        raise ValueError('Wrong source image size: ' + str(png))
    return manifest


def fonts():
    return {name: ImageFont.truetype('C:/Windows/Fonts/' + filename, size)
            for name, filename, size in (('title', 'seguisb.ttf', 34),
                                        ('text', 'segoeui.ttf', 22),
                                        ('label', 'seguisb.ttf', 22),
                                        ('small', 'segoeui.ttf', 19))}


def composite(directory, clip, index, typefaces, version):
    canvas = Image.new('RGB', SIZE, '#17212d')
    draw = ImageDraw.Draw(canvas)
    ordinal = ORDER.index(clip['name']) + 1
    draw.text((24, 16), f"Маг {version} · {ordinal}/9 · {NAMES[clip['name']]}",
              font=typefaces['title'], fill='#f1f4f8')
    draw.text((24, 63), 'Предпросмотр в Blender · исходный темп 1× · 30 кадр/с',
              font=typefaces['text'], fill='#becbda')
    draw.text((24, 107), 'Спереди, 3/4', font=typefaces['label'], fill='#e4ebf3')
    draw.text((664, 107), 'Сбоку', font=typefaces['label'], fill='#e4ebf3')
    for column, view in enumerate(VIEWS):
        with Image.open(directory / clip['name'] / view / ('frame_%04d.png' % index)) as source:
            # Fit within each 640x480 panel; never stretch or crop a pose.
            panel = source.convert('RGB')
            x = column * 640 + (640 - panel.width) // 2
            y = 144 + (480 - panel.height) // 2
            canvas.paste(panel, (x, y))
    draw = ImageDraw.Draw(canvas)
    draw.line((640, 144, 640, 624), fill='#344355', width=2)
    seconds = index / FPS
    draw.text((24, 641), f"Полный клип: {seconds:.2f} / {clip['timeline_seconds']:.2f} с",
              font=typefaces['label'], fill='#f1f4f8')
    draw.text((24, 675), f'Все анимации {version} · черновик до интеграции в игру',
              font=typefaces['small'], fill='#becbda')
    draw.line((800, 687, 1240, 687), fill='#425971', width=5)
    progress = 800 + 440 * index / max(1, clip['frame_count'] - 1)
    draw.ellipse((progress - 6, 681, progress + 6, 693), fill='#f1f4f8')
    return canvas


def chapter_metadata(directory, basename, clips, version):
    chapters, cursor = [], 0
    text = [';FFMETADATA1', f'title=Маг {version} — полный обзор анимаций']
    for clip in clips:
        count = int(clip['frame_count'])
        title = NAMES[clip['name']] + ' — ' + version
        text += ['[CHAPTER]', 'TIMEBASE=1/30', 'START=' + str(cursor),
                 'END=' + str(cursor + count), 'title=' + title]
        chapters.append(dict(name=clip['name'], title=title, action=clip['action_name'],
                             start_frame=cursor, frames=count, start_seconds=cursor / FPS,
                             duration_seconds=count / FPS, source_duration_seconds=clip['source_duration_seconds']))
        cursor += count
    path = directory / (basename + '.ffmetadata')
    path.write_text('\n'.join(text) + '\n', encoding='utf8')
    return path, chapters, cursor


def probe(ffprobe, video, expected):
    result = subprocess.run([str(ffprobe), '-v', 'error', '-count_frames', '-select_streams', 'v:0',
        '-show_entries', 'stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames,duration',
        '-show_entries', 'format=duration', '-of', 'json', str(video)],
        check=True, capture_output=True, text=True)
    evidence = json.loads(result.stdout)
    stream = evidence['streams'][0]
    if ((stream['width'], stream['height']) != SIZE or stream['r_frame_rate'] != '30/1'
            or int(stream['nb_read_frames']) != expected
            or abs(float(stream['duration']) - expected / FPS) > .02):
        raise ValueError('Encoded timeline does not match source frames: ' + str(video))
    return evidence


def encode(directory, basename, clips, typefaces, ffmpeg, ffprobe, version):
    metadata, chapters, count = chapter_metadata(directory, basename, clips, version)
    video = directory / (basename + '.mp4')
    log = directory / (basename + '.Encoder.log')
    command = [str(ffmpeg), '-y', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo',
               '-pix_fmt', 'rgb24', '-s:v', '1280x720', '-r', str(FPS), '-i', 'pipe:0',
               '-f', 'ffmetadata', '-i', str(metadata), '-map', '0:v:0',
               '-map_metadata', '1', '-map_chapters', '1', '-an', '-c:v', 'libx264',
               '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p',
               '-fps_mode', 'passthrough', '-movflags', '+faststart', str(video)]
    with log.open('wb') as errors:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=errors)
        try:
            for clip in clips:
                for index in range(int(clip['frame_count'])):
                    process.stdin.write(composite(directory, clip, index, typefaces, version).tobytes())
            process.stdin.close()
            code = process.wait()
            if code:
                raise RuntimeError('Video encoder failed: ' + log.read_text(encoding='utf8', errors='replace'))
        except Exception:
            process.kill()
            process.wait()
            raise
    evidence = probe(ffprobe, video, count)
    poster_clip = next((c for c in clips if c['name'] == 'Cast'), clips[0])
    poster_index = int((poster_clip['frame_count'] - 1) * .45)
    poster = directory / (basename + '.png')
    composite(directory, poster_clip, poster_index, typefaces, version).save(poster)
    return dict(video=str(video), video_sha256=sha256(video), poster=str(poster),
                poster_sha256=sha256(poster), fps=FPS, size=list(SIZE), frame_count=count,
                duration_seconds=count / FPS, chapters=chapters, probe=evidence,
                playback_speed=1, interpolated_frames=False, complete_source_timelines=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=OUTPUT)
    parser.add_argument('--version', default='V8')
    parser.add_argument('--ffmpeg', type=Path, default=FFMPEG)
    parser.add_argument('--ffprobe', type=Path, default=FFPROBE)
    parser.add_argument('--no-cast', action='store_true', help='Skip the separate complete Cast video')
    args = parser.parse_args()
    if not args.version or any(character not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for character in args.version):
        raise ValueError('Version must be a nonempty filename-safe label')
    directory = args.input_dir.resolve()
    manifest = verify_renders(directory)
    typefaces = fonts()
    basename = 'Wizard_Human_' + args.version
    videos = [encode(directory, basename + '_AllAnimations', manifest['clips'],
                     typefaces, args.ffmpeg, args.ffprobe, args.version)]
    if not args.no_cast:
        cast = next(c for c in manifest['clips'] if c['name'] == 'Cast')
        videos.append(encode(directory, basename + '_Cast', [cast],
                             typefaces, args.ffmpeg, args.ffprobe, args.version))
    render_path = directory / 'RenderManifest.json'
    report = dict(status='complete', version=args.version, source_version=manifest['version'], source=manifest['source'],
                  source_sha256=manifest['source_sha256'], source_unchanged=manifest['source_unchanged'],
                  render_manifest=str(render_path), render_manifest_sha256=sha256(render_path),
                  rendered_png_count=manifest['rendered_png_count'],
                  view_size=manifest['size'], views=list(VIEWS), fps=FPS,
                  scope='Full Blender source timelines at 1x; preview before game integration', videos=videos)
    (directory / 'VideoManifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    for video in videos:
        print(video['video'], flush=True)


if __name__ == '__main__':
    main()
