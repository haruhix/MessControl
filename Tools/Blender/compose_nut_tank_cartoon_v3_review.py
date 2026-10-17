"""Compose saved playblasts and rendered frames; does not open/change Blender."""
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/TankCartoonV3'
FFMPEG = 'C:/ffmpeg/ffmpeg.exe'
FFPROBE = 'C:/ffmpeg/ffprobe.exe'
FONT = 'C:/Windows/Fonts/arial.ttf'


def footer(label):
    return ("drawbox=x=0:y=348:w=iw:h=36:color=black@0.72:t=fill,"
        "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='"+label+
        "':expansion=none:x=12:y=358:fontsize=16:fontcolor=white")


def main():
    video = OUT / 'Tank_Cartoon_Review.mp4'
    filters = ('[0:v]setpts=PTS-STARTPTS,'+footer('WALK | 1.92s cycle | 2 repeats')+'[walk];'
        '[1:v]setpts=PTS-STARTPTS,'+footer('MELEE | 0.65s windup + 2.50s recovery | impact 50%')+'[melee];'
        '[2:v]setpts=PTS-STARTPTS,pad=768:384:192:0:color=0x343c49,'+
        footer('HIT | 0.40s | 5 repeats | new authored clip')+'[hit];'
        '[walk][melee][hit]concat=n=3:v=1:a=0[out]')
    subprocess.run([FFMPEG,'-hide_banner','-loglevel','error','-y',
        '-stream_loop','1','-i',str(OUT/'Walk_Old_New.mp4'),
        '-i',str(OUT/'Melee_Old_New.mp4'),
        '-stream_loop','4','-i',str(OUT/'Hit_New.mp4'),
        '-filter_complex',filters,'-map','[out]','-c:v','libx264','-crf','18',
        '-pix_fmt','yuv420p','-movflags','+faststart',str(video)],check=True)

    poster=Image.new('RGB',(768,1040),(25,30,39))
    draw=ImageDraw.Draw(poster)
    title=ImageFont.truetype(FONT,29)
    text=ImageFont.truetype(FONT,21)
    small=ImageFont.truetype(FONT,16)
    draw.text((24,16),'Tank cartoon animation | first pass',font=title,fill='white')
    draw.text((24,56),'Same rig / camera. Left: previous clip. Right: new clip.',font=small,fill=(186,197,214))
    for label,frame,y,subtitle in (
        ('Walk',2,90,'1.92s cycle | heel contact and weight transfer'),
        ('Melee',9,410,'0.65s windup / 2.50s recovery | contact at 50%')):
        draw.text((24,y),label+' | '+subtitle,font=text,fill=(237,194,110))
        for column,version in enumerate(('Old','New')):
            picture=Image.open(OUT/'PreviewFrames'/f'{label}_{version}_{frame:03}.png').convert('RGB')
            picture=picture.resize((288,288),Image.Resampling.LANCZOS)
            x=48+384*column
            poster.paste(picture,(x,y+29))
            draw.text((x+10,y+38),version,font=small,fill='white')
    draw.text((24,735),'Hit | new clip, 0.40s | compression / rebound / settle',font=text,fill=(237,194,110))
    for column,(frame,label) in enumerate(((3,'Compression'),(5,'Rebound'),(7,'Settle'))):
        picture=Image.open(OUT/f'Hit_{frame:03}.png').convert('RGB')
        picture=picture.resize((224,224),Image.Resampling.LANCZOS)
        x=16+256*column
        poster.paste(picture,(x,772))
        draw.text((x+8,778),label,font=small,fill='white')
    draw.text((24,1011),'Blender preview. Native overlays, collision and VFX require game validation.',
        font=small,fill=(186,197,214))
    poster.save(OUT/'Tank_Cartoon_Comparison.png')
    probe=json.loads(subprocess.check_output([FFPROBE,'-v','error','-show_entries',
        'format=duration','-show_entries','stream=width,height,r_frame_rate','-of','json',str(video)],text=True))
    (OUT/'CompactReviewReport.json').write_text(json.dumps({'video':video.name,
        'poster':'Tank_Cartoon_Comparison.png','probe':probe,'walk_repeats':2,'hit_repeats':5,
        'native_timing':{'walk_cycle':1.92,'melee_windup':.65,'melee_recovery':2.5,'hit':.4},
        'authored_actions_modified':False,'limitation':'15fps playblast quantizes nominal game timing; no previous authored Tank Hit clip.'},indent=2),encoding='utf8')
    print(json.dumps({'video':str(video),'poster':str(OUT/'Tank_Cartoon_Comparison.png'),'probe':probe}))


if __name__=='__main__':main()
