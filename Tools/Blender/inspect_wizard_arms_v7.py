"""Read-only saved V7 arm audit and isolated in-memory previews; never saves blend."""
import bpy,json,math
from pathlib import Path
from collections import defaultdict
from mathutils import Matrix,Vector
ROOT=Path(__file__).resolve().parents[2]
rig=bpy.data.objects['rig'];mesh=bpy.data.objects['SM_NutWizard'];scene=bpy.context.scene
names={g.index:g.name for g in mesh.vertex_groups}
coords=[mesh.matrix_world@v.co for v in mesh.data.vertices]
weights=[{names[g.group]:g.weight for g in v.groups if g.weight>0} for v in mesh.data.vertices]
def category(n):
    if n.startswith('shoulder.'):return 'shoulder'
    if n.startswith(('arm_stretch.','c_arm_twist_offset.')):return 'upper_arm'
    if n.startswith(('forearm_stretch.','forearm_twist.')):return 'forearm'
    if n.startswith('hand.'):return 'hand'
    return 'digits_other'
arm_ids={side:[i for i,w in enumerate(weights) if any(n.endswith('.'+side) and
             category(n) in ('upper_arm','forearm','hand') for n in w)] for side in ('r','l')}
def snapshot():
    rig.update_tag();bpy.context.view_layer.update()
    result={}
    for side in ('r','l'):
        records={}
        for prefix in ('c_shoulder_shell','c_shoulder','shoulder','c_arm_shell_fk','c_arm_fk','arm',
                       'arm_stretch','c_arm_twist_offset','c_forearm_fk','forearm','forearm_stretch',
                       'forearm_twist','c_hand_fk','hand'):
            n=prefix+'.'+side;pb=rig.pose.bones[n];b=pb.bone
            records[n]=dict(rest_head=list(b.head_local),rest_tail=list(b.tail_local),rest_length=b.length,
                parent=b.parent.name if b.parent else None,head=list(pb.head),tail=list(pb.tail),
                length=(pb.tail-pb.head).length,euler=list(pb.rotation_euler),scale=list(pb.matrix.to_scale()),
                basis=[list(row) for row in pb.matrix_basis],world=[list(row) for row in pb.matrix])
        a=rig.pose.bones['c_arm_shell_fk.'+side];f=rig.pose.bones['c_forearm_fk.'+side]
        ua=(a.tail-a.head).normalized();lo=(f.tail-f.head).normalized()
        h=rig.pose.bones['hand.'+side];hd=(h.tail-h.head).normalized()
        swing,twist=rig.pose.bones['c_hand_fk.'+side].matrix_basis.to_quaternion().to_swing_twist('Y')
        result[side]=dict(bones=records,elbow_flex_deg=math.degrees(math.acos(max(-1,min(1,ua.dot(lo))))),
            wrist_swing_deg=math.degrees(math.acos(max(-1,min(1,lo.dot(hd))))),
            wrist_twist_deg=math.degrees(twist),
            wrist=list(rig.pose.bones['hand.'+side].head),hand_control_wrist=list(rig.pose.bones['c_hand_fk.'+side].head))
    return result
action=bpy.data.actions['MC_Wizard_Cast'];rig.animation_data.action=action
if action.slots:rig.animation_data.action_slot=action.slots[0]
rig.animation_data.use_nla=False
frames={}
for frame in (1,20,47,55,60,70,91,109):
    scene.frame_set(frame);frames[frame]=snapshot()
bands={}
for side,sign in (('r',-1),('l',1)):
    rows=[]
    for band in range(22):
        lo=.40+band*.045;hi=lo+.045
        ids=[i for i in arm_ids[side] if lo<=sign*coords[i].x<hi]
        if not ids:continue
        sums=defaultdict(float)
        for i in ids:
            for n,w in weights[i].items():sums[category(n)]+=w
        rows.append(dict(x_distance_min=lo,x_distance_max=hi,vertices=len(ids),
                         category_average={n:w/len(ids) for n,w in sums.items()},
                         z_min=min(coords[i].z for i in ids),z_max=max(coords[i].z for i in ids)))
    bands[side]=rows

# Reset only this ephemeral standalone scene for simple deform quality evidence.
rig.animation_data.action=None
for pb in rig.pose.bones:pb.matrix_basis=Matrix.Identity(4)
for side in ('r','l'):
    rig.pose.bones['c_hand_ik.'+side]['ik_fk_switch']=1
    rig.pose.bones['c_hand_ik.'+side]['auto_stretch']=0
    rig.pose.bones['c_hand_fk.'+side]['stretch_length']=1
    rig.pose.bones['c_hand_fk.'+side]['fingers_grasp']=0
rig.update_tag();bpy.context.view_layer.update()
neutral=snapshot()
scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=1200;scene.render.resolution_y=800
scene.render.resolution_percentage=100;scene.display.shading.light='STUDIO'
scene.display.shading.studio_light='paint.sl';scene.display.shading.color_type='MATERIAL'
camera=scene.camera;camera.data.type='ORTHO';camera.data.ortho_scale=3.6
camera.location=(0,-4,1.8);camera.rotation_euler=(Vector((0,.12,.85))-camera.location).to_track_quat('-Z','Y').to_euler()
tests={}
for label,angle,wrist_twist in (('Neutral',0,0),('Elbow60',60,0),('Elbow100',100,0),('Elbow60_Wrist90',60,90)):
    for side,sign in (('r',1),('l',-1)):
        fore=rig.pose.bones['c_forearm_fk.'+side];fore.rotation_mode='XYZ'
        fore.rotation_euler=(0,0,math.radians(sign*angle))
        hand=rig.pose.bones['c_hand_fk.'+side];hand.rotation_mode='XYZ'
        hand.rotation_euler=(0,math.radians(wrist_twist),0)
    tests[label]=snapshot()
    scene.render.filepath=str(ROOT/'Saved/Codex'/('WizardArm_'+label+'.png'))
    bpy.ops.render.render(write_still=True)
result=dict(file=bpy.data.filepath,frames=frames,rest_weight_bands=bands,neutral=neutral,isolated_tests=tests)
(ROOT/'Saved/Codex/WizardArmV7Inspection.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print('WIZARD_ARM_V7_AUDIT_COMPLETE',flush=True)
for frame,record in frames.items():
    print(json.dumps(dict(frame=frame,sides={s:dict(elbow_flex_deg=r['elbow_flex_deg'],wrist=r['wrist'],
        wrist_swing_deg=r['wrist_swing_deg'],wrist_twist_deg=r['wrist_twist_deg'],
        fore_z_deg=math.degrees(r['bones']['c_forearm_fk.'+s]['euler'][2])) for s,r in record.items()})),flush=True)
rig.animation_data.action=action
if action.slots:rig.animation_data.action_slot=action.slots[0]
for side in ('r','l'):rig.pose.bones['c_hand_fk.'+side].rotation_mode='QUATERNION'
for frame in (1,55):
    scene.frame_set(frame);bpy.context.view_layer.update()
    for view,pos in (('Front',(0,-4,1.8)),('Side',(4,-.25,1.25))):
        camera.location=pos;camera.rotation_euler=(Vector((0,-.15,.75))-camera.location).to_track_quat('-Z','Y').to_euler()
        scene.render.filepath=str(ROOT/'Saved/Codex'/f'WizardArm_Actual_{frame}_{view}.png')
        bpy.ops.render.render(write_still=True)
