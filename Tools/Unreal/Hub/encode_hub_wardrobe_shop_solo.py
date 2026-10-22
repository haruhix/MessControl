"""Encode the two real timestamped windows from the owned Hub wardrobe/shop check.

Only a passed, stopped, preference-restored solo report is accepted. Reuses the
existing native-capture encoder: H.264 1280x720, 30 fps presentation, source game
intervals preserved, no optical-flow interpolation, acceleration or added audio.
All input media, output, concat manifests and temporary paths remain on E:.
Encoding verifies files/cleanup; root still reviews the actual visuals.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from encode_kenney_snow_powder_capture import encode_clip


CLIPS = ('WardrobeRGB', 'EmptyShop')
# Keep the established capture-report key for backward compatibility; the
# delivered filename names the current UI, which now uses a 2D color palette.
OUTPUT_NAMES = {'WardrobeRGB': 'WardrobePalette', 'EmptyShop': 'EmptyShop'}


def on_e(value):
    result = Path(value).resolve()
    if result.drive.upper() != 'E:':
        raise ValueError('Task inputs and outputs must remain on E: '+str(result))
    return result


def digest(filename):
    with filename.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ffmpeg', default='C:/ffmpeg/ffmpeg.exe')
    parser.add_argument('--ffprobe', default='C:/ffmpeg/ffprobe.exe')
    args = parser.parse_args()
    source = on_e(args.report)
    source_sha = digest(source)
    proof = json.loads(source.read_text(encoding='utf-8-sig'))
    if proof.get('passed') is not True or proof.get('status') != 'passed':
        raise ValueError('Only a completed passed native solo report may be encoded')
    for key in ('owned_pie_stopped', 'restored_settings', 'config_bytes_restored'):
        if proof.get(key) is not True:
            raise ValueError('The solo report must confirm '+key)
    for key in ('original_custom_flag_restored', 'original_saved_rgb_restored'):
        if proof.get('checks', {}).get(key) is not True:
            raise ValueError('Appearance restoration check is missing or failed: '+key)
    if proof.get('preferences_before') != proof.get('preferences_restored'):
        raise ValueError('Original local appearance preferences were not restored exactly')

    # Preflight BOTH clips before creating any output or running ffmpeg.
    windows, hashes = {}, {}
    for name in CLIPS:
        rows = proof.get('clips', {}).get(name, [])
        if len(rows) < 2:
            raise ValueError('Required clip has fewer than two native frames: '+name)
        times = [float(row['time']) for row in rows]
        if any(not math.isfinite(t) for t in times) or any(b <= a for a, b in zip(times, times[1:])):
            raise ValueError('Native game timestamps must be finite and strictly increasing: '+name)
        size = None
        for row in rows:
            frame = on_e(row['file'])
            if not frame.is_file():
                raise ValueError('Native source frame is missing: '+str(frame))
            sha = digest(frame)
            if not row.get('sha256') or sha != row['sha256']:
                raise ValueError('Native frame hash differs from its capture report: '+str(frame))
            with frame.open('rb') as stream:
                header = stream.read(24)
            if len(header) != 24 or not header.startswith(b'\x89PNG\r\n\x1a\n'):
                raise ValueError('Native source is not a valid PNG header: '+str(frame))
            current_size = (int.from_bytes(header[16:20], 'big'), int.from_bytes(header[20:24], 'big'))
            if current_size != (row.get('width'), row.get('height')):
                raise ValueError('Native frame dimensions disagree with the report: '+str(frame))
            if size is not None and current_size != size:
                raise ValueError('Game-window capture dimensions changed inside '+name)
            size = current_size
            hashes[frame] = sha
        windows[name] = rows

    output = on_e(args.output)
    if output.exists() and any(output.iterdir()):
        raise ValueError('Choose a fresh or empty output folder; existing review files are preserved')
    output.mkdir(parents=True, exist_ok=True)
    temporary = output/'Temp'; temporary.mkdir(exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(temporary)
    report = dict(source_report=str(source), source_report_sha256=source_sha,
        source_helper_sha256=proof.get('source_sha256'), native_report_passed=True,
        checks=proof['checks'], limitations=proof.get('limitations'),
        review_fixtures=proof.get('review_fixtures'), input_requests=proof.get('input_requests'),
        original_preferences_restored=True, original_config_bytes_restored=True,
        source_capture_hz=proof.get('capture_hz'),
        timing='Separate real game-time windows; source frames repeated for 30 fps without temporal interpolation or time scaling.',
        final_hold='The shared encoder adds one median capture interval to present the final native still; it is not an extra gameplay sample.',
        visual_review='Pending root review. Successful encoding and gameplay assertions do not certify visual quality.',
        performance='This native PNG review capture is not an FPS or rendering-cost benchmark.', windows={})
    for name, rows in windows.items():
        clip = encode_clip(OUTPUT_NAMES[name], rows, output, args.ffmpeg, args.ffprobe)
        clip['capture_report_key'] = name
        interval = statistics.median(float(b['time'])-float(a['time']) for a, b in zip(rows, rows[1:]))
        clip.update(video_sha256=digest(Path(clip['video'])), observed_median_game_interval_seconds=interval,
            observed_median_game_capture_hz=1./interval, source_frames_unchanged=True)
        report['windows'][name] = clip
    if any(digest(frame) != sha for frame, sha in hashes.items()):
        raise ValueError('Native source frame changed during encoding')
    if digest(source) != source_sha:
        raise ValueError('Native source report changed during encoding')
    report['decode_passed'] = True
    target = output/'VideoSourceReport.json'
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps(dict(manifest=str(target), videos={name: clip['video']
        for name, clip in report['windows'].items()}), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
