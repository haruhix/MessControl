"""Encode genuine solo capture frames, with descriptive captions at normal speed."""
from pathlib import Path
import json,subprocess,tempfile
from PIL import Image,ImageDraw,ImageFont
import imageio_ffmpeg

ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Artifacts/NutKnightAnimations_20261016'
def main():
 d=json.loads((OUT/'SoloCheck.json').read_text());assert d['passed'],d.get('error')
 ff=imageio_ffmpeg.get_ffmpeg_exe();font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22)
 labels={'attack1':'Обычный удар 1','attack2':'Обычный удар 2','attack3':'Комбо: последний удар — АОЕ','jump':'Новый прыжок','block':'Фронтальный блок','rear_hit':'Урон со спины','break':'Разрушение щита','stun':'Стан и урон в стане','death':'Смерть'}
 clips=[]
 with tempfile.TemporaryDirectory(prefix='mc_knight_preview_') as tmp:
  tmp=Path(tmp)
  for case,label in labels.items():
   rows=[r for r in d['captures'] if r['case']==case];assert rows,case
   lines=['ffconcat version 1.0']
   for i,r in enumerate(rows):
    target=tmp/f'{case}_{i:04d}.png'
    with Image.open(r['file']) as source:
     im=source.convert('RGB');draw=ImageDraw.Draw(im);draw.rectangle((0,0,960,34),fill=(15,17,23));draw.text((12,4),label,font=font,fill=(250,235,200));im.save(target)
    duration=rows[i+1]['game_time']-r['game_time'] if i+1<len(rows) else 1/15
    assert 0<duration<.15,duration
    lines.extend([f"file '{target.as_posix()}'",f'duration {duration:.9f}'])
   lines.append(f"file '{target.as_posix()}'")
   playlist=tmp/(case+'.ffconcat');playlist.write_text('\n'.join(lines)+'\n')
   video=OUT/(case+'.mp4')
   subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(playlist),'-vf','fps=30,format=yuv420p','-c:v','libx264','-crf','19','-movflags','+faststart',str(video)],check=True)
   subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
   clips.append({'case':case,'caption':label,'video':str(video),'frames':len(rows),'full_decode':True})
  joined=tmp/'joined.ffconcat';joined.write_text('ffconcat version 1.0\n'+'\n'.join(f"file '{Path(r['video']).as_posix()}'" for r in clips)+'\n')
  video=OUT/'preview.mp4'
  subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(joined),'-c','copy','-movflags','+faststart',str(video)],check=True)
  subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
 (OUT/'VideoManifest.json').write_text(json.dumps({'preview':str(video),'clips':clips,'source':'Genuine solo PIE SceneCapture2D under L_Mouth lighting and copied player-camera postprocess. Fixed review camera; wider jump framing. 15 Hz capture, 30 fps encoding with repeated frames, normal simulation speed. Text captions added; no generated poses or interpolated motion. Silent.'},ensure_ascii=False,indent=2))
 print('NUT_KNIGHT_PREVIEW_ENCODED',str(video))
if __name__=='__main__':main()
