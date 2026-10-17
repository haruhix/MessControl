"""Encode genuine solo capture frames at their recorded game-clock spacing."""
from pathlib import Path
import json,subprocess,tempfile
from PIL import Image
import imageio_ffmpeg

root=Path(__file__).resolve().parents[2]
out=root/'Artifacts/NutWizardUpdate_20261017'
data=json.loads((out/'SoloCheck.json').read_text())
assert data['passed'],data.get('error')
frames=data['captures']
ff=imageio_ffmpeg.get_ffmpeg_exe();video=out/'preview.mp4'
with tempfile.TemporaryDirectory(prefix='mc_wizard_encode_') as temporary:
    folder=Path(temporary);lines=['ffconcat version 1.0']
    for i,row in enumerate(frames):
        # Normalize Unreal's PNG container for this ffmpeg decoder. Preserve
        # every source RGB pixel; no captions, paint or synthetic motion.
        path=folder/f'{i:04}.png'
        with Image.open(row['file']) as source:source.convert('RGB').save(path)
        duration=frames[i+1]['game_time']-row['game_time'] if i+1<len(frames) else 1/15
        assert 0<duration<.2,duration
        lines.extend([f"file '{path.as_posix()}'",f'duration {duration:.9f}'])
    lines.append(f"file '{path.as_posix()}'")
    playlist=folder/'preview.ffconcat';playlist.write_text('\n'.join(lines)+'\n')
    subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(playlist),'-vf','fps=30,format=yuv420p','-c:v','libx264','-crf','19','-movflags','+faststart',str(video)],check=True)
subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
cases={}
for row in frames:
    entry=cases.setdefault(row['case'],dict(start=row['game_time']-frames[0]['game_time'],end=0))
    entry['end']=row['game_time']-frames[0]['game_time']
(out/'VideoManifest.json').write_text(json.dumps(dict(video=str(video),frames=len(frames),full_decode=True,cases=cases,source='Genuine solo PIE SceneCapture frames under L_Mouth lighting, copied hero-camera postprocess; 15 Hz capture repeated at 30 fps, original game-clock timing, no generated/interpolated poses, silent.'),indent=2))
print('WIZARD_PREVIEW_ENCODED',video,len(frames))
