"""Original, deterministic short telegraphs; mono PCM source assets, no external samples."""
from pathlib import Path
import array
import math
import random
import wave
import sys

root = Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
out = root / 'ContentSource/Audio/FogBrawl'
out.mkdir(parents=True, exist_ok=True)
rate = 44100
for name, duration in [('Windup', .75), ('Knock', .17), ('Final', .32), ('Beacon', .42), ('Impact', .35)]:
    rng = random.Random(41)
    values = []
    for i in range(round(rate * duration)):
        t = i / rate
        a = min(1, t / .008) * min(1, (duration - t) / .025)
        if name == 'Windup':
            value = (.25 * math.sin(2 * math.pi * (85 * t + 50 * t*t)) + .22 * rng.uniform(-1, 1)) * math.sin(math.pi * t / duration)**1.5
        elif name == 'Knock':
            value = (.62 * math.sin(2*math.pi*710*t) + .26*math.sin(2*math.pi*1130*t) + .15*rng.uniform(-1, 1)) * math.exp(-34*t)
        elif name == 'Final':
            value = (.4*math.sin(2*math.pi*(340*t+420*t*t)) + .16*math.sin(2*math.pi*680*t)) * math.exp(-5*t)
        elif name == 'Beacon':
            value = (.3*math.sin(2*math.pi*660*t) + .25*math.sin(2*math.pi*990*t)) * math.exp(-7*t)
        else:
            value = (.6*math.sin(2*math.pi*(68*t-35*t*t)) + .34*rng.uniform(-1, 1)) * math.exp(-16*t)
        values.append(value * a)
    peak = max(abs(v) for v in values)
    pcm = array.array('h', (round(v / max(1, peak) * 28000) for v in values))
    with wave.open(str(out / ('S_Fog' + name + '.wav')), 'wb') as f:
        f.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        f.writeframes(pcm.tobytes())
print('FOG_AUDIO_SOURCES', len(list(out.glob('*.wav'))))
