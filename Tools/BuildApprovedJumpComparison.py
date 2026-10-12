"""Compose confirmed StandingWave/JumpWave gameplay into a short comparison.

Reads the solo Combat.json evidence and uses the common genuine-frame encoder.
No Unreal/editor calls, new gameplay, generated frames, or image retouching.
Russian labels report actual confirmed HP loss. The last genuine frame is held
briefly so the result can be read; that presentation hold is recorded in JSON.

Usage:
    python Tools/BuildApprovedJumpComparison.py <path-to-Combat.json>
"""
import argparse
import json
import math
import subprocess
from pathlib import Path

from BuildApprovedMechanicsVideos import encode


def checked_clip(report, clip_name, check_name):
    check = report.get('checks', {}).get(check_name, {})
    if check.get('passed') is not True:
        raise RuntimeError(f'{check_name}: a passed gameplay check is required')
    values = [float(check[name]) for name in ('health_before', 'health_after', 'damage')]
    before, after, damage = values
    if not all(math.isfinite(value) for value in values) or damage < -.001:
        raise RuntimeError(f'{check_name}: invalid confirmed health/damage values')
    if abs(before-after-damage) > .001:
        raise RuntimeError(f'{check_name}: damage differs from observed HP loss')
    clip = report.get('clips', {}).get(clip_name, {})
    if not isinstance(clip, dict) or clip.get('genuine_engine_frames') is not True:
        raise RuntimeError(f'{clip_name}: genuine engine-frame evidence is required')
    frames = clip.get('frames', [])
    if len(frames) < 3:
        raise RuntimeError(f'{clip_name}: at least three recorded frames are required')
    times = [float(frame['time']) for frame in frames]
    if not all(math.isfinite(stamp) for stamp in times):
        raise RuntimeError(f'{clip_name}: non-finite recorded frame timestamp')
    if any(b <= a for a, b in zip(times, times[1:])):
        raise RuntimeError(f'{clip_name}: frame timestamps must increase')
    if clip.get('frame_count', len(frames)) != len(frames):
        raise RuntimeError(f'{clip_name}: frame count differs from recorded evidence')
    return frames, check, max(0., damage)


def filter_path(path):
    # The argument is passed directly to subprocess, without a shell. Escape
    # drive-letter colons for FFmpeg's filter parser, then quote path whitespace.
    return "'"+path.resolve().as_posix().replace('\\', '\\\\').replace(':', '\\:').replace("'", "\\'")+"'"


def loss_label(name, damage):
    value = f'{damage:.2f}'.rstrip('0').rstrip('.')
    if not value:
        value = '0'
    return name+'  |  '+('\u2212'+value if damage > .0001 else '0')+' HP'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--output', type=Path, default=Path('Artifacts/ApprovedMechanics_20261012'))
    parser.add_argument('--name', default='JumpComparison')
    parser.add_argument('--ffmpeg', type=Path, default=Path('C:/ffmpeg/ffmpeg.exe'))
    parser.add_argument('--font', type=Path, default=Path('C:/Windows/Fonts/arial.ttf'))
    parser.add_argument('--hold', type=float, default=1.5,
                        help='Seconds to hold the final genuine frames (default 1.5)')
    args = parser.parse_args()
    if Path(args.name).name != args.name or not args.name or args.name in ('.', '..'):
        raise RuntimeError('--name must be a plain filename stem')
    if not math.isfinite(args.hold) or not 0. <= args.hold <= 5.:
        raise RuntimeError('--hold must be between zero and five seconds')
    if not args.ffmpeg.is_file() or not args.font.is_file():
        raise RuntimeError('FFmpeg executable and a Cyrillic-capable font file are required')
    source_report = args.report.resolve()
    report = json.loads(source_report.read_text(encoding='utf8'))
    standing_frames, standing_check, standing_damage = checked_clip(report, 'StandingWave', 'standing_pain_wave')
    jumping_frames, jumping_check, jumping_damage = checked_clip(report, 'JumpWave', 'jump_pain_wave')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    standing = encode(args.name+'_StandingSource', standing_frames, output, args.ffmpeg)
    jumping = encode(args.name+'_JumpSource', jumping_frames, output, args.ffmpeg)
    # Both source timelines begin with their own actual wave spawn. Show their
    # common recorded interval, rather than inventing motion for a shorter clip.
    duration = min(float(standing['game_duration']), float(jumping['game_duration']))
    if duration <= 0.:
        raise RuntimeError('Source clips contain no positive gameplay interval')
    label_files = [output/(args.name+'_StandingLabel.txt'), output/(args.name+'_JumpLabel.txt')]
    labels = [loss_label('\u0421\u0442\u043e\u044f', standing_damage),
              loss_label('\u041f\u0440\u044b\u0436\u043e\u043a', jumping_damage)]
    for path, label in zip(label_files, labels):
        path.write_text(label, encoding='utf8')
    font = filter_path(args.font)
    def panel(index, label_path, color, name):
        return (f'[{index}:v]setpts=PTS-STARTPTS,'
                'scale=640:480:force_original_aspect_ratio=decrease:flags=lanczos,'
                'pad=640:480:(ow-iw)/2:(oh-ih)/2:color=0x10131a,'
                'pad=640:552:0:72:color=0x10131a,'
                f'drawtext=fontfile={font}:textfile={filter_path(label_path)}:'
                f'expansion=none:fontcolor={color}:fontsize=30:x=(w-tw)/2:y=(72-th)/2[{name}]')
    filters = ';'.join([
        panel(0, label_files[0], '0xff9d91', 'standing'),
        panel(1, label_files[1], '0x9af5cb', 'jumping'),
        ('[standing][jumping]hstack=inputs=2:shortest=1,'
         f'trim=duration={duration:.6f},setpts=PTS-STARTPTS,'
         f'tpad=stop_mode=clone:stop_duration={args.hold:.6f},'
         'drawbox=x=638:y=0:w=4:h=ih:color=0x343c49:t=fill[comparison]')])
    filter_file = output/(args.name+'.filtergraph')
    filter_file.write_text(filters, encoding='utf8')
    video = output/(args.name+'.mp4')
    subprocess.run([str(args.ffmpeg), '-hide_banner', '-loglevel', 'error', '-y',
                    '-i', standing['video'], '-i', jumping['video'],
                    '-filter_complex_script', str(filter_file), '-map', '[comparison]', '-an',
                    '-r', '30', '-c:v', 'libx264', '-preset', 'medium', '-crf', '19',
                    '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(video)], check=True)
    poster = output/(args.name+'.png')
    subprocess.run([str(args.ffmpeg), '-hide_banner', '-loglevel', 'error', '-y',
                    '-ss', str(max(0., duration-.1)), '-i', str(video),
                    '-frames:v', '1', str(poster)], check=True)
    ffprobe = args.ffmpeg.with_name('ffprobe.exe')
    inspected = subprocess.run([str(ffprobe), '-v', 'error', '-show_entries',
                                'format=duration:stream=codec_name,width,height,nb_frames',
                                '-of', 'json', str(video)], capture_output=True, text=True, check=True)
    metadata = dict(video=video.as_posix(), poster=poster.as_posix(),
                    source_report=source_report.as_posix(), source_report_status=report.get('status'),
                    confirmed_checks={'standing_pain_wave': standing_check, 'jump_pain_wave': jumping_check},
                    source_clips={'StandingWave': standing, 'JumpWave': jumping},
                    labels=labels, gameplay_duration=duration, final_frame_hold_seconds=args.hold,
                    presentation='Side by side actual wave recordings, own starts aligned; final frames held for reading',
                    encoded=json.loads(inspected.stdout))
    (output/(args.name+'.json')).write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding='utf8')
    # Windows redirected stdout can use cp1252. File labels/metadata retain UTF-8;
    # ASCII JSON escapes make the CLI result safe in that output encoding.
    print(json.dumps(metadata, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
