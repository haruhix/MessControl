"""Encode the corrected knight's genuine solo PIE capture at simulation speed."""
from pathlib import Path
import json, subprocess, tempfile
from PIL import Image, ImageDraw, ImageFont
import imageio_ffmpeg

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Artifacts/NutKnightCorrected_20261016'
LABELS={'idle':'Ожидание','attack1':'Удар 1','attack2':'Удар 2','attack3':'Комбо',
        'jump':'Прыжок','walk':'Ходьба','roll':'Вход в шар и выход из шара',
        'retarget':'Поворот головы при смене цели','block':'Блок щитом','rear_hit':'Урон со спины',
        'break':'Разрушение щита','stun':'Стан и реакция на урон','death':'Смерть'}

def main():
    data=json.loads((OUT/'SoloCheck.json').read_text());assert data['passed'],data.get('error')
    ff=imageio_ffmpeg.get_ffmpeg_exe()
    font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22)
    with tempfile.TemporaryDirectory(prefix='mc_knight_corrected_') as directory:
        tmp=Path(directory);playlist=['ffconcat version 1.0']
        for i,row in enumerate(data['captures']):
            path=tmp/f'{i:05}.jpg'
            with Image.open(row['file']) as source:
                im=source.convert('RGB');draw=ImageDraw.Draw(im)
                draw.rectangle((0,0,960,34),fill=(15,17,23))
                draw.text((12,4),LABELS[row['case']],font=font,fill=(250,235,200))
                im.save(path,quality=97)
            duration=data['captures'][i+1]['game_time']-row['game_time'] if i+1<len(data['captures']) else 1/15
            assert 0<duration<.2,duration
            playlist.extend([f"file '{path.as_posix()}'",f'duration {duration:.9f}'])
        playlist.append(f"file '{path.as_posix()}'")
        concat=tmp/'frames.ffconcat';concat.write_text('\n'.join(playlist)+'\n')
        video=OUT/'preview.mp4'
        subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(concat),'-vf','fps=30,format=yuv420p','-c:v','libx264','-crf','19','-movflags','+faststart',str(video)],check=True)
        subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
    sheet=Image.new('RGB',(1440,4*300),(20,22,28));draw=ImageDraw.Draw(sheet)
    picks=[('idle',.3),('attack1',.6),('attack2',.6),('attack3',1.5),('jump',1.1),('roll',.7),
           ('roll',3.6),('retarget',5.7),('block',.5),('rear_hit',.6),('stun',1.3),('death',3.2)]
    for i,(case,t) in enumerate(picks):
        rows=[r for r in data['captures'] if r['case']==case];row=min(rows,key=lambda r:abs(r['time']-t))
        x=(i%3)*480;y=(i//3)*300
        with Image.open(row['file']) as im:sheet.paste(im.convert('RGB').resize((480,270)),(x,y+30))
        draw.text((x+8,y+3),f"{LABELS[case]} +{row['time']:.2f}s",font=font,fill='white')
    sheet.save(OUT/'ContactSheet.jpg',quality=95)
    (OUT/'VideoManifest.json').write_text(json.dumps({'preview':str(video),'frames':len(data['captures']),'full_decode':True,
        'source':'Genuine solo PIE SceneCapture2D under L_Mouth lighting, 15 Hz capture at normal simulation speed; 30 fps encoding repeats frames. Captions added. No generated poses. Silent. Jump can leave the fixed review framing.'},indent=2))
    print('KNIGHT_CORRECTED_PREVIEW',video)

if __name__=='__main__':main()
