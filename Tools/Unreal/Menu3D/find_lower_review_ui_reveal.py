"""Find first/last actual DAY03 mint button pixels for first-frame UI inspection."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image

p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);args=p.parse_args()
c=json.loads((args.folder/'capture_receipt.json').read_text(encoding='utf8'))
hits=[]
for f in c['frames']:
    if f['time']<1.2:continue
    a=np.asarray(Image.open(args.folder/f['file']).convert('RGB'))[500:590,530:755]
    count=int(((a[:,:,0]>70)&(a[:,:,0]<180)&(a[:,:,1]>205)&(a[:,:,2]>170)).sum())
    if count>500:hits.append(dict(f,mint_pixels=count))
assert hits
report={'first_ui':hits[0],'last_ui':hits[-1],
    'limits':'Pixel detector only locates frames; direct human/model image inspection decides text and enamel clearance.'}
(args.folder/'ui_reveal_frames.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
