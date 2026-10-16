"""Assemble existing renders/reference crops for QA; no generated pixels."""
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
import json

BASE = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
OUT = BASE/'Assembly/UnrealHandoff/v08_ReferenceFinish/Interior/Organic_r06_LowTongue/Refine02'
ref_path = BASE/'References/B_FullWidth_Approved.png'
ref = Image.open(ref_path).convert('RGB').crop((978, 338, 1942, 742))
ref.save(OUT/'reference_open_mouth_crop.png')
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 24)
small = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 18)
sheet = Image.new('RGB', (1500, 1110), '#17191b')
draw = ImageDraw.Draw(sheet)
draw.text((24, 15), 'Tongue shape review — r06 Refine02 / geometry candidate only', font=font, fill='white')
panels = [
    ('APPROVED REFERENCE / oral ROI', ref),
    ('r05 BEFORE / identical clay camera', Image.open(OUT/'r05_before_matched_front.png').convert('RGB').crop((136, 122, 1464, 790))),
    ('r06 AFTER / continuous low tongue bed', Image.open(OUT/'r06_after_matched_front.png').convert('RGB').crop((136, 122, 1464, 790))),
]
for row, (label, image) in enumerate(panels):
    top = 54+row*336
    draw.text((24, top), label, font=small, fill='#98ecd5')
    image.thumbnail((1452, 298), Image.Resampling.LANCZOS)
    sheet.paste(image, ((1500-image.width)//2, top+30))
draw.text((24, 1072), 'Reference and render crops retain their aspect ratios. UV0/underside/cavity unchanged; new tongue mesh maps need a rebake.', font=small, fill='#b7bec2')
sheet.save(OUT/'reference_before_after_sheet.png')
(OUT/'review_crop_contract.json').write_text(json.dumps({'reference': str(ref_path), 'reference_box': [978,338,1942,742], 'render_box': [136,122,1464,790], 'cropping_only_no_warp': True, 'scope': 'Concept oral ROI and camera-matched geometry before/after. Different reference/frame aspect ratios preserved. Not a claimed 1:1 fit.'}, indent=2))
print(OUT/'reference_before_after_sheet.png')
