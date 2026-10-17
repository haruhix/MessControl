"""Compose full paired-camera Unreal AnimSequence review videos."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from BuildNutTankIterationVideo import FFMPEG, FFPROBE, png_size

ROOT=Path(__file__).resolve().parents[1]
FPS=30
SIZE=(1280,600)

def font(size):return ImageFont.truetype('C:/Windows/Fonts/arial.ttf',size)

def check_case(directory,case):
    count=int(case['frames'])
    if count!=int(case['expected_frames']):raise ValueError('Incomplete clip '+case['name'])
    for view in ('front','side'):
        paths=sorted((directory/case['name']/view).glob('*.png'))
        if [p.name for p in paths]!=['%04d.png'%i for i in range(1,count+1)]:raise ValueError('Nonconsecutive '+view+' '+case['name'])
        if any(png_size(p)!=(640,480) for p in paths):raise ValueError('Wrong image size')
    return count

def banner(directory,case,index,total):
    image=Image.new('RGB',SIZE,(24,29,37))
    draw=ImageDraw.Draw(image)
    role='TANK' if case['role']=='tank' else 'WIZARD'
    draw.text((18,5),f"{role} {index}/{total}   {case['name']}",font=font(30),fill=(245,245,249))
    draw.text((18,44),'FRONT 3/4',font=font(20),fill=(185,202,220))
    draw.text((658,44),'SIDE',font=font(20),fill=(185,202,220))
    cycle=f"{case['cycles']} cycles" if case['cycles']>1 else 'complete clip'
    draw.text((18,558),f"RAW animation clips / 1x source   |   {case['seconds']:.2f}s source   |   {cycle}   |   30 fps",font=font(18),fill=(235,236,241))
    draw.text((18,581),'Single-node Unreal preview. Gameplay timing, VFX, secondary solver and procedural ball roll are outside this clip gallery.',font=font(15),fill=(182,190,203))
    path=directory/'Encoded'/('Banner_'+case['name']+'.png')
    image.save(path)
    return path

def probe(path,expected):
    r=subprocess.run([str(FFPROBE),'-v','error','-select_streams','v:0','-count_frames','-show_entries','stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames,duration','-of','json',str(path)],check=True,capture_output=True,text=True)
    s=json.loads(r.stdout)['streams'][0]
    if ((int(s['width']),int(s['height']))!=SIZE or s['r_frame_rate']!='30/1' or int(s['nb_read_frames'])!=expected or abs(float(s['duration'])-expected/FPS)>.02):raise ValueError('Bad output '+str(path)+' '+json.dumps(s))
    return s

def encode_case(directory,case,index,total):
    count=check_case(directory,case)
    output=directory/'Encoded'/(case['name']+'.mp4')
    if output.is_file():
        probe(output,count)
        return output
    label=banner(directory,case,index,total)
    subprocess.run([str(FFMPEG),'-y','-hide_banner','-loglevel','error',
        '-framerate',str(FPS),'-start_number','1','-i',str(directory/case['name']/'front'/'%04d.png'),
        '-framerate',str(FPS),'-start_number','1','-i',str(directory/case['name']/'side'/'%04d.png'),
        '-loop','1','-framerate',str(FPS),'-i',str(label),
        '-filter_complex','[0:v][1:v]hstack=inputs=2[views];[2:v][views]overlay=0:72:shortest=1[out]',
        '-map','[out]','-frames:v',str(count),'-an','-c:v','libx264','-preset','veryfast','-crf','18','-pix_fmt','yuv420p','-fps_mode','passthrough','-movflags','+faststart',str(output)],check=True)
    probe(output,count)
    return output

def concatenate(directory,basename,cases,encoded):
    listing=directory/'Encoded'/(basename+'.txt')
    listing.write_text(''.join("file '"+p.as_posix().replace("'","'\\''")+"'\n" for p in encoded),encoding='utf8')
    output=directory/(basename+'.mp4')
    subprocess.run([str(FFMPEG),'-y','-hide_banner','-loglevel','error','-f','concat','-safe','0','-i',str(listing),'-c','copy','-movflags','+faststart',str(output)],check=True)
    expected=sum(c['frames'] for c in cases)
    stream=probe(output,expected)
    poster=output.with_suffix('.png')
    subprocess.run([str(FFMPEG),'-y','-hide_banner','-loglevel','error','-ss','0.75','-i',str(output),'-frames:v','1',str(poster)],check=True)
    chapters=[]
    cursor=0
    for c in cases:
        chapters.append(dict(name=c['name'],role=c['role'],start_seconds=cursor/FPS,duration_seconds=c['frames']/FPS,source_seconds=c['seconds'],cycles=c['cycles'],asset=c['asset'],mesh=c['mesh']))
        cursor+=c['frames']
    return dict(video=str(output),poster=str(poster),stream=stream,chapters=chapters,complete_clips=True,genuine_unreal_frames=True,interpolation=False,playback_speed=1)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input-dir',type=Path,default=ROOT/'Artifacts/NutFullReview_20261010_Review')
    p.add_argument('--group',choices=('all','tank','wizard'),default='all')
    p.add_argument('--wizard-version',default='V5')
    args=p.parse_args()
    d=args.input_dir.resolve()
    report_path=d/'GalleryCapture.json'
    report=json.loads(report_path.read_text(encoding='utf8'))
    if report['fps']!=FPS:raise ValueError('Unexpected source FPS')
    roles=['tank','mage'] if args.group=='all' else ['tank' if args.group=='tank' else 'mage']
    cases=[c for c in report['scenarios'] if c['role'] in roles]
    # Selected Tank group can be composed while the later Wizard capture runs.
    required={'tank':14,'mage':9}
    for role in roles:
        if len([c for c in cases if c['role']==role])!=required[role]:raise ValueError('Missing assigned clips '+role)
    (d/'Encoded').mkdir(exist_ok=True)
    encoded=[]
    for c in cases:
        role_cases=[v for v in cases if v['role']==c['role']]
        index=role_cases.index(c)+1
        encoded.append(encode_case(d,c,index,len(role_cases)))
    videos=[]
    for role in roles:
        selected=[(c,e) for c,e in zip(cases,encoded) if c['role']==role]
        basename='Tank_AllAnimations' if role=='tank' else 'Wizard_AllAnimations_'+args.wizard_version
        videos.append(concatenate(d,basename,[c for c,e in selected],[e for c,e in selected]))
    if args.group=='all':videos.append(concatenate(d,'All_Animations_Tank_Wizard_'+args.wizard_version,cases,encoded))
    provenance=dict(capture_json=str(report_path),sha256=hashlib.sha256(report_path.read_bytes()).hexdigest(),capture_status=report['status'],scope=report['scope'],lighting=report.get('lighting'),restoration=report.get('restoration'),restore_errors=report.get('restore_errors'),all_selected_assigned_clips=True)
    existing=d/'Video.json'
    old=json.loads(existing.read_text(encoding='utf8')) if existing.is_file() else {}
    merged={v['video']:v for v in old.get('videos',[])}
    merged.update({v['video']:v for v in videos})
    existing.write_text(json.dumps(dict(videos=list(merged.values()),provenance=provenance),indent=2),encoding='utf8')
    for v in videos:print(v['video'])

if __name__=='__main__':main()
