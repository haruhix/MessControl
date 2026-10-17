"""Merge two complete pool-render manifests without importing Blender or encoding.

Checks the saved author report's 1..32 order, shared render settings, source blend
hashes, and every frame index/path. The video compositor separately checks PNG SHA.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'Artifacts/WizardMixamoPool'
AUTHOR_REPORT = ROOT / 'ArtSource/NutAnimationStyle/Wizard/MixamoPool/AnimationPoolReport_v2.json'
PARTS = ('RenderManifest_01_16.json', 'RenderManifest_17_32.json')
REQUIRED_SHARED = ('fps', 'size', 'sources', 'camera', 'ortho_scale')
OPTIONAL_SHARED = ('fps_base', 'source_fps', 'source_animation_resampled',
                   'evaluation_subframes', 'playback_speed', 'engine', 'render_aa', 'scope', 'limitations')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def resolve(directory, value):
    path = Path(value)
    return (path if path.is_absolute() else directory / path).resolve()


def source_map(directory, records):
    result = {}
    for row in records:
        path, expected = resolve(directory, row['path']), str(row['sha256']).lower()
        if path in result or sha256(path) != expected:
            raise ValueError('Duplicate or changed source blend: ' + str(path))
        result[path] = expected
    if not result:
        raise ValueError('Source blend provenance is missing')
    return result


def validate_part(directory, path, first, last, ordered_names):
    value = json.loads(path.read_text(encoding='utf8'))
    if value.get('status') != 'complete' or value.get('source_files_unchanged') is not True:
        raise ValueError('Part is incomplete or its saved source changed: ' + str(path))
    if value.get('first') != first or value.get('last') != last:
        raise ValueError('Part ordinal range differs: ' + str(path))
    clips = value['clips']
    names = [clip['name'] for clip in clips]
    if names != ordered_names[first-1:last] or len(set(names)) != last-first+1:
        raise ValueError('Part clip order differs from authored pool: ' + str(path))
    for key in REQUIRED_SHARED:
        if key not in value:
            raise ValueError('Shared render parameter is missing: ' + key)
    scale = float(value['ortho_scale'])
    if not math.isfinite(scale) or scale <= 0:
        raise ValueError('Invalid orthographic camera scale')
    by_clip, seen_keys, seen_paths = {name: [] for name in names}, set(), set()
    for frame in value['frames']:
        name, index = frame['clip'], frame['index']
        if name not in by_clip or not isinstance(index, int) or isinstance(index, bool):
            raise ValueError('Unexpected clip or noninteger evaluation index')
        key = (name, index)
        frame_path = resolve(directory, frame['path'])
        expected_path = (directory / name / ('frame_%04d.png' % index)).resolve()
        if key in seen_keys or frame_path in seen_paths:
            raise ValueError('Duplicate rendered frame: ' + str(key))
        if (not frame_path.is_relative_to(directory) or frame_path != expected_path
                or not frame_path.is_file() or frame['source_frame'] != index):
            raise ValueError('Frame path/source index differs: ' + str(key))
        digest = str(frame['sha256'])
        if len(digest) != 64 or any(char not in '0123456789abcdefABCDEF' for char in digest):
            raise ValueError('Frame SHA256 record is invalid: ' + str(key))
        seen_keys.add(key)
        seen_paths.add(frame_path)
        by_clip[name].append(index)
    count = 0
    for ordinal, clip in enumerate(clips, first):
        total = int(clip['frame_count'])
        if (total <= 0 or clip['frame_start'] != 0 or clip['frame_end'] != total-1
                or by_clip[clip['name']] != list(range(total))
                or ('ordinal' in clip and clip['ordinal'] != ordinal)):
            raise ValueError('Incomplete clip timeline: ' + clip['name'])
        files = sorted((directory / clip['name']).glob('frame_*.png'))
        if [file.name for file in files] != ['frame_%04d.png' % index for index in range(total)]:
            raise ValueError('Disk frame count/index differs: ' + clip['name'])
        count += total
    if count != len(value['frames']) or value.get('rendered_png_count') != count:
        raise ValueError('Part total rendered PNG count differs: ' + str(path))
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, default=OUTPUT)
    parser.add_argument('--author-report', type=Path, default=AUTHOR_REPORT)
    parser.add_argument('--clip-count', type=int, default=32)
    parser.add_argument('--split-at', type=int, default=16)
    parser.add_argument('--output-name', default='RenderManifest.json')
    args = parser.parse_args()
    started = time.monotonic()
    directory = args.input_dir.resolve()
    report_path = args.author_report.resolve()
    report_hash = sha256(report_path)
    report = json.loads(report_path.read_text(encoding='utf8'))
    count, split = args.clip_count, args.split_at
    if not 0 < split < count or Path(args.output_name).name != args.output_name:
        raise ValueError('Expected a valid split and a plain output filename')
    authored = report['clips'][:count]
    names = [clip['name'] for clip in authored]
    if (report.get('passed') is not True or len(names) != count or len(set(names)) != count
            or [clip['ordinal'] for clip in authored] != list(range(1, count+1))):
        raise ValueError('Expected a passed author report with ordered unique clips')
    ranges = ((1, split), (split+1, count))
    part_paths = [directory / ('RenderManifest_%02d_%02d.json' % pair) for pair in ranges]
    part_hashes = [sha256(path) for path in part_paths]
    parts = [validate_part(directory, path, first, last, names)
             for path, (first, last) in zip(part_paths, ranges)]
    for key in REQUIRED_SHARED + OPTIONAL_SHARED:
        if (key in parts[0]) != (key in parts[1]) or parts[0].get(key) != parts[1].get(key):
            raise ValueError('Parts use different render settings/provenance: ' + key)
    sources = source_map(directory, parts[0]['sources'])
    blend = resolve(report_path.parent, report['blend'])
    if sources.get(blend) != report['blend_sha256']:
        raise ValueError('Rendered source differs from the saved authored pool blend')
    merged = dict(parts[0])
    merged.pop('wall_seconds', None)
    merged['clips'] = [dict(clip, ordinal=ordinal) for ordinal, clip in enumerate(
        parts[0]['clips'] + parts[1]['clips'], 1)]
    merged['frames'] = parts[0]['frames'] + parts[1]['frames']
    if [clip['name'] for clip in merged['clips']] != names:
        raise ValueError('Merged pool order differs')
    for source, expected in sources.items():
        if sha256(source) != expected:
            raise ValueError('Source blend changed during merge: ' + str(source))
    if sha256(report_path) != report_hash or any(sha256(path) != expected for path, expected in zip(part_paths, part_hashes)):
        raise ValueError('Input evidence changed during merge')
    merged.update(status='complete', first=1, last=count, source_files_unchanged=True,
                  rendered_png_count=len(merged['frames']), merged_parts=[
                      dict(path=str(path), sha256=digest, first=part['first'], last=part['last'],
                           frames=part['rendered_png_count'], wall_seconds=part.get('wall_seconds'))
                      for path, digest, part in zip(part_paths, part_hashes, parts)],
                  author_report=str(report_path), author_report_sha256=report_hash,
                  merge_wall_seconds=time.monotonic()-started)
    output = directory / args.output_name
    temporary = output.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(merged, ensure_ascii=False, indent=2), encoding='utf8')
    temporary.replace(output)
    print(str(output), flush=True)
    print('%d clips; %d PNGs; shared sources/camera/FPS verified' % (count,len(merged['frames'])), flush=True)


if __name__ == '__main__':
    main()
