"""Bake tooth/gum contact coordinates from the live UE geometry snapshot.

Run in a separate factory-startup Blender background process. No blend file is
loaded or saved. The actor placement, gum material and UV0 remain untouched.
"""
import json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT=Path('D:/PROJECT/GAME/WaifuTD/MessControl')
OUT=ROOT/'Artifacts/ArenaRefresh_20261017'
rows=json.loads((OUT/'GeometryBefore.json').read_text(encoding='utf-8'))
gums={}
for label in ['SM_Gum_Bot','SM_Gum_Up.001']:
 row=next(r for r in rows if r['label']==label)
 verts=[Vector(p) for p in row['world_vertices']]
 tris=[row['triangles'][i:i+3] for i in range(0,len(row['triangles']),3)]
 gums[label]=(row,verts,tris,BVHTree.FromPolygons(verts,tris,all_triangles=True))

def barycentric(p,a,b,c):
 v0=b-a;v1=c-a;v2=p-a
 d00=v0.dot(v0);d01=v0.dot(v1);d11=v1.dot(v1);d20=v2.dot(v0);d21=v2.dot(v1)
 denom=d00*d11-d01*d01
 if abs(denom)<1e-12:return (1.,0.,0.)
 v=(d11*d20-d01*d21)/denom;w=(d00*d21-d01*d20)/denom
 return (1-v-w,v,w)

result=[]
for row in rows:
 if not row['label'].startswith('SM_Teeth'):continue
 upper=row['location'][2]>700
 label='SM_Gum_Up.001' if upper else 'SM_Gum_Bot'
 gum,verts,tris,bvh=gums[label]
 # Calibrate the imported winding against the exposed tip of this tooth.
 points=[Vector(p) for p in row['world_vertices']]
 crown=(min if upper else max)(points,key=lambda p:p.z)
 cp,cn,ci,cd=bvh.find_nearest(crown)
 sign=1 if (crown-cp).dot(cn)>=0 else -1
 samples=[];distances=[]
 for local,p in zip(row['vertices'],points):
  q,n,index,distance=bvh.find_nearest(p)
  tri=tris[index];weights=barycentric(q,*[verts[i] for i in tri])
  uv=[sum(weights[j]*gum['uv'][tri[j]][k] for j in range(3)) for k in range(2)]
  signed=distance*(1 if (p-q).dot(n)*sign>=0 else -1)
  distances.append(signed)
  samples.append(dict(local=local,gum_uv=uv,distance_cm=signed))
 result.append(dict(mesh=row['mesh'],actor=row['path'],label=row['label'],upper=upper,gum=label,
                    samples=samples,mask_band_vertices=sum(-8<d<45 for d in distances),
                    signed_range=[min(distances),max(distances)]))
(OUT/'GumSeamBake.json').write_text(json.dumps(result),encoding='utf-8')
(OUT/'GumSeamBakeSummary.json').write_text(json.dumps([
 {k:v for k,v in r.items() if k!='samples'} for r in result],indent=2),encoding='utf-8')
print('ARENA_GUM_SEAMS_BAKED',len(result),sum(r['mask_band_vertices'] for r in result))
