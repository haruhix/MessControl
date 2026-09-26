"""Compose rendered face reviews. Run with Python and Pillow after Blender previews."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'ArtSource/CharacterFace'
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',24)
title=ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf',34)
sheet=Image.new('RGB',(1280,530),(27,33,43));draw=ImageDraw.Draw(sheet)
draw.text((24,14),'Динамические зрачки',font=title,fill=(241,234,222))
for i,(name,label) in enumerate([('Calm','Спокойно'),('Danger','Опасность'),('Focus','Сосредоточен')]):
    frame=Image.open(ROOT/'Saved/PupilPreview'/(name+'.png')).convert('RGB').resize((408,408),Image.Resampling.LANCZOS)
    x=16+i*424;sheet.paste(frame,(x,68));draw.text((x+10,487),label,font=font,fill=(235,219,212))
sheet.save(OUT/'PupilStates.png')
src=ROOT/'Saved/MouthPreview'
names=[('Mouth_A','А'),('Mouth_E','Э / Е'),('Mouth_I','И'),('Mouth_O','О'),
       ('Mouth_U','У'),('Mouth_MBP','М / Б / П'),('Mouth_FV','Ф / В'),('Mouth_L','Л'),
       ('Mouth_TH','TH'),('Mouth_CH','Ч / Ш'),('Mouth_S','С'),('Mouth_D','Д / Т')]
sheet=Image.new('RGB',(1328,1192),(27,33,43));draw=ImageDraw.Draw(sheet)
draw.text((24,16),'Формы губ',font=title,fill=(241,234,222))
draw.text((24,61),'Губы и внутренняя полость • 12 речевых форм',font=font,fill=(179,190,207))
for i,(name,label) in enumerate(names):
    x=16+(i%4)*328;y=110+(i//4)*356
    frame=Image.open(src/(name+'.png')).convert('RGB').resize((312,312),Image.Resampling.LANCZOS)
    sheet.paste(frame,(x,y));draw.text((x+10,y+318),label,font=font,fill=(235,219,212))
sheet.save(OUT/'MouthForms.png')
frames=[Image.open(p).convert('RGB') for p in sorted((src/'Frames').glob('*.png'))]
assert len(frames)==72
palette=frames[7].quantize(colors=192);indexed=[f.quantize(palette=palette,dither=Image.Dither.NONE) for f in frames]
indexed[0].save(src/'MouthTransitions.gif',save_all=True,append_images=indexed[1:],duration=90,loop=0,optimize=True,disposal=2)
sheet=Image.new('RGB',(1000,1030),(27,33,43));draw=ImageDraw.Draw(sheet)
draw.text((20,14),'Рот: исправленный стык с лицом',font=title,fill=(241,234,222))
for i,(name,label) in enumerate([('Basis','В покое'),('Mouth_A','Раскрытый рот')]):
    frame=Image.open(ROOT/'Saved/MouthCloseup'/(name+'.png')).convert('RGB')
    y=70+i*480;sheet.paste(frame,(0,y));draw.text((20,y+12),label,font=font,fill=(45,35,35))
sheet.save(OUT/'MouthJunction.png')
print('MC_FACE_PREVIEWS_ASSEMBLED',len(indexed),'frames')
