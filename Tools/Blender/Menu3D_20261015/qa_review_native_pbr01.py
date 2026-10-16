"""Independent actual-frame/state review support; no generated motion or engine edits."""
import json,math,hashlib
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
import cv2

root=Path(__file__).resolve().parents[3]
art=root/'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly'
capture=art/'Review/Unreal_FunctionalBlocking_v03_FullRoute/LowerRow_v08b_PBR01'
out=art/'UnrealHandoff/v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair/QA_Independent';out.mkdir(exist_ok=True)
states=json.loads((capture/'native_route_actual_states.json').read_text());receipt=json.loads((capture/'capture_receipt.json').read_text());frames=receipt['frames']
for name,indices in [('native_pbr01_opening',[5,6,9,11,12,13,14,15,18,25,29,30]),('native_pbr01_back_routes',[30,31,33,36,40,51,52,59,60,64,66,81])]:
 sheet=Image.new('RGB',(4*644,3*405),(22,22,22));draw=ImageDraw.Draw(sheet)
 for j,i in enumerate(indices):
  im=Image.open(capture/f'frame_{i:04d}.png').convert('RGB');im.thumbnail((640,378));x=(j%4)*644;y=(j//4)*405;sheet.paste(im,(x,y+25));draw.text((x+5,y+5),f'ACTUAL {i:04d}  t={frames[i]["time"]:.3f}s',fill='white')
 sheet.save(out/(name+'.jpg'),quality=94)

post=[s for s in states['samples'] if s['observation_phase']=='after_world_tick'];errors=[]
for s in post:
 t=s['elapsed'];o=s['openness'][2]
 expected={f'Pose_{n}':max(0.,1-abs(o-v)/.25)for n,v in [('000',0),('025',.25),('050',.5),('075',.75)]}
 for n,v in expected.items():
  if abs(v-s['pose_weights'][n])>1e-5:errors.append({'t':t,'kind':'pose','name':n,'expected':v,'actual':s['pose_weights'][n]})
 if abs(s['three_row_sum']-740)>1e-3:errors.append({'t':t,'kind':'row_height_sum','actual':s['three_row_sum']})
 if s['page']==1 and s['day_counter']!=f'{s["selected"]:02d} / 07':errors.append({'t':t,'kind':'footer_counter','actual':s['day_counter'],'selected':s['selected']})
 for j in range(7):
  if not(-1e-6<=s['openness'][j]<=1+1e-6):errors.append({'t':t,'kind':'openness_range','row':j})
 if s['dilation']!=1:errors.append({'t':t,'kind':'dilation','actual':s['dilation']})

def observed(predicate,start,end):
 matches=[s for s in post if start<s['elapsed']<end and predicate(s)]
 return {'first_t':matches[0]['elapsed'],'last_t':matches[-1]['elapsed'],'observations':len(matches)} if matches else None
functional={'source':str(capture),'frame_count':len(frames),'first_frame_time':frames[0]['time'],'last_frame_time':frames[-1]['time'],'post_tick_samples':len(post),'command_time_samples_excluded':len(states['samples'])-len(post),'actions':states['actions'],'post_tick_invariant_errors':errors,
 'observed_route':{'catalog_closed':observed(lambda s:s['page']==1 and s['active']==0 and s['openness'][2]<.001,.53,1.015),'day3_first_fully_open':observed(lambda s:s['page']==1 and s['active']==3 and s['openness'][2]>.999,1.015,2.812),'footer1_collapses_in_catalog':observed(lambda s:s['page']==1 and s['active']==0 and s['openness'][2]<.001,2.812,4.89),'footer2_returns_main':observed(lambda s:s['page']==0,4.89,5.609),'day3_second_fully_open':observed(lambda s:s['page']==1 and s['active']==3 and s['openness'][2]>.999,6,8.1)},
 'limits':['Controlled shared Blueprint function route, pointer hit delivery isolated; not physical input validation','Capture ~10.6 actual frames/s; cannot exclude sub-frame transient flicker','State assertions are based on after-world-tick observations only']}
functional['bounded_route_pass']=not errors and all(v is not None for v in functional['observed_route'].values())
(out/'native_pbr01_state_review.json').write_text(json.dumps(functional,indent=2))

ref=json.loads((art/'UnrealHandoff/v08_ReferenceFinish/QA_Independent/reference_color_patches.json').read_text())
patches={'enamel_upper_central_body':(25,[654,436,684,455]),'enamel_upper_lateral_body':(25,[722,438,749,455]),'enamel_fang_body':(25,[488,445,501,478]),'enamel_lower_central_body':(25,[654,632,682,651]),'gingiva_open_upper_rim':(25,[654,400,681,410]),'gingiva_closed_front_body':(40,[655,663,682,678])}
colors={}
for name,(fi,box) in patches.items():
 pixels=np.asarray(Image.open(capture/f'frame_{fi:04d}.png').convert('RGB'))[box[1]:box[3],box[0]:box[2]].reshape(-1,3)
 median=np.median(pixels,axis=0);target=np.array(ref[name]['median_rgb_0_255']);hsv=cv2.cvtColor(pixels.reshape(-1,1,3).astype('uint8'),cv2.COLOR_RGB2HSV).reshape(-1,3)
 def linear(v):
  v=v/255.;return np.where(v<=.04045,v/12.92,((v+.055)/1.055)**2.4)
 colors[name]={'actual_frame':fi,'actual_box_xyxy':box,'native_median_rgb':median.tolist(),'reference_box_xyxy':ref[name]['box_xyxy'],'reference_median_rgb':target.tolist(),'native_hsv_median':np.median(hsv,axis=0).tolist(),'native_minus_reference_rgb':(median-target).tolist(),'screen_linear_reference_div_native':(linear(target)/np.maximum(linear(median),1e-6)).tolist()}
(out/'native_pbr01_color_patches.json').write_text(json.dumps({'method':'Manually selected analogous anatomical patches; actual screenshot radiance, not albedo or a texture prescription','native_frame':str(capture/'frame_0025.png'),'patches':colors,'limitations':['Reference is AI concept with uncalibrated lighting; patches are directional diagnostics, not a sufficient 1:1 metric','Closed gum sample compares another pose and position; do not infer one universal material multiplier','Unfinished interior excluded']},indent=2))
print(json.dumps({'state_report':str(out/'native_pbr01_state_review.json'),'route_pass':functional['bounded_route_pass'],'errors':errors,'color_patches':colors}))
