"""Check recorded, settled native menu states against the requested interaction flow."""
import argparse
import json
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('capture_directory',type=Path)
args=parser.parse_args()
directory=args.capture_directory.resolve()
route=json.loads((directory/'native_route_actual_states.json').read_text(encoding='utf8'))
assert route['error'] is None,route['error']
assert len(route['actions'])==19
# elapsed, page, selected, active, first visible day
expected=[(1.,0,None,None,None),(3.5,1,1,0,1),(6.,1,1,1,1),
    (9.,1,2,2,1),(11.8,5,2,2,1),(14.2,1,2,2,1),
    (16.5,1,3,3,1),(19.5,1,4,4,2),(22.5,1,5,5,3),
    (25.5,1,6,6,4),(28.5,1,7,7,5),(30.8,1,7,7,5),
    (33.,1,6,6,4),(36.,1,6,0,4),(38.,0,None,None,None),
    (40.5,1,1,0,1),(43.5,1,3,3,1),(46.5,1,3,0,1),
    (49.,0,None,None,None)]
cases=[]
for elapsed,page,selected,active,first in expected:
    sample=min(route['samples'],key=lambda s:abs(s['elapsed']-elapsed))
    checks={'page':sample['page']==page}
    if first is not None:
        checks.update(selected=sample['selected']==selected,active=sample['active']==active,
            window_first=sample['window_first']==first,
            counter=sample['day_counter']==f'{selected:02d} / 07',
            scroll=abs(sample['scroll_offset']-(first-1)*740/3)<.25,
            three_rows=abs(sample['three_row_sum']-740)<.1,
            openness=all(abs(amount-(1. if day==active else 0.))<.015
                for day,amount in enumerate(sample['openness'],1)),
            visible_mouths=[m['day'] for m in sample['mouths'] if not m['hidden']]==list(range(first,first+3)),
            component_visibility=all(m['component_hidden']==m['hidden'] for m in sample['mouths']))
    cases.append({'requested_time':elapsed,'actual_time':sample['elapsed'],
        'expected_page':page,'expected_selected':selected,'expected_active':active,
        'checks':checks,'passed':all(checks.values())})
report={'source':'Actual solo PIE route samples; shared BP functions, not physical input',
    'passed':all(c['passed'] for c in cases),'cases':cases,
    'limits':['Physical wheel and keyboard have separate receipts.',
        'Visual quality and motion-edge clipping require actual frame inspection.',
        'No gameplay travel, network, packaged build or final-art approval.']}
(directory/'route_validation.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'passed':report['passed'],'cases':len(cases),
    'failed':[c for c in cases if not c['passed']]},indent=2))
raise SystemExit(0 if report['passed'] else 1)
