"""Export this task's completed solo captures through the existing strict encoder.

Does not launch Unreal, record frames, alter reports or manufacture gameplay.
Use --mode for one probe; --report can point to its completed alternate run.
"""
import argparse
import datetime
import json
from pathlib import Path

import encode_cataclysm_previews as encoder


ROOT = Path(__file__).resolve().parents[2]
RECIPES = {
    'manual_food': {
        'report': 'Saved/ManualFood_20261017/Capture/Report_manual_food.json',
        'required': ('grape_twenty_fragments', 'stack_pickup_tap_hold'),
        'items': [3, 9],
    },
    'manual_intake': {
        'report': 'Saved/ManualIntake/Capture/Report.json',
        'required': ('uvula_watergun', 'intake_no_automatic_caps',
                     'manual_intake_reticle', 'manual_intake_throw', 'spoiled_overboard'),
        'items': [1, 2, 3, 4, 5],
    },
    'coffee_wave': {
        'report': 'Saved/CoffeeWaveStains/Capture/Report_coffee_wave.json',
        'required': ('coffee_wave_progressive', 'coffee_wave_teeth', 'coffee_film_closeup',
                     'coffee_wave_brush', 'coffee_film_after_wipe'),
        'items': [6, 8],
    },
    'brush_selfcare': {
        'report': 'Saved/BrushSelfcare/Capture/Report_brush_selfcare.json',
        'required': ('selfcare_then_brush',),
        'items': [7],
    },
    'knight_shield': {
        'report': 'Artifacts/ManualIntake_20261017/Shield/Report.json',
        'required': ('knight_shield_break',),
        'items': [10, 11],
    },
}
THROW_REQUIRED = ('intake_no_automatic_caps','manual_intake_reticle','manual_intake_throw')


def load_task_report(report_path, mode, exclude_nonclient_frames=False):
    """Narrow only the explicitly recorded throws focus, retaining all gates."""
    required = RECIPES[mode]['required']
    source = None
    if mode == 'manual_intake':
        encoder.require(report_path.is_file(),'Completed probe report missing: '+str(report_path))
        source = json.loads(report_path.read_text(encoding='utf-8-sig'))
        if source.get('focus') == 'throws':
            required = THROW_REQUIRED
    encoder.REQUIRED[mode] = required
    report = encoder.load_report(report_path,mode,True,exclude_nonclient_frames)
    if source is not None:
        report.update(focus=source.get('focus','full'),scope=source.get('scope'),
            required_clips=list(required),not_run=source.get('not_run',[]))
        if source.get('focus') == 'throws':
            report['task_items'] = [2,3,4]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('all', *RECIPES), default='all')
    parser.add_argument('--report', type=Path, help='Completed report override for one mode')
    parser.add_argument('--output', type=Path, default=ROOT/'Artifacts/ManualIntake_20261017/Previews')
    parser.add_argument('--ffmpeg', type=Path, default=Path('C:/ffmpeg/ffmpeg.exe'))
    parser.add_argument('--exclude-nonclient-frames', action='store_true',
                        help='Explicitly omit wrong-size captures with recorded provenance')
    args = parser.parse_args()
    encoder.require(not args.report or args.mode != 'all', '--report requires a single --mode')
    encoder.require(args.ffmpeg.is_file() and args.ffmpeg.with_name('ffprobe.exe').is_file(),
                    'ffmpeg.exe and ffprobe.exe are required')
    modes = tuple(RECIPES) if args.mode == 'all' else (args.mode,)
    for mode, recipe in RECIPES.items():
        encoder.REQUIRED[mode] = recipe['required']
    reports = [load_task_report((args.report or ROOT/RECIPES[mode]['report']).resolve(),
               mode,args.exclude_nonclient_frames) for mode in modes]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = dict(exported_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        reports=[], previews=[], limitations=[
            'Source images are genuine sampled engine frames; output repeats images without motion interpolation.',
            'Probe fixtures and deferred validation are retained in each report.',
            'Successful encoding does not establish visual quality; actual clips still require review.',
        ])
    for report in reports:
        clips = report.pop('clips')
        report.setdefault('task_items',RECIPES[report['mode']]['items'])
        manifest['reports'].append(report)
        for name, frames in clips.items():
            manifest['previews'].append(encoder.encode_clip(report['mode'], name, frames,
                                                            output, args.ffmpeg.resolve()))
    destination = output/('ExportManifest_' + args.mode + '.json')
    destination.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps(dict(manifest=str(destination), videos=[p['video'] for p in manifest['previews']]), indent=2))


if __name__ == '__main__':
    main()
