"""Encode feedback PNG reports with the existing approved-mechanics encoder.

Accepts a clips dictionary (each value is a frame list or {frames: [...]}) or a
top-level frames array. --clock declares the recorded clock; no time scaling,
interpolation, gameplay validation or visual approval is inferred by encoding.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import zlib

from BuildApprovedMechanicsVideos import encode


def require(condition, message):
    if not condition:
        raise ValueError(message)


def on_e(path, label):
    path = Path(path).resolve()
    require(path.drive.upper() == 'E:', label+' must resolve to E: '+str(path))
    return path


def png_dimensions(data, label):
    require(data.startswith(b'\x89PNG\r\n\x1a\n'), label+': not PNG')
    offset, size, pixels = 8, None, False
    while offset+12 <= len(data):
        length = int.from_bytes(data[offset:offset+4], 'big')
        end = offset+8+length
        require(end+4 <= len(data), label+': truncated PNG chunk')
        kind = data[offset+4:offset+8]
        require(zlib.crc32(data[offset+4:end]) & 0xffffffff == int.from_bytes(data[end:end+4], 'big'), label+': PNG CRC mismatch')
        if offset == 8:
            require(kind == b'IHDR' and length == 13, label+': invalid IHDR')
            size = struct.unpack('>II', data[offset+8:offset+16])
            require(size[0] > 0 and size[1] > 0, label+': invalid dimensions')
        pixels |= kind == b'IDAT'
        if kind == b'IEND':
            require(length == 0 and pixels and size is not None, label+': incomplete PNG image')
            return size
        offset = end+4
    raise ValueError(label+': PNG IEND missing')


def validate_frames(entries, clock, label):
    require(isinstance(entries, list) and len(entries) >= 3, label+': at least three captured frames required')
    keys = ('wall_seconds', 'time') if clock == 'wall' else ('game_time', 'game_seconds', 'native_game_time', 'time')
    result, previous, seen, size = [], None, set(), None
    for index, entry in enumerate(entries):
        require(isinstance(entry, dict), label+': frame must be an object')
        path = Path(entry['file'])
        require(path.is_absolute(), label+': frame paths must be absolute')
        path = on_e(path, 'Source PNG')
        require(path.is_file() and path not in seen, label+': missing or repeated exact source PNG: '+str(path))
        seen.add(path)
        data = path.read_bytes()
        dimensions = png_dimensions(data, str(path))
        require(size is None or size == dimensions, label+': frame dimensions change mid-clip')
        size = dimensions
        require('width' not in entry or entry['width'] == dimensions[0], label+': reported width differs')
        require('height' not in entry or entry['height'] == dimensions[1], label+': reported height differs')
        digest = hashlib.sha256(data).hexdigest()
        require('sha256' not in entry or isinstance(entry['sha256'], str) and digest == entry['sha256'].lower(), label+': source SHA mismatch')
        key = next((key for key in keys if key in entry), None)
        require(key is not None, label+': no '+clock+' timestamp')
        stamp = entry[key]
        require(isinstance(stamp, (int, float)) and not isinstance(stamp, bool) and math.isfinite(stamp) and stamp >= 0., label+': invalid timestamp')
        require(previous is None or stamp > previous, label+': '+clock+' timestamps must strictly increase')
        previous = float(stamp)
        result.append(dict(file=path.as_posix(), time=previous, source_time_key=key,
            sha256=digest, width=dimensions[0], height=dimensions[1], phase=entry.get('phase')))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--prefix', required=True, help='Safe output stem; clip names are appended when present')
    parser.add_argument('--clock', required=True, choices=('game', 'wall'))
    parser.add_argument('--ffmpeg', type=Path, default=Path('C:/ffmpeg/ffmpeg.exe'))
    args = parser.parse_args()
    require(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', args.prefix), 'Unsafe prefix')
    source, output = on_e(args.report, 'Report'), on_e(args.output, 'Output')
    raw = source.read_bytes()
    report = json.loads(raw.decode('utf-8-sig'))
    require(isinstance(report, dict), 'Report must be an object')
    require(report.get('status') != 'running', 'Wait for the capture to finish before encoding')
    clips = report.get('clips')
    require(clips is None or isinstance(clips, dict), 'clips must be a dictionary')
    clips = clips or {'': report.get('frames')}
    verified = {}
    for name, entries in clips.items():
        require(isinstance(name, str) and (not name or re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', name)), 'Unsafe clip name')
        if isinstance(entries, dict): entries = entries.get('frames')
        verified[name] = validate_frames(entries, args.clock, name or args.prefix)
    ffmpeg = args.ffmpeg.resolve()
    require(ffmpeg.is_file() and ffmpeg.with_name('ffprobe.exe').is_file(), 'ffmpeg.exe and ffprobe.exe required')
    output.mkdir(parents=True, exist_ok=True)
    temporary = output/'_temp'
    temporary.mkdir(exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(temporary)
    manifest = dict(source_report=source.as_posix(), source_report_sha256=hashlib.sha256(raw).hexdigest(),
        source_status=report.get('status'), source_passed=report.get('passed'), source_checks=report.get('checks'),
        scope=report.get('scope'), fixture_tuning=report.get('fixture_tuning'), limitations=report.get('limitations'),
        clock_basis='recorded '+args.clock+' seconds', visual_review='Pending inspection of actual frames and video',
        presentation='30fps repetition of genuine source PNGs; no generated motion. Existing encoder adds a final 1/12s hold and may quantize output timing.',
        previews=[])
    for name, frames in verified.items():
        stem = args.prefix+('_'+name if name else '')
        result = encode(stem, frames, output, ffmpeg)
        result['source_duration'] = result.pop('game_duration')
        result.update(clock_basis=manifest['clock_basis'], source_time_start=frames[0]['time'], source_time_end=frames[-1]['time'],
            video_sha256=hashlib.sha256(Path(result['video']).read_bytes()).hexdigest(), source_frames=frames)
        subprocess.run([str(ffmpeg), '-hide_banner', '-loglevel', 'error', '-xerror', '-i', result['video'], '-f', 'null', '-'], check=True)
        result['full_decode'] = 'PASS'
        for frame in frames:
            require(hashlib.sha256(Path(frame['file']).read_bytes()).hexdigest() == frame['sha256'], 'Source PNG changed while encoding')
        manifest['previews'].append(result)
    require(source.read_bytes() == raw, 'Source report changed while encoding')
    target = output/(args.prefix+'_ExportManifest.json')
    target.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf8')
    print(json.dumps(dict(manifest=target.as_posix(), videos=[item['video'] for item in manifest['previews']]), indent=2))


if __name__ == '__main__':
    main()
