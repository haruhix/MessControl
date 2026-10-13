"""Encode genuine nut-fight clip windows from completed passed solo PIE reports.

Accept one or several --report arguments and an --output directory. The shared
encoder preserves actual game-time intervals, reports missing requests, repeats
captured images for 30 fps output, and never synthesizes intermediate poses.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

from encode_kenney_snow_powder_capture import encode_clip


CLIPS = {
    'storm_arrival': 'NutStormArrival',
    'wizard_summon': 'NutWizardSummon',
    'shield_damage': 'NutShieldDamage',
    'fire_nut': 'NutFireNut',
}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, action='append', required=True,
                        help='Passed Report.json; repeat to combine the gameplay and VFX captures.')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--clips', nargs='+', choices=tuple(CLIPS), help='Optional subset of the known recorded windows.')
    parser.add_argument('--ffmpeg', default='C:/ffmpeg/ffmpeg.exe')
    parser.add_argument('--ffprobe', default='C:/ffmpeg/ffprobe.exe')
    args = parser.parse_args()
    output = args.output.resolve()
    inputs, selected = [], {}
    wanted = set(args.clips or CLIPS)
    for argument in args.report:
        source = argument.resolve(strict=True)
        proof = json.loads(source.read_text(encoding='utf-8-sig'))
        if proof.get('status') != 'passed' or proof.get('passed') is not True:
            raise ValueError('Only a completed passed native PIE report may be encoded: ' + str(source))
        fingerprint = digest(source)
        inputs.append(dict(source_report=str(source), sha256=fingerprint,
            native_report_passed=True, scope=proof.get('scope'),
            fixture_tuning=proof.get('fixture_tuning'), limitations=proof.get('limitations'),
            native_checks=proof.get('checks'), restored_settings=proof.get('restored_settings')))
        for name, rows in proof.get('clips', {}).items():
            if name not in wanted or name not in CLIPS or not rows:
                continue
            if name in selected:
                raise ValueError('Two reports provide the same clip window: ' + name)
            selected[name] = source, proof, rows
    if not selected:
        raise ValueError('No requested nonempty nut-fight clip windows were recorded')
    if args.clips and wanted != set(selected):
        raise ValueError('Requested clips are missing: ' + ', '.join(sorted(wanted-set(selected))))
    output.mkdir(parents=True, exist_ok=True)
    report = dict(source_reports=inputs,
        timing='Separate actual game-time windows; every written source frame is retained with its timestamp. No temporal interpolation or time scaling.',
        presentation='30 fps H.264 output repeats real captures; final hold is the median requested source interval. Demux/output timing quantization is explicitly recorded.',
        missing_policy='Missing screenshot requests are listed, not filled with invented frames. Real intervals across missing requests are preserved.',
        visual_approval='Pending inspection of the actual gameplay/VFX footage; successful encoding is not visual approval.',
        windows={})
    for name, title in CLIPS.items():
        if name not in selected:
            continue
        source, proof, rows = selected[name]
        before = {}
        for row in rows:
            path = Path(row['file'])
            if not path.is_absolute():
                raise ValueError('Source frame paths must be absolute: ' + str(path))
            if path.is_file():
                path = path.resolve()
                before[str(path)] = digest(path)
        clip = encode_clip(title, rows, output, args.ffmpeg, args.ffprobe)
        if any(digest(Path(path)) != sha for path, sha in before.items()):
            raise ValueError('Source frames changed while encoding: ' + name)
        for row in clip['source_sequence']:
            row['sha256'] = before[row['file']]
        for poster in clip['posters']:
            poster['sha256'] = digest(Path(poster['file']))
            if poster['sha256'] != before[poster['source_file']]:
                raise ValueError('Poster differs from the genuine source image: ' + poster['file'])
        cadence = [float(b['time'])-float(a['time']) for a, b in zip(rows, rows[1:])]
        median = statistics.median(cadence)
        clip.update(source_report=str(source), source_report_sha256=next(item['sha256'] for item in inputs if item['source_report']==str(source)),
            video_sha256=digest(Path(clip['video'])), observed_median_source_hz=1./median,
            source_capture_hz=proof.get('capture_hz', round(1./median, 6)),
            source_frames_unchanged=True)
        report['windows'][name] = clip
    for item in inputs:
        if digest(Path(item['source_report'])) != item['sha256']:
            raise ValueError('Source gameplay report changed while encoding: ' + item['source_report'])
    report['decode_passed'] = True
    manifest = output/'VideoSourceReport.json'
    manifest.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps(dict(manifest=str(manifest), videos={name:{key:clip[key] for key in
        ('video', 'requested', 'available', 'encoded_duration_seconds', 'encoded_frame_count', 'full_decode')}
        for name, clip in report['windows'].items()}), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
