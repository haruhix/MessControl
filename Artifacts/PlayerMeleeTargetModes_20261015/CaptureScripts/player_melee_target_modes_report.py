from pathlib import Path
import json,re,subprocess,shutil
from PIL import Image,ImageDraw
import imageio_ffmpeg

root=Path('D:/PROJECT/GAME/WaifuTD/MessControl');out=root/'Artifacts/PlayerMeleeTargetModes_20261015'
d=json.loads((out/'SoloCheck.json').read_text());assert d['passed'],d.get('error')
expected={'knife_food_hold':['overhead']*3,'pickaxe_food_hold':['overhead']*3,'mob_hold':[1],'mob_combo':[1,2,3],'boss_combo':[1,2,3],'food_to_enemy_hold':['overhead']}
events={k:[] for k in expected};case=None;keep=[]
for line in (root/'Saved/Logs/MessControl.log').read_text(encoding='utf-8',errors='replace').splitlines():
 m=re.search(r'MC_TARGET_MODE_CASE (\w+)',line)
 if m:case=m[1];keep.append(line)
 if case in events and 'MC_MELEE_COMBO' in line:
  keep.append(line)
  if 'food_chop' in line:events[case].append('overhead')
  else:
   m=re.search(r'begin step=(\d+)',line)
   if m:events[case].append(int(m[1]))
assert events==expected,(events,expected)
(out/'ModeEvents.log').write_text('\n'.join(keep)+'\n')
(out/'ModeSequenceCheck.json').write_text(json.dumps({'passed':True,'observed':events,'expected':expected},indent=2))

ff=imageio_ffmpeg.get_ffmpeg_exe();frames=out/'VideoFrames';frames.mkdir(exist_ok=True)
for r in d['captures']:
 dest=frames/Path(r['file']).name
 with Image.open(r['file']) as im:
  im.save(dest,format='PNG')
  with Image.open(dest) as copy:assert copy.tobytes()==im.tobytes()
manifest=[];sheet=Image.new('RGB',(1440,885),(25,28,35));draw=ImageDraw.Draw(sheet)
for row,case in enumerate(['knife_food_hold','mob_combo','boss_combo']):
 captures=[r for r in d['captures'] if r['case']==case];lines=['ffconcat version 1.0']
 for j,r in enumerate(captures):
  lines.extend(["file '"+(frames/Path(r['file']).name).as_posix()+"'",f"duration {(captures[j+1]['time']-r['time']) if j+1<len(captures) else 1/30:.9f}"])
 lines.append("file '"+(frames/Path(captures[-1]['file']).name).as_posix()+"'")
 concat=out/(case+'.ffconcat');concat.write_text('\n'.join(lines)+'\n');video=out/(case+'.mp4')
 subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(concat),'-vf','fps=30,format=yuv420p','-c:v','libx264','-crf','19','-movflags','+faststart',str(video)],check=True)
 subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
 samples=[s for s in d['samples'] if s['case']==case];target='food' if row==0 else 'mob' if row==1 else 'boss';hits=[];hp=samples[0]['health'][target]
 for s in samples:
  if s['health'][target]<hp:hits.append(s['time']);hp=s['health'][target]
 assert len(hits)==3,hits
 for col,t in enumerate(hits):
  r=min(captures,key=lambda r:abs(r['time']-t+.12))
  with Image.open(r['file']) as im:sheet.paste(im.crop((250,90,750,400)).convert('RGB').resize((480,270)),(col*480,row*295+25))
  draw.text((col*480+10,row*295+7),case+' | '+('overhead' if row==0 else 'combo '+str(col+1)),fill='white')
 manifest.append({'case':case,'video':str(video),'hit_times':hits,'captured_frames':len(captures),'full_decode':True})
sheet.save(out/'TargetModePoses.jpg',quality=95)
joined=out/'preview.ffconcat';joined.write_text('ffconcat version 1.0\n'+'\n'.join("file '"+Path(r['video']).as_posix()+"'" for r in manifest)+'\n')
preview=out/'preview.mp4';subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(joined),'-c','copy','-movflags','+faststart',str(preview)],check=True)
subprocess.run([ff,'-v','error','-xerror','-i',str(preview),'-f','null','-'],check=True)
(out/'VideoManifest.json').write_text(json.dumps({'preview':str(preview),'clips':manifest,'scope':'Genuine solo PIE gameplay-camera captures, normalized losslessly before video encoding. Silent preview.'},indent=2))
scripts=out/'CaptureScripts';scripts.mkdir(exist_ok=True)
for name in ['player_melee_target_modes_check.py','player_melee_target_modes_report.py']:shutil.copy2(name,scripts/name)
print('TARGET_MODES_SEQUENCES_AND_PREVIEW_VERIFIED',json.dumps(events))
