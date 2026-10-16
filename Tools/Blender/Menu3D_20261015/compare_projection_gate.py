"""Compare actual Blender mattes against reviewed source masks."""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np
from PIL import Image,ImageDraw

p=argparse.ArgumentParser();p.add_argument('--manifest',required=True);opt=p.parse_args()
path=Path(opt.manifest);manifest=json.loads(path.read_text(encoding='utf8'));out=path.parent
records=[];tiles=[]
for rec in manifest['records']:
    a=cv2.imread(rec['reference_mask'],cv2.IMREAD_GRAYSCALE)>127
    b=cv2.imread(rec['render_mask'],cv2.IMREAD_GRAYSCALE)>127
    if a.shape!=b.shape:raise ValueError((rec['part'],a.shape,b.shape))
    # Known native DAY03 label occludes a piece of the left canine/side crowns.
    valid=np.ones(a.shape,bool);valid[46:95,23:238]=False
    occlusion_path=rec.get('occlusion_mask')
    if occlusion_path and Path(occlusion_path).is_file():
        occlusion=cv2.imread(occlusion_path,cv2.IMREAD_GRAYSCALE)>127
        if occlusion.shape!=a.shape:raise ValueError(('occlusion shape',rec['part']))
        valid&=~occlusion
    a&=valid;b&=valid
    intersection=int((a&b).sum());union=int((a|b).sum());iou=intersection/union if union else 0
    kernel=np.ones((3,3),np.uint8)
    # OpenCV's default erosion border treats outside as foreground, which
    # suppresses a reference edge touching the image boundary and inflates its
    # distance to an otherwise matching render by tens of pixels. Explicit
    # black padding makes the raster-frame edge comparison symmetric.
    ea=a&~cv2.erode(a.astype(np.uint8),kernel,borderType=cv2.BORDER_CONSTANT,borderValue=0).astype(bool)
    eb=b&~cv2.erode(b.astype(np.uint8),kernel,borderType=cv2.BORDER_CONSTANT,borderValue=0).astype(bool)
    da=cv2.distanceTransform((~ea).astype(np.uint8),cv2.DIST_L2,cv2.DIST_MASK_PRECISE)
    db=cv2.distanceTransform((~eb).astype(np.uint8),cv2.DIST_L2,cv2.DIST_MASK_PRECISE)
    distances=np.r_[da[eb],db[ea]]
    metrics={'iou':iou,'boundary_mean_px':float(np.mean(distances)) if len(distances)else None,'boundary_p95_px':float(np.percentile(distances,95))if len(distances)else None,'ref_area':int(a.sum()),'render_area':int(b.sum())}
    records.append({**rec,**metrics,'strict_projection_candidate':bool(iou>=.98 and metrics['boundary_p95_px'] is not None and metrics['boundary_p95_px']<=1.5 and not rec['ambiguous'])})
    overlay=np.zeros((*a.shape,3),np.uint8);overlay[a&b]=[215,215,215];overlay[a&~b]=[250,70,100];overlay[b&~a]=[50,200,255]
    yy,xx=np.nonzero(a|b)
    if not len(xx):continue
    box=(max(0,int(xx.min())-10),max(0,int(yy.min())-10),min(a.shape[1],int(xx.max())+11),min(a.shape[0],int(yy.max())+11))
    crop=Image.fromarray(overlay).crop(box);crop.thumbnail((235,230))
    tile=Image.new('RGB',(255,280),(24,24,26));d=ImageDraw.Draw(tile);d.text((10,8),rec['part'],fill='white');d.text((10,25),f'IoU {iou:.4f}; p95 {metrics["boundary_p95_px"]:.2f}px',fill='white');tile.paste(crop,((255-crop.width)//2,48));tiles.append(tile)
sheet=Image.new('RGB',(1020,280*((len(tiles)+3)//4)),(10,10,12))
for i,t in enumerate(tiles):sheet.paste(t,((i%4)*255,(i//4)*280))
sheet.save(out/'projection_contact_sheet.png')
report={'records':records,'all_strict_candidates':all(r['strict_projection_candidate']for r in records),'limitations':['Masks require visual/manual review. These are geometry-only projection measurements, not final visual quality or1:1PBR proof.','Known DAY03 label region excluded from both masks; ambiguous/cropped sourceparts are flagged.']}
(out/'projection_comparison.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'out':str(out),'parts':len(records),'strict_candidates':sum(r['strict_projection_candidate']for r in records)}))
