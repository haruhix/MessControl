"""Combine validated Melee/Jump and the new Charge/Roll review, read-only.

Run with Python after preview_nut_tank_abilities_v2.py has finished.
Only the combined movie and its report are written.
"""
import hashlib, json, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Tank'
FFMPEG = 'C:/ffmpeg/ffmpeg.exe'
FFPROBE = 'C:/ffmpeg/ffprobe.exe'
old_video = OUT / 'Tank_Style_Review.mp4'
new_video = OUT / 'Tank_Abilities_v2_Review.mp4'
destination = OUT / 'Tank_AllAbilities_v2.mp4'
old_report = json.loads((OUT / 'PreviewReport.json').read_text(encoding='utf8'))
new_report = json.loads((OUT / 'Tank_Abilities_v2_PreviewReport.json').read_text(encoding='utf8'))
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = {str(p): digest(p) for p in (old_video, new_video)}
melee = next(s for s in old_report['sections'] if s['label'] == 'Melee')
jump = next(s for s in old_report['sections'] if s['label'] == 'Jump')
first, end = melee['start_index'], jump['end_index'] + 1
prefix_frames = end - first
filters = (f'[0:v]trim=start_frame={first}:end_frame={end},setpts=PTS-STARTPTS,'
           'scale=640:640,pad=760:640:60:0:color=0x15181e,setsar=1,fps=25,format=yuv420p[a];'
           '[1:v]setpts=PTS-STARTPTS,scale=760:640,setsar=1,fps=25,format=yuv420p[b];'
           '[a][b]concat=n=2:v=1:a=0[out]')
subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y',
                '-i', str(old_video), '-i', str(new_video), '-filter_complex', filters,
                '-map', '[out]', '-c:v', 'libx264', '-threads', '2', '-crf', '19',
                '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(destination)], check=True)
probe = json.loads(subprocess.check_output([FFPROBE, '-v', 'error', '-select_streams', 'v:0',
    '-show_entries', 'stream=codec_name,width,height,avg_frame_rate,nb_frames',
    '-show_entries', 'format=duration', '-of', 'json', str(destination)], text=True))
assert int(probe['streams'][0]['nb_frames']) == prefix_frames + new_report['frames']
assert before == {str(p): digest(p) for p in (old_video, new_video)}
sections = [{'ability': 'Melee', 'start_seconds': 0,
             'end_seconds': (melee['end_index'] - first + 1) / 25},
            {'ability': 'Jump', 'start_seconds': (jump['start_index'] - first) / 25,
             'end_seconds': prefix_frames / 25}]
report = {'video': str(destination), 'probe': probe, 'source_videos_preserved': True,
          'source_videos_sha256': before, 'prefix_frames': prefix_frames,
          'new_charge_roll_frames': new_report['frames'], 'sections': sections,
          'charge_roll_start_seconds': prefix_frames / 25,
          'ability_coverage': ['Melee', 'Jump', 'Charge', 'Roll'],
          'scope': 'Blender animation/presentation review. Game VFX, collision, push physics, network and new animation slot integration are not validated.'}
(OUT / 'Tank_AllAbilities_v2_Report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('TANK_ALL_ABILITIES_V2_COMPLETE', json.dumps({'video': str(destination), 'probe': probe}))
