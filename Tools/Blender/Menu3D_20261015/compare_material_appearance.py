"""Fixed-camera appearance diagnostics; scores are not an art acceptance claim."""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np
from PIL import Image,ImageDraw

ROOT=Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
p=argparse.ArgumentParser();p.add_argument('--render',required=True);p.add_argument('--out',required=True);opt=p.parse_args()
out=Path(opt.out);out.mkdir(parents=True,exist_ok=True)
source=np.array(Image.open(ROOT/'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
render=np.array(Image.open(opt.render).convert('RGB').resize((963,410),Image.Resampling.LANCZOS))
base=ROOT/'Evidence/SourceAnalysis/CanonicalOpen'
def mask(path):return cv2.imread(str(path),cv2.IMREAD_GRAYSCALE)>127
iv=json.loads((base/'TeethRefined/part_inventory.json').read_text(encoding='utf8'))
enamel=np.zeros(source.shape[:2],bool)
for part in iv['parts']:enamel|=mask(part['mask'])
exclusion=mask(base/'InteriorRefined/ui_occlusion.png')|mask(base/'InteriorRefined/character_occlusion.png')
zones={'enamel_visible':enamel&~exclusion,'tongue_visible':mask(base/'InteriorRefined/tongue_visible_high_confidence.png'),
       'mucosa_visible':mask(base/'InteriorRefined/mucosa_visible_high_confidence.png')}
# Gum source masks have a documented separate uncertainty/exclusion stage.
g=ROOT/'Textures/Revisions/v05_SourceLocked/Gingiva'
for name in ('upper_gingiva_visible.png','lower_gingiva_visible_strict.png'):
    if (g/name).exists():zones[name.removesuffix('.png')]=mask(g/name)&~exclusion
lab_source=cv2.cvtColor(source.astype(np.float32)/255,cv2.COLOR_RGB2Lab)
lab_render=cv2.cvtColor(render.astype(np.float32)/255,cv2.COLOR_RGB2Lab)
delta=np.linalg.norm(lab_source-lab_render,axis=2)
records={}
for name,valid in zones.items():
    # Remove only segmentation edges, which can mix materials after raster AA.
    valid=cv2.erode(valid.astype(np.uint8),np.ones((3,3),np.uint8),borderType=cv2.BORDER_CONSTANT,borderValue=0)>0
    records[name]={'pixels':int(valid.sum()),'source_median_srgb':np.median(source[valid],axis=0).tolist(),
      'render_median_srgb':np.median(render[valid],axis=0).tolist(),'deltaE76_median':float(np.median(delta[valid])),
      'deltaE76_p95':float(np.percentile(delta[valid],95)),
      'mean_absolute_srgb_error_255':float(np.abs(source[valid].astype(float)-render[valid]).mean())}
union=np.logical_or.reduce(list(zones.values()));heat=cv2.applyColorMap(np.uint8(np.clip(delta/30,0,1)*255),cv2.COLORMAP_TURBO)
heat=cv2.cvtColor(heat,cv2.COLOR_BGR2RGB);heat[~union]=[20,20,22]
sheet=Image.new('RGB',(963,3*446),(18,18,20));d=ImageDraw.Draw(sheet)
for i,(label,im)in enumerate([('SOURCE / fixed canonical crop',source),('ACTUAL RENDER / same projection',render),('APPEARANCE DIFFERENCE / material masks, excluded UI and character',heat)]):
    d.text((12,i*446+10),label,fill='white');sheet.paste(Image.fromarray(im),(0,i*446+36))
sheet.save(out/'material_appearance_comparison.png')
report={'render':opt.render,'regions':records,'limitations':['The reference is a lit concept. These are appearance differences, not recovered albedo/roughness ground truth.','UI/character excluded, source segmentation and hidden geometry remain uncertain.','No1:1completion follows from these numbers; actual assembled visual review is mandatory.']}
(out/'appearance_comparison.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'out':str(out),'regions':records}))
