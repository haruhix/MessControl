"""Encode actual native Slate frames with their measured presentation times."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('capture_directory',type=Path)
parser.add_argument('--ffmpeg',default='C:/ffmpeg/ffmpeg.exe')
args=parser.parse_args()
directory=args.capture_directory.resolve()
allowed=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review').resolve()
assert directory.is_relative_to(allowed)
receipt=json.loads((directory/'capture_receipt.json').read_text(encoding='utf8'))
frames=receipt['frames'];assert len(frames)>1
intervals=[b['time']-a['time'] for a,b in zip(frames,frames[1:])]
assert min(intervals)>0
manifest=directory/'actual_timing.ffconcat'
lines=['ffconcat version 1.0']
for index,frame in enumerate(frames):
    path=(directory/frame['file']).resolve()
    assert path.is_relative_to(directory) and path.is_file()
    assert "'" not in str(path)
    duration=intervals[index] if index<len(intervals) else statistics.median(intervals)
    lines += ["file '"+path.as_posix()+"'",'duration '+format(duration,'.9f')]
lines += ["file '"+(directory/frames[-1]['file']).as_posix()+"'"]
manifest.write_text('\n'.join(lines)+'\n',encoding='utf8')
output=directory/'NativeMenuBlocking.mp4'
subprocess.run([args.ffmpeg,'-hide_banner','-loglevel','error','-y','-safe','0','-f','concat','-i',str(manifest),
    '-vf','crop=1280:720:3:36,fps=30','-c:v','libx264','-preset','medium','-crf','19','-pix_fmt','yuv420p',
    '-movflags','+faststart',str(output)],check=True)
report={'video':str(output),'source':'Unaltered live Unreal PIE frames; only window chrome cropped',
    'frames':len(frames),'measured_duration':frames[-1]['time']-frames[0]['time'],
    'median_capture_interval':statistics.median(intervals),'max_capture_interval':max(intervals),
    'output_dimensions':[1280,720],'timing':'Measured per-frame durations, no invented intermediate poses'}
(directory/'encoded_video_receipt.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
