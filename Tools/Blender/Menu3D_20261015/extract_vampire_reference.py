"""Deterministic seeded segmentation of visible reference teeth + organic boundaries.

UI-occluded posterior crowns are explicitly ambiguous. Masks are inspected, never
treated as ground truth merely because GrabCut completed.
"""
import json
from pathlib import Path
import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT=Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
OUT=ROOT/'Evidence/SourceAnalysis/CanonicalOpen';OUT.mkdir(parents=True,exist_ok=True)
im=np.array(Image.open(ROOT/'References/B_FullWidth_Approved.png').convert('RGB').crop((979,331,1942,741)))
bgr=cv2.cvtColor(im,cv2.COLOR_RGB2BGR)
# Seed rectangles are in the reviewed 963x410 open crop; labels correspond to
# visible parts, not symmetry-generated copies.
parts=[
 ('Upper_Fang_L',(228,12,85,163),(267,70)),
 ('Upper_Lateral_L',(309,17,73,84),(344,58)),
 ('Upper_Central_L',(379,13,100,90),(429,55)),
 ('Upper_Central_R',(476,13,99,90),(526,55)),
 ('Upper_Lateral_R',(569,17,81,85),(609,58)),
 ('Upper_Fang_R',(645,10,89,165),(692,70)),
 ('Lower_Central_L',(310,346,116,64),(366,379)),
 ('Lower_Central_R',(425,349,115,61),(485,380)),
 ('Lower_Lateral_L',(208,339,106,71),(261,375)),
 ('Lower_Lateral_R',(538,344,103,66),(587,377)),
 ('Lower_Canine_L',(109,321,104,86),(163,363)),
 ('Lower_Canine_R',(639,337,107,73),(692,372)),
 ('Lower_Premolar_L',(57,303,75,88),(91,351)),
 ('Lower_Premolar_R',(741,318,94,82),(783,360)),
 ('Lower_Molar_L',(14,289,65,83),(43,337)),
 ('Lower_Molar_R',(831,302,77,84),(868,345)),
]
inventory=[];overlay=Image.fromarray(im.copy());draw=ImageDraw.Draw(overlay)
palette=[(80,245,215),(255,210,70),(150,190,255),(255,100,160)]
for i,(name,rect,seed) in enumerate(parts):
    x,y,w,h=rect;mask=np.zeros(im.shape[:2],np.uint8)
    mask[y:y+h,x:x+w]=cv2.GC_PR_BGD
    # Color-informed probable FG; gum and crevices usually have much less green.
    roi=im[y:y+h,x:x+w].astype(float);r,g,b=roi[:,:,0],roi[:,:,1],roi[:,:,2]
    cream=(r>110)&(g/(r+1)>.59)&(b/(r+1)>.47)
    local=mask[y:y+h,x:x+w];local[cream]=cv2.GC_PR_FGD
    cv2.circle(mask,seed,5,cv2.GC_FGD,-1)
    bg=np.zeros((1,65),np.float64);fg=np.zeros((1,65),np.float64)
    cv2.grabCut(bgr,mask,None,bg,fg,4,cv2.GC_INIT_WITH_MASK)
    binary=np.where((mask==1)|(mask==3),255,0).astype(np.uint8)
    count,labels,stats,centers=cv2.connectedComponentsWithStats(binary)
    label=int(labels[seed[1],seed[0]])
    if label==0:
        label=1+np.argmax(stats[1:,cv2.CC_STAT_AREA])
    binary=(labels==label).astype(np.uint8)*255
    contours,_=cv2.findContours(binary,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_NONE)
    contour=max(contours,key=cv2.contourArea)
    smooth=cv2.approxPolyDP(contour,.65,True)[:,0,:]
    Image.fromarray(binary).save(OUT/(name+'.png'))
    pts=[tuple(map(int,q))for q in smooth]
    draw.line(pts+[pts[0]],fill=palette[i%4],width=2)
    draw.text((x,y+4),str(i+1),fill=(0,255,170))
    bx,by,bw,bh=cv2.boundingRect(contour)
    inventory.append({'name':name,'class':'structural','mask':str(OUT/(name+'.png')),
      'seed_xy':seed,'seed_rectangle_xywh':rect,'bbox_xywh':[bx,by,bw,bh],
      'contour_xy':pts,'area_px':int((binary>0).sum()),'status':'segmentation_candidate_requires_visual_review',
      'truncated_at_image_bottom':by+bh>=409})
overlay.save(OUT/'segmentation_overlay.png')
# Visible tongue/recess divider: manually reviewed points along the upper tongue
# silhouette, across the center where UI partially occludes it. Keep ambiguity.
tongue=[(0,312),(49,296),(91,277),(137,278),(188,294),(232,312),(275,327),(321,341),(366,346),(406,343),(445,334),(486,323),(531,309),(576,295),(622,281),(670,269),(712,263),(752,263),(789,273),(826,292),(858,313),(887,330),(927,331),(962,312)]
draw.line(tongue,fill=(50,230,255),width=3)
overlay.save(OUT/'segmentation_and_tongue_overlay.png')
report={'schema':'source_part_inventory.v1','source_crop_xyxy':[979,331,1942,741],
 'image_size':[963,410],'source_role':'canonical open frontal concept','parts':inventory,
 'tongue_upper_boundary_xy':tongue,'tongue_boundary_ambiguity':'Character/UI occlude left-center. Intermediate points inferred only inside those occlusions; visible contour must be reviewed.',
 'world_mapping':{'width_units':16.0,'pixel_x_to_X':'(x/963-.5)*16','pixel_y_to_Z':'(205-y)/963*16','front_camera':'negative Y, exact orthographic X/Z for contour gate'},
 'unresolved':['Posterior upper teeth are partly obscured by DAY03 label; do not claim exact hidden contour.','Gum outer edge is cropped by upper neighboring panel; visible gingival lower margin controls fit.','Lower crowns cross bottom crop boundary; top and side outline only is source constrained.']}
(OUT/'part_inventory.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'parts':len(inventory),'overlay':str(OUT/'segmentation_and_tongue_overlay.png'),'inventory':str(OUT/'part_inventory.json')}))
