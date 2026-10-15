from pathlib import Path
import json,subprocess
import imageio_ffmpeg
from PIL import Image,ImageDraw

out=Path('D:/PROJECT/GAME/WaifuTD/MessControl/Artifacts/PlayerComboDiagonal_20261015')
d=json.loads((out/'SoloCheck.json').read_text());assert d['passed'],d.get('error')
ff=imageio_ffmpeg.get_ffmpeg_exe()
frames=out/'VideoFrames';frames.mkdir(exist_ok=True)
for r in d['captures']:
    src=Path(r['file']);dest=frames/src.name
    with Image.open(src) as img:
        img.save(dest,format='PNG')
        with Image.open(dest) as copy:assert copy.tobytes()==img.tobytes()
manifest=[];sheet=Image.new('RGB',(1440,2*295),(20,22,28));draw=ImageDraw.Draw(sheet)
for i,case in enumerate(['knife_combo','pickaxe_combo']):
    rows=[r for r in d['captures'] if r['case']==case]
    lines=['ffconcat version 1.0']
    for j,r in enumerate(rows):
        lines.extend(["file '"+(frames/Path(r['file']).name).as_posix()+"'",f"duration {(rows[j+1]['time']-r['time']) if j+1<len(rows) else 1/60:.9f}"])
    lines.append("file '"+(frames/Path(rows[-1]['file']).name).as_posix()+"'")
    concat=out/(case+'.ffconcat');concat.write_text('\n'.join(lines)+'\n')
    video=out/(case+'.mp4')
    subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(concat),'-vf','fps=60,format=yuv420p','-c:v','libx264','-crf','19','-movflags','+faststart',str(video)],check=True)
    subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
    samples=[r for r in d['samples'] if r['case']==case];hits=[];previous=samples[0]['serial']
    for r in samples:
        if r['serial']!=previous:hits.append(r['time']);previous=r['serial']
    assert len(hits)==3,hits
    for j,t in enumerate(hits):
        frame=min(rows,key=lambda r:abs(r['time']-(t-(.10 if j!=1 else .06))))
        with Image.open(frame['file']) as img:sheet.paste(img.convert('RGB').resize((480,270)),(j*480,i*295+25))
        draw.text((j*480+10,i*295+6),case+' | '+['1: left shoulder','2: upper right to lower left','3: overhead'][j],fill='white')
    manifest.append({'case':case,'video':str(video),'captured_frames':len(rows),'hit_times':hits,'full_decode':True})
sheet.save(out/'ComboPoses.jpg',quality=95)
joined=out/'preview.ffconcat';joined.write_text('ffconcat version 1.0\n'+'\n'.join("file '"+Path(r['video']).as_posix()+"'" for r in manifest)+'\n')
video=out/'preview.mp4'
subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(joined),'-c','copy','-movflags','+faststart',str(video)],check=True)
subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
(out/'VideoManifest.json').write_text(json.dumps({'preview':str(video),'clips':manifest,'audio':'Silent gameplay preview. Native swing/hit audio events are verified separately in the engine log.','source':'Genuine solo PIE SceneCapture frames; no generated or interpolated poses. Encoder PNG copies retain every RGBA pixel.'},indent=2))
print(json.dumps(manifest,indent=2))
