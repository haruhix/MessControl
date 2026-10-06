"""Encode time-stamped real Unreal render-target frames. Never captures a desktop."""
from pathlib import Path
import subprocess,csv,sys,json,re,hashlib
from datetime import datetime,timezone
root=Path(__file__).resolve().parents[1]; name=sys.argv[1]
scenario=sys.argv[2] if len(sys.argv)>2 else name
folder=root/'Saved/ApprovalFrames'/name; out=root/'Artifacts/Approval'; out.mkdir(parents=True,exist_ok=True)
rows=[]
for filename,time in csv.reader((folder/'times.csv').read_text(encoding='utf-8-sig').splitlines()):
    if (folder/filename).exists() and (folder/filename).stat().st_size>54: rows.append((filename,float(time)))
if name in ('Grip','SprayNetwork'):
    # Peers boot before the network fixture starts. Keep the measured gameplay.
    log_path='Approval_Grip.log' if name=='Grip' else 'SprayNetwork1.log'
    log=(root/'Saved/Logs'/log_path).read_text(encoding='utf-8',errors='replace')
    start_pattern=r'MC_GRIP net=2 t=0\.0 ' if name=='Grip' else r'MC_SPRAY_NETWORK_PROGRESS net=\d+ t=0\.\d+ '
    event=re.search(r'\[([^\]]+)\].*'+start_pattern,log)
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
'FoodCollision':'Еда ×20: падение и контакт | шар проходит в пустом углу и упирается в поверхность | реальные convex контуры',
'FoodReaction':'Landing and damage: brief red flash + slight bounce | fall squash / stretch | no impact particles',
'FoodCollect':'One LMB click: collect small food into a vertical stack | second click: drop | Q: throw',
'FoodBalance':'Held stack sways with movement | hits and collisions spill the load into physical pieces',
'FoodPickup':'Подхват еды: подготовка, прыжок по дуге, мягкая посадка | покачивание стопки и выпадение при ударе',
'FoodThroat':'Еда автоматически затягивается в глотку | увула декоративная',
'FoodYawn':'Зевание рта: поток тянет к глотке | упор руками, сопротивление и эмоции | затем восстановление',
'FoodSpoil':'Freshness clock demonstrated at 24x: spoil after 180 seconds | no absorption or ulcer from food',
'FoodDamage':'Chili landing: fire road + ulcer | hold SPRAY to extinguish each segment, then heal the lesion',
'FoodStars':'Every fully completed task: check badge, gold stars, confetti and a happy hop',
'Climb':'E: attach to an actual arena tooth | W/S: climb | A/D: sideways | SPACE: wall jump',
'Coffee':'Coffee pour: controllable swimming, paddle input, drain | no front-wave stun',
'Cola':'Cold cola: top-down drink, growing frost, slippery ground, falling ice | pickaxe breaks every shape',
'ShiftReset':'Two live shift restarts: frozen swimming, then treatment and paused hazards | fresh day director, timer and tasks',
'Camera':'Gameplay following camera: front teeth boundary, side movement, throat approach',
'Ulcer':'Food spoil creates no ulcer | fire damages tongue | hold spray 7s | release saves progress',
'SprayNetwork':'Remote owning client: hold and release spray twice without a healing target | four real network peers',
'Tools':'Four inventory slots: brush, pickaxe, knife, spray | procedural hand and body poses',
'Brush':'Real tooth and tongue cleaning contact | character faces target | Niagara contact foam',
'Hazards':'Pepper fuse, pulsing red warning and detonation | jump over low ulcer shockwave',
'Grip':'Grab / carry / push / pull | procedural hand contact and body weight reactions',
'Throat':'Living throat, breathing morphs, uvula and atomic swallowing batch',
'Materials':'Painter tissue maps, saliva sheen and subsurface shading | gum, palate, throat and uvula',
'Pickaxe':'Wide pickaxe swings on the tongue and next to an actual tooth | hand and tool surface constraint',
'BossPhase3':'Босс · фаза 3 | анимации в Unreal · управление через F3'}
(folder/'caption.txt').write_text(labels.get(scenario,name),encoding='utf-8')
caption=str(folder/'caption.txt').replace('\\','/').replace(':','\\:')
font='C\\:/Windows/Fonts/arial.ttf'
vf=f"fps=30,drawbox=x=0:y=ih-52:w=iw:h=52:color=black@0.62:t=fill,drawtext=fontfile='{font}':textfile='{caption}':fontcolor=white:fontsize=17:x=20:y=h-36"
if name=='BossPhase3':
    validation=(out/'BossPhase3_Validation.txt').read_text(encoding='utf-8-sig')
    events=re.findall(r'MC_BOSS_PHASE3_CLIP index=(\d+) name=(\w+) start=([\d.]+) length=([\d.]+) hold=([\d.]+)',validation)
    clip_labels={'Idle':'Ожидание / дыхание','Walk':'Ходьба','PunchLeft':'Удар левой рукой',
                 'PunchRight':'Удар правой рукой','Kick':'Пинок','Hurt':'Получение урона',
                 'Death':'Падение / смерть','Roar':'Рёв'}
    if len(events)!=8: raise RuntimeError('Phase 3 video requires all eight validated clip timestamps')
    for index,clip,start,length,hold in events:
        label_file=folder/f'clip_{index}.txt'
        label_file.write_text(f'{index}/8 · {clip_labels[clip]}',encoding='utf-8')
        label_path=str(label_file).replace('\\','/').replace(':','\\:')
        begin=max(0,float(start)-rows[0][1]); end=begin+float(hold)
        vf+=f",drawtext=fontfile='{font}':textfile='{label_path}':fontcolor=white:fontsize=24:x=(w-tw)/2:y=112:box=1:boxcolor=black@0.65:boxborderw=10:enable='between(t,{begin:.6f},{end:.6f})'"
subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y','-f','concat','-safe','0','-i',str(folder/'frames.ffconcat'),'-vf',vf,'-c:v','libx264','-threads','2','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(out/(name+'.mp4'))],check=True)
duration=rows[-1][1]-rows[0][1]
subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y','-ss',str(min(duration/2,8)),'-i',str(out/(name+'.mp4')),'-frames:v','1','-update','1',str(out/(name+'.png'))],check=True)
with (out/(name+'.mp4')).open('rb') as video_file:
    video_sha256=hashlib.file_digest(video_file,'sha256').hexdigest()
recording=dict(case=name,source='Unreal FScreenshotRequest offscreen game render; encoded from game timestamps',frames=len(rows),seconds=round(duration,3),sample_fps=round((len(rows)-1)/duration,2) if duration>0 else 0,caption=labels.get(scenario,name),encodedUtc=datetime.now(timezone.utc).isoformat(),videoSha256=video_sha256)
capture_manifest=folder/'Capture_Manifest.json'
if capture_manifest.exists():
    fingerprint=json.loads(capture_manifest.read_text(encoding='utf-8-sig'))
    if fingerprint.get('case')!=name: raise RuntimeError('Capture manifest belongs to a different scenario')
    recording['captureFingerprint']=fingerprint
    validation_file=out/(name+'_Validation.txt')
    recording['fingerprintValidationMatches']=bool(validation_file.exists() and fingerprint.get('validation',{}).get('sha256')==hashlib.sha256(validation_file.read_bytes()).hexdigest())
(out/(name+'_Recording.json')).write_text(json.dumps(recording,indent=2),encoding='utf-8')
print(str(out/(name+'.mp4')))
