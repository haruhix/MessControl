"""Encode the genuine knight-to-wizard solo capture at normal simulation speed."""
from pathlib import Path
import json,subprocess,tempfile
from PIL import Image,ImageDraw,ImageFont
import imageio_ffmpeg

ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Artifacts/NutSequentialPhases_20261017'
LABELS={'entry':'Появляется только Ореховый Рыцарь','knight':'Первая фаза — Рыцарь',
        'split':'Голова раскалывается: Рыцарь превращается в Колдуна',
        'wizard':'Вторая фаза — Ореховый Колдун','defeat_wizard':'Победа над Колдуном','victory':'Обе формы побеждены — событие завершено'}

def main():
    data=json.loads((OUT/'SoloCheck.json').read_text());assert data['passed'],data.get('error')
    ff=imageio_ffmpeg.get_ffmpeg_exe();font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',21)
    with tempfile.TemporaryDirectory(prefix='mc_nut_phases_') as directory:
        tmp=Path(directory);lines=['ffconcat version 1.0']
        for i,row in enumerate(data['captures']):
            path=tmp/f'{i:05}.png'
            with Image.open(row['file']) as source:
                im=source.convert('RGB');draw=ImageDraw.Draw(im);draw.rectangle((0,0,960,32),fill=(15,17,23))
                draw.text((10,3),LABELS[row['case']],font=font,fill=(250,235,200));im.save(path)
            duration=data['captures'][i+1]['game_time']-row['game_time'] if i+1<len(data['captures']) else 1/15
            assert 0<duration<.2,duration
            lines.extend([f"file '{path.as_posix()}'",f'duration {duration:.9f}'])
        lines.append(f"file '{path.as_posix()}'");playlist=tmp/'frames.ffconcat';playlist.write_text('\n'.join(lines)+'\n')
        video=OUT/'preview.mp4'
        subprocess.run([ff,'-v','error','-xerror','-y','-f','concat','-safe','0','-i',str(playlist),'-vf','fps=30,format=yuv420p','-c:v','libx264','-crf','19','-movflags','+faststart',str(video)],check=True)
        subprocess.run([ff,'-v','error','-xerror','-i',str(video),'-f','null','-'],check=True)
    sheet=Image.new('RGB',(1440,2*300),(15,17,23));draw=ImageDraw.Draw(sheet)
    for i,(case,t) in enumerate([('knight',.3),('split',.6),('split',1.1),('split',1.7),('wizard',2.3),('victory',.7)]):
        rows=[r for r in data['captures'] if r['case']==case];r=min(rows,key=lambda r:abs(r['time']-t));x=i%3*480;y=i//3*300
        with Image.open(r['file']) as im:sheet.paste(im.convert('RGB').resize((480,270)),(x,y+30))
        caption={'knight':'Рыцарь','split':'Раскол и превращение','wizard':'Колдун','victory':'Победа'}[case]
        draw.text((x+8,y+3),f"{caption} +{r['time']:.2f}s",font=font,fill='white')
    sheet.save(OUT/'ContactSheet.jpg',quality=95)
    (OUT/'VideoManifest.json').write_text(json.dumps({'preview':str(video),'frames':len(data['captures']),'full_decode':True,
        'source':'Genuine solo PIE SceneCapture2D under L_Mouth lighting at 15 Hz. Normal simulation speed, 30 fps output repeats frames. Captions added. Boss health/shield fixtures are documented in SoloCheck; transition, attacks and real input are native. Silent.'},indent=2))
    print('NUT_PHASE_PREVIEW',video)

if __name__=='__main__':main()
