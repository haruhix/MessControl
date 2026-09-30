"""Encode time-stamped real Unreal render-target frames. Never captures a desktop."""
from pathlib import Path
import subprocess,csv,sys,json,re
root=Path(__file__).resolve().parents[1]; name=sys.argv[1]
folder=root/'Saved/ApprovalFrames'/name; out=root/'Artifacts/Approval'; out.mkdir(parents=True,exist_ok=True)
rows=[]
for filename,time in csv.reader((folder/'times.csv').read_text(encoding='utf-8-sig').splitlines()):
    if (folder/filename).exists() and (folder/filename).stat().st_size>54: rows.append((filename,float(time)))
if name=='Grip':
    # Clients boot before the grip fixture starts. Keep only the gameplay part.
    log=(root/'Saved/Logs/Approval_Grip.log').read_text(encoding='utf-8',errors='replace')
    event=re.search(r'\[([^\]]+)\].*MC_GRIP net=2 t=0\.0 ',log)
    if event:
        shots=re.findall(r'\[([^\]]+)\].*Tracing Screenshot "(\d+)"',log)
        start=next((int(number) for stamp,number in shots if stamp>=event[1]),0)
        rows=[r for r in rows if int(Path(r[0]).stem)>=start]
if len(rows)<30: raise RuntimeError(f'Only {len(rows)} rendered frames for {name}')
concat=[]
for i,(file,t) in enumerate(rows):
    concat += [f"file '{file}'",f'duration {max(.01,min(.5,rows[i+1][1]-t)) if i+1<len(rows) else .1:.6f}']
concat += [f"file '{rows[-1][0]}'"]
(folder/'frames.ffconcat').write_text('\n'.join(concat),encoding='utf-8')
labels={
'Climb':'E: attach to an actual arena tooth | W/S: climb | A/D: sideways | SPACE: wall jump',
'Coffee':'Coffee pour: controllable swimming, paddle input, drain | no front-wave stun',
'Cola':'Cold cola: top-down drink, growing frost, slippery ground, falling ice | pickaxe breaks every shape',
'Camera':'Gameplay following camera: front teeth boundary, side movement, throat approach',
'Ulcer':'Unattended food absorbs into tongue | hold spray 7s | release saves circular treatment progress',
'Tools':'Four inventory slots: brush, pickaxe, knife, spray | procedural hand and body poses',
'Brush':'Real tooth and tongue cleaning contact | character faces target | Niagara contact foam',
'Hazards':'Pepper fuse, pulsing red warning and detonation | jump over low ulcer shockwave',
'Grip':'Grab / carry / push / pull | procedural hand contact and body weight reactions',
'Throat':'Living throat, breathing morphs, uvula and atomic swallowing batch',
'Materials':'Painter tissue maps, saliva sheen and subsurface shading | gum, palate, throat and uvula',
'Pickaxe':'Wide pickaxe swings on the tongue and next to an actual tooth | hand and tool surface constraint'}
(folder/'caption.txt').write_text(labels.get(name,name),encoding='utf-8')
caption=str(folder/'caption.txt').replace('\\','/').replace(':','\\:')
font='C\\:/Windows/Fonts/arial.ttf'
vf=f"fps=30,drawbox=x=0:y=ih-52:w=iw:h=52:color=black@0.62:t=fill,drawtext=fontfile='{font}':textfile='{caption}':fontcolor=white:fontsize=17:x=20:y=h-36"
subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y','-f','concat','-safe','0','-i',str(folder/'frames.ffconcat'),'-vf',vf,'-c:v','libx264','-threads','2','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(out/(name+'.mp4'))],check=True)
duration=rows[-1][1]-rows[0][1]
subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y','-ss',str(min(duration/2,8)),'-i',str(out/(name+'.mp4')),'-frames:v','1','-update','1',str(out/(name+'.png'))],check=True)
(out/(name+'_Recording.json')).write_text(json.dumps(dict(case=name,source='Unreal FScreenshotRequest offscreen game render',frames=len(rows),seconds=round(duration,3),caption=labels.get(name,name)),indent=2),encoding='utf-8')
print(str(out/(name+'.mp4')))
