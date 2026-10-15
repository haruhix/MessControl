"""Encode genuine solo proof of complete clips, stable size and corrected pivot."""
from pathlib import Path
import json,subprocess,tempfile
from PIL import Image,ImageDraw,ImageFont
import imageio_ffmpeg
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Artifacts/NutKnightPlayback_20261016'
def main():
 d=json.loads((OUT/'SoloCheck.json').read_text());assert d['passed'],d.get('error')
 ff=imageio_ffmpeg.get_ffmpeg_exe();font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22)
 labels={'attack3':'Полное комбо, включая последний кадр','jump':'Полный прыжок и восстановление','rotate':'Ось поворота под орехом, между ногами','damage':'Получение урона: постоянный размер','roll':'Вход и выход из шара сохраняют свой масштаб','death':'Смерть: постоянный размер до конца'}
 clips=[]
 with tempfile.TemporaryDirectory(prefix='mc_knight_playback_') as folder:
  folder=Path(folder)
  for case,label in labels.items():
   rows=[r for r in d['captures'] if r['case']==case];assert rows,case
   lines=['ffconcat version 1.0']
   for i,r in enumerate(rows):
    p=folder/f'{case}_{i:04}.png'
    with Image.open(r['file']) as source:
     im=source.convert('RGB');draw=ImageDraw.Draw(im);draw.rectangle((0,0,960,34),fill=(15,17,23));draw.text((12,4),label,font=font,fill=(250,235,200));im.save(p)
    duration=rows[i+1]['game_time']-r['game_time'] if i+1<len(rows) else 1/15;assert 0<duration<.15,duration
    lines.extend([f"file '{p.as_posix()}'",f'duration {duration:.9f}'])
   lines.append(f"file '{p.as_posix()}'");playlist=folder/f'{case}.ffconcat';playlist.write_text('\n'.join(lines)+'\n')
   video=OUT/f'{case}.mp4'
   subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(playlist),'-vf','fps=30,format=yuv420p','-c:v','libx264','-crf','19','-movflags','+faststart',str(video)],check=True)
   subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
   clips.append({'case':case,'caption':label,'video':str(video),'source_frames':len(rows),'decode_passed':True})
  playlist=folder/'joined.ffconcat';playlist.write_text('ffconcat version 1.0\n'+'\n'.join(f"file '{Path(r['video']).as_posix()}'" for r in clips)+'\n')
  video=OUT/'preview.mp4';subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(playlist),'-c','copy','-movflags','+faststart',str(video)],check=True)
  subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
 sheet=Image.new('RGB',(960,4*295),(20,22,28));draw=ImageDraw.Draw(sheet)
 for i,(case,t) in enumerate([('attack3',1.7),('attack3',5.35),('jump',2.2),('jump',5.95),('rotate',.55),('rotate',1.3),('damage',.5),('death',5.1)]):
  rows=[r for r in d['captures'] if r['case']==case];row=min(rows,key=lambda x:abs(x['time']-t));x=i%2*480;y=i//2*295
  with Image.open(row['file']) as im:sheet.paste(im.convert('RGB').resize((480,270)),(x,y+25))
  draw.text((x+8,y+6),f'{case} +{row["time"]:.2f}s',fill='white')
 sheet.save(OUT/'ContactSheet.jpg',quality=95)
 (OUT/'VideoManifest.json').write_text(json.dumps({'preview':str(video),'clips':clips,'provenance':'Genuine solo PIE SceneCapture2D, L_Mouth lighting and copied player-camera postprocess. Camera follows jump/roll for complete framing and stays fixed during turning. Normal simulation speed, 15 Hz capture, 30 fps encoding with repeated frames, captions only, silent. Stress scale-key reaction uses a temporary PIE-only binding and is excluded from this ordinary gameplay preview; its metrics and source frames remain in SoloCheck.json.'},ensure_ascii=False,indent=2),encoding='utf-8')
 print('KNIGHT_PLAYBACK_PREVIEW',str(video))
if __name__=='__main__':main()
