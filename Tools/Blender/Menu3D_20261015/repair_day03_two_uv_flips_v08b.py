"""Compose accepted enamel/gum UVs, untangling only two tiny lingual triangles.

The nearest feasible UV point is computed from its one-ring half-plane kernel.
No global re-unwrap, repack, geometry change or UV boundary redesign.
Run in ordinary Python; Blender packet freeze is a separate deterministic step.
"""
from pathlib import Path
import numpy as np,json,hashlib,itertools
BASE=Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08b_SeamUV/Candidates');OLD=BASE/'MinimumStretch_3Charts';GUM=BASE/'GumMinimumStretch_Boundary08';OUT=BASE/'Final96_GumMinStretch_LocalFangRepair';OUT.mkdir(parents=True,exist_ok=True)
p=np.load(OLD/'texture_mesh_packet.npz');g=np.load(GUM/'texture_mesh_packet.npz');q=np.load(OLD/'QA_Independent/candidate_uv_extraction.npz');tri=q['triloops'];lv=q['loopverts'];uv=q['uv'].astype(np.float64).copy();original=uv.copy();mat=np.repeat(q['material'],q['counts']);charts=np.load(OLD/'QA_Independent/uv_triangle_charts.npy');loopchart=np.empty(len(lv),np.int32);loopchart[tri.ravel()]=np.repeat(charts,3);_,node=np.unique(np.column_stack([lv,loopchart]),axis=0,return_inverse=True);nn=node.max()+1;pos=np.zeros((nn,2));count=np.bincount(node,minlength=nn);np.add.at(pos,node,uv);pos/=count[:,None];tn=node[tri]
def cross(a,b):return a[...,0]*b[...,1]-a[...,1]*b[...,0]
def area(v):return cross(v[:,1]-v[:,0],v[:,2]-v[:,0])
ar=area(pos[tn]);sign=np.zeros(int(charts.max()+1))
for ch in np.unique(charts):sign[ch]=1 if np.sum(ar[charts==ch])>0 else-1
sg=sign[charts];enameltri=p['triangle_material_indices']==0;flips=np.flatnonzero((ar*sg<0)&enameltri);assert flips.tolist()==[123531,150702],flips
repairs=[]
for ti in flips:
 best=None
 for v in tn[ti]:
  incident=np.flatnonzero(np.any(tn==v,axis=1));P=pos[tn[incident]].copy();idx=np.argmax(tn[incident]==v,axis=1);base=area(P)*sg[incident];grad=np.empty((len(incident),2))
  for j,i in enumerate(idx):
   a=P[j,(i+1)%3];b=P[j,(i+2)%3];grad[j]=sg[incident[j]]*np.array([a[1]-b[1],b[0]-a[0]])
  # Keep every incident triangle positive; the repaired target gets half a
  # square texel area, other tiny slivers keep a positive fraction of theirs.
  epsilon=np.where(base>0,np.minimum(base*.1,1e-10),1/(4096**2));rhs=epsilon-base+grad@pos[v];norm=np.linalg.norm(grad,axis=1);G=grad/norm[:,None];B=rhs/norm;candidates=[];x0=pos[v]
  for j in range(len(G)):candidates.append(x0+max(0.,B[j]-G[j]@x0)*G[j])
  for a,b in itertools.combinations(range(len(G)),2):
   M=G[[a,b]]
   if abs(np.linalg.det(M))>1e-10:candidates.append(np.linalg.solve(M,B[[a,b]]))
  feasible=[x for x in candidates if np.all(G@x>=B-1e-13)]
  if feasible:
   x=min(feasible,key=lambda z:float(np.linalg.norm(z-x0)));d=float(np.linalg.norm(x-x0))
   if best is None or d<best[0]:best=(d,int(v),x,incident)
 assert best is not None,f'No one-vertex feasible kernel for triangle{ti}'
 d,v,x,incident=best;before=pos[v].copy();pos[v]=x;repairs.append({'triangle':int(ti),'vertex':int(lv[np.flatnonzero(node==v)[0]]),'uv_chart':int(loopchart[np.flatnonzero(node==v)[0]]),'affected_loops':np.flatnonzero(node==v).tolist(),'UV_before':before.tolist(),'UV_after':x.tolist(),'displacement_4K_px':d*4096,'incident_triangles':incident.tolist()})
fixed=pos[node];changed=np.any(np.abs(fixed-original)>1e-12,axis=1);assert np.count_nonzero(changed&(mat==0))<30,int(changed.sum());after_ar=area(fixed[tri]);assert np.all((after_ar*sg)[enameltri]>0),np.flatnonzero((after_ar*sg<=0)&enameltri);assert all(r['displacement_4K_px']<2 for r in repairs)
fixed[mat==1]=g['loop_uv'][mat==1];packet={k:p[k]for k in p.files};packet['loop_uv']=fixed.astype(np.float32)
# Repeat the positivity check after float32 serialization (actual file payload).
test=area(packet['loop_uv'][tri]);assert np.all((test*sg)[enameltri]>0),np.flatnonzero((test*sg<=0)&enameltri)
np.savez_compressed(OUT/'texture_mesh_packet.npz',**packet);report={'policy':'Accepted96chart enamel restored exactly except two local lingual UVvertex repairs. New measured MinimumStretch gum UV reused exactly. No global enamel re-solve/repack.','enamel_parent':str(OLD),'gum_parent':str(GUM),'enamel_changed_loops':int(np.count_nonzero(changed&(mat==0))),'repairs':repairs,'enamel_triangles_consistent_winding':True,'geometry_arrays_exact':all(np.array_equal(p[k],g[k])for k in p.files if k!='loop_uv'),'packet_sha256':hashlib.sha256((OUT/'texture_mesh_packet.npz').read_bytes()).hexdigest()};(OUT/'local_uv_repair.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
