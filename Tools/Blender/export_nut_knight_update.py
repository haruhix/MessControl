"""Bake the supplied knight actions to the existing ARP UE deform hierarchy.

Run in an isolated Blender process. The supplied blend and every legacy asset
remain read-only. Generated FBX/report go to ArtSource/NutKnightUpdate.
"""
import bpy,addon_utils,sys,json
from pathlib import Path
from mathutils import Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_nut_tank import SETTINGS,digest,fbx_inventory,select,sparse_pose_samples

ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path('C:/Users/vasil/Downloads/Telegram Desktop/Nut_Tank.blend')
OUT=ROOT/'ArtSource/NutKnightUpdate'
CLIPS={'Idle':'NUT_Idle1','Jump':'NUT_Jump_atack1','Block':'NUT_block',
       'Hit':'NUT_damage1','Stun':'NUT_stun','StunHit':'NUT_damage_2',
       'Attack1':'NUT_Atack1','Attack2':'NUT_Atack2','Combo':'NUT_Atack3','Death':'NUT_death1'}

def main():
 OUT.mkdir(parents=True,exist_ok=True)
 original=digest(SOURCE)
 version=f'{bpy.app.version[0]}.{bpy.app.version[1]}'
 extension=Path.home()/f'AppData/Roaming/Blender Foundation/Blender/{version}/extensions/user_default'
 sys.path.insert(0,str(extension))
 arp=addon_utils.enable('auto_rig_pro',default_set=True,persistent=False)
 assert arp and hasattr(bpy.types.Scene,'arp_export_rig_type'),'ARP unavailable in isolated process'
 bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
 scene=bpy.context.scene;rig=bpy.data.objects['rig'];meshes=[bpy.data.objects[n] for n in ['SM_NutTank','SM_Sheld','SM_Club']]
 rig.animation_data_create();rig.animation_data.action=None;rig.animation_data.use_nla=False
 for track in rig.animation_data.nla_tracks:track.mute=True
 props={b.name:{k:b[k] for k in b.keys() if isinstance(b[k],(bool,int,float))} for b in rig.pose.bones}
 for name,value in SETTINGS.items():
  assert hasattr(scene,name),name
  setattr(scene,name,value)
 old=json.loads((ROOT/'ArtSource/NutTank/ExportReport.json').read_text())
 ref=next(f['fbx'] for f in old['files'] if f['file']=='SK_NutTank.fbx')
 bind={p['name']:p['matrix'] for p in ref['bind_poses']}
 report={'status':'exporting','source':str(SOURCE),'source_sha256_before':original,'blender':bpy.app.version_string,
         'fps':scene.render.fps/scene.render.fps_base,'clips':[],'files':[],'source_pose_samples':{},'dense_pose_samples':{},
         'scope':'Animation only; preserve existing skeleton, meshes, walking and ball transform.'}
 def save():(OUT/'ExportReport.json').write_text(json.dumps(report,indent=2))
 save()
 for label,name in CLIPS.items():
  rig.animation_data.action=None
  for b in rig.pose.bones:
   b.matrix_basis=Matrix.Identity(4)
   for k,value in props[b.name].items():b[k]=value
  action=bpy.data.actions[name];rig.animation_data.action=action;rig.animation_data.action_slot=next(s for s in action.slots if s.target_id_type=='OBJECT')
  start,end=map(round,action.frame_range)
  scene.arp_bake_anim=True;scene.arp_frame_range_type='CUSTOM'
  scene.arp_export_start_frame=start;scene.arp_export_end_frame=end;scene.frame_start=start;scene.frame_end=end
  report['source_pose_samples'][label]=sparse_pose_samples(scene,rig,start,end)
  dense=[]
  for frame in range(start,end+1):
   scene.frame_set(frame);ev=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
   names=[n for n in ['root.x','foot.l','foot.r','hand.l','hand.r','head.x'] if n in ev.pose.bones]
   dense.append({'frame':frame,'points':{n:list((ev.matrix_world@ev.pose.bones[n].matrix).translation) for n in names}})
  report['dense_pose_samples'][label]=dense
  scene.frame_set(start);bpy.context.view_layer.update();select([rig,*meshes],rig)
  path=OUT/f'A_NutKnight_{label}.fbx'
  assert not path.exists(),'Do not overwrite previous exports without review'
  result=bpy.ops.arp.arp_export_fbx_panel(filepath=str(path));assert 'FINISHED' in result and path.stat().st_size>10000
  info=fbx_inventory(path);assert info['hierarchy_sha256']==ref['hierarchy_sha256'],('Hierarchy differs',label)
  assert info['root_animation_constant'],('Animated root',label)
  current={p['name']:p['matrix'] for p in info['bind_poses']};assert set(current)==set(bind)
  delta=max(abs(a-b) for n in bind for a,b in zip(bind[n],current[n]));assert delta<.0001,('Bind differs',delta)
  report['files'].append({'file':path.name,'sha256':digest(path),'fbx':info,'bind_max_delta':delta})
  report['clips'].append({'label':label,'source_action':name,'file':path.name,'start_frame':start,'end_frame':end,'fps':report['fps'],'duration_seconds':(end-start)/report['fps']})
  save();print('NUT_KNIGHT_EXPORTED',label,start,end,flush=True)
 report.update(status='complete',source_sha256_after=digest(SOURCE),source_preserved=digest(SOURCE)==original,
               hierarchy_consistent=True,reference_unchanged=True,root_animation_constant=True)
 assert report['source_preserved'];save();print('NUT_KNIGHT_EXPORT_PASS',len(CLIPS),flush=True)

if __name__=='__main__':main()
