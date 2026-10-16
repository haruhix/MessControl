"""Read-only audit of post-action state observations versus stage/morph updates."""
import json
from pathlib import Path
folder=Path('ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute/LowerRow_v07_Run01')
r=json.loads((folder/'native_route_actual_states.json').read_text(encoding='utf8'))
out=[]
for s in r['samples']:
    o=s['openness'][2]
    error=max(abs(max(0,1-abs(o-i*.25)*4)-s['pose_weights'][n]) for i,n in enumerate(('Pose_000','Pose_025','Pose_050','Pose_075')))
    row=abs(s['three_row_sum']-740) if s['page']==1 else 0
    if error>1e-5 or row>.01:
        a=[a for a in r['actions'] if a['elapsed']<=s['elapsed']][-1]
        out.append({'t':s['elapsed'],'delta_last_action':s['elapsed']-a['elapsed'],'action':a,
            'open3':o,'weights':s['pose_weights'],'pose_error':error,'row_error':row,'page':s['page']})
report={'outliers':out,'total_samples':len(r['samples'])}
(folder/'post_action_outliers.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
