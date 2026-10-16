"""Predict uniform native width and crown mask retention from frozen v06 geometry."""
import json
from pathlib import Path
import numpy as np

base=Path('ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v06_SymmetricRemodel')
meta=json.loads((base/'unreal_mouth_handoff.json').read_text(encoding='utf8'))
poses=np.load(base/'source_world_poses_cm.npz')
f=1280/(2*np.tan(np.deg2rad(39.970001220703125)/2))
def width(a,s):
    x=f*a[:,0]*s/(2200-a[:,1]*s)
    return float(x.max()-x.min())
a=poses['1.0']
lo,hi=.3,.7
for _ in range(40):
    mid=(lo+hi)/2
    if width(a,mid)<674:lo=mid
    else:hi=mid
scale=(lo+hi)/2
parts=[x for x in meta['parts'] if x['mesh'].startswith('MC_DAY03_Lower_') and x['mesh'].endswith('_L')]
samples=[]
for o in np.linspace(0,1,9):
    j=int(np.floor(o*4));k=min(4,j+1);t=o*4-j
    a=poses[str(float(j/4))]*(1-t)+poses[str(float(k/4))]*t
    half=(246.666667+123.333333*o)/2
    crowns=[]
    for p in parts:
        v=a[p['vertex_start']:p['vertex_start']+p['vertex_count']]*scale
        z=v[:,2];raw=float(z.max()-z.min())
        kept=max(0,min(float(z.max()),half)-max(float(z.min()),-half))
        crowns.append({'part':p['mesh'],'z': [float(z.min()),float(z.max())],'mask_height_kept_fraction':kept/raw})
    samples.append({'open':float(o),'predicted_full_width_px':width(a,scale),'crowns':crowns})
report={'source_npz':'native cm with Y conversion already applied','camera_y_cm':2200,'fov_horizontal':39.970001220703125,
    'width_target_px':674,'uniform_scale':scale,'factor_relative_to_054':scale/.54,
    'limits':'All vertices projected; final rendered edge, overlap, tongue and scene lighting must be checked in Unreal.',
    'samples':samples}
out=base/'QA_Lead/predicted_stage_fit.json';out.write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'scale':scale,'factor':scale/.54,'samples':[{'open':s['open'],'width':s['predicted_full_width_px'],'central':s['crowns'][0]} for s in samples]},indent=2))
