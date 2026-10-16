"""Uniform-only normalized camera comparison, not an Unreal import/fit claim."""
from pathlib import Path
import math,json
import numpy as np
root=Path(__file__).resolve().parents[3];base=root/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff';family=base/'v09_FamilyReferenceForms';vamp=np.load(base/'v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair/QA_Independent/outer_source_geometry.npz');f=800/math.tan(math.radians(39.97)/2)
def width(x,scale):
 px=f*x[:,0]*scale/(2200-x[:,1]*scale);return float(np.ptp(px))
target=max(width(vamp[k],53.7654)for k in vamp.files);report={'method':'Uniform actor scale fitted to final DAY03 maximum projected width, cameraY2200/FOV39.97/design1600. No anisotropic correction or geometry editing. Actual native fit remains lead-owned.','target_width_design_px':target,'days':{}}
for day,rev in [('DAY01','UV_v02_GumReturns'),('DAY02','UV_v01')]:
 poses=np.load(family/'QA_Independent'/day/'source_geometry.npz');m=json.loads((family/day/rev/'unreal_mouth_handoff.json').read_text());a=poses['Basis'];lo,hi=0.,100.
 for _ in range(40):
  scale=(lo+hi)/2
  if width(a,scale)<target:lo=scale
  else:hi=scale
 scale=(lo+hi)/2;seams={}
 for row in ['Upper','Lower']:
  front=[p for p in m['parts']if p.get('row')==row and p.get('ordinal')==1];R=next(p for p in front if p['mesh'].endswith('_R'));L=next(p for p in front if p['mesh'].endswith('_L'));r=a[R['vertex_start']:R['vertex_start']+R['vertex_count']];l=a[L['vertex_start']:L['vertex_start']+L['vertex_count']];seams[row]={'right_crown_min_x':float(r[:,0].min()),'left_crown_max_x':float(l[:,0].max()),'central_gap_source_units':float(r[:,0].min()-l[:,0].max()),'two_distinct_mirrored_front_parts':[R['mesh'],L['mesh']]}
 d={'uniform_actor_scale':scale/100,'source_units_to_world_cm':scale,'projected_widths_all9':{k:width(poses[k],scale)for k in poses.files},'center_seams':seams};report['days'][day]=d;(family/'QA_Independent'/day/'uniform_width_comparison.json').write_text(json.dumps(d,indent=2))
(family/'QA_Independent/uniform_family_width_comparison.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
