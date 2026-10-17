"""Live, inspectable one-cast authoring on an isolated improved Wizard rig.

No editor or game asset changes. execute main() in the connected workbench.
The original artist rig and V6 remain separate files.
"""
import copy
import hashlib
import json
import math
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard'
OUTPUT = OUT / 'NutWizard_Performance_v7.blend'


def curves(action):
    return [f for layer in action.layers for strip in layer.strips
            for bag in strip.channelbags for f in bag.fcurves]


def hermite(keys, t):
    """Passing tangents through breakdowns, flat only at deliberate holds.

    Values are tuples. Slopes use component-wise monotone limits to keep the
    release accent from introducing unintended hand reversal or body pops.
    """
    times, values = zip(*keys)
    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]
    i = next(j for j in range(len(times)-1) if t <= times[j+1])
    h = times[i+1]-times[i]
    u = (t-times[i])/h

    def slope(index, component):
        if index == 0 or index == len(times)-1:
            return 0.0
        a = (values[index][component]-values[index-1][component])/(times[index]-times[index-1])
        b = (values[index+1][component]-values[index][component])/(times[index+1]-times[index])
        if a*b <= 0:
            return 0.0
        return math.copysign(min(abs((a+b)*.5), 2*min(abs(a), abs(b))), a)

    h00, h10 = 2*u**3-3*u*u+1, u**3-2*u*u+u
    h01, h11 = -2*u**3+3*u*u, u**3-u*u
    return tuple(h00*a+h10*h*slope(i,j)+h01*b+h11*h*slope(i+1,j)
                 for j,(a,b) in enumerate(zip(values[i],values[i+1])))


def world_rotation(pb, degrees):
    m = Euler(tuple(math.radians(v) for v in degrees),'XYZ').to_matrix().to_4x4() @ pb.matrix
    m.translation = pb.matrix.translation
    pb.matrix = m


def main(save=True):
    import sys
    sys.path.insert(0,str(ROOT/'Tools/Blender'))
    from setup_wizard_performance_rig_v7 import apply_rig_v7
    from wizard_cast_v7_pose_design import STORYBOARD, CONTROL_SCHEDULES
    scene = bpy.context.scene
    rig, mesh = bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    assert Path(bpy.data.filepath).name in ('NutWizard_Workbench_v7.blend','NutWizard_Performance_v7.blend')
    rig.animation_data.action = None
    rig.animation_data.use_nla = False
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    for pb in rig.pose.bones:
        pb.matrix_basis.identity()
    bpy.context.view_layer.update()
    rig_report = (json.loads((OUT/'WizardPerformanceV7Report.json').read_text(encoding='utf8'))['rig']
                  if rig.get('MC_PerformanceRigV7') else apply_rig_v7(rig,mesh))
    original_props = {b.name:{k:b[k] for k in b.keys() if isinstance(b[k],(bool,int,float))}
                      for b in rig.pose.bones}
    rest = {b.name:b.matrix_local.copy() for b in rig.data.bones}
    old = bpy.data.actions.get('MC_Wizard_Cast')
    if old:
        old.name = 'MC_PrePerformanceV7_Wizard_Cast'
    action = bpy.data.actions.new('MC_Wizard_Cast')
    action.use_fake_user = True
    poses = copy.deepcopy(STORYBOARD)
    # The character is a single walnut mass above a short pelvis, not a human
    # spine/head. Keep the release's face up and the supporting fist below it.
    poses[0]['body'] = (0,0,-5)
    poses[1]['body'] = (4,-2,-12)
    poses[2]['body'] = (-13,-3,-28)
    poses[3]['body'] = (2,2,18)
    poses[4]['body'] = (6,3,23)
    poses[5]['body'] = poses[0]['body']
    poses[2]['root']=(-.07,.06,-.15)
    # Joint-first replacement after visual review rejected the world-palm
    # posing. Those targets forced wrist twists near 180 degrees and clamped
    # the arm near full extension. Every arm now has an authored upper-arm
    # direction, an explicit elbow plane, and a comfortable local wrist.
    # A shortest-arc upper-arm aim leaves roll undefined and gave mismatched
    # elbows: use BOTH segment directions to construct the full FK frame.
    directions=[
        {'r':(-.40,-.25,-.88),'l':(.40,-.25,-.88)},
        {'r':(-.52,-.30,-.80),'l':(.52,-.30,-.80)},
        {'r':(-.30,-.20,-.934),'l':(.30,-.20,-.934)},
        {'r':(-.55,-.82,-.15),'l':(.38,.10,-.92)},
        {'r':(-.59,-.789,-.17),'l':(.42,.12,-.899)},
        {'r':(-.40,-.25,-.88),'l':(.40,-.25,-.88)}]
    lower_directions=[
        {'r':(.40,-.82,-.40),'l':(-.40,-.82,-.40)},
        {'r':(.62,-.71,-.33),'l':(-.62,-.71,-.33)},
        {'r':(.42,-.874,-.247),'l':(-.42,-.874,-.247)},
        {'r':(-.48,-.865,.13),'l':(-.70,-.55,-.455)},
        {'r':(-.50,-.846,.18),'l':(-.65,-.48,-.59)},
        {'r':(.40,-.82,-.40),'l':(-.40,-.82,-.40)}]
    wrists=[{'r':(-5,18,2),'l':(-5,-18,-2)},
            {'r':(-8,25,3),'l':(-8,-25,-3)},
            {'r':(-8,48,3),'l':(-8,-48,-3)},
            {'r':(-5,25,2),'l':(-3,-8,-2)},
            {'r':(-3,18,2),'l':(-4,-10,-3)},
            {'r':(-5,18,2),'l':(-5,-18,-2)}]
    grips=[{'r':(.55,.50,.40),'l':(.55,.50,.40)},
           {'r':(.62,.56,.40),'l':(.62,.56,.40)},
           {'r':(.72,.65,.48),'l':(.68,.61,.46)},
           {'r':(.20,.16,.18),'l':(1.05,.92,.70)},
           {'r':(.24,.21,.20),'l':(.95,.86,.65)},
           {'r':(.55,.50,.40),'l':(.55,.50,.40)}]
    shoulder_angles=[(10,-10),(18,-18),(20,-20),(25,-8),(27,-10),(10,-10)]
    poses[2]['body']=(-10,-2,-12)
    for i,p in enumerate(poses):
        p['upper_directions']=directions[i];p['lower_directions']=lower_directions[i]
        p['wrist_local']=wrists[i];p['digit_grip']=grips[i]
        p['shoulders']={'r':(0,0,shoulder_angles[i][0]),'l':(0,0,shoulder_angles[i][1])}
    foot_ids={side:[v.index for v in mesh.data.vertices
                   if (mesh.matrix_world@v.co).z<.19 and ((mesh.matrix_world@v.co).x<0)==(side=='r')]
              for side in ('r','l')}
    schedules = dict(CONTROL_SCHEDULES)
    schedules['shell'] = [(0,0),(.19,1),(.45,2),(.505,2),(.57,3),(.635,4),(.86,1),(1,5)]

    def channel(name, getter, t):
        return hermite([(at,tuple(getter(poses[index]))) for at,index in schedules[name]],t)

    def reset_pose():
        rig.animation_data.action = None
        for pb in rig.pose.bones:
            pb.matrix_basis.identity()
            for k,v in original_props[pb.name].items():
                pb[k]=v
        for side in ('r','l'):
            rig.pose.bones['c_hand_ik.'+side]['ik_fk_switch']=1.0
            rig.pose.bones['c_hand_ik.'+side]['auto_stretch']=0.0
            rig.pose.bones['c_foot_ik.'+side]['ik_fk_switch']=0.0
            rig.pose.bones['c_foot_ik.'+side]['auto_stretch']=0.0
            rig.pose.bones['c_hand_fk.'+side]['fingers_grasp']=0.0
            for digit in ('pinky','ring','thumb'):
                rig.pose.bones['c_'+digit+'1_base.'+side]['bend_all']=0.0
        bpy.context.view_layer.update()

    records=[]

    def apply(t):
        reset_pose()
        root_delta = Vector(channel('root_translation',lambda p:p['root'],t))
        master=rig.pose.bones['c_root_master.x']
        m=rest[master.name].copy(); m.translation+=root_delta; master.matrix=m
        pelvis=rig.pose.bones['c_root.x']
        # Pelvis loads/brakes first; walnut mass follows on its own schedule.
        pelvis_rot=hermite([(0,(0,0,0)),(.43,(-4,0,-8)),(.485,(-4,0,-8)),
                           (.525,(2,0,7)),(.59,(2,0,7)),(.78,(-1,0,-2)),(1,(0,0,0))],t)
        bpy.context.view_layer.update(); world_rotation(pelvis,pelvis_rot)
        bpy.context.view_layer.update()
        shell=rig.pose.bones['c_head.x']
        world_rotation(shell,channel('shell',lambda p:p['body'],t))
        shell.scale=hermite([(0,(1,1,1)),(.44,(1.035,.93,1.035)),(.495,(1.04,.925,1.04)),
                             (.565,(.982,1.04,.982)),(.64,(1.01,.98,1.01)),(.82,(1,1,1)),(1,(1,1,1))],t)
        bpy.context.view_layer.update()
        for side in ('r','l'):
            sh=rig.pose.bones['c_shoulder_shell.'+side]
            sh.rotation_mode='XYZ'
            sh.rotation_euler=tuple(math.radians(v) for v in channel('shoulder_'+side,lambda p:p['shoulders'][side],t))
        bpy.context.view_layer.update()
        for side in ('r','l'):
            arm=rig.pose.bones['c_arm_shell_fk.'+side]
            fore=rig.pose.bones['c_forearm_fk.'+side]
            hand=rig.pose.bones['c_hand_fk.'+side]
            arm.rotation_mode='QUATERNION'
            local=Vector(channel('arm_'+side,lambda p:p['upper_directions'][side],t)).normalized()
            lower_local=Vector(channel('elbow_'+side,lambda p:p['lower_directions'][side],t)).normalized()
            shell_delta=shell.matrix.to_quaternion()@rest['c_head.x'].to_quaternion().inverted()
            wanted=shell_delta@local;lower_wanted=shell_delta@lower_local
            sign=1 if side=='r' else -1
            hinge=wanted.cross(lower_wanted).normalized()*sign
            m=Matrix((wanted.cross(hinge),wanted,hinge)).transposed().to_4x4()
            m.translation=arm.matrix.translation;arm.matrix=m
            fore.rotation_mode='XYZ'
            relative=rest[arm.name].to_3x3().inverted()@rest[fore.name].to_3x3()
            rest_y=relative@Vector((0,1,0))
            rest_bend=math.atan2(-rest_y.x,rest_y.y)
            bend=sign*math.acos(max(-1,min(1,wanted.dot(lower_wanted))))
            fore.rotation_euler=(0,0,bend-rest_bend)
            bpy.context.view_layer.update()
            hand.rotation_mode='QUATERNION'
            hand.rotation_quaternion=Euler(tuple(math.radians(v) for v in
                channel('wrist_'+side,lambda p:p['wrist_local'][side],t)),'XYZ').to_quaternion()
            bpy.context.view_layer.update()
            grip=channel('fingers_'+side,lambda p:p['digit_grip'][side],t)
            # The artist rig already has correctly rolled digit flexion
            # drivers. Preserve their hinge directions instead of bending all
            # digits about a guessed common world axis.
            for digit,amount in zip(('ring','pinky','thumb'),grip):
                rig.pose.bones['c_'+digit+'1_base.'+side]['bend_all']=amount
            bpy.context.view_layer.update()
            # Native bend_all drives only the middle/tip hinges: the first
            # joint's Copy Rotation has zero influence in this artist rig.
            # Curl its first joint in the actual palm plane, keeping each
            # digit's own roll (ring1 has a different roll from pinky1).
            palm=-(hand.matrix.to_quaternion()@Vector((0,0,1)))
            for digit,amount in zip(('ring','pinky','thumb'),grip):
                first=rig.pose.bones['c_'+digit+'1.'+side]
                first.rotation_mode='QUATERNION'
                if digit=='thumb':
                    # Thumb opposition is its own hinge: native bend_all
                    # leaves this digit pointing away from the palm.
                    axis=first.matrix.to_quaternion().inverted()@(-palm)
                    angle=math.radians(min(amount*90,52))*(-1 if side=='r' else 1)
                    first.rotation_quaternion=Quaternion(axis,angle)
                    bpy.context.view_layer.update()
                direction=first.matrix.to_quaternion()@Vector((0,1,0))
                axis=direction.cross(palm).normalized()
                local_axis=first.matrix.to_quaternion().inverted()@axis
                flex=min(amount*.60,.30 if digit=='thumb' else .48)
                first.rotation_quaternion=first.rotation_quaternion@Quaternion(local_axis,flex)
                bpy.context.view_layer.update()
            if int(round(t*100)) in (0,18,43,55,64,100):
                swing,twist=hand.rotation_quaternion.to_swing_twist('Y')
                records.append({'t':t,'side':side,'wrist':list(hand.matrix.translation),
                    'elbow_local_z_deg':math.degrees(fore.rotation_euler.z),
                    'wrist_swing_deg':math.degrees(swing.angle),'wrist_twist_deg':math.degrees(twist),
                    'pose_method':'Authored FK joint direction, elbow hinge, local wrist and native digit flexion'})
        for side,position,yaw in (('r',(-.27,.295,.156448588),-9),('l',(.27,-.005,.156448588),8)):
            foot=rig.pose.bones['c_foot_ik.'+side]
            foot.matrix=rest[foot.name].copy()
            heel=hermite([(0,(0,)),(.485,(0,)),(.56,(18,)),(.63,(22,)),(.80,(0,)),(1,(0,))],t)[0] if side=='r' else 0
            rotation=Euler((math.radians(heel),0,math.radians(yaw)),'XYZ').to_matrix().to_4x4()
            m=rotation@rest[foot.name];m.translation=Vector(position)
            foot.matrix=m
            pole=rig.pose.bones['c_leg_pole.'+side]
            m=rest[pole.name].copy();m.translation=Vector(((-.29 if side=='r' else .29),-.40,.32));pole.matrix=m
        bpy.context.view_layer.update()
        # Plant the real skinned toe/sole, including the rear heel-peel poses.
        evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
        geometry=evaluated.to_mesh()
        minimum={side:min((evaluated.matrix_world@geometry.vertices[i].co).z for i in foot_ids[side])
                 for side in ('r','l')}
        evaluated.to_mesh_clear()
        for side in ('r','l'):
            foot=rig.pose.bones['c_foot_ik.'+side]
            m=foot.matrix.copy();m.translation.z-=minimum[side];foot.matrix=m
        bpy.context.view_layer.update()

    animated_names=['c_root_master.x','c_root.x','c_head.x']
    for side in ('r','l'):
        animated_names += [n+'.'+side for n in ('c_shoulder_shell','c_arm_shell_fk','c_forearm_fk','c_hand_fk','c_foot_ik','c_leg_pole')]
        animated_names += [f'c_{d}{j}.{side}' for d in ('ring','pinky','thumb') for j in (1,2,3)]
        animated_names += [f'c_{d}1_base.{side}' for d in ('ring','pinky','thumb')]
    previous_quats={}
    for frame in range(1,102):
        apply((frame-1)/100)
        rig.animation_data.action=action
        for name in animated_names:
            pb=rig.pose.bones[name]
            rotation='rotation_quaternion' if pb.rotation_mode=='QUATERNION' else 'rotation_euler'
            if rotation=='rotation_quaternion':
                q=pb.rotation_quaternion
                if name in previous_quats and q.dot(previous_quats[name])<0:
                    q.negate()
                previous_quats[name]=q.copy()
            for property_name in ('location',rotation,'scale'):
                pb.keyframe_insert(property_name,frame=frame,group=name)
            if 'bend_all' in pb:
                pb.keyframe_insert('["bend_all"]',frame=frame,group=name)
        for side in ('r','l'):
            rig.pose.bones['c_hand_ik.'+side].keyframe_insert('["ik_fk_switch"]',frame=frame)
            rig.pose.bones['c_foot_ik.'+side].keyframe_insert('["ik_fk_switch"]',frame=frame)
    for f in curves(action):
        for k in f.keyframe_points:
            k.interpolation='LINEAR'
    action['game_slot']='Cast'
    action['source_release_fraction']=.55
    action['native_tell_seconds']=.9
    action['native_recovery_seconds']=.9
    action['authored_rig_revision']='PerformanceV7'
    action.asset_mark()
    action.asset_data.description='Pelvis-driven fire cast. Whole walnut mass and attached shoulders; independently delayed wrists and hooked digits.'
    scene.render.fps=30;scene.render.fps_base=1
    scene.frame_start=1;scene.frame_end=101
    for marker in list(scene.timeline_markers):
        scene.timeline_markers.remove(marker)
    for name,frame in [('Ready',1),('Scoop',19),('Loaded',44),('Release',56),('Follow',65),('Settle',101)]:
        scene.timeline_markers.new(name,frame=frame)
    rig.animation_data.action=action
    rig.animation_data.action_slot=action.slots[0]
    scene.frame_set(44)
    bpy.context.view_layer.update()
    if save:
        bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
    report={'source':str(OUTPUT),'source_sha256':hashlib.sha256(OUTPUT.read_bytes()).hexdigest() if save else None,
            'rig':rig_report,'action':action.name,'samples':records,'passed':True,'draft_cast_only':True,
            'scope':'One new cast and rig, visual authoring sample; no game integration implied.'}
    (OUT/'WizardPerformanceV7Report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return report


if __name__=='__main__':
    main()
