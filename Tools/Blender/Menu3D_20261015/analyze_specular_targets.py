"""Measure observed upper-crown highlight targets; these are light, not normals."""
import json
from pathlib import Path
import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT=Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
out=ROOT/'Evidence/SourceAnalysis/SpecularTargets'
out.mkdir(parents=True,exist_ok=True)
inv=json.loads((ROOT/'Evidence/SourceAnalysis/CanonicalOpen/TeethRefined/Vector_v10/part_inventory.json').read_text())
source=np.asarray(Image.open(ROOT/'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
overlay=Image.fromarray(source.copy());draw=ImageDraw.Draw(overlay)
parts=[]
for part in inv['parts']:
    name=part.get('name',part.get('part',''))
    if not name.startswith('Upper_'):
        continue
    mask=np.asarray(Image.open(part['mask']).convert('L'))>127
    eroded=cv2.erode(mask.astype('uint8'),np.ones((3,3),'uint8'),iterations=2).astype(bool)
    white=(source.min(axis=2)>220)&(source.astype('float32').sum(axis=2)>710)&eroded
    count,labels,stats,centroids=cv2.connectedComponentsWithStats(white.astype('uint8'),8)
    if count<=1:
        raise RuntimeError(f'No reliable white specular target for {name}')
    label=1+int(np.argmax(stats[1:,cv2.CC_STAT_AREA]))
    ys,xs=np.where(labels==label)
    weight=source[ys,xs].min(axis=1).astype('float64')-220
    x=float(np.average(xs+.5,weights=weight));y=float(np.average(ys+.5,weights=weight))
    my,mx=np.where(mask)
    bbox=[int(mx.min()),int(my.min()),int(mx.max()+1),int(my.max()+1)]
    item={'part':name,'highlight_xy':[x,y],'component_pixels':len(xs),
          'highlight_bbox_xyxy':[int(xs.min()),int(ys.min()),int(xs.max()+1),int(ys.max()+1)],
          'source_crown_bbox_xyxy':bbox,
          'horizontal_fraction':float((x-bbox[0])/(bbox[2]-bbox[0])),
          'target_role':'Observed reflection; never baked into height/normal',
          'source_uncertainty_px':1.5}
    parts.append(item)
    draw.ellipse((x-4,y-4,x+4,y+4),outline=(60,255,220),width=1)
    draw.line((x-7,y,x+7,y),fill=(60,255,220));draw.line((x,y-7,x,y+7),fill=(60,255,220))
overlay.save(out/'upper_highlight_targets.png')
report={'schema':'source_specular_targets.v1','status':'OBSERVED_LIGHT_TARGETS_NOT_MODEL_ACCEPTANCE',
        'source_dimensions':[963,410],'world_width':16,
        'world_ray':'X=(x/963-.5)*16; Z=(205-y)/963*16; cast from negativeY toward +Y',
        'parts':parts}
(out/'upper_highlight_targets.json').write_text(json.dumps(report,indent=2))
print(json.dumps(parts,indent=2))
