"""Encode a genuine 60 fps before/after PIE comparison; never invent frames."""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FFMPEG = Path('C:/ffmpeg/ffmpeg.exe')
FFPROBE = FFMPEG.with_name('ffprobe.exe')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--before', type=Path, default=ROOT / 'Artifacts/TankCartoonV3_20261010_60fps')
    parser.add_argument('--after', type=Path, default=ROOT / 'Artifacts/TankCartoonV3_20261010_60fps_Fixed')
    args = parser.parse_args()
    reports = [json.loads((path / 'Capture.json').read_text(encoding='utf8')) for path in (args.before, args.after)]
    assert all(r['status'] == 'complete' and r['video_fps'] == 60 for r in reports)
    output = args.after / 'Tank_Interpolation_Before_After_60fps.mp4'
    inputs, filters, stages = [], [], []
    cases = [('Melee', 360), ('ShieldHit', 180), ('ShieldBreak', 180)]
    for index, (name, count) in enumerate(cases):
        for version, path in enumerate((args.before, args.after)):
            assert len(list((path / name).glob('*.png'))) == count
            inputs += ['-framerate', '60', '-start_number', '1', '-i', str(path / name / '%04d.png')]
            label = ('Before interpolation fix' if version == 0 else 'After interpolation fix') + ' | ' + name
            filters.append(f'[{index*2+version}:v]trim=start_frame=72:end_frame={count},setpts=PTS-STARTPTS,'
                           f'drawbox=x=0:y=0:w=iw:h=48:color=black@0.75:t=fill,'
                           f'drawtext=fontfile=\'C\\:/Windows/Fonts/arial.ttf\':text=\'{label}\':fontcolor=white:fontsize=24:x=18:y=12[v{index}_{version}]')
        filters.append(f'[v{index}_0][v{index}_1]hstack=inputs=2[stage{index}]')
        stages.append(f'[stage{index}]')
    filters.append(''.join(stages) + 'concat=n=3:v=1:a=0[out]')
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error', *inputs,
                    '-filter_complex', ';'.join(filters), '-map', '[out]', '-an', '-c:v', 'libx264',
                    '-preset', 'veryfast', '-crf', '19', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(output)], check=True)
    result = subprocess.run([str(FFPROBE), '-v', 'error', '-select_streams', 'v:0', '-count_frames',
                             '-show_entries', 'stream=width,height,r_frame_rate,nb_read_frames,duration', '-of', 'json', str(output)],
                            check=True, capture_output=True, text=True)
    stream = json.loads(result.stdout)['streams'][0]
    assert stream['r_frame_rate'] == '60/1' and int(stream['nb_read_frames']) == 504
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error', '-ss', '1.1', '-i', str(output),
                    '-frames:v', '1', str(args.after / 'Tank_Interpolation_Comparison.png')], check=True)
    (args.after / 'Video.json').write_text(json.dumps(dict(video=str(output), stream=stream,
        genuine_source_frames=True, source_fps=60, before=str(args.before), after=str(args.after),
        scope='Actual solo PIE; identical camera and game timing. Animation curves, release spacing and frame cadence repaired.'), indent=2), encoding='utf8')
    print(output)


if __name__ == '__main__':
    main()
