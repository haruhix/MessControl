"""Align spell release poses to the preserved gameplay emitter in a new saved copy."""
import bpy, ast, math, json, hashlib
from pathlib import Path
from mathutils import Matrix, Vector, Euler
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/NutAnimationStyle/Wizard'
INPUT=OUT/'NutWizard_Style_v2.blend'
OUTPUT=OUT/'NutWizard_Style_v2_Game.blend'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source_hash=digest(INPUT)
bpy.ops.wm.open_mainfile(filepath=str(INPUT))
rig=bpy.data.objects['rig'];scene=bpy.context.scene
rig.animation_data.use_nla=False
for t in rig.animation_data.nla_tracks:t.mute=True
rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
restsig=[(b.name,b.parent.name if b.parent else None,[list(r)for r in b.matrix_local],b.use_deform)for b in rig.data.bones]
controls=[b for b in rig.pose.bones if b.name.startswith('c_')]
original_props={b.name:{k:b[k]for k in b.keys()if isinstance(b[k],(float,int,bool))}for b in rig.pose.bones}
tree=ast.parse((ROOT/'Tools/Blender/author_nut_wizard_style.py').read_text(encoding='utf8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name in {'bind','set_matrix','key'}],type_ignores=[]),'wizard_helpers','exec'))
def curves(a):return[f for l in a.layers for s in l.strips for bag in s.channelbags for f in bag.fcurves]
def action_hash(a):return hashlib.sha256(json.dumps([(f.data_path,f.array_index,[(list(k.co),list(k.handle_left),list(k.handle_right),k.interpolation)for k in f.keyframe_points])for f in curves(a)],sort_keys=True).encode()).hexdigest()
old_hashes={a.name:action_hash(a)for a in bpy.data.actions}
labels=('Cast','HeavyCast','Summon','Rain');new_actions={};target=Vector((-.067,-.904,.713));fixed=['c_thigh_b.l','c_thigh_b.r','c_hand_ik.l','c_foot_ik.l','c_foot_ik.r','c_arms_pole.l','c_arms_pole.r','c_leg_pole.l','c_leg_pole.r']
def envelope(f):
 t=(f-33)/23 if f<56 else (85-f)/22 if f>63 else 1
 t=max(0,min(1,t));return t*t*(3-2*t)
for label in labels:
 src=bpy.data.actions['MC_Wizard_'+label];dest=src.copy();dest.name='MC_Game_Wizard_'+label;dest.use_fake_user=True
 for f in range(1,102):
  bind(src);scene.frame_set(f);bpy.context.view_layer.update()
  originals={n:rig.pose.bones[n].matrix.copy()for n in fixed+['c_root_master.x','c_hand_ik.r']}
  w=envelope(f)
  if w>0:
   root=originals['c_root_master.x'];eul=(root.to_quaternion()@rest['c_root_master.x'].to_quaternion().inverted()).to_euler('XYZ')
   rot=Euler((eul.x,eul.y,math.radians(70)),'XYZ').to_matrix().to_4x4()@rest['c_root_master.x']
   rot.translation=rest['c_root_master.x'].translation+Vector((0,-.18,-.07))
   blend=root.to_quaternion().slerp(rot.to_quaternion(),w).to_matrix().to_4x4();blend.translation=root.translation.lerp(rot.translation,w)
   rig.pose.bones['c_root_master.x'].matrix=blend;bpy.context.view_layer.update()
   for n in fixed:rig.pose.bones[n].matrix=originals[n]
   hand=originals['c_hand_ik.r'].copy();hand.translation=hand.translation.lerp(target,w);rig.pose.bones['c_hand_ik.r'].matrix=hand
   bpy.context.view_layer.update()
  rig.animation_data.action=dest;rig.animation_data.action_slot=dest.slots[0];key(f)
 for curve in curves(dest):
  for point in curve.keyframe_points:point.interpolation='LINEAR'
 src.name='MC_Source_Wizard_'+label;dest.name='MC_Wizard_'+label
 dest['game_emitter_pose_correction']='Actor emitter unchanged129.014,11.936,5.58cm; zeroAim release correction, planted feet, no stretch.'
 new_actions[label]=dest
 print('GAME_POSE_BAKED',label,flush=True)
# All meshes attached to the original rig, source rest and original animation backups remain intact.
limbs=['thigh_stretch.l','thigh_stretch.r','leg_stretch.l','leg_stretch.r','arm_stretch.l','arm_stretch.r','forearm_stretch.l','forearm_stretch.r']
meshes=[o for o in scene.objects if o.type=='MESH'and any(m.type=='ARMATURE'and m.object==rig for m in o.modifiers)]
report={'passed':True,'blend':str(OUTPUT),'input':str(INPUT),'input_sha256':source_hash,'preserved_gameplay_emitter_actor_cm':[129.014337,11.936049,5.57991],'component_wrist_target_m':[-.067,.904,.713],'source_wrist_target_m':list(target),'primary_aim_only':True,'actions':{},'scope':'Source DCC wrist/reach, finite bones, no stretch, contacts, floor. Imported/native-overlay measurements remain separate.'}
for label,action in new_actions.items():
 bind(action);ratios={n:[float('inf'),0]for n in limbs};feet={n:[]for n in ('foot.l','foot.r')};minz=float('inf');release=[];root_positions=[]
 for f in range(1,102):
  scene.frame_set(f);bpy.context.view_layer.update()
  for b in rig.pose.bones:
   assert all(math.isfinite(v)for row in b.matrix for v in row),(label,f,b.name)
  for n in limbs:
   b=rig.pose.bones[n];r=(b.tail-b.head).length/rig.data.bones[n].length;ratios[n][0]=min(ratios[n][0],r);ratios[n][1]=max(ratios[n][1],r);assert .95<r<1.05,(label,f,n,r)
  for n in feet:feet[n].append(rig.pose.bones[n].matrix.translation.copy())
  if 'root.x'in rig.pose.bones:root_positions.append(rig.pose.bones['root.x'].matrix.copy())
  deps=bpy.context.evaluated_depsgraph_get()
  for obj in meshes:
   ev=obj.evaluated_get(deps);mesh=ev.to_mesh()
   try:minz=min(minz,min((ev.matrix_world@v.co).z for v in mesh.vertices))
   finally:ev.to_mesh_clear()
  if f in (56,59,63):
   shoulder=rig.pose.bones['arm_stretch.r'].head;forearm=rig.pose.bones['forearm_stretch.r'].head;wrist=rig.pose.bones['hand.r'].head
   release.append({'frame':f,'wrist_m':list(wrist),'wrist_target_error_m':(wrist-target).length,'error_actor_cm':(wrist-target).length*135.547752,'span_from_joint_heads_m':(shoulder-forearm).length+(forearm-wrist).length,'shoulder_to_wrist_target_m':(shoulder-target).length})
 drift={n:max((p-pts[0]).length for p in pts)for n,pts in feet.items()}
 assert minz>=-.005,(label,'floor',minz)
 assert max(drift.values())<.003,(label,'footdrift',drift)
 assert max(r['error_actor_cm']for r in release)<10,(label,'reach',release)
 report['actions'][label]={'frames_checked':101,'mesh_min_z_m':minz,'limb_ratio_ranges':ratios,'foot_drift_m':drift,'release_samples':release,'finite':True}
 print('GAME_POSE_QA',label,minz,max(r['error_actor_cm']for r in release),flush=True)
for name,h in old_hashes.items():
 n='MC_Source_Wizard_'+name[len('MC_Wizard_'):]if name in ['MC_Wizard_'+l for l in labels]else name
 assert action_hash(bpy.data.actions[n])==h,(name,'changedbackup')
assert restsig==[(b.name,b.parent.name if b.parent else None,[list(r)for r in b.matrix_local],b.use_deform)for b in rig.data.bones]
assert digest(INPUT)==source_hash
report.update(input_preserved=True,all_previous_actions_preserved_as_backups=True,rig_rest_preserved=True)
bind(new_actions['Cast']);scene.frame_set(56)
scene['MC_GamePoseRevision']='zeroAim gameplay cast emitter alignment v2Game; original actions preserved.'
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT));report['blend_sha256']=digest(OUTPUT)
(OUT/'WizardGamePoseReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('WIZARD_GAME_POSE_COMPLETE',str(OUTPUT),flush=True)
