"""Compose the V5 solo PIE Tank/Wizard montage from genuine 30 Hz source frames."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from BuildNutTankIterationVideo import FFMPEG, FFPROBE, png_size

ROOT = Path(__file__).resolve().parents[1]
FPS = 30
GROUPS = [('Tank_Melee_30fps', ['TankMelee']),
          ('Wizard_Casts_30fps', ['WizardFireball', 'WizardSummon', 'WizardRain', 'WizardTeleport'])]
CAPTIONS = dict(TankMelee='Tank | Melee', WizardFireball='Wizard | Fireball',
                WizardSummon='Wizard | Summon', WizardRain='Wizard | Nut rain', WizardTeleport='Wizard | Teleport')


def read_capture(directory, groups=GROUPS):
    report_path = directory/'Capture.json'
    report = json.loads(report_path.read_text(encoding='utf8'))
    if report.get('status') != 'complete' or report.get('game_fps') != FPS or report.get('video_fps') != FPS:
        raise ValueError('Expected a complete genuine solo PIE 30 Hz capture')
    scenarios = {item['name']: item for item in report['scenarios']}
    dimensions = None
    cadence = {}
    for _, names in groups:
        for name in names:
            scenario = scenarios[name]
            count = int(scenario['frames'])
            if count != int(scenario['expected_frames']):
                raise ValueError('Incomplete frame count: '+name)
            files = sorted((directory/name).glob('*.png'))
            if [item.name for item in files] != ['%04d.png' % i for i in range(1, count+1)]:
                raise ValueError('Missing, additional, or nonconsecutive PNGs: '+name)
            for file in files:
                size = png_size(file)
                if dimensions and size != dimensions:
                    raise ValueError('Mixed image dimensions: '+str(file))
                dimensions = size
            samples = [item for item in report['frames'] if item['scenario'] == name]
            if len(samples) != count or [item['frame'] for item in samples] != list(range(1, count+1)):
                raise ValueError('Missing per-frame engine evidence: '+name)
            steps = [b['game_time']-a['game_time'] for a, b in zip(samples, samples[1:])]
            if any(abs(step-1/FPS) > .001 for step in steps):
                raise ValueError('Nonconsecutive engine frames: '+name)
            if any(abs(item['game_dt']-1/FPS) > .001 for item in samples):
                raise ValueError('Wrong engine frame rate: '+name)
            if len({item['boss'] for item in samples}) != 1:
                raise ValueError('Fixture respawned mid-shot: '+name)
            cadence[name] = dict(frames=count, minimum_step_seconds=min(steps), maximum_step_seconds=max(steps))
    return report, scenarios, dimensions, cadence


def encode(directory, basename, names, scenarios, dimensions):
    output = directory/(basename+'.mp4')
    inputs, filters, stages = [], [], []
    chapter_frame = 0
    chapters = []
    for index, name in enumerate(names):
        inputs.extend(['-framerate', str(FPS), '-start_number', '1', '-i', str(directory/name/'%04d.png')])
        caption = CAPTIONS[name]+' | solo PIE 30 fps'
        filters.append(f'[{index}:v]setpts=PTS-STARTPTS,'
            f'drawbox=x=0:y=0:w=iw:h=38:color=black@0.75:t=fill,'
            f'drawtext=fontfile=\'C\\:/Windows/Fonts/arial.ttf\':text=\'{caption}\':'
            f'fontcolor=white:fontsize=20:x=14:y=9[v{index}]')
        stages.append(f'[v{index}]')
        count = int(scenarios[name]['frames'])
        chapters.append(dict(name=name, start_seconds=chapter_frame/FPS, duration_seconds=count/FPS,
                             frames=count, observed_states=scenarios[name].get('observed_states')))
        chapter_frame += count
    if len(stages) == 1:
        filters.append(stages[0]+'null[out]')
    else:
        filters.append(''.join(stages)+f'concat=n={len(stages)}:v=1:a=0[out]')
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error', *inputs,
        '-filter_complex', ';'.join(filters), '-map', '[out]', '-an', '-c:v', 'libx264',
        '-preset', 'veryfast', '-crf', '18', '-pix_fmt', 'yuv420p', '-fps_mode', 'passthrough',
        '-movflags', '+faststart', str(output)], check=True)
    result = subprocess.run([str(FFPROBE), '-v', 'error', '-select_streams', 'v:0', '-count_frames',
        '-show_entries', 'stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames,duration',
        '-of', 'json', str(output)], check=True, capture_output=True, text=True)
    stream = json.loads(result.stdout)['streams'][0]
    if ((int(stream['width']), int(stream['height'])) != dimensions or stream['r_frame_rate'] != '30/1'
            or int(stream['nb_read_frames']) != chapter_frame
            or abs(float(stream['duration'])-chapter_frame/FPS) > .02):
        raise ValueError('Encoded frame count/rate/duration mismatch: '+json.dumps(stream))
    poster = output.with_suffix('.png')
    poster_time = .30 if names[0] == 'TankMelee' else .75
    subprocess.run([str(FFMPEG), '-y', '-hide_banner', '-loglevel', 'error', '-ss', str(poster_time),
                    '-i', str(output), '-frames:v', '1', str(poster)], check=True)
    return dict(video=str(output), poster=str(poster), stream=stream, chapters=chapters,
        original_input_fps=FPS, genuine_source_frames=True, interpolation=False, playback_speed=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=ROOT/'Artifacts/NutCombatV5_20261010')
    parser.add_argument('--group', choices=('tank', 'wizard', 'all'), default='all',
                        help='Encode a complete selected group; its Capture.json can contain only that group')
    args = parser.parse_args()
    directory = args.input_dir.resolve()
    groups = GROUPS if args.group == 'all' else [GROUPS[0 if args.group == 'tank' else 1]]
    report, scenarios, dimensions, cadence = read_capture(directory, groups)
    videos = [encode(directory, basename, names, scenarios, dimensions) for basename, names in groups]
    report_path = directory/'Capture.json'
    provenance = dict(capture_json=str(report_path), sha256=hashlib.sha256(report_path.read_bytes()).hexdigest(),
        map=report.get('map'), animation_mode=report.get('animation_mode'), game_fps=report['game_fps'],
        original_input_fps=report['video_fps'], camera=report.get('camera'), lighting=report.get('lighting'),
        scenarios=report['scenarios'], cadence=cadence, restoration=report.get('restoration'),
        restore_errors=report.get('restore_errors'))
    (directory/'Video.json').write_text(json.dumps(dict(group=args.group, videos=videos, provenance=provenance,
        scope='Actual solo PIE; current gameplay timing and effects. No multiplayer validation.'), indent=2), encoding='utf8')
    for video in videos:
        print(video['video'])


if __name__ == '__main__':
    main()
