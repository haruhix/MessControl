"""Three-second Branch9 timing study (offline compositing, not Unreal footage).

Uses the exact supplied transparent layers. All transient frames/logs stay on E:.
The runtime can reproduce this with UMG image transforms and one reveal mask.
"""
from pathlib import Path
import json, math, subprocess
import numpy as np
from PIL import Image, ImageChops, ImageDraw

ROOT=Path(__file__).resolve().parent
OUT=Path('E:/CodexWork/MessControlFrontEndBranch9_20261021/Art')
OUT.mkdir(parents=True,exist_ok=True)
SIZE=(1280,720); FPS=60
LOGO=(960,360)

def ease(t): return 1-(1-max(0,min(1,t)))**3
def smooth(t):
 t=max(0,min(1,t)); return t*t*(3-2*t)
def load(name): return Image.open(ROOT/f'T_Branch9_{name}.png').convert('RGBA').resize(LOGO,Image.Resampling.LANCZOS)
layers={k:load(k) for k in ['Wordmark','Branch','Leaves','Logo']}
xx=np.arange(LOGO[0],dtype=np.float32)[None,:]

def revealed(im,progress,soft=28):
 # A soft leading edge, never a resize/stretch of the source logo.
 edge=-soft+(LOGO[0]+2*soft)*progress
 a=np.clip((edge-xx)/soft,0,1);a=a*a*(3-2*a)
 wipe=Image.fromarray(np.repeat((a*255).astype('uint8'),LOGO[1],axis=0))
 out=im.copy();out.putalpha(ImageChops.multiply(out.getchannel('A'),wipe));return out

def alpha(im,value):
 out=im.copy();out.putalpha(im.getchannel('A').point(lambda v:int(v*max(0,min(1,value)))));return out

def frame(t):
 # Nearly-black neutral background is consistent with a native UMG full-screen
 # overlay. No particles, fake lens flares or high-frequency flicker.
 out=Image.new('RGBA',SIZE,(6,7,8,255))
 composite=Image.new('RGBA',LOGO,(255,255,255,0))
 p=ease((t-.10)/.42)
 word=revealed(layers['Wordmark'],p,34)
 if t<.52:
  # Low-amplitude directional velocity echo; full letters remain rigid.
  echo=alpha(word,.10*(1-p));composite.alpha_composite(echo,(-14,0))
 composite.alpha_composite(word)
 branch=revealed(layers['Branch'],ease((t-.29)/.36),22)
 composite.alpha_composite(branch,(round(-8*(1-ease((t-.29)/.42))),round(3*(1-ease((t-.29)/.42)))))
 leaves=alpha(layers['Leaves'],smooth((t-.43)/.30))
 # One controlled two-degree settle affects the leaf layer only.
 rot=1.6*(1-ease((t-.43)/.42))
 if rot: leaves=leaves.rotate(rot,Image.Resampling.BICUBIC,expand=False,center=(LOGO[0]*.66,LOGO[1]*.52))
 composite.alpha_composite(leaves)
 fade=1-smooth((t-2.61)/.37)
 composite=alpha(composite,fade)
 settle=1+.012*(1-ease((t-.10)/.58))
 if abs(settle-1)>.0001:
  wh=(round(LOGO[0]*settle),round(LOGO[1]*settle));composite=composite.resize(wh,Image.Resampling.LANCZOS)
 x=(SIZE[0]-composite.width)//2+round(18*smooth((t-2.61)/.37))
 y=(SIZE[1]-composite.height)//2
 out.alpha_composite(composite,(x,y));return out.convert('RGB')

mp4=OUT/'Branch9_MotionStudy_3s.mp4'
cmd=['C:/ffmpeg/ffmpeg.exe','-hide_banner','-y','-f','rawvideo','-pixel_format','rgb24','-video_size','1280x720','-framerate',str(FPS),'-i','-','-an','-c:v','libx264','-preset','medium','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(mp4)]
with (OUT/'MotionStudyEncode.log').open('wb') as log:
 proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=log,stderr=log)
 for i in range(FPS*3):proc.stdin.write(frame(i/FPS).tobytes())
 proc.stdin.close();code=proc.wait()
 if code:raise RuntimeError(f'ffmpeg exit {code}')

times=[0,.15,.35,.6,1.0,2.2,2.7,3.0]
sheet=Image.new('RGB',(1280,2*202),(22,23,25));draw=ImageDraw.Draw(sheet)
for i,t in enumerate(times):
 im=frame(t).resize((320,180),Image.Resampling.LANCZOS);x=(i%4)*320;y=(i//4)*202
 sheet.paste(im,(x,y));draw.text((x+10,y+183),f'{t:.2f}s',(220,220,220))
sheet.save(OUT/'Branch9_MotionContactSheet.png')
probe=subprocess.run(['C:/ffmpeg/ffprobe.exe','-v','error','-count_frames','-show_entries','stream=width,height,nb_read_frames,r_frame_rate,duration','-of','json',str(mp4)],capture_output=True,text=True,check=True)
data=json.loads(probe.stdout);data['kind']='offline vector compositing timing study; not Unreal runtime footage';data['frame_count_expected']=180
assert int(data['streams'][0]['nb_read_frames'])==180
subprocess.run(['C:/ffmpeg/ffmpeg.exe','-v','error','-i',str(mp4),'-f','null','-'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,check=True)
(OUT/'MotionStudy.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
print(mp4)
