"""Independent unique-UV-set checks from frozen Blender extraction; CPU only.

Raster checks use per-island union masks, so shared edges inside a chart are not
mistaken for UV overlap. Intra-island folds are separately checked by winding.
"""
import argparse,json,math
from pathlib import Path
import numpy as np
import cv2
p=argparse.ArgumentParser();p.add_argument('--input',required=True);p.add_argument('--output',required=True);p.add_argument('--resolution',type=int,default=4096);p.add_argument('--manifest')
a=p.parse_args();src=Path(a.input);out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
d=np.load(src);uv=d['uv'].astype(np.float64);starts=d['starts'];counts=d['counts'];lv=d['loopverts'];le=d['loopedges'];mats=d['material'];tri=d['triloops'];tp=d['tripoly'];co=d['co']
manifest=json.loads(Path(a.manifest).read_text()) if a.manifest else None
parts=manifest['parts'] if manifest else []
partstarts=np.array([p['vertex_start'] for p in parts]) if parts else None
n=len(starts);parent=np.arange(n);sizes=np.ones(n,np.int32)
def find(x):
 while parent[x]!=x:parent[x]=parent[parent[x]];x=parent[x]
 return x
def union(x,y):
 x=find(x);y=find(y)
 if x==y:return
 if sizes[x]<sizes[y]:x,y=y,x
 parent[y]=x;sizes[x]+=sizes[y]
poly=np.repeat(np.arange(n),counts);nxt=np.arange(len(lv))+1;nxt[starts+counts-1]=starts
reverse=lv>lv[nxt];u0=np.where(reverse[:,None],uv[nxt],uv);u1=np.where(reverse[:,None],uv,uv[nxt])
keys=np.column_stack((le,mats[poly],np.rint(u0*1e7).astype(np.int64),np.rint(u1*1e7).astype(np.int64)))
order=np.lexsort(tuple(keys[:,i] for i in range(keys.shape[1]-1,-1,-1)));same=np.all(keys[order][1:]==keys[order][:-1],axis=1)
for i in np.where(same)[0]:union(int(poly[order[i]]),int(poly[order[i+1]]))
roots=np.array([find(i) for i in range(n)]);unique,chart=np.unique(roots,return_inverse=True)
R=a.resolution;report=json.loads(src.with_suffix('.json').read_text());report.update({'resolution':R,'islands':len(unique),'materials':{},'limitations':['Raster overlap/padding accuracy approximately2pixels','Intra-island same-winding self-overlap not exhaustively proven absent; checker/render review still required']})
tuv=uv[tri];cross=(tuv[:,1,0]-tuv[:,0,0])*(tuv[:,2,1]-tuv[:,0,1])-(tuv[:,1,1]-tuv[:,0,1])*(tuv[:,2,0]-tuv[:,0,0]);tco=co[lv[tri]];norm=np.cross(tco[:,1]-tco[:,0],tco[:,2]-tco[:,0]);areas3=np.linalg.norm(norm,axis=1)/2
charttri=chart[tp]
np.save(out.parent/'uv_triangle_charts.npy',charttri)
minority_faces=[]
for ci in np.unique(charttri):
 ids=np.where((charttri==ci)&(areas3>1e-12))[0];positive=ids[cross[ids]>1e-14];negative=ids[cross[ids]<-1e-14]
 if len(positive)and len(negative):
  for ti in(positive if len(positive)<len(negative)else negative):
   vertices=tco[ti];edges=vertices[[1,2,0]]-vertices;part=parts[int(np.searchsorted(partstarts,lv[tri[ti,0]],side='right')-1)]['mesh'] if parts else None
   minority_faces.append({'triangle':int(ti),'polygon':int(tp[ti]),'chart':int(ci),'part':part,'max_edge_source_units':float(np.linalg.norm(edges,axis=1).max()),'area_source_units2':float(areas3[ti]),'uv_area_pixels_4k':float(abs(cross[ti])/2*4096**2),'center':vertices.mean(0).tolist(),'normal':(norm[ti]/max(2*areas3[ti],1e-30)).tolist()})
report['minority_winding_face_locations']=minority_faces
for material in sorted(set(mats.tolist())):
 occupancy=np.zeros((R,R),np.int32);overlap=np.zeros((R,R),np.uint8);island_records=[];island_masks=[]
 for ci in np.unique(chart[mats==material]):
  ids=np.where(chart==ci)[0];loopids=np.concatenate([np.arange(starts[k],starts[k]+counts[k]) for k in ids]);points=uv[loopids]
  lo=np.floor(points.min(0)*(R-1)).astype(int);hi=np.ceil(points.max(0)*(R-1)).astype(int);lo=np.clip(lo,0,R-1);hi=np.clip(hi,0,R-1)
  mask=np.zeros((hi[1]-lo[1]+1,hi[0]-lo[0]+1),np.uint8)
  polygons=[np.rint(uv[starts[k]:starts[k]+counts[k]]*(R-1)-lo).astype(np.int32) for k in ids]
  cv2.fillPoly(mask,polygons,1);region=occupancy[lo[1]:hi[1]+1,lo[0]:hi[0]+1];collision=(mask>0)&(region!=0);overlap[lo[1]:hi[1]+1,lo[0]:hi[0]+1][collision]=1;region[mask>0]=int(ci)+1
  signs=cross[(charttri==ci)&(areas3>1e-12)];pos=int((signs>1e-14).sum());neg=int((signs<-1e-14).sum())
  record={'island':int(ci),'polygons':len(ids),'uv_bbox':[points.min(0).tolist(),points.max(0).tolist()], 'raster_pixels':int(mask.sum()),'mixed_winding_triangles':min(pos,neg),'positive_uv_triangles':pos,'negative_uv_triangles':neg,
          'edge_padding_px':float(min(points[:,0].min(),points[:,1].min(),1-points[:,0].max(),1-points[:,1].max())*R)}
  if parts:record['part']=parts[int(np.searchsorted(partstarts,lv[starts[ids[0]]],side='right')-1)]['mesh']
  island_records.append(record);island_masks.append((int(ci),lo,hi,mask))
 for rec,(ci,lo,hi,mask) in zip(island_records,island_masks):
  margin=34;elo=np.maximum(lo-margin,0);ehi=np.minimum(hi+margin,R-1);region=occupancy[elo[1]:ehi[1]+1,elo[0]:ehi[0]+1];selfmask=np.zeros(region.shape,np.uint8);off=lo-elo;selfmask[off[1]:off[1]+mask.shape[0],off[0]:off[0]+mask.shape[1]]=mask
  others=(region!=0)&(region!=ci+1)
  if others.any():distance=cv2.distanceTransform(1-selfmask,cv2.DIST_L2,5);rec['nearest_other_island_px']=float(distance[others].min())
  else:rec['nearest_other_island_px_at_least']=margin
 selected=tp[mats[tp]==material];tris=(mats[tp]==material);uvbad=(np.abs(cross)<1e-14)&(areas3>1e-12)&tris
 mat={'name':report['material_names'][material],'island_count':len(island_records),'occupied_pixels':int((occupancy>0).sum()),'coverage_fraction':float((occupancy>0).mean()),'inter_island_overlap_pixels':int(overlap.sum()),'collapsed_uv_nonzero_surface_triangles':int(uvbad.sum()),'mixed_winding_triangles':sum(r['mixed_winding_triangles'] for r in island_records),'islands':island_records}
 report['materials'][str(material)]=mat
 cv2.imwrite(str(out.parent/('uv_occupancy_material_'+str(material)+'.png')),(occupancy>0).astype(np.uint8)*255)
 palette=np.zeros((len(unique)+1,3),np.uint8)
 for ci in range(len(unique)):palette[ci+1]=cv2.cvtColor(np.uint8([[[int((ci*.61803398875%1)*179),160,235]]]),cv2.COLOR_HSV2BGR)[0,0]
 atlas=palette[occupancy]
 for rec,(ci,lo,hi,mask) in zip(island_records,island_masks):
  contours,_=cv2.findContours(mask,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_SIMPLE)
  for contour in contours:cv2.drawContours(atlas,[contour+lo[None,None,:]],-1,(245,245,245),1)
  if rec['raster_pixels']>1200:cv2.putText(atlas,str(ci),(int((lo[0]+hi[0])/2),int((lo[1]+hi[1])/2)),cv2.FONT_HERSHEY_SIMPLEX,.6,(20,20,20),1,cv2.LINE_AA)
 cv2.imwrite(str(out.parent/('uv_chart_layout_material_'+str(material)+'.png')),atlas)
 if overlap.any():cv2.imwrite(str(out.parent/('uv_overlap_material_'+str(material)+'.png')),overlap*255)
report['uv_finite']=bool(np.isfinite(uv).all());report['uv_outside_0_1_loops']=int(np.any((uv<0)|(uv>1),axis=1).sum())
report['charts_per_part']={}
for mat in report['materials'].values():
 for island in mat['islands']:
  if 'part' in island:report['charts_per_part'][island['part']]=report['charts_per_part'].get(island['part'],0)+1
report['gates']={'finite_uv':report['uv_finite'],'all_uv_in_0_1':report['uv_outside_0_1_loops']==0,'no_inter_island_overlap_at_4k':all(m['inter_island_overlap_pixels']==0 for m in report['materials'].values()),'no_collapsed_uv_visible_surface':all(m['collapsed_uv_nonzero_surface_triangles']==0 for m in report['materials'].values()),'no_intra_island_winding_folds':all(m['mixed_winding_triangles']==0 for m in report['materials'].values())}
out.write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps({'output':str(out),'gates':report['gates'],'islands':len(unique),'materials':{k:{p:v[p] for p in ['island_count','coverage_fraction','inter_island_overlap_pixels','collapsed_uv_nonzero_surface_triangles','mixed_winding_triangles']} for k,v in report['materials'].items()}}))
