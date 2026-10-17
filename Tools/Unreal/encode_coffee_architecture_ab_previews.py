"""Validate/export completed tongue/tooth architecture preview reports.

No Unreal imports, gameplay recording or generated motion. --validate-only runs
strict PNG SHA/CRC/camera/clip checks without FFmpeg or image composition.
Otherwise ROOT explicitly runs this encoder after rendering/profiling finishes.
30fps videos repeat genuine captures at recorded WALL-clock intervals. Optional
deformation clips remain disclosed lockstep samples, never continuous gameplay.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw
import encode_cataclysm_previews as encoder

ROOT = Path(__file__).resolve().parents[2]
SUBJECTS = {
    'tongue': dict(mode='coffee_architecture_ab',
        default_report=ROOT/'Saved/CoffeeArchitectureAB/Preview/Report_preview.json',
        clips=tuple(f'architecture_{a}_{s}' for a in ('A','B') for s in ('before','after','orbit')),
        optional=('architecture_A_deformation','architecture_B_deformation')),
    'tooth': dict(mode='coffee_tooth_architecture_ab',
        default_report=ROOT/'Saved/CoffeeToothArchitectureAB/Preview/Report_preview.json',
        clips=tuple(f'tooth_{a}_{s}' for s in ('before','partial','damaged','orbit') for a in ('a','b')),
        optional=()),
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_report(filename, subject):
    """Reuse the strict encoder validator, with this finite A/B clip-name set.

    The old generic loader permits lowercase clip IDs only. Tongue's existing
    genuine reports use uppercase A/B, so validate the exact whitelist here;
    PNG/chunk/CRC/hash/dimensions/real-camera validation remains shared.
    """
    spec = SUBJECTS[subject]
    raw = filename.read_bytes();source = json.loads(raw.decode('utf-8-sig'))
    encoder.require(source.get('passed') is True and source.get('status') == 'passed', 'Report has not passed')
    encoder.require(source.get('mode') == spec['mode'] and source.get('lab_mode') == 'preview', 'Wrong preview report mode')
    encoder.require(source.get('time_clock') == 'wall_seconds', 'Explicit wall-clock capture time is required')
    clips = source.get('clips');frames = source.get('frames')
    encoder.require(isinstance(clips,dict) and isinstance(frames,list) and frames, 'Missing clips/frames')
    required = set(spec['clips']);optional = set(spec['optional'])
    present_optional = set(clips)&optional
    encoder.require(not present_optional or present_optional == optional, 'Deformation needs both A and B clips')
    encoder.require(set(clips) == required|present_optional, 'Missing or unexpected architecture clips')
    encoder.require(set(source.get('required_clips', [])) == set(clips), 'Declared required clips differ from concrete clips')
    by_file = {};original = {};previous = None
    for index, entry in enumerate(frames):
        encoder.require(entry.get('time_clock') == 'wall_seconds', 'Frame uses a different capture clock')
        native = encoder.finite_number(entry.get('native_game_time'), 'Native game time')
        encoder.require(native >= 0., 'Negative native game time')
        verified = encoder.validate_frame(entry, subject+' frame '+str(index), False)
        encoder.require(verified['file'] not in by_file, 'Repeated source file')
        encoder.require(previous is None or verified['time'] > previous, 'Non-increasing WALL capture timestamps')
        encoder.require(verified['clip_window'] in clips, 'Frame belongs to an undeclared clip')
        candidate = entry.get('candidate', {})
        architecture = candidate.get('architecture')
        encoder.require(architecture in ('A','B'), 'Missing A/B architecture identity')
        expected_name = ('architecture_'+architecture+'_'+(
            'orbit' if candidate.get('view') == 'orbit' else 'deformation' if candidate.get('view') == 'deformation'
            else candidate.get('state',''))) if subject == 'tongue' else 'tooth_'+architecture.lower()+'_'+candidate.get('state','')
        encoder.require(expected_name == entry['clip_window'], 'Clip/candidate identity differs')
        if subject == 'tongue':
            encoder.require(candidate.get('resolution') == 128, 'Tongue A/B must use the same128 mask')
        else:
            encoder.require(candidate.get('synthetic') is True and candidate.get('native_brush_claim') is False,
                            'Tooth renderer preview must retain its synthetic scope')
        verified.update(candidate=candidate, native_game_time=native, time_clock='wall_seconds')
        if 'paired_native_geometry' in entry:
            verified['paired_native_geometry'] = entry['paired_native_geometry']
        by_file[verified['file']] = verified;original[verified['file']] = entry;previous = verified['time']
    verified_clips = {};seen = set()
    for name, entries in clips.items():
        encoder.require(isinstance(entries,list) and len(entries) >= 8, 'Fewer than8 genuine frames: '+name)
        verified = [];previous = None
        for entry in entries:
            key = Path(entry['file']).resolve().as_posix()
            encoder.require(key in by_file and entry == original[key], 'Clip/global frame metadata differs')
            frame = by_file[key]
            encoder.require(frame['clip_window'] == name and key not in seen, 'Duplicate/misassigned clip frame')
            encoder.require(previous is None or frame['time'] > previous, 'Non-increasing clip wall times')
            verified.append(frame);seen.add(key);previous = frame['time']
        encoder.require(len({(f['width'],f['height']) for f in verified}) == 1, 'Dimensions changed mid-clip')
        if subject == 'tongue' and not name.endswith(('orbit','deformation')):
            for view in ('gameplay','close'):
                encoder.require(sum(f['candidate'].get('view') == view for f in verified) >= 8, 'Missing8 real '+view+' frames in '+name)
        verified_clips[name] = verified
    encoder.require(seen == set(by_file), 'Unreferenced global frame')
    if present_optional:
        a = {f['candidate']['deformation_step']:f for f in verified_clips['architecture_A_deformation']}
        b = {f['candidate']['deformation_step']:f for f in verified_clips['architecture_B_deformation']}
        encoder.require(len(a) >= 8 and set(a) == set(b)
            and len(a) == len(verified_clips['architecture_A_deformation'])
            and len(b) == len(verified_clips['architecture_B_deformation']), 'Deformation steps do not pair uniquely')
        for step in a:
            encoder.require(abs(a[step]['native_game_time']-b[step]['native_game_time']) < 1.e-6
                and a[step].get('paired_native_geometry') is not None
                and a[step]['paired_native_geometry'] == b[step].get('paired_native_geometry'),
                'Deformation A/B native time or geometry SHA differs')
    return dict(subject=subject, mode=spec['mode'], report=filename.as_posix(), report_sha256=hashlib.sha256(raw).hexdigest(),
        source_sha256=source.get('source_sha256', {}), checks=source.get('checks', {}),
        fixture_tuning=source.get('fixture_tuning', {}), limitations=source.get('limitations', []),
        source_default_attributes_and_revisions={k:source[k] for k in (
            'purpose','baseline','architecture','transient_material','synthetic_fixture','damage_fixture',
            'dataset','camera_scope','fixture','tooth','deformation_fixture','native_ledger_verification',
            'cpu_geometry_scope') if k in source},
        genuine_frames=len(by_file), capture_clock='recorded wall_seconds; native_game_time retained separately',
        clips=verified_clips)


def matched_pair(a, b):
    encoder.require(a['candidate']['architecture'] == 'A' and b['candidate']['architecture'] == 'B', 'A/B panel order differs')
    for key in ('expected_cm','actual_cm'):
        left,right = a['camera_evidence'].get(key),b['camera_evidence'].get(key)
        encoder.require(isinstance(left,list) and len(left) == 3 and isinstance(right,list) and len(right) == 3
            and max(abs(float(x)-float(y)) for x,y in zip(left,right)) < .01, 'A/B panel camera positions differ')
    encoder.require(a['camera_evidence']['expected_target'] == b['camera_evidence']['expected_target'], 'A/B use different camera actors')
    encoder.require(abs(a['native_game_time']-b['native_game_time']) < 1.e-6, 'A/B panel native times differ')
    key = 'synthetic_mask_sha256' if a['candidate'].get('synthetic') else 'texture'
    encoder.require(a['candidate'].get(key) and a['candidate'].get(key) == b['candidate'].get(key), 'A/B mask identity differs')
    if 'current_mesh' in a['candidate']:
        encoder.require(a['candidate']['current_mesh'] == b['candidate'].get('current_mesh'), 'A/B tooth meshes differ')


def image(frame):
    with Image.open(frame['file']) as original:
        result = original.convert('RGB')
    if result.size == (1286,760):
        result = result.crop((3,35,1283,755))
    encoder.require(result.size == (1280,720), 'Unsupported native capture dimensions')
    return result


def comparison_pairs(report):
    subject = report['subject']
    states = ('before','after') if subject == 'tongue' else ('before','partial','damaged')
    views = ('gameplay','close') if subject == 'tongue' else (None,)
    for state in states:
        for view in views:
            pair = []
            for variant in ('A','B'):
                name = f'architecture_{variant}_{state}' if subject == 'tongue' else f'tooth_{variant.lower()}_{state}'
                frames = [f for f in report['clips'][name] if view is None or f['candidate'].get('view') == view]
                encoder.require(frames, 'Missing exact A/B state/view')
                pair.append(frames[len(frames)//2])
            matched_pair(*pair)
            yield state,view,pair


def compose_comparisons(report, output):
    subject = report['subject'];out = []
    for state,view,pair in comparison_pairs(report):
        canvas = Image.new('RGB',(2560,764),(22,22,25));draw = ImageDraw.Draw(canvas)
        for column,(variant,frame) in enumerate(zip(('A','B'),pair)):
            canvas.paste(image(frame),(1280*column,44))
            label = 'A - separate coffee geometry' if variant == 'A' else 'B - coffee in original surface material'
            draw.text((1280*column+16,15),f'{label} | {subject} | {state}'+(' | '+view if view else ''),fill='white')
        filename = output/('comparison_'+subject+'_'+state+('_'+view if view else '')+'.png');canvas.save(filename)
        out.append(dict(file=filename.as_posix(),sha256=sha(filename),width=2560,height=764,
            source_frames=pair,presentation='Native1:1 captures; known window chrome cropped only; added44px label strip; no scaling, warping or retouching',
            camera_validation='Matching reported actual/expected positions and gated actual-view error; the probe uses one shared view recipe. Exact rotation vectors were not serialized.'))
    return out


def encode_preview(report, name, frames, output, ffmpeg):
    result = encoder.encode_clip(report['mode'],name,frames,output,ffmpeg)
    result['timestamp_basis'] = 'recorded wall_seconds'
    result['source_wall_span_seconds'] = result.pop('source_game_span_seconds')
    result['source_native_game_time_start_seconds'] = frames[0]['native_game_time']
    result['source_native_game_time_end_seconds'] = frames[-1]['native_game_time']
    result['scope'] = ('Lockstep native-deformation states: world advanced between paired paused A/B samples; not continuously simulated motion'
        if name.endswith('deformation') else 'Static renderer architecture preview; native gameplay remains unchanged')
    if report['subject'] == 'tooth':
        result['scope'] += '; explicit synthetic future tooth coffee domain/masks; no native brush integration claim'
    result['poster_sha256'] = sha(result['poster'])
    manifest = Path(result['source_frames_manifest']);payload=json.loads(manifest.read_text(encoding='utf-8'))
    payload['metadata'] = result
    manifest.write_text(json.dumps(payload,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    result['source_frames_manifest_sha256'] = sha(manifest)
    result['concat_manifest_sha256'] = sha(result['concat_manifest'])
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--subject',choices=('tongue','tooth','both'),default='tongue')
    parser.add_argument('--report',type=Path,help='Override completed report for a single subject')
    parser.add_argument('--tongue-report',type=Path,default=SUBJECTS['tongue']['default_report'])
    parser.add_argument('--tooth-report',type=Path,default=SUBJECTS['tooth']['default_report'])
    parser.add_argument('--output',type=Path,default=ROOT/'Artifacts/CoffeeArchitectureAB_20261017')
    parser.add_argument('--ffmpeg',type=Path,default=Path('C:/ffmpeg/ffmpeg.exe'))
    parser.add_argument('--validate-only',action='store_true',help='No FFmpeg, panels, or output files; integrity checks only')
    args=parser.parse_args()
    encoder.require(not (args.report and args.subject == 'both'), '--report needs a single subject')
    subjects=('tongue','tooth') if args.subject == 'both' else (args.subject,)
    reports=[load_report((args.report or getattr(args,subject+'_report')).resolve(),subject) for subject in subjects]
    pair_counts = {r['subject']:sum(1 for _ in comparison_pairs(r)) for r in reports}
    if args.validate_only:
        print(json.dumps(dict(validated=True,ffmpeg_run=False,reports=[dict(subject=r['subject'],
            report=r['report'],sha256=r['report_sha256'],genuine_frames=r['genuine_frames'],clips=list(r['clips']),
            matching_comparison_pairs=pair_counts[r['subject']]) for r in reports]),indent=2))
        return
    ffmpeg=args.ffmpeg.resolve()
    encoder.require(ffmpeg.is_file() and ffmpeg.with_name('ffprobe.exe').is_file(), 'FFmpeg/FFprobe required')
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    manifest=dict(exported_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        encoder_source_sha256={str(p.relative_to(ROOT)):sha(p) for p in (Path(__file__),Path(encoder.__file__))},
        capture_clock='WALL-clock source gaps; native game time preserved separately',reports=[],previews=[],comparisons=[],
        limitations=[
            'Genuine sampled engine PNGs;30fps repeats observed frames with no interpolation or invented motion.',
            'Known floating-window chrome is cropped uniformly. Native image scale and HUD are preserved.',
            'Static views and lockstep deformation evidence are rendering experiments, not continuous gameplay or native higher-resolution cleaning.',
            'Tooth previews use disclosed synthetic future domain/masks and a native damage-only fixture; no tooth-system migration is delivered.',
            'Native actors/ticks remain in both architectures; exported previews establish no CPU-geometry savings.',
            'Material revisions/source defaults/attributes and report/source SHA are preserved, not reinterpreted as visually equivalent shading models.',
            'Successful capture/encoding requires separate root visual review; camera telemetry cannot detect occlusion.',
            'Terminal1ms and30fps rounding are presentation padding, not extra observed simulation.'])
    for report in reports:
        manifest['comparisons'].extend(compose_comparisons(report,output))
        for name,frames in report['clips'].items():
            manifest['previews'].append(encode_preview(report,name,frames,output,ffmpeg))
        manifest['reports'].append({k:v for k,v in report.items() if k != 'clips'})
    filename=output/'ExportManifest.json';filename.write_text(json.dumps(manifest,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(dict(manifest=filename.as_posix(),videos=len(manifest['previews']),comparisons=[c['file'] for c in manifest['comparisons']]),indent=2))


if __name__ == '__main__':
    main()
