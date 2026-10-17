"""Assemble a Blender review from new renders and unchanged approved old PNGs.

No bpy, application startup or encoding. The passed author report supplies the
final clip order and captions. Hardlinks fall back to copies; source art and the
previous review remain untouched. A complete manifest is published only after
frame/source SHA, counts and shared camera settings pass.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import struct
import time
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'Artifacts/WizardMixamoUtilityPool'
PREVIOUS = ROOT / 'Artifacts/WizardMixamoPool/RenderManifest.json'
EXCLUDED_SINGLE = {'Fire_DoublePulse'}
SHARED = ('size', 'camera', 'ortho_scale', 'source_fps', 'engine', 'render_aa',
          'source_animation_resampled', 'evaluation_subframes', 'playback_speed')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def resolve(directory, value):
    path = Path(value)
    return (path if path.is_absolute() else directory / path).resolve()


def read(path):
    raw = path.read_bytes()
    return json.loads(raw.decode('utf8')), hashlib.sha256(raw).hexdigest()


def integer(value, label):
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError('Expected integer ' + label)
    return value


def fps(value):
    rate = Fraction(str(value['fps'])) / Fraction(str(value.get('fps_base', 1)))
    if rate <= 0:
        raise ValueError('FPS must be positive')
    return rate


def excluded(name):
    return name.casefold().startswith('combo_') or name in EXCLUDED_SINGLE


def safe_name(name):
    if (not isinstance(name, str) or not name or name in ('.', '..')
            or any(char in name for char in '/\\:<>"|?*') or name[-1] in '. '):
        raise ValueError('Unsafe clip folder name: ' + str(name))


def png_size(path):
    with path.open('rb') as stream:
        header = stream.read(24)
    if len(header) != 24 or header[:8] != b'\x89PNG\r\n\x1a\n' or header[12:16] != b'IHDR':
        raise ValueError('Not a PNG image: ' + str(path))
    return struct.unpack('>II', header[16:24])


def source_records(directory, records):
    result = {}
    for row in records:
        path, digest = resolve(directory, row['path']), str(row['sha256']).lower()
        if path in result and result[path] != digest:
            raise ValueError('Conflicting source SHA: ' + str(path))
        if sha256(path) != digest:
            raise ValueError('Source blend changed: ' + str(path))
        result[path] = digest
    if not result:
        raise ValueError('Source provenance is required')
    return result


def validate_render(path, selected_names):
    value, digest = read(path)
    directory = path.parent
    if value.get('status') != 'complete' or value.get('source_files_unchanged') is not True:
        raise ValueError('Render is incomplete or source changed: ' + str(path))
    if tuple(value['size']) != (640, 480):
        raise ValueError('Expected 640x480 source views')
    fps(value)
    scale = float(value['ortho_scale'])
    if not math.isfinite(scale) or scale <= 0 or not value.get('camera'):
        raise ValueError('Camera name and positive orthographic scale are required')
    clips = {}
    for clip in value['clips']:
        name = clip['name']
        safe_name(name)
        if name in clips:
            raise ValueError('Duplicate clip: ' + name)
        count = integer(clip['frame_count'], 'frame_count')
        if count <= 0 or clip['frame_start'] != 0 or clip['frame_end'] != count-1:
            raise ValueError('Incomplete clip frame range: ' + name)
        clips[name] = clip
    if not set(selected_names).issubset(clips):
        raise ValueError('Selected render clips are missing: ' + str(path))
    records, seen_paths = {}, set()
    for frame in value['frames']:
        name, index = frame['clip'], integer(frame['index'], 'frame index')
        key = (name, index)
        if name not in clips or not 0 <= index < clips[name]['frame_count'] or key in records:
            raise ValueError('Duplicate or unexpected frame: ' + str(key))
        actual = resolve(directory, frame['path'])
        expected = (directory / name / ('frame_%04d.png' % index)).resolve()
        if (actual != expected or not actual.is_relative_to(directory)
                or actual in seen_paths or frame['source_frame'] != index):
            raise ValueError('Frame path/source index differs: ' + str(key))
        if name in selected_names:
            if sha256(actual) != frame['sha256'] or png_size(actual) != (640, 480):
                raise ValueError('Frame SHA/dimensions differ: ' + str(key))
        records[key] = (frame, actual)
        seen_paths.add(actual)
    total = sum(clip['frame_count'] for clip in clips.values())
    if len(records) != total or value.get('rendered_png_count') != total:
        raise ValueError('Render manifest total count differs: ' + str(path))
    for name in selected_names:
        count = clips[name]['frame_count']
        disk = sorted((directory / name).glob('*.png'))
        if [file.name for file in disk] != ['frame_%04d.png' % index for index in range(count)]:
            raise ValueError('PNG folder count/indices differ: ' + name)
        if any((name, index) not in records for index in range(count)):
            raise ValueError('Missing source frame: ' + name)
    sources = source_records(directory, value['sources'])
    return value, digest, clips, records, sources


def atomic_json(path, value):
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf8')
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--author-report', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=OUTPUT)
    parser.add_argument('--new-manifest', type=Path,
                        help='Default: output-dir/RenderManifest_new.json')
    parser.add_argument('--previous-manifest', type=Path, default=PREVIOUS)
    parser.add_argument('--expected-retained', type=int, default=27)
    parser.add_argument('--copy', action='store_true', help='Use copies instead of hardlinks')
    args = parser.parse_args()
    started = time.monotonic()
    directory = args.output_dir.resolve()
    report_path, previous_path = args.author_report.resolve(), args.previous_manifest.resolve()
    new_path = (args.new_manifest or directory / 'RenderManifest_new.json').resolve()
    output = directory / 'RenderManifest.json'
    if output in (report_path, previous_path, new_path) or directory == previous_path.parent:
        raise ValueError('A separate output review directory is required')
    report, report_hash = read(report_path)
    old_header, _ = read(previous_path)
    new_header, _ = read(new_path)
    authored = report['clips']
    names = [clip['name'] for clip in authored]
    if report.get('passed') is not True or not names or len(set(names)) != len(names):
        raise ValueError('Expected a passed author report with unique ordered clips')
    if report.get('clip_count', len(names)) != len(names):
        raise ValueError('Author report clip count differs')
    for ordinal, row in enumerate(authored, 1):
        safe_name(row['name'])
        if excluded(row['name']) or ('ordinal' in row and row['ordinal'] != ordinal):
            raise ValueError('Excluded clip or wrong author ordinal: ' + row['name'])
    new_names = [clip['name'] for clip in new_header['clips']]
    retained = [name for name in names if name not in new_names]
    approved = [clip['name'] for clip in old_header['clips'] if not excluded(clip['name'])]
    if (args.expected_retained < 0 or len(retained) != args.expected_retained
            or set(retained) != set(approved) or not set(new_names).issubset(names)
            or not new_names or set(new_names).intersection(approved)):
        raise ValueError('Expected all %d approved retained clips and only new renders' % args.expected_retained)
    if ('retained_names' in report and (len(report['retained_names']) != len(retained)
            or set(report['retained_names']) != set(retained))):
        raise ValueError('Report retained_names differs from final order')
    previous, previous_hash, old_clips, old_frames, old_sources = validate_render(previous_path, set(retained))
    new, new_hash, new_clips, new_frames, new_sources = validate_render(new_path, set(new_names))
    if fps(previous) != fps(new):
        raise ValueError('Old/new native FPS differs; do not resample reused frames')
    for key in SHARED:
        if (key in previous) != (key in new) or previous.get(key) != new.get(key):
            raise ValueError('Old/new render setting differs: ' + key)
    sources = dict(old_sources)
    for path, digest in new_sources.items():
        if path in sources and sources[path] != digest:
            raise ValueError('Conflicting source provenance: ' + str(path))
        sources[path] = digest
    blend = resolve(report_path.parent, report['blend'])
    if new_sources.get(blend) != report['blend_sha256']:
        raise ValueError('New renders are not from the passed V4 author blend')
    original = resolve(report_path.parent, report['source'])
    if sources.get(original) != report['source_sha256']:
        raise ValueError('Original artist source differs from author provenance')
    for row in authored:
        origin = new_clips if row['name'] in new_clips else old_clips
        clip = origin[row['name']]
        if ('frames' in row and row['frames'] != clip.get('authored_frames')
                or 'action' in row and row['action'] != clip.get('action')):
            raise ValueError('Author action/frame count differs: ' + row['name'])
    directory.mkdir(parents=True, exist_ok=True)
    clips, frames, transfers = [], [], {'hardlinked': 0, 'copied': 0, 'already_present': 0}
    for ordinal, row in enumerate(authored, 1):
        name, is_new = row['name'], row['name'] in new_clips
        origin, record_map = (new_clips, new_frames) if is_new else (old_clips, old_frames)
        origin_path, origin_hash = (new_path, new_hash) if is_new else (previous_path, previous_hash)
        clip = dict(origin[name], ordinal=ordinal)
        for target, aliases in (('label', ('labelRU', 'label')), ('group', ('groupRU', 'group')),
                                ('note', ('useRU', 'note'))):
            for alias in aliases:
                if alias in row:
                    clip[target] = row[alias]
                    break
        clip.update(render_origin=dict(path=str(origin_path), sha256=origin_hash),
                    reused_render=not is_new)
        clips.append(clip)
        folder = directory / name
        if not folder.resolve().is_relative_to(directory):
            raise ValueError('Output clip folder leaves the review directory: ' + str(folder))
        folder.mkdir(parents=True, exist_ok=True)
        expected_files = {'frame_%04d.png' % index for index in range(clip['frame_count'])}
        if any(file.name not in expected_files for file in folder.glob('*.png')):
            raise ValueError('Output folder has unexpected PNGs: ' + str(folder))
        for index in range(clip['frame_count']):
            record, source = record_map[name, index]
            target = folder / ('frame_%04d.png' % index)
            if target.exists():
                transfers['already_present'] += 1
            elif args.copy:
                shutil.copy2(source, target)
                transfers['copied'] += 1
            else:
                try:
                    os.link(source, target)
                    transfers['hardlinked'] += 1
                except OSError:
                    shutil.copy2(source, target)
                    transfers['copied'] += 1
            if sha256(target) != record['sha256'] or png_size(target) != (640, 480):
                raise ValueError('Output frame SHA/dimensions differ: ' + str(target))
            frames.append(dict(record, path=str(target)))
    for path, digest in sources.items():
        if sha256(path) != digest:
            raise ValueError('Source blend changed during assembly: ' + str(path))
    for path, digest in ((report_path, report_hash), (previous_path, previous_hash), (new_path, new_hash)):
        if sha256(path) != digest:
            raise ValueError('Input evidence changed during assembly: ' + str(path))
    evidence = dict(status='complete', clip_count=len(clips), retained_count=len(retained),
                    retained_names=retained, new_names=[name for name in names if name in new_names],
                    excluded_previous_names=[clip['name'] for clip in previous['clips'] if excluded(clip['name'])],
                    rendered_png_count=len(frames), native_fps_preserved=True,
                    frame_sha_dimensions_counts_verified=True, source_files_unchanged=True,
                    shared_camera_settings_verified=True, transfers=transfers,
                    author_report=dict(path=str(report_path), sha256=report_hash),
                    previous_manifest=dict(path=str(previous_path), sha256=previous_hash),
                    new_manifest=dict(path=str(new_path), sha256=new_hash),
                    wall_seconds=time.monotonic()-started)
    manifest = {key: value for key, value in new.items()
                if key not in ('clips', 'frames', 'sources', 'first', 'last', 'wall_seconds',
                               'rendered_png_count', 'merged_parts', 'corrected_clips', 'final_review_blend')}
    manifest.update(status='complete', clips=clips, frames=frames,
                    sources=[dict(path=str(path), sha256=digest) for path, digest in sources.items()],
                    first=1, last=len(clips), rendered_png_count=len(frames),
                    source_files_unchanged=True, scope=report.get('scope', new.get('scope')),
                    limitations=report.get('limitations', new.get('limitations', [])),
                    author_report=str(report_path), author_report_sha256=report_hash,
                    assembly=evidence)
    atomic_json(directory / 'Assembly.json', evidence)
    atomic_json(output, manifest)
    print(str(output), flush=True)
    print('%d clips: %d retained, %d new; %d verified PNGs' %
          (len(clips), len(retained), len(new_names), len(frames)), flush=True)


if __name__ == '__main__':
    main()
