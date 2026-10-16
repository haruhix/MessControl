"""Existing-image QA crops for root's Refine03 form gate. No resculpt/warping."""
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
import json

BASE = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
OUT = BASE/'Assembly/UnrealHandoff/v08_ReferenceFinish/Interior/Organic_r06_LowTongue/Refine03'
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 25)
small = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 20)
sheet = Image.new('RGB', (1800, 1340), '#17191b')
draw = ImageDraw.Draw(sheet)
draw.text((20, 14), 'Refine03 shape gate / 28% of removed r05 posterolateral fullness, centre bed retained', font=font, fill='white')
ref = Image.open(BASE/'References/B_FullWidth_Approved.png').convert('RGB').crop((978,338,1942,742))
ref.thumbnail((864,360), Image.Resampling.LANCZOS)
sheet.paste(ref, ((1800-ref.width)//2, 85))
draw.text((20, 57), 'REFERENCE oral ROI — aspect preserved; no 1:1 claim', font=small, fill='#9be8d4')
for row, view, box in [(0, 'matched_front', (136,122,1464,790)), (1, 'matched_oblique', (80,72,1420,780))]:
    for col, prefix, label in [(0, 'refine02_before', 'REFINE02'), (1, 'r06_after', 'REFINE03')]:
        top = 468+row*402
        draw.text((20+col*900,top), label+' / '+view, font=small, fill='#9be8d4')
        image = Image.open(OUT/(prefix+'_'+view+'.png')).convert('RGB').crop(box)
        image.thumbnail((864,356), Image.Resampling.LANCZOS)
        sheet.paste(image,(col*900+(900-image.width)//2,top+32))
draw.text((20,1300), 'Identical camera, neutral materials and lighting per pair. Frozen cavity/UV/underside unchanged. Form gate before UV repair or export.', font=small, fill='#b7bec2')
sheet.save(OUT/'refine02_vs_refine03_front_oblique_sheet.png')
ref.save(OUT/'reference_open_mouth_crop.png')
(OUT/'review_crop_contract.json').write_text(json.dumps({'reference_box':[978,338,1942,742],'front_box':[136,122,1464,790],'oblique_box':[80,72,1420,780],'aspect_preserved':True,'scope':'Shape-only form comparison; no claimed fit/texture/native-runtime validation'},indent=2))
print(OUT/'refine02_vs_refine03_front_oblique_sheet.png')
