"""Offline export of completed Nut/Raft probes. No Unreal import or new captures.

Use repeated --report arguments for any of the three supported probe modes.
PNG CRC/SHA, complete reports, actual camera evidence and wall timestamps are
required.30fps output only repeats genuine images; motion is never interpolated.
Sources and reports remain untouched. Output, normalized PNG copies and temp
files must stay on E:. Each MP4 also has a decoded8-frame QC contact sheet.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import datetime
import hashlib
import json
import math
import os
import re
import struct
import subprocess
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REQUIRED = {
    'nut_pattern_kinetics_solo': ('pattern_A_then_B', 'pattern_B_then_A'),
    'nut_impacts_solo': ('weak', 'strong_and_cooldown', 'nut_to_nut'),
    'raft_seated_controls': ('raft_slots', 'raft_one_rower', 'raft_same', 'raft_opposed',
                             'raft_diagonal', 'raft_exit_board', 'raft_debris_ejection'),
}
PADDING = .001


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def number(value, description):
    require(isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value),
            description+' must be finite')
    return float(value)


def on_e(path, description):
    path = Path(path).resolve()
    require(path.drive.upper() == 'E:', description+' must resolve to E: '+str(path))
    return path


def png_information(data, description):
    require(data.startswith(b'\x89PNG\r\n\x1a\n'), description+': missing PNG signature')
    offset, dimensions, pixels = 8, None, False
    while offset+12 <= len(data):
        length = int.from_bytes(data[offset:offset+4], 'big')
        end = offset+8+length
        require(end+4 <= len(data), description+': incomplete PNG chunk')
        kind = data[offset+4:offset+8]
        require(zlib.crc32(data[offset+4:end]) & 0xffffffff == int.from_bytes(data[end:end+4], 'big'),
                description+': PNG CRC mismatch')
        if offset == 8:
            require(kind == b'IHDR' and length == 13, description+': invalid IHDR')
            dimensions = struct.unpack('>II', data[offset+8:offset+16])
        pixels |= kind == b'IDAT'
        offset = end+4
        if kind == b'IEND':
            require(length == 0 and pixels, description+': invalid IEND or no image data')
            return dimensions, offset
    raise RuntimeError(description+': IEND missing')


def validate_frame(entry, mode, description):
    source = on_e(entry['file'], 'Source PNG')
    require(source.is_file(), description+': captured PNG missing')
    data = source.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    require(isinstance(entry.get('sha256'), str) and sha == entry['sha256'].lower(), description+': source SHA mismatch')
    dimensions, end = png_information(data, description)
    require(dimensions in ((1280,720), (1286,760)), description+': wrong capture dimensions; no resizing fallback')
    require(dimensions == (entry.get('width'), entry.get('height')), description+': dimensions disagree with the report')
    if 'png_bytes' in entry:
        require(entry['png_bytes'] == end and entry.get('trailing_bytes') == len(data)-end, description+': PNG boundary metadata mismatch')
    if mode == 'raft_seated_controls':
        stamp = number(entry.get('wall_seconds'), description+': wall_seconds')
        game = number(entry.get('game_seconds'), description+': game_seconds')
        require(abs(game-number(entry.get('time'), description+': original game time')) < .001,
                description+': raft game-clock fields disagree')
    else:
        require(entry.get('time_clock', 'wall_seconds') == 'wall_seconds', description+': wrong frame clock')
        stamp = number(entry.get('time'), description+': wall timestamp')
        game = number(entry.get('native_game_time'), description+': native_game_time')
    require(stamp >= 0. and game >= 0., description+': negative timestamp')
    camera = entry.get('camera_evidence')
    require(isinstance(camera, dict), description+': actual camera evidence missing')
    expected = camera.get('expected_target') or camera.get('requested_view_target')
    require(isinstance(expected, str) and expected and camera.get('view_target') == expected,
            description+': actual view target differs from the requested review target')
    if camera.get('expected_target') and camera.get('requested_view_target'):
        require(camera['expected_target'] == camera['requested_view_target'], description+': contradictory camera target fields')
    location = number(camera.get('location_error_cm'), description+': camera location error')
    angle = number(camera.get('rotation_error_degrees'), description+': camera angle error')
    max_location, max_angle = (12., 5.) if mode == 'raft_seated_controls' else (2., 2.)
    require(0. <= location < max_location and 0. <= angle < max_angle, description+': actual camera exceeds the probe gate')
    return dict(file=source.as_posix(), source_sha256=sha, width=dimensions[0], height=dimensions[1],
                png_bytes=end, trailing_bytes=len(data)-end, wall_seconds=stamp, native_game_seconds=game,
                original_report_time=entry.get('time'), original_report_time_clock=entry.get('time_clock'),
                camera_evidence=camera, phase=entry.get('phase'), clip_window=entry.get('clip_window'))


def load_report(report_path):
    source = on_e(report_path, 'Report')
    raw = source.read_bytes()
    report = json.loads(raw.decode('utf-8-sig'))
    require(report.get('passed') is True and report.get('status') == 'passed', str(source)+': only completed passed reports may be exported')
    mode = report.get('mode')
    require(mode in REQUIRED, str(source)+': unsupported probe mode '+repr(mode))
    if mode != 'raft_seated_controls':
        require(report.get('time_clock') == 'wall_seconds', str(source)+': report must declare its wall clock')
    raw_frames, clips = report.get('frames'), report.get('clips')
    require(isinstance(raw_frames, list) and raw_frames and isinstance(clips, dict), str(source)+': captures/clips missing')
    require(set(clips) == set(REQUIRED[mode]), str(source)+': required clip set differs from the completed probe')
    by_file, original, previous = {}, {}, None
    for index, entry in enumerate(raw_frames):
        frame = validate_frame(entry, mode, mode+' frame '+str(index))
        require(frame['file'] not in by_file, mode+': source file repeats in the global frame list')
        require(previous is None or frame['wall_seconds'] > previous, mode+': wall timestamps must strictly increase')
        by_file[frame['file']], original[frame['file']] = frame, entry
        previous = frame['wall_seconds']
    hz = 6. if mode == 'nut_impacts_solo' else 8.
    if mode == 'raft_seated_controls':
        hz = number(report.get('capture_hz'), 'Raft capture_hz')
        require(0. < hz <= 8., 'Raft capture rate must be greater than0 and at most8Hz')
    # Raft records wall_seconds after Shot returns, rather than at its gate.
    # One Slate tick can shift adjacent reported intervals in opposite ways.
    jitter = 1./60.+.002 if mode == 'raft_seated_controls' else .006
    exported, consumed = {}, set()
    for name in REQUIRED[mode]:
        require(re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', name), 'Unsafe clip name')
        entries = clips[name]
        minimum = 16 if mode == 'nut_pattern_kinetics_solo' else 4 if mode == 'nut_impacts_solo' else 8
        require(isinstance(entries, list) and len(entries) >= minimum, name+': too few genuine captures')
        frames = []
        for entry in entries:
            key = Path(entry['file']).resolve().as_posix()
            require(key in by_file and entry == original[key], name+': clip metadata differs from the global capture list')
            require(key not in consumed, name+': source frame is duplicated across clips')
            frame = by_file[key]
            require(frame['clip_window'] == name, name+': wrong clip window')
            if frames:
                delta = frame['wall_seconds']-frames[-1]['wall_seconds']
                require(delta > 0. and delta >= max(1./hz-jitter, 1./8.-.006),
                        name+': non-increasing timestamp or capture faster than the declared cap')
            frames.append(frame)
            consumed.add(key)
        require(len({(f['width'], f['height']) for f in frames}) == 1, name+': dimensions changed mid-clip')
        exported[name] = frames
    require(consumed == set(by_file), mode+': global frames exist outside the required clips')
    return dict(mode=mode, source_report=source.as_posix(), source_report_sha256=hashlib.sha256(raw).hexdigest(),
        source_sha256=report.get('source_sha256', {}), checks=report.get('checks', {}),
        fixture_tuning=report.get('fixture_tuning', {}), limitations=report.get('limitations', []),
        requested_capture_hz=hz, timestamp_jitter_tolerance_seconds=jitter,
        missed_capture_requests=report.get('missed_capture_requests', []),
        capture_request_accounting=report.get('capture_request_accounting', {}), clips=exported)


def run(command, cwd, capture=False):
    return subprocess.run([str(value) for value in command], cwd=cwd, stdin=subprocess.DEVNULL,
                          check=True, capture_output=capture, text=capture)


def concat_line(path):
    value = path.as_posix()
    require('\n' not in value and '\r' not in value, 'Newline in source path')
    return "file '"+value.replace("'", "'\\''")+"'"


def encode_clip(mode, name, frames, destination, ffmpeg):
    output = destination/mode
    output.mkdir(parents=True, exist_ok=True)
    rows, spans, normalized_copies = ['ffconcat version 1.0'], [], []
    for index, frame in enumerate(frames):
        original = Path(frame['file'])
        presentation = original
        if frame['trailing_bytes']:
            presentation = output/'_normalized_png'/name/('%05d.png' % index)
            presentation.parent.mkdir(parents=True, exist_ok=True)
            presentation.write_bytes(original.read_bytes()[:frame['png_bytes']])
            normalized_copies.append(dict(source=original.as_posix(), copy=presentation.as_posix(),
                removed_nonpixel_trailing_bytes=frame['trailing_bytes'], source_unchanged=True))
        duration = frames[index+1]['wall_seconds']-frame['wall_seconds'] if index+1 < len(frames) else PADDING
        spans.append(duration)
        rows.extend((concat_line(presentation), 'option framerate 1000', 'duration %.9f' % duration))
    rows.extend((concat_line(presentation), 'option framerate 1000'))
    concat = output/(name+'.ffconcat')
    concat.write_text('\n'.join(rows)+'\n', encoding='utf-8')
    span = frames[-1]['wall_seconds']-frames[0]['wall_seconds']
    duration = span+PADDING
    video, poster, sheet = (output/(name+ext) for ext in ('.mp4', '.png', '_qc.png'))
    crop = (frames[0]['width'], frames[0]['height']) == (1286,760)
    filters = 'crop=1280:720:3:35,setsar=1' if crop else 'setsar=1'
    run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-xerror', '-y', '-safe', '0', '-f', 'concat',
         '-i', concat, '-vf', filters, '-t', '%.9f' % duration, '-r', '30', '-fps_mode', 'cfr', '-an',
         '-c:v', 'libx264', '-preset', 'medium', '-crf', '19', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', video], output)
    ffprobe = ffmpeg.with_name('ffprobe.exe')
    probe = json.loads(run([ffprobe, '-v', 'error', '-show_entries',
        'format=duration:stream=codec_name,codec_type,width,height,nb_frames,r_frame_rate', '-of', 'json', video], output, True).stdout)
    streams = [s for s in probe.get('streams', []) if s.get('codec_type') == 'video']
    require(len(streams) == 1 and streams[0].get('codec_name') == 'h264'
            and (streams[0].get('width'), streams[0].get('height')) == (1280,720)
            and streams[0].get('r_frame_rate') == '30/1', name+': output video format mismatch')
    require(abs(float(probe['format']['duration'])-duration) <= 2./30., name+': encoded duration differs from the wall-clock capture span')
    count = int(streams[0]['nb_frames'])
    require(count >= 8, name+': encoded stream is too short for the8-frame decoded quality sheet')
    indices = [round(i*(count-1)/7) for i in range(8)]
    selection = '+'.join('eq(n\\,%d)' % index for index in indices)
    qc_filter = 'select='+selection+',scale=480:270,tile=4x2:padding=6:margin=6:color=0x202020'
    run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-xerror', '-y', '-i', video,
         '-vf', qc_filter, '-fps_mode', 'passthrough', '-frames:v', '1', sheet], output)
    run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-xerror', '-y', '-ss', '%.6f' % min(1., span*.35),
         '-i', video, '-frames:v', '1', poster], output)
    dimensions, _ = png_information(sheet.read_bytes(), name+' decoded QC sheet')
    require(dimensions == (1950,558), name+': QC sheet must contain4x2 decoded frames')
    for frame in frames:
        require(hashlib.sha256(Path(frame['file']).read_bytes()).hexdigest() == frame['source_sha256'], name+': a source PNG changed during encoding')
    metadata = dict(mode=mode, clip=name, video=video.as_posix(), poster=poster.as_posix(),
        qc_contact_sheet=sheet.as_posix(), qc_decoded_frame_indices=indices,
        qc_decoded_timestamps_seconds=[i/30. for i in indices],
        video_sha256=hashlib.sha256(video.read_bytes()).hexdigest(),
        qc_sha256=hashlib.sha256(sheet.read_bytes()).hexdigest(), concat_manifest=concat.as_posix(),
        genuine_source_frames=len(frames), timestamp_basis='recorded monotonic wall seconds',
        source_wall_start_seconds=frames[0]['wall_seconds'], source_wall_end_seconds=frames[-1]['wall_seconds'],
        source_wall_span_seconds=span, terminal_presentation_padding_seconds=PADDING,
        output_timing='30fps repetition of genuine PNGs at recorded wall-time gaps; no optical flow, interpolation or invented motion',
        crop='Only floating-window chrome: x3 y35 width1280 height720' if crop else 'None',
        normalized_png_copies=normalized_copies, source_pngs_preserved=True,
        visual_quality_review='Pending root inspection of decoded contact sheet and MP4', encoded=probe)
    manifest = output/(name+'.frames.json')
    manifest.write_text(json.dumps(dict(metadata=metadata, durations_seconds=spans, frames=frames),
                                   ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    metadata['frames_manifest'] = manifest.as_posix()
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, action='append', required=True, help='Completed Report.json; repeat for more probe modes')
    parser.add_argument('--output', type=Path, default=ROOT/'Saved/NutRaftRedesign_20261017/Previews')
    parser.add_argument('--ffmpeg', type=Path, default=Path('C:/ffmpeg/ffmpeg.exe'))
    parser.add_argument('--validate-only', action='store_true', help='Read-only input validation; no media or output directories')
    args = parser.parse_args()
    reports = [load_report(path) for path in args.report]
    require(len({r['mode'] for r in reports}) == len(reports), 'Pass one immutable report per probe mode')
    output = on_e(args.output, 'Output')
    if args.validate_only:
        print(json.dumps(dict(passed=True, reports=[dict(mode=r['mode'], source_report=r['source_report'],
            clips={k:len(v) for k,v in r['clips'].items()}) for r in reports]), indent=2))
        return
    ffmpeg = args.ffmpeg.resolve()
    require(ffmpeg.is_file() and ffmpeg.with_name('ffprobe.exe').is_file(), 'ffmpeg.exe and ffprobe.exe are required')
    output.mkdir(parents=True, exist_ok=True)
    temporary = on_e(output/'_temp', 'Process temporary directory')
    temporary.mkdir(parents=True, exist_ok=True)
    for variable in ('TEMP', 'TMP', 'TMPDIR'):
        os.environ[variable] = str(temporary)
    manifest = dict(exported_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        exporter_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), reports=[], previews=[],
        limitations=['Only source PNGs are repeated; source sampling is at most8Hz and does not capture every game frame.',
            'Wall timestamps measure screenshot requests; engine Shot SHOWUI writes them asynchronously.',
            '30fps quantizes presentation time by up to one output frame; the final1ms is explicitly labelled padding.',
            'Decoded QC sheets use eight selected actual MP4 frames scaled only for inspection; visual approval remains with root.',
            'Actual camera evidence and successful encoding do not prove artistic quality or multiplayer behavior.'])
    for report in reports:
        clips = report.pop('clips')
        manifest['reports'].append(report)
        for name, frames in clips.items():
            manifest['previews'].append(encode_clip(report['mode'], name, frames, output, ffmpeg))
        require(hashlib.sha256(Path(report['source_report']).read_bytes()).hexdigest() == report['source_report_sha256'],
                'Source report changed during encoding')
    destination = output/'ExportManifest.json'
    destination.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps(dict(manifest=destination.as_posix(), previews=[dict(mode=p['mode'], clip=p['clip'],
        video=p['video'], qc=p['qc_contact_sheet']) for p in manifest['previews']]), ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
