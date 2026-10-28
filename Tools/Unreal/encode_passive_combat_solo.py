"""Encode actual probe frames with their recorded game-time spacing; no interpolated imagery.

Usage: python Tools/Unreal/encode_passive_combat_solo.py E:/.../validation-run --ffmpeg C:/ffmpeg/ffmpeg.exe
Writes videos, ffconcat recipes and VideoSourceReport.json beside Report.json on E:.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',type=Path)
parser.add_argument('--ffmpeg',default=None)
args=parser.parse_args()
output=args.output.resolve();assert output.drive.upper()=='E:'
temp=Path('E:/CodexTemp/MessControl/PassivesCombat/Encode')/output.name;temp.mkdir(parents=True,exist_ok=True)
os.environ['TEMP']=os.environ['TMP']=str(temp)
ffmpeg=args.ffmpeg or shutil.which('ffmpeg')
assert ffmpeg,'Supply an installed ffmpeg executable; this recipe never downloads a binary'
report=json.loads((output/'Report.json').read_text(encoding='utf8'))
assert report.get('frames'),'Probe contains no real recorded frames'
sources=[]
for clip,indices in report.get('clips',{}).items():
    frames=[report['frames'][index] for index in indices]
    if len(frames)<2:continue
    durations=[max(.001,frames[i+1]['time']-frames[i]['time']) for i in range(len(frames)-1)]
    durations.append(statistics.median(durations))
    lines=['ffconcat version 1.0']
    for frame,duration in zip(frames,durations):
        path=Path(frame['file']).resolve();assert path.drive.upper()=='E:' and path.exists()
        assert hashlib.sha256(path.read_bytes()).hexdigest()==frame['sha256'],'Source capture changed: '+str(path)
        quoted=path.as_posix().replace("'","'\\''")
        lines.extend(["file '"+quoted+"'",'duration %.9f'%duration])
    # Repeat the last source only as an ffconcat sentinel so its final duration is honored.
    lines.append("file '"+Path(frames[-1]['file']).as_posix().replace("'","'\\''")+"'")
    manifest=output/(clip+'.ffconcat');manifest.write_text('\n'.join(lines)+'\n',encoding='utf8')
    video=output/(clip+'.mp4');assert not video.exists(),'Retain existing video: '+str(video)
    command=[str(ffmpeg),'-hide_banner','-loglevel','warning','-f','concat','-safe','0','-i',str(manifest),
        '-an','-c:v','libx264','-crf','18','-preset','medium','-pix_fmt','yuv420p','-r','30','-vsync','cfr','-movflags','+faststart',str(video)]
    subprocess.run(command,check=True,cwd=output,env=os.environ.copy())
    sources.append(dict(clip=clip,video=str(video),manifest=str(manifest),source_frames=frames,
        requested_capture_hz=12,encoded_fps=30,recorded_game_duration=sum(durations),
        method='Genuine PIE SWindow frames repeated at recorded game timestamps; no optical flow, synthetic render or motion interpolation',command=command))
(output/'VideoSourceReport.json').write_text(json.dumps(dict(clips=sources,probe_status=report.get('status'),
    limitation='Video creation does not change probe checks or constitute visual approval.'),ensure_ascii=False,indent=2),encoding='utf8')
