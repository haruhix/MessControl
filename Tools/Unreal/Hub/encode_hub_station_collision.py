"""Encode explicit completed collision subcases without hiding a failed run.

FourDoors mode accepts the failed Solo01 ONLY if all seven real-walk checks
passed for every door and the known later vending-query failure is preserved.
Full mode requires all five real walks, 35 guards and no teleports. Vendor mode
requires a completed passed focused vendor report. Native source
hashes and genuine timestamps are retained; video repeats captures at 30fps.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from encode_kenney_snow_powder_capture import encode_clip


def on_e(value):
    result=Path(value).resolve()
    if result.drive.upper()!='E:':
        raise ValueError('All inputs and outputs must stay on E: '+str(result))
    return result


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--mode',required=True,choices=('Full','FourDoors','Vendor'))
    parser.add_argument('--ffmpeg',default='C:/ffmpeg/ffmpeg.exe')
    parser.add_argument('--ffprobe',default='C:/ffmpeg/ffprobe.exe')
    args=parser.parse_args();source=on_e(args.report)
    proof=json.loads(source.read_text(encoding='utf-8-sig'));source_sha=digest(source)
    if not proof.get('owned_pie_stopped') or not proof.get('restored_settings'):
        raise ValueError('Owned PIE stop and restoration must be verified')
    checks=proof.get('checks',{});cases={row['name']:row for row in proof.get('tests',[])}
    suffixes=('approach_has_actual_walkable_floor','approach_capsule_is_clear',
        'swept_capsule_hits_exact_prop_front','starts_outside_surface',
        'reaches_the_actual_surface','blocks_capsule_not_pass_through','can_back_away')
    if args.mode=='Full':
        names=['Cabin'+str(index)+'Door' for index in range(1,5)]+['VendingBody']
        if proof.get('passed') is not True or proof.get('status')!='passed' or proof.get('teleports') is not False:
            raise ValueError('Full mode requires a passed five-case run without teleports')
        if proof.get('review_fixtures'):
            raise ValueError('Full walking proof must not use player placement fixtures')
        for name in names:
            if name not in cases or not all(checks.get(name+'_'+suffix) is True for suffix in suffixes):
                raise ValueError('Full mode requires all seven completed real-walk guards: '+name)
        if len(checks)!=35 or not all(value is True for value in checks.values()):
            raise ValueError('Full mode requires exactly 35 passed checks')
        clips=names
    elif args.mode=='FourDoors':
        names=['Cabin'+str(index)+'Door' for index in range(1,5)]
        for name in names:
            if name not in cases or not all(checks.get(name+'_'+suffix) is True for suffix in suffixes):
                raise ValueError('Partial clip requires every completed real-walk guard: '+name)
            if 'end_signed_distance_cm' not in cases[name]:
                raise ValueError('Door contact evidence missing: '+name)
        if proof.get('status')!='failed' or proof.get('passed') is not False:
            raise ValueError('FourDoors mode is explicitly for the reviewed partial failed run')
        if checks.get('VendingBody_swept_capsule_hits_exact_prop_front') is not False:
            raise ValueError('Expected preserved later vending approach-query failure')
        clips=names
    else:
        if proof.get('passed') is not True or proof.get('status')!='passed':
            raise ValueError('Focused vendor report must have passed')
        if not all(value is True for value in checks.values()) or not checks:
            raise ValueError('Focused vendor report has missing/failing checks')
        clips=['VendorPorchContact']
    output=on_e(args.output)
    if output.exists():
        raise ValueError('Preserve prior evidence; choose a fresh output directory')
    output.mkdir(parents=True)
    temporary=output/'Temp';temporary.mkdir(exist_ok=True)
    os.environ['TEMP']=os.environ['TMP']=str(temporary)
    manifest={'mode':args.mode,'source_report':str(source),'source_report_sha256':source_sha,
        'native_full_report_passed':proof.get('passed'),'native_full_report_status':proof.get('status'),
        'native_full_report_error':proof.get('error'),'all_native_checks':checks,
        'teleports':proof.get('teleports'),
        'encoded_completed_cases':clips,'tests':proof.get('tests'),
        'review_fixtures':proof.get('review_fixtures',[]),'limitations':proof.get('limitations'),
        'capture_hz':proof.get('capture_hz'),'windows':{},
        'timing':'Real game-time windows; native captures repeated for 30fps, no interpolation/time scaling.',
        'visual_review':'Completed door contact stills reviewed; videos retain the original scene/UI. No art quality claim.'}
    for name in clips:
        rows=proof.get('clips',{}).get(name,[])
        if len(rows)<2:
            raise ValueError('At least two native frames needed: '+name)
        hashes={}
        for row in rows:
            frame=on_e(row['file']);hashes[str(frame)]=digest(frame)
            if row.get('sha256') and hashes[str(frame)]!=row['sha256']:
                raise ValueError('Native source capture hash changed')
        result=encode_clip(name,rows,output,args.ffmpeg,args.ffprobe)
        if any(digest(Path(path))!=sha for path,sha in hashes.items()):
            raise ValueError('Native source frames changed while encoding')
        result.update(video_sha256=digest(Path(result['video'])),source_frames_unchanged=True)
        manifest['windows'][name]=result
    manifest['exact_contact_stills']=[]
    for row in proof.get('screenshots',[]):
        if 'Blocked' not in Path(row['file']).name and 'Vending' not in Path(row['file']).name:
            continue
        native=on_e(row['file'])
        if row.get('sha256') and digest(native)!=row['sha256']:
            raise ValueError('Native exact screenshot hash changed')
        target=output/native.name;shutil.copyfile(native,target)
        manifest['exact_contact_stills'].append({'file':str(target),'source_file':str(native),
            'sha256':digest(target),'time':row['time']})
    if digest(source)!=source_sha:
        raise ValueError('Native report changed while encoding')
    manifest['decode_passed']=True
    destination=output/'VideoSourceReport.json'
    destination.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'manifest':str(destination),'full_source_status':proof.get('status'),
        'videos':{name:{key:row[key] for key in ('video','available','encoded_duration_seconds','encoded_frame_count','full_decode')}
            for name,row in manifest['windows'].items()}},ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
