"""Encode clean arrival/claim windows on their original GAME timestamps.

Native PNG capture can stall wall time. These two short motion-review clips use
the recorded game clock to show intended animation speed, with no interpolated
poses or extrapolation. The paused card UI is outside both selected windows.
Original files and game/wall times are retained in the output manifest.
"""
import argparse
import json
import os
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from encode_kenney_snow_powder_capture import encode_clip
from encode_reward_chest_presentation_solo import digest, on_e, prepare_windows, validate_cleanup


def native_game_rows(rows, start, end):
    result, duplicates = [], []
    for row in rows:
        game = float(row['source_game_seconds'])
        if game < start or game > end:
            continue
        selected = dict(row, time=game)
        if result and result[-1]['time'] == game:
            duplicates.append(result[-1]['file'])
            result[-1] = selected
        else:
            result.append(selected)
    if len(result) < 2:
        raise ValueError('The native game-time window has fewer than two frames')
    return result, duplicates


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--cleanup-report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ffmpeg', default='C:/ffmpeg/ffmpeg.exe')
    parser.add_argument('--ffprobe', default='C:/ffmpeg/ffprobe.exe')
    args = parser.parse_args()
    source, cleanup_path = on_e(args.report), on_e(args.cleanup_report)
    source_sha, cleanup_sha = digest(source), digest(cleanup_path)
    proof = json.loads(source.read_text(encoding='utf-8-sig'))
    cleanup = json.loads(cleanup_path.read_text(encoding='utf-8-sig'))
    validate_cleanup(proof, source_sha, cleanup)
    if proof.get('material_comparison_enabled') is not False or proof.get('gold_surface_comparison'):
        raise ValueError('Primary motion clips require a clean capture with no material experiment')
    hashes = {}
    windows, _ = prepare_windows(proof, hashes)
    first_actor = proof['fixtures'][0]['chest']
    samples = [row for row in proof['samples'] if row['actor'] == first_actor]
    def first_time(state):
        return next(float(row['time']) for row in samples if row['state'] == state)
    rows = windows['SpawnedRewardChest']
    selections = {
        'Arrival_GameTime': (first_time('FALLING'), first_time('LANDED')+.65),
        'Claim_GameTime': (first_time('EXHAUSTED'), rows[-1]['source_game_seconds']),
    }
    selected = {name: native_game_rows(rows, start, end)
                for name, (start, end) in selections.items()}
    output = on_e(args.output)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Use a fresh or empty output folder; preserve existing review evidence')
    output.mkdir(parents=True, exist_ok=True)
    temporary = output/'Temp'; temporary.mkdir(exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(temporary)
    report = dict(source_report=str(source), source_report_sha256=source_sha,
        cleanup_report=str(cleanup_path), cleanup_report_sha256=cleanup_sha,
        native_report_passed=True, material_comparison_enabled=False,
        encoder_helper_sha256=digest(Path(__file__).resolve()),
        speed='Original GAME timestamps, without temporal scaling, pose interpolation or extrapolation. Synchronous capture stalls are not game time.',
        cadence=f"Native source requests capped at {proof.get('capture_hz', 12.):g} per game second; encoded at 30 fps by repeating native images. This is not a native 30 fps render.",
        selection='Arrival: first falling sample through the 0.65 s landing spring, before approach. Claim: first exhausted sample through actual actor removal, after the paused card UI.',
        final_hold='One median recorded game-frame interval presents the last still; no additional source pose is generated.',
        visual_review='Pending root review; source assertions/decode do not certify VFX visual strength.',
        performance='This capture is not a runtime FPS or GPU-cost benchmark.', windows={})
    sources = {row['file']: row for row in proof['frames']}
    for name, (native_rows, duplicates) in selected.items():
        clip = encode_clip(name, native_rows, output, args.ffmpeg, args.ffprobe)
        for item in clip['source_sequence']:
            original = sources[item['file']]
            item.update(capture_request_wall_seconds=original['capture_request_wall_seconds'],
                        phase=original['phase'], sha256=original['sha256'])
        clip.update(video_sha256=digest(Path(clip['video'])), source_frames_unchanged=True,
                    requested_game_window_seconds=list(selections[name]),
                    equal_game_timestamp_frames_omitted=duplicates,
                    duplicate_policy='At the same game time keep the last actual source image; no new time or image is invented.')
        report['windows'][name] = clip
    if digest(source) != source_sha or digest(cleanup_path) != cleanup_sha:
        raise ValueError('Source/cleanup report changed during encoding')
    if any(digest(path) != sha for path, sha in hashes.items()):
        raise ValueError('Native source PNG changed during encoding')
    report['decode_passed'] = True
    manifest = output/'GameTimeVideoSourceReport.json'
    manifest.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps(dict(manifest=str(manifest), videos={name: row['video']
        for name, row in report['windows'].items()}), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
