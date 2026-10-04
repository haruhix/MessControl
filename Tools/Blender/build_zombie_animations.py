"""Author in-place zombie bone clips on the independent ZombieBoss skeleton.

Run isolated Blender with ArtSource/ZombieBoss/ZombieBoss.blend open. The
deform export intentionally has no artist controls. Analytic shoulder/elbow and
hip/knee pivots drive its weighted bones without changing the reference pose.
The saved review blend has all eight actions and a consecutive NLA timeline.
"""
import bpy
import hashlib
import json
import math
import sys
from pathlib import Path
from mathutils import Matrix, Vector, Euler

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/ZombieBossAnimations'
OUT.mkdir(parents=True, exist_ok=True)
source = Path(bpy.data.filepath)
assert source.resolve() == (ROOT / 'ArtSource/ZombieBoss/ZombieBoss.blend').resolve()
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
rig = bpy.data.objects['Armature']
assert len(rig.data.bones) == 85
scene = bpy.context.scene
scene.render.fps = 30
rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
parents = {b.name: b.parent.name if b.parent else None for b in rig.data.bones}
meshes = [o for o in scene.objects if o.type == 'MESH']
assert len(meshes) == 2
rig.animation_data_clear()
for bone in rig.pose.bones:
    bone.rotation_mode = 'QUATERNION'

def clamp(x):
    return min(1.0, max(0.0, x))

def smooth(x):
    x = clamp(x)
    return x*x*(3-2*x)

def pulse(t, begin, peak, end):
    return smooth((t-begin)/(peak-begin)) if t < peak else 1-smooth((t-peak)/(end-peak))

def rotation(x=0, y=0, z=0):
    return Euler(tuple(math.radians(v) for v in (x,y,z)), 'XYZ').to_matrix().to_4x4()

def pivot(point, transform):
    return Matrix.Translation(Vector(point)) @ transform @ Matrix.Translation(-Vector(point))

def pose(kind, t, duration):
    wave = math.sin(t*2*math.pi/2.4)
    body = Matrix.Translation((0,0,.9*wave)) @ pivot((0,8,41), rotation(4+1.5*wave,0,1.8*wave))
    head = pivot((0,6,47), rotation(3,0,3+2*math.sin(t*2*math.pi/2.4)))
    # Reference arms point sideways. Keep the upper arms near shoulder height,
    # sweep them forward and bend the elbows; the old 52/60-degree droop made
    # the hands hang at the feet and hid the combat silhouette.
    upper = {'l': [-8,-35], 'r': [-3,-31]}
    lower = {'l': [-12,-38], 'r': [-17,-42]}
    hips = {'l': 0., 'r': 0.}
    knees = {'l': 3., 'r': 3.}
    curl = .25
    if kind == 'Shamble':
        phase = t*2*math.pi/duration
        step = math.sin(phase)
        bob = .9*math.cos(phase*2)
        body = Matrix.Translation((1.4*step,0,bob)) @ pivot((0,8,41), rotation(8,0,3*step))
        head = pivot((0,6,47), rotation(-2,0,-3*step))
        for side, sign in (('l',1),('r',-1)):
            v = step*sign
            hips[side] = -22*v
            knees[side] = 5+28*max(0,-v)
            upper[side][0] += 8*v
            upper[side][1] += 7*v
    elif kind in ('PunchLeft','PunchRight'):
        side = 'l' if kind == 'PunchLeft' else 'r'
        other = 'r' if side == 'l' else 'l'
        sign = 1 if side == 'l' else -1
        impact = .70 if side == 'l' else .76
        windup = smooth(t/(impact-.17)) if t < impact-.17 else 1.
        extension = pulse(t,impact-.17,impact,impact+.30)
        recover = 1-smooth((t-impact-.05)/(duration-impact-.05))
        windup *= recover
        upper[side] = [-8+16*windup-23*extension, -35+40*windup-85*extension]
        lower[side] = [-12-26*windup+38*extension,-38+30*windup+8*extension]
        upper[other] = [-3,-35]
        lower[other] = [-24,-48]
        body = Matrix.Translation((sign*1.7*extension,-3.5*extension,-.6*windup)) @ pivot((0,8,41),rotation(5+7*extension,0,sign*(-9*windup+15*extension)))
        head = pivot((0,6,47),rotation(3+3*extension,0,-sign*5*windup))
        curl = .8
    elif kind == 'Kick':
        chamber = pulse(t,.10,.62,1.25)
        kick = pulse(t,.66,.90,1.23)
        hips['r'] = -30*chamber-60*kick
        knees['r'] = 75*chamber*(1-kick)+4
        hips['l'] = 5*chamber
        knees['l'] = 7+8*chamber
        body = Matrix.Translation((5*chamber,0,-1.3*chamber)) @ pivot((0,8,41),rotation(4-13*kick,0,4*chamber))
        upper['l']=[-8,-35-14*chamber]
        upper['r']=[-3,-31+30*chamber]
        head=pivot((0,6,47),rotation(3+8*kick,0,-4*chamber))
        curl=.65
    elif kind == 'Hurt':
        flinch=pulse(t,0,.14,duration)
        body=Matrix.Translation((0,3*flinch,-1*flinch)) @ pivot((0,8,41),rotation(4-15*flinch,0,-6*flinch))
        head=pivot((0,6,47),rotation(-10*flinch,0,8*flinch))
        upper['l'][0] -= 12*flinch
        upper['r'][0] -= 15*flinch
        knees={s:3+10*flinch for s in knees}
    elif kind == 'Roar':
        # Five seconds: crouch/inhalation, broad chest opening, shaking roar,
        # then settle back to the raised in-place combat guard.
        inhale=pulse(t,0,.75,1.45)
        roar=pulse(t,.75,1.35,4.35)
        shake=math.sin(t*31)*roar*.85
        body=Matrix.Translation((0,-1.8*roar,-3*inhale+1.4*roar)) @ pivot((0,8,41),rotation(4+10*inhale-9*roar,0,shake))
        head=pivot((0,6,47),rotation(3-14*roar+2*shake,0,3+shake*.7))
        upper['l']=[-8-12*roar,-35+16*roar]
        upper['r']=[-3-17*roar,-31+13*roar]
        lower['l']=[-12+8*roar,-38+18*roar]
        lower['r']=[-17+9*roar,-42+20*roar]
        knees={s:3+11*inhale for s in knees}
        curl=.25+.65*roar
    elif kind == 'Death':
        collapse=smooth((t-.2)/1.05)
        settle=1.2*math.exp(-max(0,t-1.4)*5)*math.sin(max(0,t-1.4)*22) if t>1.4 else 0
        body=Matrix.Translation((0,0,7*collapse+settle)) @ pivot((0,0,0),rotation(-88*collapse,0,-9*collapse))
        head=pivot((0,6,47),rotation(8*collapse,0,9*collapse))
        upper['l']=[-8+63*collapse,-35+30*collapse]
        upper['r']=[-3+38*collapse,-31+45*collapse]
        hips={'l':-9*collapse,'r':12*collapse}
        knees={'l':3+18*collapse,'r':3+28*collapse}
        curl=.25*(1-collapse)

    arms={}
    arm_upper={}
    for side, sign in (('l',1),('r',-1)):
        shoulder=(sign*16,8,56)
        elbow=(sign*41,12,54)
        arm_upper[side]=body @ pivot(shoulder,rotation(0,sign*upper[side][0],sign*upper[side][1]))
        arms[side]=arm_upper[side] @ pivot(elbow,rotation(0,sign*lower[side][0],sign*lower[side][1]))
    upper_legs={}
    lower_legs={}
    feet={}
    for side, sign in (('l',1),('r',-1)):
        hip=(sign*18,4,37)
        knee=(sign*21.6,-2.4,20.7)
        ankle=(sign*23.85,5.05,8.85)
        upper_legs[side]=body @ pivot(hip,rotation(hips[side]))
        lower_legs[side]=upper_legs[side] @ pivot(knee,rotation(knees[side]))
        foot_compensation=-(hips[side]+knees[side])*.8 if kind!='Death' else -5.
        feet[side]=lower_legs[side] @ pivot(ankle,rotation(foot_compensation))

    result={}
    for name, reference in rest.items():
        side=name[-1] if name.endswith(('_l','_r')) else None
        transform=body
        if name=='root': transform=Matrix.Identity(4)
        elif name=='head_x' or name.startswith('c_'): transform=body @ head
        elif name.startswith('shoulder_'): transform=arm_upper[side]
        elif name.startswith(('forearm_','hand_','index','thumb')): transform=arms[side]
        elif name.startswith('thigh_'): transform=upper_legs[side]
        elif name.startswith('leg_'): transform=lower_legs[side]
        elif name.startswith(('foot_','toes_')): transform=feet[side]
        result[name]=transform @ reference
    # Fingers are true chains; propagate their curl through each child joint.
    pending=[n for n in rest if n.startswith(('index','thumb'))]
    while pending:
        name=pending.pop(0)
        parent=parents[name]
        if parent in pending:
            pending.append(name)
            continue
        joint=25 if name.startswith('thumb') else 37
        result[name]=result[parent] @ rest[parent].inverted() @ rest[name] @ rotation(joint*curl)
    # Move all weighted bones together to prevent a death pose intersecting the ground.
    return result

clips=[('Idle',2.4,None,True),('Shamble',1.6,None,True),
       ('PunchLeft',1.4,.70,False),('PunchRight',1.5,.76,False),
       ('Kick',1.8,.90,False),('Hurt',2/3,None,False),('Death',2.4,None,False),
       ('Roar',5.,None,False)]
records=[]
actions=[]
for name,duration,impact,loop in clips:
    action=bpy.data.actions.new('AN_Zombie_'+name)
    action.use_fake_user=True
    rig.animation_data_create()
    rig.animation_data.action=action
    end=round(duration*30)+1
    for frame in range(1,end+1):
        t=(frame-1)/30
        desired=pose(name,t,duration)
        def apply_pose():
            for bone in rig.pose.bones:
                parent=bone.parent
                bone.matrix_basis=bone.bone.convert_local_to_pose(
                    desired[bone.name],rest[bone.name],
                    parent_matrix=desired[parent.name] if parent else Matrix.Identity(4),
                    parent_matrix_local=rest[parent.name] if parent else Matrix.Identity(4),invert=True)
        apply_pose()
        bpy.context.view_layer.update()
        deps=bpy.context.evaluated_depsgraph_get()
        lowest=min((mesh.evaluated_get(deps).matrix_world @ Vector(corner)).z
                   for mesh in meshes for corner in mesh.evaluated_get(deps).bound_box)
        if lowest < .005:
            lift=Matrix.Translation((0,0,(.005-lowest)*100))
            desired={n:(lift @ value if n!='root' else value) for n,value in desired.items()}
            apply_pose()
        for bone in rig.pose.bones:
            bone.keyframe_insert(data_path='location',frame=frame,group=bone.name)
            bone.keyframe_insert(data_path='rotation_quaternion',frame=frame,group=bone.name)
            bone.keyframe_insert(data_path='scale',frame=frame,group=bone.name)
    scene.frame_start,scene.frame_end=1,end
    scene.frame_set(1)
    bpy.context.view_layer.update()
    # FBX active action only: preserve the source skeleton and disable root motion.
    bpy.ops.object.select_all(action='DESELECT')
    for obj in (rig,*meshes): obj.select_set(True)
    bpy.context.view_layer.objects.active=rig
    filename=action.name+'.fbx'
    bpy.ops.export_scene.fbx(filepath=str(OUT/filename),use_selection=True,
        object_types={'ARMATURE','MESH'},add_leaf_bones=False,use_armature_deform_only=False,
        bake_anim=True,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,
        bake_anim_simplify_factor=0,bake_anim_step=1,mesh_smooth_type='FACE')
    records.append({'name':action.name,'file':filename,'frames':end,'fps':30,
                    'duration_seconds':duration,'impact_seconds':impact,'loop':loop,'root_motion':False})
    actions.append(action)

rig.animation_data.action=None
track=rig.animation_data.nla_tracks.new()
track.name='F3 Animation Review — consecutive clips'
timeline=1
for action,record in zip(actions,records):
    strip=track.strips.new(action.name,timeline,action)
    strip.action_frame_start=1
    strip.action_frame_end=record['frames']
    strip.blend_type='REPLACE'
    strip.extrapolation='HOLD_FORWARD'
    scene.timeline_markers.new(record['name'],frame=timeline)
    if record['impact_seconds'] is not None:
        scene.timeline_markers.new(record['name']+' impact',frame=timeline+round(record['impact_seconds']*30))
    record['review_start_frame']=timeline
    timeline+=record['frames']+8
scene.frame_start,scene.frame_end=1,timeline-8
scene.frame_set(1)

# Review lighting/camera live only in the derived blend, never in exported FBX.
scene.render.engine='BLENDER_EEVEE'
scene.render.resolution_x,scene.render.resolution_y=960,720
scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('ZombieAnimationReviewWorld')
scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.028,.039,.055,1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.35
camera_data=bpy.data.cameras.new('Zombie Review Camera')
camera=bpy.data.objects.new('Zombie Review Camera',camera_data)
scene.collection.objects.link(camera)
camera.location=(1.85,-3.4,1.5)
target=Vector((0,0,.55))
camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera_data.lens=65
scene.camera=camera
for name,position,power,size,color in (
    ('Soft Key',(1.6,-2.2,2.8),550,3.2,(.81,.89,1.)),
    ('Warm Fill',(-2,-.8,1.4),180,2.0,(1.,.62,.44)),
    ('Rim',(0,1.8,2.4),700,2.2,(.46,1.,.75))):
    data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='DISK';data.size=size;data.color=color
    obj=bpy.data.objects.new(name,data);scene.collection.objects.link(obj);obj.location=position
    obj.rotation_euler=(target-obj.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.006))
floor=bpy.context.object;floor.name='Review Floor'
material=bpy.data.materials.new('Review charcoal floor');material.diffuse_color=(.026,.035,.05,1)
material.use_nodes=True
material.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.026,.035,.05,1)
material.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.8
floor.data.materials.append(material)
for area in bpy.context.screen.areas if bpy.context.screen else []:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_distance=3.6
        area.spaces.active.region_3d.view_location=(0,0,.55)
        area.spaces.active.shading.type='MATERIAL'
    elif area.type=='DOPESHEET_EDITOR': area.spaces.active.mode='DOPESHEET'
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'ZombieBossAnimations.blend'))
report={'source':str(source),'source_sha256':source_hash,'bones':len(rest),
        'clips':records,'review_blend':'ZombieBossAnimations.blend',
        'review_timeline_frames':scene.frame_end,'root_motion':False,
        'reference_pose_unchanged':True}
report['reference_pose_unchanged']=all(max(abs(rig.data.bones[n].matrix_local[i][j]-rest[n][i][j]) for i in range(4) for j in range(4))<1e-6 for n in rest)
assert report['reference_pose_unchanged']
assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
(OUT/'ZombieAnimationReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if '--preview' in sys.argv:
    for record in records:
        sample=record['impact_seconds'] if record['impact_seconds'] is not None else .6
        if record['name'].endswith('Death'): sample=1.7
        if record['name'].endswith('Roar'): sample=1.55
        scene.frame_set(record['review_start_frame']+round(sample*30))
        scene.render.filepath=str(OUT/(record['name']+'.png'))
        bpy.ops.render.render(write_still=True)
if '--video' in sys.argv:
    import subprocess
    frames=ROOT/'Saved/ChestBossReview/ZombieBlenderFrames'
    frames.mkdir(parents=True,exist_ok=True)
    scene.render.resolution_x,scene.render.resolution_y=800,600
    for i,frame in enumerate(range(1,scene.frame_end+1,2)):
        scene.frame_set(frame)
        scene.render.filepath=str(frames/('frame_%04d.png'%i))
        bpy.ops.render.render(write_still=True)
    filters=[]
    labels={'Idle':'Idle / breathing','Shamble':'Shamble walk','PunchLeft':'Left hand punch',
            'PunchRight':'Right hand punch','Kick':'Kick','Hurt':'Hurt reaction','Death':'Death / fall','Roar':'Roar intro / 5 seconds'}
    for index,record in enumerate(records):
        begin=(record['review_start_frame']-1)/30
        end=(records[index+1]['review_start_frame']-1)/30 if index+1<len(records) else scene.frame_end/30
        key=record['name'].removeprefix('AN_Zombie_')
        filters.append("drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='%s':fontcolor=white:fontsize=26:box=1:boxcolor=black@0.65:boxborderw=12:x=20:y=20:enable='between(t,%.4f,%.4f)'"%(labels[key],begin,end))
    subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y',
        '-framerate','15','-i',str(frames/'frame_%04d.png'),'-vf',','.join(filters),
        '-c:v','libx264','-threads','2','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',
        str(OUT/'ZombieBossAnimationReview.mp4')],check=True)
print('MC_ZOMBIE_ANIMATIONS_PASS',json.dumps(records))
