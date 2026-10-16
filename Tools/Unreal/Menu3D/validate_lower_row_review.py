"""Validate recorded solo PIE state/morph continuity; visual review is separate."""
import argparse
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path)
p.add_argument('--phase-qualified',action='store_true');args=p.parse_args()
route=json.loads((args.folder/'native_route_actual_states.json').read_text(encoding='utf8'))
capture=json.loads((args.folder/'capture_receipt.json').read_text(encoding='utf8'))
raw_samples=route['samples'];actions=route['actions'];checks=[]
command_samples=[s for s in raw_samples if any(abs(s['elapsed']-a['elapsed'])<1e-7 for a in actions)]
samples=[s for s in raw_samples if s not in command_samples] if args.phase_qualified else raw_samples
def check(name,passed,measured):checks.append({'check':name,'passed':bool(passed),'measured':measured})
check('capture completed without error',route['error'] is None and capture['error'] is None,[route['error'],capture['error']])
check('all13 scheduled actions reached',len(actions)==13,len(actions))
stage=[s for s in samples if s['page']==1]
row_error=max(abs(s['three_row_sum']-740) for s in stage)
check('three-card height stays740',row_error<.01,row_error)
pose_error=0
for s in samples:
    o=s['openness'][2]
    for i,name in enumerate(('Pose_000','Pose_025','Pose_050','Pose_075')):
        weight=max(0.,1.-abs(o-i*.25)*4)
        pose_error=max(pose_error,abs(weight-s['pose_weights'][name]))
check('native morph weights follow recorded openness without stale pose',pose_error<1e-5,pose_error)
for label,start,end,direction in [('slow opening',2.2,6.8,1),('slow closing',6.8,11.4,-1)]:
    seq=[s['openness'][2] for s in samples if start+.1<s['elapsed']<end-.1]
    violations=sum((b-a)*direction < -1e-5 for a,b in zip(seq,seq[1:]))
    endpoint=seq[-1]
    check(label+' is monotonic',violations==0,{'samples':len(seq),'violations':violations})
    check(label+' reaches endpoint',endpoint>.99 if direction>0 else endpoint<.01,endpoint)
expected={1:(1,1,0),3:(1,3,3),4:(1,3,0),6:(1,3,3),7:(1,4,4),8:(1,3,3),9:(1,3,0),10:(0,3,0),11:(1,1,0),12:(1,3,3)}
for i,(page,selected,active) in expected.items():
    end=actions[i+1]['elapsed'] if i+1<len(actions) else samples[-1]['elapsed']+.01
    eligible=[s for s in samples if actions[i]['elapsed']<=s['elapsed']<end-.08]
    s=eligible[-1];actual=(s['page'],s['selected'],s['active'])
    check(f'action{i} {actions[i]["action"]} settled state',actual==(page,selected,active),actual)
intervals=[b['time']-a['time'] for a,b in zip(capture['frames'],capture['frames'][1:])]
report={'status':'PASS' if all(c['passed'] for c in checks) else 'FAIL','checks':checks,
    'observation_phase':'after next world tick only; commands are injected in Slate post-tick after the prior world update' if args.phase_qualified else 'raw, including command-before-world-update observations',
    'command_time_observations_separately_classified':len(command_samples) if args.phase_qualified else 0,
    'actual_frames':len(capture['frames']),'max_capture_interval_s':max(intervals),
    'limits':'Shared Blueprint-function route in solo PIE. Slowed segment is game dilation0.15, followed by normal1.0. No physical input, every-rendered-frame, FPS or final-art claim. Crown/gingiva/tongue visibility requires actual frame review.'}
(args.folder/('phase_state_validation.json' if args.phase_qualified else 'state_validation.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
