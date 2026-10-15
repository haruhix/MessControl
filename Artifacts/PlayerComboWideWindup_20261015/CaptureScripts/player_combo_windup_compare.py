from pathlib import Path
import json,numpy as np
from PIL import Image,ImageDraw
root=Path('D:/PROJECT/GAME/WaifuTD/MessControl/Artifacts')
out=root/'PlayerComboWideWindup_20261015'
old=json.loads((root/'PlayerComboDiagonal_20261015/DiagonalCheck.json').read_text())
new=json.loads((out/'DiagonalCheck.json').read_text())
report=[]
for a,b in zip(old,new):
 assert a['case']==b['case']
 before=np.array(a['ready_hand']);after=np.array(b['ready_hand'])
 extra_back=float(before[0]-after[0])
 width_before=abs(a['hand_delta'][1]);width_after=abs(b['hand_delta'][1])
 assert extra_back>35,(a['case'],extra_back)
 assert width_after>width_before+15,(a['case'],width_after,width_before)
 assert min(v['edge_dot_sharp_point_velocity'] for v in b['cutting_alignment_samples'])>.9
 report.append({'case':a['case'],'before_ready_hand':before.tolist(),'after_ready_hand':after.tolist(),'additional_backward_reach_cm':extra_back,'before_lateral_sweep_cm':width_before,'after_lateral_sweep_cm':width_after,'readable_windup':True,'sharp_edge_leads':True})
(out/'WindupComparison.json').write_text(json.dumps(report,indent=2))
sheet=Image.new('RGB',(1200,700),(25,28,35));draw=ImageDraw.Draw(sheet)
for row,case in enumerate(['knife_combo','pickaxe_combo']):
 for col,folder in enumerate(['PlayerComboDiagonal_20261015','PlayerComboWideWindup_20261015']):
  data=json.loads((root/folder/'SoloCheck.json').read_text())
  samples=[r for r in data['samples'] if r['case']==case];hits=[];previous=samples[0]['serial']
  for r in samples:
   if r['serial']!=previous:hits.append(r['time']);previous=r['serial']
  frame=min([r for r in data['captures'] if r['case']==case],key=lambda r:abs(r['time']-hits[1]+.10))
  with Image.open(frame['file']) as im:
   sheet.paste(im.crop((250,100,750,370)).convert('RGB').resize((600,324)),(col*600,row*350+26))
  draw.text((col*600+10,row*350+8),case+' | '+['previous wind-up','drawn-back wind-up'][col],fill='white')
sheet.save(out/'WindupComparison.jpg',quality=95)
print(json.dumps(report,indent=2))
