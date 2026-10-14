"""Encode verified Cataclysm solo captures; never starts Unreal or records media.

Default inputs are Report_rafts.json and Report_jellies.json. The default exports
are the raft sink/return, actual RMB boarding, and native jelly rupture clips.
Use --mode to export one completed probe, or --all-clips for its other captures.
--mode drowning reads CataclysmDrowningSolo/Report_drowning.json by default and
exports drowning_countdown; --all-clips also exports fatigue and water death.
--mode storm reads CataclysmStormSolo/Report_storm.json and exports brush rowing,
rail and unbraced contact clips, respecting the completed probe's focus.
--mode inflow reads CataclysmInflowSolo/Report_inflow.json and exports the
incoming front and continuous inlet-to-throat flow during Holding.
--mode paint reads JellyPaintSolo/Report_paint.json and exports wandering gel
trails, paint-balloon rupture and actual brush cleanup.
Strict capture dimensions are required unless --exclude-nonclient-frames is
explicitly selected; that flag records and omits only wrong-size captures.
Only genuine captured PNGs are presented, with their recorded game-time gaps;
30 fps output repeats source images and never interpolates new motion.
"""
import argparse
import datetime
import hashlib
import json
import math
import re
import struct
import subprocess
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REQUIRED = {
    'rafts': ('raft_sink_return', 'raft_boarding'),
    'jellies': ('jelly_rupture',),
    'drowning': ('drowning_countdown',),
    'storm': ('storm_brush_rowing', 'debris_rail', 'debris_unbraced'),
    'inflow': ('inflow_arrival', 'inflow_holding'),
    'paint': ('jelly_paint_trail', 'jelly_paint_rupture', 'jelly_paint_brush'),
}
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'
TERMINAL_PADDING_SECONDS = .001


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def finite_number(value, description):
    require(isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value), description + ' must be a finite number')
    return float(value)


def png_information(data, description):
    """Check the complete PNG and identify a harmless UE tail after IEND."""
    require(data.startswith(PNG_SIGNATURE), description + ': PNG signature missing')
    offset, dimensions, has_pixels = 8, None, False
    while offset + 12 <= len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        end = offset + 12 + length
        require(end <= len(data), description + ': incomplete PNG chunk')
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        checksum = struct.unpack('>I', data[end - 4:end])[0]
        require(zlib.crc32(kind + payload) & 0xffffffff == checksum,
                description + ': PNG chunk CRC mismatch')
        if offset == 8:
            require(kind == b'IHDR' and length == 13, description + ': invalid IHDR')
            dimensions = struct.unpack('>II', payload[:8])
        has_pixels = has_pixels or kind == b'IDAT'
        if kind == b'IEND':
            require(length == 0 and has_pixels, description + ': invalid IEND or no image data')
            return dimensions, end
        offset = end
    raise RuntimeError(description + ': PNG IEND missing')


def validate_frame(entry, description, exclude_nonclient_frames=False):
    frame = Path(entry['file']).resolve()
    require(frame.is_file(), description + ': captured PNG missing: ' + str(frame))
    data = frame.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    require(isinstance(entry.get('sha256'), str)
            and digest == entry['sha256'].lower(), description + ': source SHA256 mismatch')
    dimensions, png_end = png_information(data, description)
    require(dimensions == (entry.get('width'), entry.get('height')),
            description + ': dimensions disagree with capture metadata')
    client_dimensions = dimensions in ((1280, 720), (1286, 760))
    require(client_dimensions or exclude_nonclient_frames,
            description + ': expected 1280x720 client or its 1286x760 floating window')
    stamp = finite_number(entry.get('time'), description + ': game timestamp')
    require(stamp >= 0., description + ': negative game timestamp')
    camera = entry.get('camera_evidence')
    require(isinstance(camera, dict), description + ': camera evidence missing')
    location_error = finite_number(camera.get('location_error_cm'), description + ': camera location error')
    angle_error = finite_number(camera.get('rotation_error_degrees'), description + ': camera rotation error')
    require(0. <= location_error < 12., description + ': camera location error exceeds capture gate')
    require(0. <= angle_error < 5., description + ': camera rotation error exceeds capture gate')
    require(isinstance(camera.get('view_target'), str) and camera['view_target']
            and camera['view_target'] == camera.get('expected_target'),
            description + ': actual view target differs from the review camera')
    return dict(file=frame.as_posix(), time=stamp, sha256=digest,
                width=dimensions[0], height=dimensions[1], png_bytes=png_end,
                trailing_bytes=len(data) - png_end, phase=entry.get('phase'),
                clip_window=entry.get('clip_window'), camera_evidence=camera,
                client_dimensions=client_dimensions)


def load_report(report_path, mode, all_clips, exclude_nonclient_frames=False):
    require(report_path.is_file(), 'Completed probe report missing: ' + str(report_path))
    raw = report_path.read_bytes()
    report = json.loads(raw.decode('utf-8-sig'))
    require(report.get('passed') is True and report.get('status') == 'passed',
            str(report_path) + ': probe must have finished and passed before export')
    require(report.get('mode') == mode, str(report_path) + ': wrong Cataclysm mode')
    frames = report.get('frames')
    clips = report.get('clips')
    require(isinstance(frames, list) and frames, str(report_path) + ': no captured frames')
    require(isinstance(clips, dict), str(report_path) + ': clips dictionary missing')
    by_file, original_entries, last_stamp, excluded = {}, {}, None, []
    for index, entry in enumerate(frames):
        verified = validate_frame(entry, mode + ' frame ' + str(index), exclude_nonclient_frames)
        require(verified['file'] not in by_file, mode + ': repeated source file in global frame list')
        require(last_stamp is None or verified['time'] > last_stamp,
                mode + ': game timestamps must strictly increase')
        by_file[verified['file']] = verified
        original_entries[verified['file']] = entry
        last_stamp = verified['time']
        if not verified['client_dimensions']:
            excluded.append(dict(source_frame_index=index, file=verified['file'], sha256=verified['sha256'],
                time=verified['time'], width=verified['width'], height=verified['height'],
                clip_window=verified['clip_window'],
                reason='PNG dimensions are neither 1280x720 game client nor 1286x760 floating client window; excluded with explicit --exclude-nonclient-frames',
                integrity_validation='Source SHA256, PNG chunks/CRC, metadata dimensions and game timestamp verified; source file left unchanged'))
    required = REQUIRED[mode]
    if mode == 'storm':
        if report.get('focus') == 'rowing': required = ('storm_brush_rowing',)
        elif report.get('focus') == 'debris': required = ('debris_rail', 'debris_unbraced')
    selected = set(clips) if all_clips else set(required)
    require(set(required).issubset(clips), mode + ': required clip missing')
    export_clips = {}
    for name, entries in clips.items():
        require(re.fullmatch('[a-z][a-z0-9_]*', name) is not None,
                mode + ': unsupported clip name ' + repr(name))
        require(isinstance(entries, list), mode + ' ' + name + ': frame list missing')
        verified_clip, previous = [], None
        for index, entry in enumerate(entries):
            key = Path(entry['file']).resolve().as_posix()
            require(key in by_file, mode + ' ' + name + ': clip frame absent from global capture list')
            verified = by_file[key]
            require(entry == original_entries[key],
                    mode + ' ' + name + ': clip metadata differs from its global capture entry')
            require(verified['clip_window'] == name, mode + ' ' + name + ': clip window mismatch')
            require(previous is None or verified['time'] > previous,
                    mode + ' ' + name + ': non-increasing game timestamps')
            if verified['client_dimensions']:
                verified_clip.append(verified)
            previous = verified['time']
        if name in selected:
            minimum = 8 if name in required else 3
            require(len(verified_clip) >= minimum,
                    mode + ' ' + name + ': fewer than ' + str(minimum) + ' verified frames')
            require(len({(f['width'], f['height']) for f in verified_clip}) == 1,
                    mode + ' ' + name + ': capture dimensions changed mid-clip')
            export_clips[name] = verified_clip
    return dict(mode=mode, report=report_path.as_posix(), report_sha256=hashlib.sha256(raw).hexdigest(),
                source_sha256=report.get('source_sha256', {}), checks=report.get('checks', {}),
                fixture_tuning=report.get('fixture_tuning', {}), limitations=report.get('limitations', []),
                manual_checkpoints=report.get('manual_checkpoints', []),
                integrity_verified_source_frames=len(by_file), accepted_client_frames=len(by_file) - len(excluded),
                excluded_nonclient_frames=excluded,
                capture_validation='Export uses a client-sized subset; excluded captures are not gameplay preview evidence' if excluded
                    else 'All source captures have supported client dimensions; visual review remains separate',
                clips=export_clips)


def concat_file_line(path):
    value = path.as_posix()
    require('\n' not in value and '\r' not in value, 'Unsupported newline in captured filename')
    return "file '" + value.replace("'", "'\\''") + "'"


def encode_clip(mode, name, frames, output, ffmpeg):
    # Names shared by both branches remain separate when --all-clips is used.
    shared = name in ('common_wave', 'cleanup_brush')
    export_name = name + '_' + mode if shared else name
    source_manifest = output / (export_name + '.frames.json')
    normalized = output / '_frames' / export_name
    rows = ['ffconcat version 1.0']
    spans = []
    for index, frame in enumerate(frames):
        original = Path(frame['file'])
        presentation_path = original
        if frame['trailing_bytes']:
            normalized.mkdir(parents=True, exist_ok=True)
            presentation_path = normalized / ('%05d.png' % index)
            # Discard only bytes after the verified IEND; pixels and chunks stay exact.
            presentation_path.write_bytes(original.read_bytes()[:frame['png_bytes']])
        duration = frames[index + 1]['time'] - frame['time'] if index + 1 < len(frames) else TERMINAL_PADDING_SECONDS
        spans.append(duration)
        rows.extend((concat_file_line(presentation_path), 'option framerate 1000', 'duration %.9f' % duration))
    rows.extend((concat_file_line(presentation_path), 'option framerate 1000'))
    concat = output / (export_name + '.ffconcat')
    concat.write_text('\n'.join(rows) + '\n', encoding='utf-8')
    span = frames[-1]['time'] - frames[0]['time']
    duration = span + TERMINAL_PADDING_SECONDS
    video, poster = output / (export_name + '.mp4'), output / (export_name + '.png')
    filters = 'crop=1280:720:3:35,setsar=1' if (frames[0]['width'], frames[0]['height']) == (1286, 760) else 'setsar=1'
    subprocess.run([str(ffmpeg), '-hide_banner', '-loglevel', 'error', '-y', '-safe', '0',
                    '-f', 'concat', '-i', str(concat), '-vf', filters, '-t', '%.9f' % duration,
                    '-r', '30', '-c:v', 'libx264', '-preset', 'medium', '-crf', '19',
                    '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(video)], check=True)
    ffprobe = ffmpeg.with_name('ffprobe.exe')
    result = subprocess.run([str(ffprobe), '-v', 'error', '-show_entries',
                             'format=duration:stream=codec_name,codec_type,width,height,nb_frames,r_frame_rate',
                             '-of', 'json', str(video)], capture_output=True, text=True, check=True)
    encoded = json.loads(result.stdout)
    streams = [s for s in encoded.get('streams', []) if s.get('codec_type') == 'video']
    require(len(streams) == 1 and streams[0].get('codec_name') == 'h264'
            and (streams[0].get('width'), streams[0].get('height')) == (1280, 720),
            export_name + ': output must be one H264 1280x720 video stream')
    require(abs(float(encoded['format']['duration']) - duration) <= 2. / 30.,
            export_name + ': encoded duration differs from captured game span')
    subprocess.run([str(ffmpeg), '-hide_banner', '-loglevel', 'error', '-y', '-ss',
                    '%.6f' % min(1.2, span * .35), '-i', str(video), '-frames:v', '1', str(poster)], check=True)
    metadata = dict(mode=mode, clip=name, video=video.as_posix(), poster=poster.as_posix(),
        video_sha256=hashlib.sha256(video.read_bytes()).hexdigest(),
        source_frames_manifest=source_manifest.as_posix(), concat_manifest=concat.as_posix(),
        genuine_source_frames=len(frames), timestamp_basis='recorded game seconds',
        source_time_start_seconds=frames[0]['time'], source_time_end_seconds=frames[-1]['time'],
        source_game_span_seconds=span, terminal_padding_seconds=TERMINAL_PADDING_SECONDS,
        output_timing='30 fps image repetition; no generated or interpolated motion; at most one output-frame timestamp rounding',
        crop='Floating-window chrome removed at x=3 y=35' if frames[0]['width'] == 1286 else 'None; exact game client',
        camera_validation=dict(view_target_matches=True,
            maximum_location_error_cm=max(f['camera_evidence']['location_error_cm'] for f in frames),
            maximum_rotation_error_degrees=max(f['camera_evidence']['rotation_error_degrees'] for f in frames)),
        visual_quality_review='Pending root inspection of actual video', encoded=encoded)
    source_manifest.write_text(json.dumps(dict(metadata=metadata, frame_durations_seconds=spans,
        frames=frames), ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reports', type=Path, help='Report directory; defaults to Saved/Checks/CataclysmSolo (CataclysmDrowningSolo for drowning)')
    parser.add_argument('--mode', choices=('both', 'rafts', 'jellies', 'drowning', 'storm', 'inflow', 'paint'), default='both')
    parser.add_argument('--all-clips', action='store_true', help='Also export common wave, cleanup, and other recorded clip windows')
    parser.add_argument('--exclude-nonclient-frames', action='store_true',
                        help='Explicitly omit wrong-size captures after integrity validation; record every omission and preserve game-time gaps')
    parser.add_argument('--output', type=Path, default=ROOT / 'Artifacts/CataclysmGameplay_20261014')
    parser.add_argument('--ffmpeg', type=Path, default=Path('C:/ffmpeg/ffmpeg.exe'))
    args = parser.parse_args()
    modes = ('rafts', 'jellies') if args.mode == 'both' else (args.mode,)
    reports_directory = args.reports or ROOT / ('Saved/Checks/CataclysmDrowningSolo' if args.mode == 'drowning'
        else 'Saved/Checks/CataclysmStormSolo' if args.mode == 'storm'
        else 'Saved/Checks/CataclysmInflowSolo' if args.mode == 'inflow'
        else 'Saved/Checks/JellyPaintSolo' if args.mode == 'paint' else 'Saved/Checks/CataclysmSolo')
    reports = [load_report(reports_directory.resolve() / ('Report_' + mode + '.json'), mode,
                           args.all_clips, args.exclude_nonclient_frames) for mode in modes]
    require(args.ffmpeg.is_file() and args.ffmpeg.with_name('ffprobe.exe').is_file(), 'ffmpeg.exe and ffprobe.exe are required')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = dict(exported_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        exclude_nonclient_frames_requested=args.exclude_nonclient_frames,
        previews=[], reports=[], limitations=['Sampled genuine engine screenshots do not record every game frame.',
            'CFR output repeats source images at their actual game-time intervals; no interpolation or title cards.',
            'Excluded nonclient captures stay recorded in report metadata. Their genuine time gaps remain as holds of preceding accepted frames; no wrong viewport is resized into output.',
            'PlayerCamera metadata does not establish the screenshot surface; visual review of accepted client captures remains necessary.',
            'The terminal 1 ms and 30 fps duration rounding are presentation padding, not additional observed gameplay.',
            'Telemetry and successful encoding do not establish visual quality; inspect the previews in the gameplay camera.'])
    for report in reports:
        clips = report.pop('clips')
        manifest['reports'].append(report)
        for name, frames in clips.items():
            manifest['previews'].append(encode_clip(report['mode'], name, frames, output, args.ffmpeg.resolve()))
    manifest_path = output / ('ExportManifest.json' if args.mode == 'both' else 'ExportManifest_' + args.mode + '.json')
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps(dict(manifest=manifest_path.as_posix(), previews=manifest['previews']), ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
