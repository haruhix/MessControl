"""DAY01 CLOSED-source physical gingiva CPU geometry/UV preparation.

Current10 actual upper crowns are authoritative. No rejectedv03 family or
invented lower crowns. Open rigid separation is explicitly unprovided/inferred.
No Blender invocation; deterministic closed meshes and canonical source audit.
"""
from pathlib import Path
import os
for k in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','MKL_NUM_THREADS'):os.environ[k]='2'
import importlib.util,json,hashlib,collections,time
import numpy as np,cv2
from PIL import Image,ImageDraw
cv2.setNumThreads(2)
AR=Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1');OUT=AR/'Gums/DAY01/SourceClosed_v01';CON=OUT/'Constraints';CON.mkdir(exist_ok=True)
SOURCE=AR/'Evidence/SourceAnalysis/DAY01_Closed';JAWS=AR/'Jaws/Family_DAY01/SourceClosed_v01';SCALE=16/963;CENTER=106;STEP=.04
spec=importlib.util.spec_from_file_location('physicalhelpers',AR/'Gums/ClosedPosePrototype/prepare_closed_depth_refit.py');h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
inventory=json.loads((SOURCE/'part_inventory.json').read_text());vectors=json.loads((SOURCE/'VectorContract_v01/vector_contract.json').read_text());support=json.loads((JAWS/'DAY01_actual_cervical_front_supports.json').read_text());guides=np.load(OUT/'Evidence/source_closed_boundary_guides.npz');packet=np.load(JAWS/'DAY01_ACTUAL_Closed0_world_triangles_UV0.npz')
if hashlib.sha256((JAWS/'DAY01_ACTUAL_Closed0_world_triangles_UV0.npz').read_bytes()).hexdigest()!=support['packet_sha256']:raise RuntimeError('DAY01actualneck andgeometrysnapshot disagree')
xx=np.arange(963);raw=guides['first_enamel_y'];neck=cv2.GaussianBlur(raw.reshape(1,-1),(0,0),1.).reshape(-1);neck=np.clip(neck,raw-.75,raw+.75)+.10
lower_hidden=cv2.GaussianBlur(guides['last_enamel_y'].reshape(1,-1),(0,0),20.).reshape(-1)-18.
depth_x=[];depth_y=[]
for arc in support['arcs']:
 for q in arc['actual_camera_ray_neck_samples']:
  x,y=q['source_first_enamel_xy']
  if abs(y-raw[x])<=1:depth_x.append(x+.5);depth_y.append(q['actual_front_worldXYZ'][1])
order=np.argsort(depth_x);depth_x=np.asarray(depth_x)[order];depth_y=np.asarray(depth_y)[order]
rim_samples=np.interp(xx,depth_x,depth_y);smooth_rim=cv2.GaussianBlur(rim_samples.reshape(1,-1),(0,0),4.).reshape(-1)
xs=set(np.arange(0,963,2).tolist()+[962.,-96.3,-60.,1023.,1059.3])
for part in vectors['parts']:xs.update(float(p[0])for p in part['cervical_occlusion_path_xy'])
xs=np.array(sorted(xs));n=len(xs);T=np.array([0,.001,.004,.012,.030,.065,.12,.20,.30,.42,.55,.68,.80,.90,.96,.99,1.]);rows=len(T)
RECTS={'upper':{'front':(.025,.025,.975,.235),'back':(.025,.500,.975,.710)},'lower':{'front':(.025,.260,.975,.470),'back':(.025,.735,.975,.945)}}
reports=[];meshes={}

def smooth(t):t=np.clip(t,0,1);return t*t*t*(10+t*(-15+6*t))
def project(v):return np.column_stack(((v[:,0]/16+.5)*963,CENTER-v[:,2]/SCALE))

def make_mesh(side):
 upper=side=='upper';x=(xs/963-.5)*16;py=np.interp(xs,xx,neck if upper else lower_hidden);z0=(CENTER-py)*SCALE;outer=(4.7 if upper else-4.7)+(.08 if upper else-.08)*(x/9.6)**2
 front=np.stack([np.column_stack((x,np.zeros(n),z0*(1-t)+outer*t))for t in T]).reshape(-1,3);fc=len(front)
 bidx=list(range(n))+[j*n+n-1 for j in range(1,rows)]+list(range((rows-1)*n+n-2,(rows-1)*n-1,-1))+[j*n for j in range(rows-2,0,-1)]
 if upper:
  rim0=np.interp(xs,xx,smooth_rim);outside=np.maximum(-xs,xs-962);fade=smooth(outside/96);rim0=rim0*(1-fade)+(1.3+.15*(x/9.6)**2)*fade;rim1=1.2+.25*(x/9.6)**2
 else:rim0=.9+.2*(x/9.6)**2;rim1=rim0.copy()
 baseline_rows=np.stack([rim0*(1-t)+rim1*t for t in T]);front[:,1]=baseline_rows.ravel();poly=front[bidx][:,[0,2]]/STEP;rim=front[bidx,1]
 field,solves=h.fields(poly,rim);points=front[:,[0,2]]/STEP;d,seg,st,near=h.samples(poly,rim,points);other=(seg+1)%len(poly);w=smooth((d-1)/3)
 us=field['u_slope'][seg]*(1-st)+field['u_slope'][other]*st;ys=field['y_slope'][seg]*(1-st)+field['y_slope'][other]*st
 u=np.maximum(0,d*us*(1-w)+h.helper.sample_c2(field['depth'],points,field['origin'])*w);baseline=(near+d*ys)*(1-w)+h.helper.sample_c2(field['baseline'],points,field['origin'])*w
 amplitude=.75 if upper else .70;front[:,1]=baseline-amplitude*np.sqrt(u);front[bidx,1]=rim
 vertices=front.tolist();fg=np.arange(fc).reshape(rows,n);bg=fg.copy()
 for j in range(1,rows-1):
  for i in range(1,n-1):
   k=j*n+i;bg[j,i]=len(vertices);v=front[k].copy();v[1]=baseline[k]+1.4*np.sqrt(u[k]);vertices.append(v.tolist())
 vertices=np.asarray(vertices,float);faces=[];uv=[]
 for back,grid in((False,fg),(True,bg)):
  rect=RECTS[side]['back'if back else'front']
  for j in range(rows-1):
   for i in range(n-1):
    face=[grid[j,i],grid[j,i+1],grid[j+1,i+1],grid[j+1,i]];ua=(xs[i]-xs[0])/(xs[-1]-xs[0]);ub=(xs[i+1]-xs[0])/(xs[-1]-xs[0]);corners=[(ua,T[j]),(ub,T[j]),(ub,T[j+1]),(ua,T[j+1])];values=[[rect[0]+q[0]*(rect[2]-rect[0]),rect[1]+q[1]*(rect[3]-rect[1])]for q in corners]
    if back:face=face[::-1];values=values[::-1]
    faces.append(face);uv.append(values)
 faces=np.asarray(faces,np.int32);uv=np.asarray(uv,np.float32);tri=np.vstack((faces[:,[0,1,2]],faces[:,[0,2,3]]));a,b,c=(vertices[tri[:,i]]for i in range(3));volume=float(np.einsum('ij,ij->i',a,np.cross(b,c)).sum()/6)
 if volume<0:faces=faces[:,::-1].copy();uv=uv[:,::-1].copy();tri=np.vstack((faces[:,[0,1,2]],faces[:,[0,2,3]]));volume=-volume
 area=np.linalg.norm(np.cross(vertices[tri[:,1]]-vertices[tri[:,0]],vertices[tri[:,2]]-vertices[tri[:,0]]),axis=1)/2;edges=collections.Counter(tuple(sorted((a,b)))for face in faces for a,b in zip(face,np.roll(face,-1)))
 if np.count_nonzero(area<1e-10)or any(v!=2 for v in edges.values())or not np.isfinite(vertices).all():raise RuntimeError('DAY01preparedsurface invalid')
 basis=vertices.copy();basis[:,2]+=1.65 if upper else-1.65
 sourceuv=np.column_stack((vertices[:,0]/16+.5,.5+vertices[:,2]/(212*SCALE)))
 result={'closed_world':vertices,'basis_open':basis,'faces':faces,'UV0':uv,'UV_SourceProjection_Closed':sourceuv.astype(np.float32),'triangles':tri};meshes[side]=result;np.savez_compressed(CON/(side+'_surface.npz'),**result)
 reports.append({'body':side,'vertices':len(vertices),'triangles':len(tri),'nonmanifold_edges':sum(v!=2 for v in edges.values()),'zero_area_triangles':int((area<1e-10).sum()),'minimum_area':float(area.min()),'positive_volume':volume,'closed_bounds':[vertices.min(0).tolist(),vertices.max(0).tolist()],'physical_grid_world':STEP,'front_amplitude_world':amplitude,'back_amplitude_world':1.4,'UV0_rects':RECTS[side],'field_solves':solves})

for side in('upper','lower'):make_mesh(side)
#Exact CPU canonical first-hit raster. DCC validation remains mandatory.
depth=np.full((212,963),np.inf);owners=np.full((212,963),-1,np.int16);names=[]
def raster(v,tri,name):
 owner=len(names);names.append(name);xy=project(v)
 for ids in tri:
  p,q,r=xy[ids];u=q-p;w=r-p;det=u[0]*w[1]-u[1]*w[0]
  if abs(det)<1e-14:continue
  lo=np.maximum([0,0],np.floor(xy[ids].min(0)).astype(int));hi=np.minimum([962,211],np.ceil(xy[ids].max(0)).astype(int))
  if np.any(hi<lo):continue
  yy,xx0=np.mgrid[lo[1]:hi[1]+1,lo[0]:hi[0]+1];dx=xx0+.5-p[0];dy=yy+.5-p[1];s=(dx*w[1]-dy*w[0])/det;t=(u[0]*dy-u[1]*dx)/det;inside=(s>=-1e-7)&(t>=-1e-7)&(s+t<=1+1e-7);y=v[ids[0],1]+s*(v[ids[1],1]-v[ids[0],1])+t*(v[ids[2],1]-v[ids[0],1]);target=depth[lo[1]:hi[1]+1,lo[0]:hi[0]+1];hit=inside&(y<target);target[hit]=y[hit];o=owners[lo[1]:hi[1]+1,lo[0]:hi[0]+1];o[hit]=owner
for part in inventory['parts']:
 name='MC_'+part['name'];raster(packet[name+'__world_vertices'],packet[name+'__triangle_vertex_indices'],name)
for side in('upper','lower'):raster(meshes[side]['closed_world'],meshes[side]['triangles'],'MC_DAY01_'+side.title()+'_Gum')
sourcewhite=np.zeros((212,963),bool)
for part in inventory['parts']:sourcewhite|=np.asarray(Image.open(SOURCE/part['crown_mask']).convert('L'))>0
ui=np.zeros_like(sourcewhite);x0,y0,x1,y1=inventory['label_occlusion_bbox_xyxy'];ui[y0:y1,x0:x1]=True
observed=~ui;renderwhite=(owners>=0)&(owners<10);iou=float((sourcewhite&renderwhite&observed).sum()/max(1,((sourcewhite|renderwhite)&observed).sum()))
columns=[]
for x in range(963):
 ys=np.flatnonzero(renderwhite[:,x]);first=int(ys[0])if len(ys)else None;columns.append({'x':x,'source_first_white':float(raw[x]),'prepared_first_white':first,'error':first-float(raw[x])if first is not None else None})
bad=[r for r in columns if r['error']is None or abs(r['error'])>1]
np.savez_compressed(OUT/'DAY01_CPU_closed_camera_firsthits.npz',object_id=owners,depth_Y=depth)
palette=np.array([[235,220,190]]*10+[[70,230,170],[70,155,245]],np.uint8);image=np.zeros((212,963,3),np.uint8);valid=owners>=0;image[valid]=palette[owners[valid]];Image.fromarray(image).save(OUT/'Evidence/DAY01_prepared_closed_semantic_CPU.png')
src=np.asarray(Image.open(SOURCE/'source.png').convert('RGB'));overlay=(src*.55+image*.45).astype(np.uint8);Image.fromarray(overlay).save(OUT/'Evidence/DAY01_prepared_source_overlay_CPU.png')
receipt={'status':'CPU_DAY01_PHYSICAL_GINGIVA_PREPARED_NOT_DCC_OR_ART_ACCEPTED','camera':{'width':16,'resolution':[963,212],'center':[0,0,0]},'source_image':str(SOURCE/'source.png'),'source_sha256':hashlib.sha256((SOURCE/'source.png').read_bytes()).hexdigest(),'jaw_actual_packet':support['source_packet'],'jaw_packet_sha256':support['packet_sha256'],'upper_observed_crowns':10,'lower_observed_or_modeled_crowns':0,'upper_source_cervical_depth_samples':len(depth_x),'upper_inferred_rimY_smoothing_sigma_source_px':4,'upper_smoothed_rim_vs_source_jawY_max':float(abs(np.interp(depth_x,xx,smooth_rim)-depth_y).max()),'source_neck_XZ_regularization_max_px':float(abs(neck-raw).max()),'surfaces':reports,'UV0':'Four disjoint front/back rectangles on independentDAY01tile; .025normalizedgaps/borders>=51px2K,102px4K. Fullclosedfront/backcoverage, no planarcap.','secondaryUV':'UV_SourceProjection_Closed with closed sourceW16/H212; outsidecrop values oninferredreturns; neverrepeat sourceimage.','hierarchy':{'collection':'MenuGingiva_DAY01_Geometry','meshes':['MC_DAY01_Upper_Gum','MC_DAY01_Lower_Gum'],'wrappers':['MC_Gingiva_DAY01_Upper','MC_Gingiva_DAY01_Lower'],'material':'MC_Gum'},'parent_contract':support['parents'],'CPU_closed_ownership':{'white_source_mask_IoU_excluding_label':iou,'raw_cervical_columns':963,'within1px':963-len(bad),'outside1px':bad,'all_columns':columns},'Open_policy':'Unprovided/unobserved: only inferred rigidupper+1.65/lower−1.65Opening; no1:1Openclaim, no inventedlowerwhites.','limitations':['InitialCPUproxy only; exact actualDCCsource masks/contact/positiveclosedsolids/UVoccupancy/3quarter mustpass beforeacceptance.','Y and all backs/posteriorreturns are inferred even thoughsourcefrontX/Z isconstrained.','Lowerhiddenmargin isbehindupperoverbite, not an inventedsourcevisibleanatomicalseam.','Sourcefit failures retained verbatim, not silentlydiscarded.']}
(OUT/'geometry_preparation.json').write_text(json.dumps(receipt,indent=2),encoding='utf8');print('DAY01_GUM_CPU_READY',json.dumps({'surfaces':reports,'closed_white_IoU':iou,'cervical_raw_within1px':963-len(bad),'badcolumns':len(bad)}),flush=True)
