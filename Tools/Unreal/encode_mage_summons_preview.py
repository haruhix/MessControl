"""Encode genuine solo summon footage and a small contact sheet."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json,subprocess,tempfile
import imageio_ffmpeg
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Artifacts/MageSummons_20261016'
def main():
 d=json.loads((OUT/'SoloCheck.json').read_text());assert d['passed'],d.get('error')
 ff=imageio_ffmpeg.get_ffmpeg_exe();font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22)
 with tempfile.TemporaryDirectory(prefix='mc_summon_encode_') as folder:
  folder=Path(folder);lines=['ffconcat version 1.0']
  for i,r in enumerate(d['captures']):
   p=folder/f'{i:04d}.png'
   with Image.open(r['file']) as source:
    im=source.convert('RGB');draw=ImageDraw.Draw(im);draw.rectangle((0,0,960,34),fill=(15,17,23))
    text={'summon':'Призыв колдуна','pursuit':'Новые приспешники — скорость 360 см/с','fuse':'Приспешника можно убить до взрыва'}[r['stage']]
    draw.text((12,4),text,font=font,fill=(250,235,200));im.save(p)
   duration=d['captures'][i+1]['time']-r['time'] if i+1<len(d['captures']) else .05
   assert 0<duration<.15,duration
   lines.extend([f"file '{p.as_posix()}'",f'duration {duration:.9f}'])
  lines.append(f"file '{p.as_posix()}'");playlist=folder/'preview.ffconcat';playlist.write_text('\n'.join(lines)+'\n')
  video=OUT/'preview.mp4'
  subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(playlist),'-vf','fps=30,format=yuv420p','-c:v','libx264','-crf','19','-movflags','+faststart',str(video)],check=True)
  subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
 sheet=Image.new('RGB',(960,3*295),(20,22,28));draw=ImageDraw.Draw(sheet)
 for i,t in enumerate([.6,1.3,1.8,2.5,4.7,6.0]):
  r=min(d['captures'],key=lambda r:abs(r['time']-t));x=i%2*480;y=i//2*295
  with Image.open(r['file']) as im:sheet.paste(im.convert('RGB').resize((480,270)),(x,y+25))
  draw.text((x+8,y+6),f'{r["stage"]} +{r["time"]:.2f}s',fill='white')
 sheet.save(OUT/'ContactSheet.jpg',quality=95)
 (OUT/'VideoManifest.json').write_text(json.dumps({'preview':str(video),'frames':len(d['captures']),'full_decode':True,'source':'Genuine solo PIE SceneCapture frames; normal simulation speed, 20 Hz capture / 30 fps encode with repeated frames. L_Mouth lighting and copied hero-camera postprocess. Review overview then close-up; Mage hidden in close-up to avoid occluding summons. Text captions added; no synthesized/interpolated poses. Silent.'},indent=2));print('MAGE_SUMMON_PREVIEW_ENCODED',str(video))
if __name__=='__main__':main()
