"""Original deterministic coin clinks; no third-party audio or runtime synthesis."""
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import wave

root=Path(__file__).resolve().parent
assert root.drive.upper()=='E:'
sample_rate=48000
seconds=1.15
rng=random.Random(42026)
samples=[]
for frame in range(round(seconds*sample_rate)):
    t=frame/sample_rate
    value=0.
    for n,start in enumerate((.40,.48,.56)):
        age=t-start
        if 0<=age<.45:
            attack=1-math.exp(-age/.0007)
            fundamental=1400*(1+n*.075)
            # Inharmonic, quickly decaying partials give metal a short clink.
            for ratio,gain,decay in ((1.,1.,.063),(1.43,.38,.041),(2.38,.19,.024),(3.91,.075,.014)):
                value+=.26*(.82**n)*gain*attack*math.exp(-age/decay)*math.sin(2*math.pi*fundamental*ratio*age)
            value+=.018*math.exp(-age/.002)*rng.uniform(-1,1)
            value+=.018*math.exp(-age/.018)*math.sin(2*math.pi*310*age)
    samples.append(value)
peak=max(abs(s) for s in samples)
gain=.56/peak
samples=[s*gain for s in samples]
path=root/'TaskCoins_Original.wav'
with wave.open(str(path),'wb') as output:
    output.setnchannels(1)
    output.setsampwidth(2)
    output.setframerate(sample_rate)
    output.writeframes(b''.join(struct.pack('<h',round(s*32767)) for s in samples))
metadata={'source':'Original deterministic synthesis authored for MessControl',
          'sample_rate':sample_rate,'channels':1,'seconds':seconds,
          'first_clink_seconds':.4,'clink_offsets':[.40,.48,.56],
          'peak_dbfs':20*math.log10(max(abs(s) for s in samples)),
          'rms_dbfs':20*math.log10(math.sqrt(sum(s*s for s in samples)/len(samples))),
          'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
          'audition_status':'Candidate; listen in gameplay at the common checkpoint before artistic acceptance'}
(root/'TaskCoins_Original.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
print(json.dumps(metadata))
