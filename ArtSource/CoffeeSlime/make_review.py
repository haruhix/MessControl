"""Assemble unchanged Blender renders into labelled review sheets."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json

root=Path(__file__).resolve().parent/'v1'; review=root/'review'
manifest=json.loads((root/'manifest.json').read_text())
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',20)
def sheet(paths,labels,out,columns=4,cell=320):
    rows=(len(paths)+columns-1)//columns
    im=Image.new('RGB',(columns*cell,rows*(cell+34)),(27,32,35)); draw=ImageDraw.Draw(im)
    for i,(p,label) in enumerate(zip(paths,labels)):
        src=Image.open(p).convert('RGB'); src.thumbnail((cell,cell))
        x=(i%columns)*cell+(cell-src.width)//2; y=(i//columns)*(cell+34)
        im.paste(src,(x,y)); draw.text(((i%columns)*cell+8,y+cell+5),label,fill=(236,231,220),font=font)
    im.save(out)
views=['hero','front','back','left','right','top','gameplay','fresh_glb','fresh_fbx']
sheet([review/f'{v}.png' for v in views],views,review/'multiview_sheet.png',3,400)
paths=[]; labels=[]
for n,c in manifest['clips'].items():
    for f in c['critical_frames']:
        paths.append(review/f'{n}_{f:03}.png'); labels.append(f"{n.split('_')[-1]} {(f-1)/100:.2f} s")
sheet(paths,labels,review/'motion_contact_sheet.png',6,300)
print('COFFEE_REVIEW_READY')
