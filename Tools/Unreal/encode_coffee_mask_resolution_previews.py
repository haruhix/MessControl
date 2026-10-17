"""Encode the completed transient resolution lab; no Unreal or synthetic frames.

Uses the strict existing PNG/camera/hash encoder. Capture spacing is actual
wall-clock time because the isolated lab world is paused, not game-clock time.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw
import encode_cataclysm_previews as encoder

ROOT = Path(__file__).resolve().parents[2]
MODE = 'coffee_mask_resolution'
REQUIRED = tuple(f'coffee_mask_{n}_{s}' for n in (64,128,256)
                 for s in ('before','after','orbit'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,default=ROOT/'Saved/CoffeeMaskResolution/Preview/Report_preview.json')
    parser.add_argument('--output',type=Path,default=ROOT/'Artifacts/CoffeeMaskResolution_20261017')
    args=parser.parse_args()
    source=json.loads(args.report.read_text(encoding='utf-8-sig'))
    encoder.require(source.get('lab_mode')=='preview' and source.get('time_clock')=='wall_seconds',
                    'Expected a completed paused-world visual lab with explicit wall capture clock')
    encoder.REQUIRED[MODE]=REQUIRED
    report=encoder.load_report(args.report.resolve(),MODE,True,False)
    out=args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    manifest=dict(exported_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        source_report=report['report'],source_report_sha256=report['report_sha256'],
        capture_clock='Real wall-clock gaps, separately recorded paused native game time',
        scope=source.get('scope'),limitations=source.get('limitations',[]),
        checks=source.get('checks',{}),dataset=source.get('dataset'),
        previews=[],comparisons=[])
    ffmpeg=Path('C:/ffmpeg/ffmpeg.exe')
    for name,frames in report['clips'].items():
        manifest['previews'].append(encoder.encode_clip(MODE,name,frames,out,ffmpeg))
    # Uniformly crop only known window chrome. Identical camera views are kept
    # at the same display scale; panels are never aligned by warping the stain.
    for state in ('before','after'):
        for view in ('gameplay','close'):
            selected=[]
            for n in (64,128,256):
                frames=[f for f in source['frames'] if f.get('candidate',{}).get('resolution')==n
                    and f['candidate'].get('state')==state and f['candidate'].get('view')==view]
                encoder.require(frames,'Missing exact-resolution/view capture')
                selected.append(frames[len(frames)//2])
            reference=selected[0]['camera_evidence']['expected_cm']
            encoder.require(all(max(abs(a-b) for a,b in zip(reference,f['camera_evidence']['expected_cm']))<.01
                for f in selected),'Comparison panels do not share the same camera')
            canvas=Image.new('RGB',(3840,764),(22,22,25)); draw=ImageDraw.Draw(canvas)
            entries=[]
            for col,(n,frame) in enumerate(zip((64,128,256),selected)):
                im=Image.open(frame['file']).convert('RGB')
                if im.size==(1286,760): im=im.crop((3,35,1283,755))
                encoder.require(im.size==(1280,720),'Unsupported native capture size')
                canvas.paste(im,(col*1280,44))
                draw.text((col*1280+16,15),f'{n} x {n} | 1200 cm tile | {1200/n:.3f} cm/cell | {view} | {state}',fill='white')
                entries.append(dict(resolution=n,source=frame['file'],sha256=frame['sha256'],candidate=frame['candidate']))
            target=out/f'comparison_{view}_{state}.png'; canvas.save(target)
            manifest['comparisons'].append(dict(file=str(target),frames=entries,
                presentation='Three genuine engine captures, same camera and uniform scale, window chrome cropped only'))
    manifest['presentation_limitations']=[
        'Paused renderer experiment with independently rasterized masks, not native higher-resolution gameplay.',
        'Video repeats captured engine frames at30fps without motion interpolation; cadence follows recorded wall time.',
        'Successful export is separate from visual acceptance; root reviews decoded images.']
    path=out/'ExportManifest.json'
    path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps({'manifest':str(path),'videos':len(manifest['previews']),
                      'comparisons':[x['file'] for x in manifest['comparisons']]},indent=2))


if __name__=='__main__':
    main()
