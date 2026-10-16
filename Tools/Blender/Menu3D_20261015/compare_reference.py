"""Diagnostic crops and material-region statistics; never generates artwork."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
p=argparse.ArgumentParser();p.add_argument('--render',required=True);p.add_argument('--label',default='candidate');a=p.parse_args()
out=ROOT/'Evidence'/'ReferenceComparison'/a.label;out.mkdir(parents=True,exist_ok=True)
ref=Image.open(ROOT/'References/B_FullWidth_Approved.png').convert('RGB').crop((979,331,1942,741))
candidate=Image.open(a.render).convert('RGB')
ar=np.asarray(candidate)
# Black world is not the body. A bbox is diagnostic only; it cannot certify fit.
mask=(ar.max(axis=2)>35)&((ar.max(axis=2)-ar.min(axis=2)>10)|(ar.min(axis=2)>65))
yy,xx=np.nonzero(mask)
bbox=(int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1))
crop=candidate.crop(bbox)
def stats(im):
    x=np.asarray(im).astype(float)/255
    r,g,b=x[:,:,0],x[:,:,1],x[:,:,2]
    # Approximate diffuse regions exclude bright specular pixels; never part IDs.
    zones={'cream_enamel':(g/r.clip(.01)>.66)&(b/r.clip(.01)>.48)&(r>.48)&(r<.96),
           'coral_flesh':(g/r.clip(.01)<.62)&(b/r.clip(.01)<.72)&(r>.30)}
    return {n:{'pixels':int(m.sum()),'median_srgb':np.median(x[m],axis=0).tolist() if m.any() else None}for n,m in zones.items()}
width=1000
views=[('APPROVED REFERENCE / open vampire',ref),('CANDIDATE / '+a.label,crop)]
heights=[round(im.height*width/im.width)for _,im in views]
sheet=Image.new('RGB',(width,sum(heights)+104),(20,20,22));d=ImageDraw.Draw(sheet);y=0
for (label,im),h in zip(views,heights):
    d.text((16,y+10),label,fill=(245,238,222));y+=36
    sheet.paste(im.resize((width,h),Image.Resampling.LANCZOS),(0,y));y+=h+16
sheet.save(out/'reference_vs_candidate.png')
ref.save(out/'reference_open_crop.png');crop.save(out/'candidate_crop.png')
report={'label':a.label,'render':a.render,'candidate_body_bbox':bbox,
 'reference_crop_aspect':ref.width/ref.height,'candidate_body_aspect':crop.width/crop.height,
 'relative_aspect_error':(crop.width/crop.height)/(ref.width/ref.height)-1,
 'reference_color_zones':stats(ref),'candidate_color_zones':stats(crop),
 'limitations':['Concept reference includes UI and occlusion; masks are approximate material-color regions, not segmentation truth.','No exact anatomy or pixel-match score is claimed; human visual review controls acceptance.']}
(out/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'sheet':str(out/'reference_vs_candidate.png'),'report':report}))
