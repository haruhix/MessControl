"""Encode the inspected Blender sequence. Frame timing here is review-only, not UE clips."""
from pathlib import Path
import json, subprocess

review=Path(__file__).resolve().parent/'v1'/'review'
meta=json.loads((review/'motion_sequence.json').read_text())
ranges=[('Idle',0,57),('Hop',58,123),('Hurt',124,137),('PreBurst',138,152),('Burst',153,157)]
filters=['drawbox=x=0:y=0:w=iw:h=46:color=black@0.55:t=fill']
for name,a,b in ranges:
    filters.append(f"drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='{name}':x=20:y=12:fontsize=23:fontcolor=white:enable='between(n,{a},{b})'")
video=review/'coffee_slime_motion.mp4'
subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','warning','-y','-framerate',str(meta['fps']),
    '-i',str(review/'motion_frames'/'%04d.png'),'-vf',','.join(filters),'-c:v','libx264','-crf','18','-pix_fmt','yuv420p',
    '-movflags','+faststart',str(video)],check=True)
probe=subprocess.check_output(['C:/ffmpeg/ffprobe.exe','-v','error','-show_entries','stream=width,height,nb_frames,r_frame_rate:format=duration,size','-of','json',str(video)],text=True)
(review/'preview_video.json').write_text(probe,encoding='utf-8')
print('COFFEE_VIDEO_READY',str(video),probe)
