"""Encode genuine spawned/placed reward-chest windows from a passed solo report.

The source capture-request wall timestamps are the presentation clock. This keeps
the real paused card-selection interval, including capture overhead, instead of
discarding equal game-time frames or inventing a hybrid clock. Each source game
timestamp remains in VideoSourceReport.json. No interpolated images, acceleration,
new audio or engine launch is involved. Source/output/temp files must be on E:.

The native check requests asynchronous PIE shutdown. If its Report.json does not
already confirm owned_pie_stopped=true, supply --cleanup-report with the final
source_report_sha256 and owned_pie_stopped=true after inspecting the editor.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import sys

sys.dont_write_bytecode = True
from encode_kenney_snow_powder_capture import encode_clip


CAPTURE_METHOD = 'UMCGameplayUIEditorLibrary::CapturePIEGameWindow: synchronous owned PIE SWindow'
SPAWN_PHASES = {'spawned_drop', 'old_gold_comparison', 'new_gold_comparison'}
PLACED_PHASES = {'placed_drop', 'placed_reset'}
REVIEW_STILLS = ('01_original_gold_material_same_mesh.png',
                 '02_reviewed_gold_material_same_mesh.png')
REQUIRED_CHECKS = (
    'fixture uses the saved BP_RewardChest',
    'no free perk menu before a chest',
    'one standalone authority world',
    'native landing settled before interaction',
    'normal card API seals one shared reward',
    'exhausted releases solid collision immediately',
    'spawned reward actor is removed after the exit',
    'normal UI closes after claim',
    'placed chest hides after its short exit while retaining the same actor',
    'placed reset is visible, solid and landed without automatic reopening',
    'placed reset returns deformation to unit scale',
    'surface material matches its explicit authored override',
    'team stage reward starts falling immediately without a telegraph wait',
    'team stage reward is airborne before its first presented frame',
    'no ground landing marker or claim ring is rendered',
)


def on_e(value):
    path = Path(value).resolve()
    if path.drive.upper() != 'E:':
        raise ValueError('Task inputs and outputs must remain on E: '+str(path))
    return path


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def finite(value, label):
    if isinstance(value, bool):
        raise ValueError(label+' must be a finite number')
    try:
        result = float(value)
    except (TypeError, ValueError):
        raise ValueError(label+' must be a finite number') from None
    if not math.isfinite(result):
        raise ValueError(label+' must be a finite number')
    return result


def validate_cleanup(proof, source_sha, cleanup=None):
    """Do not confuse an asynchronous end-play request with an ended world."""
    if proof.get('passed') is not True or proof.get('status') != 'passed':
        raise ValueError('Only a completed passed native solo report may be encoded')
    if proof.get('error') or proof.get('interruption') or proof.get('restore_errors'):
        raise ValueError('The solo report contains failure, interruption or restoration errors')
    if proof.get('restored_settings') is not True:
        raise ValueError('The solo report must confirm restored_settings=true')
    if proof.get('owned_pie_end_requested') is not True:
        raise ValueError('The solo report must confirm its owned PIE end request')
    guard = proof.get('editor_viewport_capture_guard', {})
    pushed, restored = guard.get('pushed_keys'), guard.get('restored_keys')
    if not isinstance(pushed, list) or not isinstance(restored, list) or Counter(pushed) != Counter(restored):
        raise ValueError('Every named viewport realtime override must have been restored exactly once')
    if proof.get('owned_pie_stopped') is True:
        evidence = proof
    else:
        if not isinstance(cleanup, dict) or cleanup.get('source_report_sha256') != source_sha:
            raise ValueError('Confirm actual PIE shutdown in the report or a hash-bound --cleanup-report')
        evidence = cleanup
    if evidence.get('owned_pie_stopped') is not True:
        raise ValueError('Cleanup evidence must confirm owned_pie_stopped=true')
    if evidence.get('is_in_play_in_editor') not in (None, False):
        raise ValueError('Cleanup evidence still has Play active')
    if evidence.get('pie_worlds') not in (None, []):
        raise ValueError('Cleanup evidence still has PIE worlds')
    checks = proof.get('checks', {})
    if not isinstance(checks, dict) or not checks or any(value is not True for value in checks.values()):
        raise ValueError('Every reported native lifecycle check must pass')
    missing = [name for name in REQUIRED_CHECKS if checks.get(name) is not True]
    if missing:
        raise ValueError('Required native chest checks are missing: '+', '.join(missing))
    fixtures = proof.get('fixtures', [])
    if len(fixtures) != 2 or [row.get('placed') for row in fixtures] != [False, True]:
        raise ValueError('Expected the disclosed spawned fixture followed by the placed fixture')
    for fixture in fixtures:
        if fixture.get('owner') is not None:
            raise ValueError('This capture must use its disclosed ownerless fixtures')
        states = set(fixture.get('states', []))
        if not {'FALLING', 'LANDED', 'OPEN', 'EXHAUSTED'}.issubset(states):
            raise ValueError('A chest fixture did not record drop, landing, open and exhaustion')
        if fixture.get('choice', {}).get('method') != 'ChooseRewardPerk':
            raise ValueError('A chest fixture did not use the normal card-selection API')


def validate_image(row, hashes):
    path = on_e(row['file'])
    if not path.is_file():
        raise ValueError('Native source PNG is missing: '+str(path))
    sha = digest(path)
    if not row.get('sha256') or sha != row['sha256']:
        raise ValueError('Native source PNG differs from its capture report: '+str(path))
    if row.get('capture_method') != CAPTURE_METHOD or not row.get('capture_world'):
        raise ValueError('Expected an exact owned PIE SWindow capture: '+str(path))
    with path.open('rb') as stream:
        header = stream.read(24)
    if len(header) != 24 or not header.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('Native source does not have a PNG header: '+str(path))
    size = (int.from_bytes(header[16:20], 'big'), int.from_bytes(header[20:24], 'big'))
    if size != (row.get('width'), row.get('height')) or min(size) < 1:
        raise ValueError('Native source dimensions disagree with its report: '+str(path))
    hashes[path] = sha
    finite(row.get('time'), 'Source game timestamp')
    finite(row.get('capture_request_wall_seconds'), 'Source capture-request wall timestamp')
    return size


def prepare_windows(proof, hashes):
    """Preflight all evidence before creating outputs or starting FFmpeg."""
    frames = proof.get('frames', [])
    if len(frames) < 4:
        raise ValueError('The source report has too few native frames')
    size = None
    world = None
    previous_wall = previous_game = None
    placed_started = False
    windows = {'SpawnedRewardChest': [], 'PlacedRewardChestReset': []}
    for row in frames:
        current_size = validate_image(row, hashes)
        if size is not None and size != current_size:
            raise ValueError('Native SWindow capture dimensions changed during the check')
        size = current_size
        if world is not None and world != row['capture_world']:
            raise ValueError('Native frame world changed during the check')
        world = row['capture_world']
        wall = finite(row['capture_request_wall_seconds'], 'Source capture-request wall timestamp')
        game = finite(row['time'], 'Source game timestamp')
        if previous_wall is not None and wall <= previous_wall:
            raise ValueError('Source capture-request wall timestamps must strictly increase')
        if previous_game is not None and game < previous_game:
            raise ValueError('Source game timestamps must never move backwards')
        previous_wall, previous_game = wall, game
        phase = row.get('phase')
        if phase in SPAWN_PHASES and not placed_started:
            name = 'SpawnedRewardChest'
        elif phase in PLACED_PHASES:
            placed_started = True
            name = 'PlacedRewardChestReset'
        else:
            raise ValueError('Unexpected phase/order in chest capture: '+str(phase))
        # The shared encoder calls this generic time field "game seconds".
        # Re-label its resulting metadata below; the real source game time is
        # retained separately and no source clock values are invented.
        windows[name].append(dict(row, time=wall, source_game_seconds=game))
    for name, rows in windows.items():
        if len(rows) < 2:
            raise ValueError('Required clip has fewer than two native frames: '+name)
    placed_phases = {row['phase'] for row in windows['PlacedRewardChestReset']}
    if placed_phases != PLACED_PHASES:
        raise ValueError('The placed clip must include both normal use and rearm')
    stills = {}
    for row in proof.get('screenshots', []):
        name = Path(row['file']).name
        if name in REVIEW_STILLS:
            if name in stills:
                raise ValueError('Duplicate exact-review still: '+name)
            if validate_image(row, hashes) != size or row['capture_world'] != world:
                raise ValueError('Exact-review still did not use the same native capture window')
            stills[name] = row
    # Pre-flag historical reports disclose the experiment in this object.
    compare=proof.get('material_comparison_enabled',bool(proof.get('gold_surface_comparison')))
    if not isinstance(compare,bool):
        raise ValueError('material_comparison_enabled must be a bool')
    comparison_phases={row['phase'] for row in frames}&{'old_gold_comparison','new_gold_comparison'}
    if not compare and (stills or comparison_phases or proof.get('gold_surface_comparison')):
        raise ValueError('A clean presentation report contains an undisclosed material experiment')
    if compare and (set(stills) != set(REVIEW_STILLS) or len(comparison_phases)!=2):
        raise ValueError('Both same-mesh original/reviewed material stills are required')
    return windows, stills


def encode_window(name, rows, output, ffmpeg, ffprobe):
    clip = encode_clip(name, rows, output, ffmpeg, ffprobe)
    clip['source_start_capture_request_wall_seconds'] = clip.pop('source_start_game_seconds')
    clip['source_end_capture_request_wall_seconds'] = clip.pop('source_end_game_seconds')
    clip['source_game_start_seconds'] = rows[0]['source_game_seconds']
    clip['source_game_end_seconds'] = rows[-1]['source_game_seconds']
    sources = {str(Path(row['file']).resolve()): row for row in rows}
    for item in clip['source_sequence']:
        source = sources[item['file']]
        item['capture_request_wall_seconds'] = item.pop('game_seconds')
        item['game_seconds'] = source['source_game_seconds']
        item['phase'] = source['phase']
        item['sha256'] = source['sha256']
    for poster in clip['posters']:
        source = sources[poster['source_file']]
        poster['capture_request_wall_seconds'] = poster.pop('game_seconds')
        poster['game_seconds'] = source['source_game_seconds']
    intervals = [float(b['time'])-float(a['time']) for a, b in zip(rows, rows[1:])]
    clip.update(video_sha256=digest(Path(clip['video'])), source_frames_unchanged=True,
                observed_median_capture_request_wall_interval_seconds=statistics.median(intervals),
                equal_game_timestamp_frame_pairs=sum(a['source_game_seconds'] == b['source_game_seconds']
                    for a, b in zip(rows, rows[1:])))
    return clip


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--cleanup-report', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--full', action='store_true', help='Also encode the whole uninterrupted capture window')
    parser.add_argument('--ffmpeg', default='C:/ffmpeg/ffmpeg.exe')
    parser.add_argument('--ffprobe', default='C:/ffmpeg/ffprobe.exe')
    args = parser.parse_args()
    source = on_e(args.report)
    source_sha = digest(source)
    proof = json.loads(source.read_text(encoding='utf-8-sig'))
    cleanup_path = on_e(args.cleanup_report) if args.cleanup_report else None
    cleanup_sha = digest(cleanup_path) if cleanup_path else None
    cleanup = json.loads(cleanup_path.read_text(encoding='utf-8-sig')) if cleanup_path else None
    validate_cleanup(proof, source_sha, cleanup)
    hashes = {}
    windows, stills = prepare_windows(proof, hashes)
    if args.full:
        windows['FullRewardChestPresentation'] = [row for rows in windows.values() for row in rows]
    output = on_e(args.output)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Choose a fresh or empty output folder; existing evidence is preserved')
    for binary in (args.ffmpeg, args.ffprobe):
        if not Path(binary).is_file():
            raise ValueError('Required media executable is missing: '+binary)
    output.mkdir(parents=True, exist_ok=True)
    temporary = output/'Temp'
    temporary.mkdir(exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(temporary)
    report = dict(source_report=str(source), source_report_sha256=source_sha,
        cleanup_report=str(cleanup_path) if cleanup_path else None, cleanup_report_sha256=cleanup_sha,
        native_report_passed=True, owned_pie_stopped_confirmed=True, settings_restored=True,
        checks=proof['checks'], fixtures=proof['fixtures'], limitations=proof.get('limitations'),
        source_helper='Tools/Unreal/check_reward_chest_presentation_solo.py',
        encoder_helper_sha256=digest(Path(__file__).resolve()),
        timing='Original capture-request wall timestamps; all native frames retained, including equal game times while the reward menu pauses gameplay. No time scaling, temporal interpolation or added audio.',
        capture_overhead='Wall time includes synchronous PNG capture/compilation overhead. This review is not a real-time animation-speed or performance benchmark; each original game time remains in source_sequence.',
        material_comparison_enabled=proof.get('material_comparison_enabled',bool(proof.get('gold_surface_comparison'))),
        first_clip_material_comparison=('Explicit A/B experiment: the spawned clip includes old/reviewed material switching on the same actor before approach/open/claim.'
            if stills else 'Clean presentation: no temporary material switching or A/B experiment.'),
        final_hold='One median native capture-request interval presents the last still. It adds no gameplay sample.',
        visual_review='Pending root visual review; passing gameplay assertions and video decoding do not certify visual quality.',
        windows={}, exact_review_stills=[])
    for name, rows in windows.items():
        report['windows'][name] = encode_window(name, rows, output, args.ffmpeg, args.ffprobe)
    for name in REVIEW_STILLS:
        if name not in stills:continue
        row = stills[name]
        target = output/name
        shutil.copyfile(row['file'], target)
        report['exact_review_stills'].append(dict(file=str(target), source_file=row['file'],
            game_seconds=row['time'], capture_request_wall_seconds=row['capture_request_wall_seconds'],
            sha256=digest(target)))
    if any(digest(path) != sha for path, sha in hashes.items()):
        raise ValueError('Native source PNG changed during encoding')
    if digest(source) != source_sha or (cleanup_path and digest(cleanup_path) != cleanup_sha):
        raise ValueError('Native source or cleanup report changed during encoding')
    report['decode_passed'] = True
    manifest = output/'VideoSourceReport.json'
    manifest.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps(dict(manifest=str(manifest), videos={name: clip['video']
        for name, clip in report['windows'].items()}), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
