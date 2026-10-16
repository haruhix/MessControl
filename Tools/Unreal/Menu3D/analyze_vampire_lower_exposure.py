"""Read-only geometry/card-mask audit of the pre-remodel lower crown row.

This measures geometric clipping only, not surface visibility through other
meshes. Actual rendered frames must establish tongue/gum/upper-tooth occlusion.
"""
import json
import math
from pathlib import Path
import numpy as np

base=Path('ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff')
source=base/'v05_GumWidthRepair'
meta=json.loads((source/'unreal_mouth_handoff.json').read_text(encoding='utf8'))
poses=np.load(source/'source_world_poses_cm.npz')
parts=[p for p in meta['parts'] if p['mesh'] in ('MC_DAY03_Lower_Central_L','MC_DAY03_Lower_Lateral_L','MC_DAY03_Lower_Canine_L','MC_DAY03_Lower_Molar_L')]
report={'source':'v05 source vertex arrays in native cm; current runtime scale and card height',
    'limits':'Vertex fraction is not visible pixel area and does not test occlusion by other meshes.',
    'samples':[]}
for openness in np.linspace(0,1,9):
    lo=math.floor(openness*4)/4;hi=min(1,lo+.25);t=(openness-lo)*4
    a=poses[str(float(lo))]*(1-t)+poses[str(float(hi))]*t
    scale=.54*(.881020618494+.058839521506*openness)
    height=246.666667+123.333333*openness
    half=height/2
    sample={'openness':float(openness),'uniform_scale':scale,'card_height_cm':height,'lower_crowns':[]}
    for part in parts:
        vertices=a[part['vertex_start']:part['vertex_start']+part['vertex_count']]*scale
        z=vertices[:,2];inside=np.abs(z)<=half
        raw_height=float(z.max()-z.min());kept_height=max(0,min(float(z.max()),half)-max(float(z.min()),-half))
        sample['lower_crowns'].append({'part':part['mesh'],'world_z_cm':[float(z.min()),float(z.max())],
            'raw_height_cm':raw_height,'mask_surviving_height_cm':kept_height,
            'mask_surviving_vertex_fraction':float(inside.mean()),
            'height_kept_fraction':kept_height/raw_height if raw_height else 0})
    report['samples'].append(sample)
out=base/'v06_SymmetricRemodel/QA_Lead';out.mkdir(parents=True,exist_ok=True)
(out/'before_lower_mask_exposure.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps([{'open':s['openness'],'card':round(s['card_height_cm'],2),
    'central_z':s['lower_crowns'][0]['world_z_cm'],
    'central_height_kept':round(s['lower_crowns'][0]['height_kept_fraction'],3)} for s in report['samples']],indent=2))
