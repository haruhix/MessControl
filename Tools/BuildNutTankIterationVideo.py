"""Compose two before/after Tank videos from actual consecutive 60 Hz PIE PNGs.

Motion contains Roll then Charge; ShieldBreak remains a separate short video.
No interpolation, optical flow, repeated frames, slow replay, or source FPS conversion.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FFMPEG = Path('C:/ffmpeg/ffmpeg.exe')
FFPROBE = FFMPEG.with_name('ffprobe.exe')
FPS = 60


def png_size(path):
    with path.open('rb') as file:
        header = file.read(24)
    if header[:8] != b'\x89PNG\r\n\x1a\n' or header[12:16] != b'IHDR':
        raise ValueError('Invalid PNG: ' + str(path))
    return struct.unpack('>II', header[16:24])


def read_capture(path):
    path = path.resolve()
    report_path = path / 'Capture.json'
    report = json.loads(report_path.read_text(encoding='utf8'))
    if report.get('status') != 'complete' or report.get('video_fps') != FPS or report.get('game_fps') != FPS:
        raise ValueError('Expected a complete genuine 60 Hz capture: ' + str(report_path))
    cases = {item['name']: item for item in report['scenarios']}
    dimensions = None
    cadence = {}
    for name in ('Roll', 'Charge', 'ShieldBreak'):
        case = cases[name]
        count = int(case['frames'])
        if count != int(case['expected_frames']):
            raise ValueError('Incomplete frame count: ' + name)
        paths = sorted((path / name).glob('*.png'))
        expected = ['%04d.png' % index for index in range(1, count+1)]
        if [item.name for item in paths] != expected:
            raise ValueError('Missing, additional, or nonconsecutive frames: ' + str(path / name))
        for image in paths:
            size = png_size(image)
            if dimensions and size != dimensions:
                raise ValueError('Mixed PNG dimensions: ' + str(image))
            dimensions = size
        samples = [item for item in report['frames'] if item['scenario'] == name]
        if len(samples) != count or [item['frame'] for item in samples] != list(range(1, count+1)):
            raise ValueError('Missing per-frame engine evidence: ' + name)
        steps = [b['game_time']-a['game_time'] for a, b in zip(samples, samples[1:])]
        if any(abs(step-1/FPS) > .001 for step in steps):
            raise ValueError('Nonconsecutive engine frames: ' + name)
        if any(abs(item['game_dt']-1/FPS) > .001 for item in samples):
            raise ValueError('Wrong engine frame rate: ' + name)
        if len({item['boss'] for item in samples}) != 1:
            raise ValueError('Boss replaced mid-scenario: ' + name)
        cadence[name] = dict(frames=count, minimum_step_seconds=min(steps), maximum_step_seconds=max(steps))
    return dict(path=path, report=report, cases=cases, dimensions=dimensions, cadence=cadence,
                report_sha256=hashlib.sha256(report_path.read_bytes()).hexdigest())


def encode(before, after, names, output, title):
    inputs, filters, stages = [], [], []
    frame_count = sum(int(after['cases'][name]['frames']) for name in names)
    for stage, name in enumerate(names):
        for version, source in enumerate((before, after)):
            index = stage*2+version
            inputs.extend(['-framerate', str(FPS), '-start_number', '1', '-i',
                           str(source['path'] / name / '%04d.png')])
            label = ('Before' if version == 0 else 'After') + ' | ' + name + ' | actual PIE 60 fps'
            filters.append(f'[{index}:v]setpts=PTS-STARTPTS,'
                f'drawbox=x=0:y=0:w=iw:h=46:color=black@0.75:t=fill,'
                f'drawtext=fontfile=\'C\\:/Windows/Fonts/arial.ttf\':text=\'{label}\':'
                f'fontcolor=white:fontsize=24:x=16:y=11[v{stage}_{version}]')
        filters.append(f'[v{stage}_0][v{stage}_1]hstack=inputs=2:shortest=1[stage{stage}]')
        stages.append(f'[stage{stage}]')
    if len(stages) == 1:
        filters.append(stages[0] + 'null[out]')
    else:
        filters.append(''.join(stages) + f'concat=n={len(stages)}:v=1:a=0[out]')
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error', *inputs,
        '-filter_complex', ';'.join(filters), '-map', '[out]', '-an', '-c:v', 'libx264',
        '-preset', 'veryfast', '-crf', '19', '-pix_fmt', 'yuv420p', '-fps_mode', 'passthrough',
        '-movflags', '+faststart', str(output)], check=True)
    probe = subprocess.run([str(FFPROBE), '-v', 'error', '-select_streams', 'v:0', '-count_frames',
        '-show_entries', 'stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames,duration',
        '-of', 'json', str(output)], check=True, capture_output=True, text=True)
    stream = json.loads(probe.stdout)['streams'][0]
    expected_size = (after['dimensions'][0]*2, after['dimensions'][1])
    if ((int(stream['width']), int(stream['height'])) != expected_size
            or stream['r_frame_rate'] != '60/1' or int(stream['nb_read_frames']) != frame_count
            or abs(float(stream['duration'])-frame_count/FPS) > .02):
        raise ValueError('Encoded frame count/rate/duration mismatch: ' + json.dumps(stream))
    poster = output.with_suffix('.png')
    poster_time = 1.1
    if names[0] == 'Roll':
        turn = after['cases']['Roll'].get('turn_snapshot')
        if turn:
            poster_time = (int(Path(turn).stem)-1)/FPS
        else:
            poster_time = min(3., frame_count/FPS-.1)
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error', '-ss', str(poster_time),
                    '-i', str(output), '-frames:v', '1', str(poster)], check=True)
    return dict(title=title, video=str(output), poster=str(poster), scenarios=names,
                source_frames=frame_count, source_fps=FPS, genuine_source_frames=True,
                interpolation=False, playback_speed=1, stream=stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=ROOT / 'Artifacts/TankCartoonV4_20261010')
    parser.add_argument('--before', type=Path, help='Override input-dir/Before')
    parser.add_argument('--after', type=Path, help='Override input-dir/After')
    args = parser.parse_args()
    output_dir = args.input_dir.resolve()
    before = read_capture(args.before or output_dir / 'Before')
    after = read_capture(args.after or output_dir / 'After')
    if before['dimensions'] != after['dimensions']:
        raise ValueError('Before/after capture dimensions differ')
    for name in ('Roll', 'Charge', 'ShieldBreak'):
        if before['cases'][name]['frames'] != after['cases'][name]['frames']:
            raise ValueError('Before/after frame counts differ: ' + name)
    output_dir.mkdir(parents=True, exist_ok=True)
    videos = [encode(before, after, ['Roll', 'Charge'], output_dir / 'Tank_Motion_60fps.mp4', 'Roll and Charge'),
              encode(before, after, ['ShieldBreak'], output_dir / 'Tank_ShieldBreak_60fps.mp4', 'Shield break and reaction')]
    provenance = []
    for source in (before, after):
        report = source['report']
        provenance.append(dict(capture_json=str(source['path'] / 'Capture.json'), sha256=source['report_sha256'],
            map=report.get('map'), version=report.get('version'), game_fps=report['game_fps'],
            original_input_fps=report['video_fps'], camera=report.get('camera'), lighting=report.get('lighting'),
            scenarios=report['scenarios'], cadence=source['cadence'], restoration=report.get('restoration'),
            restore_errors=report.get('restore_errors'), retained_fixture_note=report.get('retained_fixture_note')))
    (output_dir / 'Video.json').write_text(json.dumps(dict(videos=videos, provenance=provenance,
        scope='Actual solo PIE gameplay; identical world camera angle and fill. Native gameplay/asset differences are preserved.'),
        indent=2), encoding='utf8')
    for video in videos:
        print(video['video'])


if __name__ == '__main__':
    main()
