from pathlib import Path
import json,numpy as np
from PIL import Image,ImageDraw
out=Path('D:/PROJECT/GAME/WaifuTD/MessControl/Artifacts/PlayerComboDiagonal_20261015')
d=json.loads((out/'SoloCheck.json').read_text());assert d['passed'],d.get('error')
report=[]
sheet=Image.new('RGB',(1440,640),(25,28,35));draw=ImageDraw.Draw(sheet)
for row,case in enumerate(['knife_combo','pickaxe_combo']):
 samples=[r for r in d['samples'] if r['case']==case]
 hits=[];previous=samples[0]['serial']
 for r in samples:
  if r['serial']!=previous:hits.append(r['time']);previous=r['serial']
 assert len(hits)==3,hits
 contact=(.62 if case=='knife_combo' else .64)*.42
 second_at=hits[1]-contact
 second=[r for r in samples if second_at<=r['time']<second_at+(.62 if case=='knife_combo' else .64)]
 # Before contact, body motion still contributes; exclude the cosmetic hit hold.
 active=[r for r in second if contact-.10<=r['time']-second_at<=contact-.015]
 vectors=[]
 for a,b in zip(active,active[1:]):
  velocity=np.array(b['hand_local'])-a['hand_local'];norm=np.linalg.norm(velocity)
  if norm>1e-3:
   edge=np.array(b['cutting_direction']);normal=np.array(b['blade_normal'])
   tip_velocity=np.array(b['tool_tip_local'])-a['tool_tip_local'];tip_velocity/=np.linalg.norm(tip_velocity)
   vectors.append({'time':b['time'],'edge_dot_hand_velocity':float(edge@velocity/norm),'edge_dot_sharp_point_velocity':float(edge@tip_velocity),'face_dot_hand_velocity':float(abs(normal@velocity/norm))})
 assert vectors and min(v['edge_dot_hand_velocity'] for v in vectors)>.9,vectors
 ready=min(second,key=lambda r:abs(r['time']-second_at-(contact-.10)))
 finish=min(second,key=lambda r:abs(r['time']-second_at-(contact+.17)))
 delta=np.array(finish['hand_local'])-ready['hand_local']
 assert delta[1]<-60 and delta[2]<-70,delta
 entry={'case':case,'second_started':second_at,'contact_offset':contact,'ready_hand':ready['hand_local'],'finish_hand':finish['hand_local'],'hand_delta':delta.tolist(),'cutting_alignment_samples':vectors}
 report.append(entry)
 times=[contact-.10,contact-.03,contact+.17]
 frames=[r for r in d['captures'] if r['case']==case]
 for col,t in enumerate(times):
  r=min(frames,key=lambda r:abs(r['time']-second_at-t))
  with Image.open(r['file']) as im:
   # Genuine gameplay-camera frame, cropped only to make the contact readable.
   im=im.crop((295,130,705,425)).convert('RGB').resize((480,295))
   sheet.paste(im,(col*480,row*320+25))
  draw.text((col*480+10,row*320+7),case+' | '+['upper right','cutting edge leads','lower left'][col],fill='white')
sheet.save(out/'SecondStrike.jpg',quality=95)
(out/'DiagonalCheck.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
