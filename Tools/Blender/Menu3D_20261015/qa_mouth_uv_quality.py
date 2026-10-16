"""Independent seam-UV quality metrics and actual atlas heatmaps; CPU only."""
import argparse,hashlib,json,math
from pathlib import Path
import cv2
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--extraction',required=True);p.add_argument('--manifest',required=True);p.add_argument('--output',required=True);p.add_argument('--packet');p.add_argument('--skip-heatmaps',action='store_true');a=p.parse_args()
src=Path(a.extraction);out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);d={k:v for k,v in np.load(src).items()};metadata=json.loads(src.with_suffix('.json').read_text());m=json.loads(Path(a.manifest).read_text())
if a.packet:
 q=np.load(a.packet)
 for target,name in [('co','vertices'),('loopverts','loop_vertex_indices'),('triloops','triangle_loop_indices'),('material','polygon_material_indices')]:assert np.array_equal(d[target],q[name]),'Candidate geometry/topology changed: '+name
 d['uv']=q['loop_uv'];packetout=out.parent/'candidate_uv_extraction.npz';np.savez_compressed(packetout,**d);metadata.update({'evidence_source':'Candidate packet with independently verified identical frozen geometry/topology; final source UV read still required','candidate_packet':a.packet,'candidate_packet_sha256':hashlib.sha256(Path(a.packet).read_bytes()).hexdigest()});packetout.with_suffix('.json').write_text(json.dumps(metadata,indent=2))
uv=d['uv'].astype(float);t=d['triloops'];tp=d['tripoly'];v=d['co'][d['loopverts'][t]].astype(float);u=uv[t];e1=v[:,1]-v[:,0];e2=v[:,2]-v[:,0];n=np.cross(e1,e2);area3=np.linalg.norm(n,axis=1)/2;L=np.linalg.norm(e1,axis=1);x=np.einsum('ij,ij->i',e1,e2)/np.maximum(L,1e-30);y=2*area3/np.maximum(L,1e-30);b1=u[:,1]-u[:,0];b2=u[:,2]-u[:,0];j1=b1/np.maximum(L[:,None],1e-30);j2=(b2-j1*x[:,None])/np.maximum(y[:,None],1e-30);trace=(j1*j1+j2*j2).sum(1);det=j1[:,0]*j2[:,1]-j1[:,1]*j2[:,0];disc=np.sqrt(np.maximum(trace*trace-4*det*det,0));lmax=(trace+disc)/2;lmin=(trace-disc)/2;stretch=np.sqrt(np.maximum(lmax,0)/np.maximum(lmin,1e-30));areaUV=abs(np.cross(b1,b2))/2;density=np.sqrt(areaUV/np.maximum(area3,1e-30))*4096
def angles(coords):
 rows=[]
 for i in range(3):
  e=coords[:,(i+1)%3]-coords[:,i];f=coords[:,(i+2)%3]-coords[:,i];rows.append(np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i',e,f)/np.maximum(np.linalg.norm(e,axis=1)*np.linalg.norm(f,axis=1),1e-30),-1,1))))
 return np.stack(rows,1)
angle=np.max(abs(angles(v)-angles(u)),axis=1);valid=(area3>1e-12)&np.isfinite(stretch)&np.isfinite(density);parts=m['parts'];partst=np.array([p['vertex_start']for p in parts]);owner=np.searchsorted(partst,d['loopverts'][t[:,0]],side='right')-1;normal=n/np.maximum(2*area3[:,None],1e-30)
# A locally conformal unwrap may still fan and change density across a crown.
# Map the tangent projection of anatomical Z to UV and compare its axis within
# each face-facing part; arbitrary packed island rotation does not affect this.
g11=np.einsum('ij,ij->i',e1,e1);g12=np.einsum('ij,ij->i',e1,e2);g22=np.einsum('ij,ij->i',e2,e2);gram=np.maximum(g11*g22-g12*g12,1e-30)
grad_u=((b1[:,0]*g22-b2[:,0]*g12)/gram)[:,None]*e1+((b2[:,0]*g11-b1[:,0]*g12)/gram)[:,None]*e2
grad_v=((b1[:,1]*g22-b2[:,1]*g12)/gram)[:,None]*e1+((b2[:,1]*g11-b1[:,1]*g12)/gram)[:,None]*e2
z_axis=np.arctan2(grad_v[:,2],grad_u[:,2]);axis_valid=np.hypot(grad_u[:,2],grad_v[:,2])>1e-12
chart_path=out.parent/'uv_triangle_charts.npy';triangle_chart=np.load(chart_path) if chart_path.exists() else np.zeros(len(t),dtype=int)
def quant(values,weights,q):
 ix=np.argsort(values);s=np.cumsum(weights[ix]);return float(values[ix[min(np.searchsorted(s,q*s[-1]),len(ix)-1)]]) if len(ix)else None
def stats(mask):
 mask=mask&valid;w=area3[mask];den=density[mask];st=stretch[mask];an=angle[mask]
 lo=quant(den,w,.05);hi=quant(den,w,.95);am=mask&axis_valid;aw=area3[am];av=z_axis[am];ac=triangle_chart[am];axis_deviation=np.zeros(len(av));axis_mean=0
 for chart in np.unique(ac):
  cm=ac==chart;cw=aw[cm];cv=av[cm];axis_mean=.5*math.atan2(float(np.sum(cw*np.sin(2*cv))),float(np.sum(cw*np.cos(2*cv))));axis_deviation[cm]=np.degrees(abs((cv-axis_mean+math.pi/2)%math.pi-math.pi/2))
 return {'surface_area':float(w.sum()),'triangles':int(mask.sum()),'density_px_per_source_unit_p05':lo,'density_px_per_source_unit_median':quant(den,w,.5),'density_px_per_source_unit_p95':hi,'intraregion_density_p95_p05_ratio':hi/lo if lo else None,'root_to_tip_uv_axis_deviation_degrees_p50':quant(axis_deviation,aw,.5),'root_to_tip_uv_axis_deviation_degrees_p95':quant(axis_deviation,aw,.95),'root_to_tip_uv_axis_mean_degrees':math.degrees(axis_mean),'stretch_ratio_p50':quant(st,w,.5),'stretch_ratio_p95':quant(st,w,.95),'stretch_ratio_p99':quant(st,w,.99),'angle_error_degrees_p95':quant(an,w,.95),'surface_fraction_stretch_over2':float(w[st>2].sum()/w.sum()) if w.sum()else None,'surface_fraction_angle_error_over15':float(w[an>15].sum()/w.sum())if w.sum()else None}
report={'independent':True,'source':metadata,'weighted_by':'Actual triangle surface area; numerical microtriangles do not dominate quantiles','uv_axis_method':'Deviation from each connected chart own mean (packing rotation excluded)' if chart_path.exists() else 'No chart map yet; preliminary axis statistic includes arbitrary island rotation and is not an acceptance gate','parts':{},'materials':{},'heatmap_resolution':2048,'heatmap_legend':{'stretch':'green=1, yellow=2, red>=4 (ratio)','density':'blue=half material median, green=median, red=double median'}}
for pi,part in enumerate(parts):report['parts'][part['mesh']]={'all':stats(owner==pi),'front_facing':stats((owner==pi)&(normal[:,1]>.35)),'hero_front':stats((owner==pi)&(normal[:,1]>.8)),'back_facing':stats((owner==pi)&(normal[:,1]<-.35))}
R=2048;pts=np.rint(u*(R-1)).astype(np.int32);pts[:,:,1]=R-1-pts[:,:,1]
for material in sorted(set(d['material'].tolist())):
 mask=(d['material'][tp]==material)&valid;st=stats(mask);report['materials'][str(material)]=st;median=st['density_px_per_source_unit_median'];dist_image=np.zeros((R,R,3),np.uint8);den_image=np.zeros_like(dist_image)
 for i in ([] if a.skip_heatmaps else np.where(mask)[0]):
  heat=float(np.clip((stretch[i]-1)/3,0,1));color=(int(20*(1-heat)),int(230*(1-heat)),int(30+225*heat));ratio=float(np.clip(math.log2(max(density[i],1e-30)/max(median,1e-30)),-1,1));dc=(int(240*max(-ratio,0)),int(220*(1-abs(ratio))),int(240*max(ratio,0)));cv2.fillConvexPoly(dist_image,pts[i],color);cv2.fillConvexPoly(den_image,pts[i],dc)
 if not a.skip_heatmaps:cv2.imwrite(str(out.parent/('uv_distortion_material_'+str(material)+'.png')),dist_image);cv2.imwrite(str(out.parent/('uv_density_material_'+str(material)+'.png')),den_image)
medians=[v['all']['density_px_per_source_unit_median']for k,v in report['parts'].items()if 'Gum'not in k and v['all']['density_px_per_source_unit_median']];report['enamel_part_median_density_max_min_ratio']=max(medians)/min(medians)
out.write_text(json.dumps(report,indent=2));print(json.dumps({'output':str(out),'materials':report['materials'],'enamel_part_density_ratio':report['enamel_part_median_density_max_min_ratio']}))
