"""Original short synthesized placeholders; safe to replace in DA_MouthSounds."""
from pathlib import Path
import math, random, struct, wave
root=Path(__file__).resolve().parents[1]/"ArtSource"/"Audio"
root.mkdir(parents=True,exist_ok=True)
random.seed(421)
rate=24000
specs={"Step":(0.08,190),"Jump":(0.24,310),"Brush":(0.16,850),"Pull":(0.19,230),"Repair":(0.3,460),"Complete":(0.5,660),"DayStart":(0.65,440),"Win":(1.1,520),"Lose":(0.7,280)}
for name,(duration,freq) in specs.items():
    samples=[]
    for i in range(int(rate*duration)):
        t=i/rate; u=t/duration
        env=min(1,t/0.008)*max(0,1-u)**1.8
        sweep=freq*(1.6-0.7*u) if name in ("Jump","Pull","Lose") else freq
        melody=(1,1.25,1.5,2)[min(3,int(u*4))] if name in ("Complete","Win","DayStart") else 1
        tone=math.sin(math.tau*sweep*melody*t)+0.25*math.sin(math.tau*sweep*melody*2*t)
        if name=="Brush": tone=random.uniform(-1,1)*0.65+0.1*tone
        if name=="Step": tone=0.5*tone+random.uniform(-0.25,0.25)
        samples.append(struct.pack("<h",int(max(-1,min(1,tone*env*0.23))*32767)))
    with wave.open(str(root/(name+".wav")),"wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(b"".join(samples))
print("MC_AUDIO_COMPLETE")
