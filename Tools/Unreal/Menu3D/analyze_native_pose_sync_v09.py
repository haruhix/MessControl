"""Compare the three actual component curve maps to recorded widget openness."""
import argparse
import json
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('capture',type=Path)
args=parser.parse_args()
state=json.loads((args.capture/'native_route_actual_states.json').read_text(encoding='utf8'))
assert state['error'] is None
result={'max_cpu_curve_error':0.,'failures':[],'boundary_samples':[]}
for sample in state['samples']:
    if sample['observation_phase']!='after_world_tick':continue
    errors={}
    for day in (1,2,3):
        amount=sample['openness'][day-1]
        expected={f'Pose_{int(pose*100):03d}':max(0.,min(1.,1.-abs((amount-pose)*4.))) for pose in (0.,.25,.5,.75)}
        actual=sample['all_pose_weights'][str(day)]
        errors[str(day)]=max(abs(expected[name]-actual[name]) for name in expected)
        result['max_cpu_curve_error']=max(result['max_cpu_curve_error'],errors[str(day)])
        if errors[str(day)]>.01:
            result['failures'].append({'elapsed':sample['elapsed'],'day':day,'Open':amount,'expected':expected,'actual':actual,'error':errors[str(day)]})
    if any(0.<sample['elapsed']-action['elapsed']<.35 for action in state['actions'] if action['action'] in ('SelectDay','Back')):
        result['boundary_samples'].append({'elapsed':sample['elapsed'],'open':sample['openness'][:3],
                                          'actual':sample['all_pose_weights'],'error':errors})
(args.capture/'cpu_curve_comparison.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps({'max_cpu_curve_error':result['max_cpu_curve_error'],'failure_count':len(result['failures']),
                  'first_failures':result['failures'][:6],'boundary_samples':result['boundary_samples'][:6]},indent=2))
