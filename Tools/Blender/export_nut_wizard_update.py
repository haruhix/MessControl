"""Export the seven supplied wizard actions in a separate background process.

The artist blend is read-only; current mesh/skeleton/walking are compatibility
targets. ARP bakes the existing deform hierarchy and checks the original bind.
"""
import bpy,addon_utils,sys,json
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_nut_tank import SETTINGS,digest,fbx_inventory,select,sparse_pose_samples
ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path('C:/Users/vasil/Downloads/Telegram Desktop/Nut_Wizard (2).blend')
OUT=ROOT/'ArtSource/NutWizardUpdate_20261017'
CLIPS={'Idle':'NUT_W_Nut_Idle','Hit':'NUT_W_Nut_damage','Death':'NUT_W_death',
 'Cast':'NUT_W_Firenut','Summon':'NUT_W_CastSummon','Rain':'NUT_W_NutRain','Teleport':'NUT_W_Nut_teleport'}

def main():
 assert bpy.app.background
 OUT.mkdir(parents=True,exist_ok=True);sha=digest(SOURCE)
 version=f'{bpy.app.version[0]}.{bpy.app.version[1]}'
 sys.path.insert(0,str(Path.home()/f'AppData/Roaming/Blender Foundation/Blender/{version}/extensions/user_default'))
 arp=addon_utils.enable('auto_rig_pro',default_set=True,persistent=False);assert arp
 bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
 scene=bpy.context.scene;rig=bpy.data.objects['rig']
 meshes=[o for o in scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers)]
 assert len(meshes)==1 and meshes[0].name=='SM_NutWizard'
 rig.animation_data_create();rig.animation_data.action=None;rig.animation_data.use_nla=False
 for track in rig.animation_data.nla_tracks:track.mute=True
 props={b.name:{k:b[k] for k in b.keys() if isinstance(b[k],(bool,int,float))} for b in rig.pose.bones}
 for name,value in SETTINGS.items():setattr(scene,name,value)
 old=json.loads((ROOT/'ArtSource/NutWizard/ExportReport.json').read_text())
 reference=next(f['fbx'] for f in old['files'] if f['file']=='SK_NutWizard.fbx')
 bind={p['name']:p['matrix'] for p in reference['bind_poses']}
 report=dict(status='exporting',source=str(SOURCE),source_sha256_before=sha,fps=scene.render.fps/scene.render.fps_base,
  clips=[],files=[],dense_pose_samples={},source_pose_samples={},scope=__doc__)
 def save():(OUT/'ExportReport.json').write_text(json.dumps(report,indent=2))
 save()
 for label,name in CLIPS.items():
  rig.animation_data.action=None
  for b in rig.pose.bones:
   b.matrix_basis=Matrix.Identity(4)
   for k,v in props[b.name].items():b[k]=v
  action=bpy.data.actions[name];rig.animation_data.action=action
  rig.animation_data.action_slot=next(s for s in action.slots if s.target_id_type=='OBJECT')
  start,end=map(round,action.frame_range)
  scene.arp_bake_anim=True;scene.arp_frame_range_type='CUSTOM'
  scene.arp_export_start_frame=start;scene.arp_export_end_frame=end;scene.frame_start=start;scene.frame_end=end
  report['source_pose_samples'][label]=sparse_pose_samples(scene,rig,start,end)
  dense=[]
  for frame in range(start,end+1):
   scene.frame_set(frame);dg=bpy.context.evaluated_depsgraph_get();ev=rig.evaluated_get(dg)
   points={n:list((ev.matrix_world@ev.pose.bones[n].matrix).translation) for n in ['root.x','foot.l','foot.r','hand.l','hand.r','head.x'] if n in ev.pose.bones}
   mesh=meshes[0].evaluated_get(dg);corners=[mesh.matrix_world@Vector(v) for v in mesh.bound_box]
   dense.append(dict(frame=frame,points=points,mesh_min=[min(v[k] for v in corners) for k in range(3)],mesh_max=[max(v[k] for v in corners) for k in range(3)]))
  report['dense_pose_samples'][label]=dense
  scene.frame_set(start);bpy.context.view_layer.update();select([rig,*meshes],rig)
  path=OUT/f'A_NutWizard_{label}.fbx'
  result=bpy.ops.arp.arp_export_fbx_panel(filepath=str(path));assert 'FINISHED' in result and path.stat().st_size>10000
  info=fbx_inventory(path);assert info['hierarchy_sha256']==reference['hierarchy_sha256'],('Different skeleton',label)
  current={p['name']:p['matrix'] for p in info['bind_poses']};assert set(current)==set(bind)
  delta=max(abs(a-b) for n in bind for a,b in zip(bind[n],current[n]));assert delta<.0001,('Bind differs',label,delta)
  assert info['root_animation_constant'],('Animated FBX root',label)
  report['files'].append(dict(file=path.name,sha256=digest(path),fbx=info,bind_max_delta=delta))
  report['clips'].append(dict(label=label,source_action=name,file=path.name,start_frame=start,end_frame=end,fps=report['fps'],duration_seconds=(end-start)/report['fps'],loop=label=='Idle'))
  save();print('WIZARD_ACTION_EXPORTED',label,start,end,flush=True)
 report.update(status='complete',source_preserved=digest(SOURCE)==sha,source_sha256_after=digest(SOURCE),walking_untouched=True)
 assert report['source_preserved'];save();print('WIZARD_UPDATE_EXPORT_PASS',len(CLIPS),flush=True)

if __name__=='__main__':main()
