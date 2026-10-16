"""CPU independent UV/normal/Painter identity and per-chart density priorities."""
import json,sys,math
from pathlib import Path
import numpy as np
import cv2
p=Path(sys.argv[1]);out=p/'QA_Independent';d=np.load(out/'source_combined.npz');packet=np.load(p/'texture_mesh_packet.npz');manifest=json.loads((p/'interior_handoff.json').read_text());chart=np.load(out/'uv_triangle_charts.npy');validation=json.loads((out/'uv_validation.json').read_text())
def corner_identity(s,f):
 s={k:s[k]for k in ['co','uv','loopverts','loopnormals','counts','material']};f={k:f[k]for k in ['co','uv','loopverts','loopnormals','counts','material']}
 index=cv2.flann_Index(s['co'].astype(np.float32),dict(algorithm=1,trees=4));nearest,dist=index.knnSearch(f['co'].astype(np.float32),1,params=dict(checks=128));mapping=nearest[:,0]
 def records(x,canonical):
  mat=np.repeat(x['material'],x['counts']);vals=np.column_stack((np.rint(canonical[x['loopverts']]*1e5).astype(np.int64),np.rint(x['uv']*1e7).astype(np.int64),mat));keys,ix=np.unique(vals,axis=0,return_index=True);return keys,x['loopnormals'][ix]
 sk,sn=records(s,s['co']);fk,fn=records(f,s['co'][mapping]);bins_equal=sk.shape==fk.shape and np.array_equal(sk,fk)
 # Text OBJ serializes8decimals; values near a quantization boundary can occupy
 # adjacent bins while differing by only5e-9. Check actual UV distance per
 # canonical position/material, rather than treating a bin mismatch as a fold.
 groups={};source_pos=np.rint(s['co'][s['loopverts']]*1e5).astype(np.int64);source_mat=np.repeat(s['material'],s['counts'])
 for i,key in enumerate(np.column_stack((source_pos,source_mat))):groups.setdefault(tuple(key),[]).append(i)
 other_pos=np.rint(s['co'][mapping][f['loopverts']]*1e5).astype(np.int64);other_mat=np.repeat(f['material'],f['counts']);matched=[];uv_errors=[];missing=0
 for i,key in enumerate(np.column_stack((other_pos,other_mat))):
  choices=groups.get(tuple(key))
  if not choices:missing+=1;matched.append(0);uv_errors.append(float('inf'));continue
  delta=np.max(abs(s['uv'][choices]-f['uv'][i]),axis=1);j=int(np.argmin(delta));matched.append(choices[j]);uv_errors.append(float(delta[j]))
 sn=s['loopnormals'][matched].astype(float);fn=f['loopnormals'].astype(float);angle=np.degrees(np.arccos(np.clip(np.sum(sn*fn,axis=1)/np.maximum(np.linalg.norm(sn,axis=1)*np.linalg.norm(fn,axis=1),1e-20),-1,1)));max_uv=max(uv_errors);match=missing==0 and max_uv<=1e-7 and len(sk)==len(fk)
 return {'unique_source_corners':len(sk),'unique_other_corners':len(fk),'exact_quantized_corner_bins_equal':bins_equal,'uv_material_corner_identity':match,'max_uv_coordinate_error':max_uv,'max_position_error_source_units':float(np.sqrt(dist.max())),'normal_angle_degrees_p95':float(np.quantile(angle,.95)),'normal_angle_degrees_max':float(angle.max()),'pass':match and float(np.sqrt(dist.max()))*100<=.001 and float(angle.max())<.2}
report={'independent':True,'fresh_fbx':{},'painter_obj':{},'charts':[],'limits':['Normals checked as world-space corner vectors, UVs at1e−7quantization; materialized appearance still needs Painter/Unreal','Density metrics weighted by actual3Dtrianglearea; chart intentionalpriorities are compared separately','Screen texel ratio is a conservative projection scale bound, not native pixel-raster texturequality']}
for name in ['SM_DAY03_Interior','SM_DAY03_Tongue']:report['fresh_fbx'][name]=corner_identity(np.load(out/('source_'+name+'.npz')),np.load(out/('fresh_'+name+'.npz')))
vertices=[];uv=[];norm=[];corners=[];material=0;counts=[];materials=[]
for line in (p/'MC_DAY03_Interior_Painter_Open.obj').read_text().splitlines():
 fields=line.split()
 if not fields:continue
 if fields[0]=='v':vertices.append([float(x)for x in fields[1:4]])
 elif fields[0]=='vt':uv.append([float(x)for x in fields[1:3]])
 elif fields[0]=='vn':norm.append([float(x)for x in fields[1:4]])
 elif fields[0]=='usemtl':material=0 if fields[1]=='MC_Interior' else 1
 elif fields[0]=='f':
  values=[[int(x)-1 for x in v.split('/')]for v in fields[1:]];assert all(len(v)==3 for v in values),'OBJ lacks explicit normal/UV';corners.extend(values);counts.append(len(values));materials.append(material)
corners=np.array(corners);obj={'co':np.array(vertices,dtype=np.float32),'loopverts':corners[:,0],'uv':np.array(uv,dtype=np.float32)[corners[:,1]],'loopnormals':np.array(norm,dtype=np.float32)[corners[:,2]],'counts':np.array(counts),'material':np.array(materials)};report['painter_obj']=corner_identity(d,obj)
t=d['triloops'];v=d['co'][d['loopverts'][t]].astype(float);u=d['uv'][t].astype(float);e1=v[:,1]-v[:,0];e2=v[:,2]-v[:,0];n=np.cross(e1,e2);area3=np.linalg.norm(n,axis=1)/2;b1=u[:,1]-u[:,0];b2=u[:,2]-u[:,0];areaUV=abs(b1[:,0]*b2[:,1]-b1[:,1]*b2[:,0])/2;density=np.sqrt(areaUV/np.maximum(area3,1e-30))*4096;L=np.linalg.norm(e1,axis=1);xx=np.sum(e1*e2,axis=1)/np.maximum(L,1e-30);yy=2*area3/np.maximum(L,1e-30);j1=b1/np.maximum(L[:,None],1e-30);j2=(b2-j1*xx[:,None])/np.maximum(yy[:,None],1e-30);trace=(j1*j1+j2*j2).sum(1);det=j1[:,0]*j2[:,1]-j1[:,1]*j2[:,0];disc=np.sqrt(np.maximum(trace*trace-4*det*det,0));kappa=np.sqrt((trace+disc)/np.maximum(trace-disc,1e-30))
def quant(x,w,q):
 order=np.argsort(x);sumw=np.cumsum(w[order]);return float(x[order[min(np.searchsorted(sumw,q*sumw[-1]),len(order)-1)]])
def angles(coords):
 result=[]
 for i in range(3):
  a=coords[:,(i+1)%3]-coords[:,i];b=coords[:,(i+2)%3]-coords[:,i];result.append(np.degrees(np.arccos(np.clip(np.sum(a*b,axis=1)/np.maximum(np.linalg.norm(a,axis=1)*np.linalg.norm(b,axis=1),1e-30),-1,1))))
 return np.stack(result,1)
ae=abs(angles(v)-angles(u)).max(1)
for ci in np.unique(chart):
 mask=(chart==ci)&(area3>1e-12);ix=np.where(mask)[0];mat=int(d['material'][d['tripoly'][ix[0]]]);weights={k:float(np.average(packet[k][d['loopverts'][t[ix]]].mean(1),weights=area3[ix]))for k in ['hidden_return_weight','tongue_dorsal_weight']};label=('inner_cavity'if weights['hidden_return_weight']<.1 else'back_return'if weights['hidden_return_weight']>.9 else'rim')if mat==0 else('dorsal'if weights['tongue_dorsal_weight']>.5 else'ventral');w=area3[mask];den=density[mask];lo=quant(den,w,.05);med=quant(den,w,.5);hi=quant(den,w,.95);uvcoords=u[mask];bbox=[uvcoords.min((0,1)).tolist(),uvcoords.max((0,1)).tolist()]
 report['charts'].append({'chart':int(ci),'material':mat,'surface':label,'anatomy_weight_means':weights,'area_source_units2':float(w.sum()),'UV_area':float(areaUV[mask].sum()),'uv_bbox':bbox,'density_p05_median_p95':[lo,med,hi],'density_intrachart_spread':hi/lo,'kappa_p95':quant(kappa[mask],w,.95),'kappa_p99':quant(kappa[mask],w,.99),'angle_error_degrees_p95':quant(ae[mask],w,.95),'minimum_texels_per_design_screen_pixel_bound':lo/(800/math.tan(math.radians(39.97)/2)*53.7654/2200)})
byname={x['surface']:x for x in report['charts']};refden=byname['inner_cavity']['density_p05_median_p95'][1];tongued=byname['dorsal']['density_p05_median_p95'][1];report['measured_priority_density_ratios']={'cavity_back_to_inner':byname['back_return']['density_p05_median_p95'][1]/refden,'cavity_rim_to_inner':byname['rim']['density_p05_median_p95'][1]/refden,'tongue_ventral_to_dorsal':byname['ventral']['density_p05_median_p95'][1]/tongued};report['uv_export_normal_identity_pass']=all(x['pass']for x in report['fresh_fbx'].values()) and report['painter_obj']['pass'];(out/'export_identity_and_priority.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
