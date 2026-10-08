"""Original keyed cartoon performances on the artist ARP rig, in a separate file.

Run: blender --factory-startup --background --python this.py -- [--preview]
Saved Downloads source, game assets and existing FBX exports are never written.
"""
import argparse, hashlib, json, math, subprocess, sys
from pathlib import Path
import bpy
from mathutils import Euler, Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('C:/Users/user/Downloads/Telegram Desktop/Nut_Wizard.blend')
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard'
OUT.mkdir(parents=True, exist_ok=True)
parser = argparse.ArgumentParser()
parser.add_argument('--preview', action='store_true')
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
source_hash = digest(SOURCE)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene = bpy.context.scene
rig = bpy.data.objects['rig']
rig.animation_data_create()
for track in rig.animation_data.nla_tracks:
    track.mute = True
rig.animation_data.use_nla = False
source_actions = {a.name: list(a.frame_range) for a in bpy.data.actions}
rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
rest_signature = {b.name: {'parent': b.parent.name if b.parent else None,
                         'matrix': [list(row) for row in b.matrix_local]} for b in rig.data.bones}
original_props = {b.name: {k:b[k] for k in b.keys() if isinstance(b[k], (float,int,bool))} for b in rig.pose.bones}
controls = [b for b in rig.pose.bones if b.name.startswith('c_')]

def bind(action):
    rig.animation_data.action = action
    rig.animation_data.action_slot = action.slots[0]

def reset():
    rig.animation_data.action = None
    for b in rig.pose.bones:
        b.matrix_basis = Matrix.Identity(4)
        for k,v in original_props[b.name].items():
            b[k] = v
    for side in ('l','r'):
        rig.pose.bones['c_hand_ik.'+side]['ik_fk_switch'] = 0.0
        rig.pose.bones['c_hand_ik.'+side]['auto_stretch'] = 0.0
        rig.pose.bones['c_foot_ik.'+side]['ik_fk_switch'] = 0.0
        rig.pose.bones['c_arms_pole.'+side]['pole_parent'] = 0
    rig.update_tag()
    bpy.context.view_layer.update()

def set_matrix(name, position=None, degrees=(0,0,0), current=False):
    bone = rig.pose.bones[name]
    base = bone.matrix.copy() if current else rest[name].copy()
    orient = Euler(tuple(math.radians(v) for v in degrees), 'XYZ').to_matrix().to_4x4()
    matrix = orient @ base
    matrix.translation = Vector(position) if position is not None else base.translation
    bone.matrix = matrix

# New poses use IK hands, fixed ground contacts, no limb stretching. Finger shape
# remains authored on the source rig; all control/property keys are isolated.
BASE = dict(root=(0,0,-.045), body=(2,0,-4), head=(-3,0,3),
            r=(-.53,-.06,.58), l=(.53,-.14,.69),
            rr=(0,0,45), lr=(0,0,-45),
            fr=(-.235,.19,.156448588), fl=(.235,.13,.156448588),
            frr=(0,0,-8), flr=(0,0,8), grasp=.15)

def pose(**changes):
    return {**BASE, **changes}

def apply(p):
    reset()
    set_matrix('c_root_master.x', rest['c_root_master.x'].translation+Vector(p['root']), p['body'])
    bpy.context.view_layer.update()
    set_matrix('c_head.x', degrees=p['head'], current=True)
    for side,hand in (('r',p['r']),('l',p['l'])):
        sign=1 if side=='l' else -1
        lift=max(0,min(1,(hand[2]-1.0)/.4))
        if lift:
            set_matrix('c_shoulder.'+side,degrees=(0,-sign*60*lift,0),current=True)
    bpy.context.view_layer.update()
    for side, hand, angles, foot, foot_angles in (
            ('r',p['r'],p['rr'],p['fr'],p['frr']),
            ('l',p['l'],p['lr'],p['fl'],p['flr'])):
        set_matrix('c_hand_ik.'+side, hand, angles)
        set_matrix('c_foot_ik.'+side, foot, foot_angles)
        # Elbows point out/back and knees toward the toes. Pole positions are
        # kept world stable instead of inheriting the previous artist action.
        sign = 1 if side=='l' else -1
        set_matrix('c_arms_pole.'+side, (sign*.88,.42,.64))
        set_matrix('c_leg_pole.'+side, (sign*.26,-.36,.32))
        rig.pose.bones['c_hand_fk.'+side]['fingers_grasp'] = p['grasp']
    rig.update_tag()
    bpy.context.view_layer.update()

def key(frame):
    for bone in controls:
        rotation = 'rotation_quaternion' if bone.rotation_mode=='QUATERNION' else 'rotation_euler'
        for prop in ('location',rotation,'scale'):
            bone.keyframe_insert(prop,frame=frame,group=bone.name)
        for prop in original_props[bone.name]:
            bone.keyframe_insert('['+json.dumps(prop)+']',frame=frame,group=bone.name)

def mix(a,b,t,linear_keys=()):
    s=t*t*(3-2*t)
    result={}
    for k in a:
        f=t if k in linear_keys else s
        if isinstance(a[k],tuple):
            result[k]=tuple(x+(y-x)*f for x,y in zip(a[k],b[k]))
        else:
            result[k]=a[k]+(b[k]-a[k])*f
    return result

records=[]
def make(label, knots, loop=False, note='', linear_keys=()):
    reset()
    action=bpy.data.actions.new('MC_Wizard_'+label)
    action.use_fake_user=True
    # Per-frame interpolation from a small explicit pose design makes the
    # accent, hold and recovery controllable, without procedural noise.
    cursor=0
    for frame in range(knots[0][0],knots[-1][0]+1):
        while cursor+1<len(knots)-1 and frame>knots[cursor+1][0]:
            cursor+=1
        fa,a=knots[cursor];fb,b=knots[cursor+1]
        apply(mix(a,b,(frame-fa)/(fb-fa),linear_keys))
        rig.animation_data.action=action
        key(frame)
    bind(action)
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for point in curve.keyframe_points:
                        point.interpolation='LINEAR'
    action.asset_mark()
    action.asset_data.description=note
    action['game_slot']=label
    action['loop']=loop
    action['key_pose_frames']=json.dumps([f for f,_ in knots])
    records.append(dict(label=label,action=action.name,start=knots[0][0],end=knots[-1][0],
                        loop=loop,key_poses=[f for f,_ in knots],description=note))
    print('WIZARD_ACTION_READY',label,flush=True)

idle=pose()
make('Idle',[(1,idle),(20,pose(root=(.014,0,-.033),head=(-6,0,6),l=(.60,-.10,.68))),
             (43,pose(root=(.009,0,-.029),body=(1,0,-3),head=(-4,0,4),r=(-.59,-.12,.755))),
             (66,pose(root=(-.011,0,-.049),head=(-1,0,-2),r=(-.58,-.12,.725))),
             (91,idle)],True,'Smug, poised wizard. Subtle breath and delayed glance; planted feet.')

# A backwards retreat while facing the players; the current runtime keeps the
# mage facing the target. The active foot stays on its ground plane during stance.
walk=[]
for frame,half,offset,height in [(1,0,.09,0),(5,0,.06,0),(13,0,-.04,0),
                                (20,0,-.095,0),(25,1,.09,0),(29,1,.06,0),
                                (37,1,-.04,0),(44,1,-.095,0),(49,0,.09,0)]:
    phase=(frame-1)/48
    # Alternate target trajectories explicitly; swing passes through an elevated
    # middle pose at frames 13 and 37, then settles into the next contact.
    right=[(-.235,.28,.156448588),(-.235,.24,.156448588),(-.235,.14,.156448588),
           (-.235,.085,.156448588),(-.235,.08,.156448588),(-.235,.09,.19),
           (-.235,.18,.225),(-.235,.275,.166),(-.235,.28,.156448588)][len(walk)]
    left=[(.235,.08,.156448588),(.235,.09,.19),(.235,.18,.225),(.235,.275,.166),
          (.235,.28,.156448588),(.235,.24,.156448588),(.235,.14,.156448588),
          (.235,.085,.156448588),(.235,.08,.156448588)][len(walk)]
    sign=1 if phase<.5 else -1
    walk.append((frame,pose(root=(.026*sign,0,-.045 if frame in (1,25,49) else -.018),
                  body=(2,0,-4+2*sign),head=(-3,0,3-2*sign),fr=right,fl=left,
                  r=(-.58,-.12,.73+.016*sign),l=(.59,-.10,.66-.016*sign))))
walk[-1]=(49,walk[0][1])
make('Walk',walk,True,'Short deliberate backsteps, guarded hands, alternating support. Faces the target.',linear_keys=('fr','fl'))

# Right-hand emitter stays reachable. Character is carried by the torso, left
# hand, head and elbow so runtime cast-palm IK does not erase the preparation.
cast_charge=pose(root=(.035,.035,-.10),body=(-9,0,-14),head=(9,0,12),
                 r=(-.58,-.18,.78),l=(.56,-.15,.91),rr=(5,0,70),lr=(0,0,-70),grasp=.5)
cast_compress=pose(root=(.035,.048,-.115),body=(-12,0,-16),head=(11,0,15),
                   r=(-.57,-.19,.80),l=(.62,-.19,.96),rr=(0,0,75),lr=(15,0,-80),grasp=.55)
cast_release=pose(root=(-.018,-.030,-.025),body=(13,0,7),head=(-8,0,-5),
                  r=(-.56,-.30,.85),l=(.66,.005,.73),rr=(-10,0,90),lr=(0,0,-15),grasp=0)
cast_follow=pose(root=(-.025,-.025,-.046),body=(17,0,12),head=(-13,0,-10),
                 r=(-.57,-.28,.86),l=(.68,.05,.70),rr=(-8,0,88),lr=(0,0,-12),grasp=.05)
make('Cast',[(1,idle),(15,cast_charge),(40,cast_compress),(50,cast_compress),
             (56,cast_release),(62,cast_follow),(78,pose(root=(.01,.008,-.055),body=(-3,0,-5),head=(3,0,3))),
             (91,pose(head=(-4,0,4))),(101,idle)],note='Fire nut: gather, held compression, sharp release at 55%, elastic follow-through.')

heavy_charge=pose(root=(0,.03,-.125),body=(-12,0,-4),head=(10,0,5),
                  r=(-.56,-.18,.78),l=(.56,-.19,.80),rr=(0,0,75),lr=(0,0,-75),grasp=.55)
heavy_release=pose(root=(0,-.015,-.015),body=(12,0,2),head=(-8,0,-2),
                   r=(-.59,-.28,.86),l=(.62,-.24,.91),rr=(0,0,90),lr=(0,0,-90),grasp=0)
make('HeavyCast',[(1,idle),(18,heavy_charge),(42,heavy_charge),(50,pose(**{**heavy_charge,'root':(0,.04,-.135)})),
                  (56,heavy_release),(63,pose(**{**heavy_release,'body':(17,0,4)})),
                  (79,pose(root=(0,0,-.065),body=(-4,0,-4))),(101,idle)],
     note='Two-hand heavy charge fallback. More compression and recoil; release at 55%.')

summon_low=pose(root=(0,.02,-.10),body=(5,0,0),head=(9,0,0),
                r=(-.64,-.10,.62),l=(.65,-.10,.62),rr=(0,-15,45),lr=(0,15,-45),grasp=.35)
summon_draw=pose(root=(0,.025,-.105),body=(-7,0,0),head=(8,0,0),
                 r=(-.59,-.20,.73),l=(.57,-.22,.72),rr=(0,-10,85),lr=(0,10,-85),grasp=.6)
summon_raise=pose(root=(0,0,-.016),body=(-5,0,0),head=(4,0,0),
                  r=(-.59,-.23,.87),l=(.71,-.11,1.00),rr=(0,0,90),lr=(0,-18,-10),grasp=.05)
make('Summon',[(1,idle),(16,summon_low),(33,summon_draw),(49,summon_draw),
               (56,summon_raise),(63,pose(**{**summon_raise,'l':(.76,-.035,1.03),'head':(1,0,0)})),
               (80,pose(r=(-.60,-.12,.81),l=(.64,-.05,.91),head=(-6,0,0))),(101,idle)],
     note='Summon: pull an invisible weight out of the ground, left-hand lift accent at 55%.')

rain_coil=pose(root=(0,.025,-.11),body=(-8,0,0),head=(6,0,0),
               r=(-.60,-.16,.75),l=(.56,-.20,.85),rr=(0,0,70),lr=(0,0,-70),grasp=.4)
rain_hold_a=pose(root=(-.014,0,-.015),body=(-5,0,-3),head=(-14,0,3),
                 r=(-.60,-.23,.88),l=(.32,.02,1.43),rr=(0,0,90),lr=(12,-76,-15),grasp=0)
rain_hold_b=pose(root=(.014,0,-.010),body=(-4,0,3),head=(-12,0,-3),
                 r=(-.60,-.23,.89),l=(.35,.04,1.45),rr=(0,0,88),lr=(9,-72,-18),grasp=0)
make('Rain',[(1,idle),(19,rain_coil),(41,rain_coil),(49,pose(**{**rain_coil,'l':(.56,-.14,.97)})),
             (56,rain_hold_a),(63,rain_hold_b),
             (71,pose(r=(-.59,-.22,.86),l=(.63,-.04,1.08),body=(-1,0,1),head=(-9,0,0))),
             (88,pose(l=(.62,-.08,.74),head=(-1,0,0))),(101,idle)],
     note='Rain conductor. Strong held skyward silhouette on 55–62%, reversible small movement in channel.')

hit=pose(root=(.035,.06,-.08),body=(-16,0,-9),head=(18,0,10),
          r=(-.65,-.01,.77),l=(.64,-.01,.74),rr=(0,0,15),lr=(0,0,-15),grasp=.45)
make('Hit',[(1,idle),(5,hit),(12,pose(root=(-.015,.02,-.10),body=(-7,0,6),head=(9,0,-6))),
            (22,pose(root=(.008,-.01,-.037),body=(5,0,-2),head=(-4,0,1))),(31,idle)],
     note='Short recoil and regain composure. Runtime blends this as a 0.4 s hit overlay.')

death_stagger=pose(root=(.04,.02,-.09),body=(-8,0,-15),head=(13,0,9),
                    r=(-.67,.08,.66),l=(.65,-.07,.63),rr=(0,0,5),lr=(0,0,-5),grasp=.55)
death_knees=pose(root=(.02,.09,-.25),body=(-23,0,-7),head=(26,0,2),
                  r=(-.56,-.13,.39),l=(.60,-.04,.47),rr=(0,0,30),lr=(0,0,-30),grasp=.1)
death_ground=pose(root=(0,-.055,-.215),body=(70,0,-7),head=(12,0,3),
                   r=(-.62,-.27,.17),l=(.62,-.21,.19),rr=(10,0,15),lr=(10,0,-15),
                   fr=(-.24,.24,.156448588),fl=(.25,.20,.156448588),grasp=0)
make('Death',[(1,idle),(13,death_stagger),(25,pose(**{**death_stagger,'head':(20,0,12)})),
              (40,death_knees),(59,death_ground),(65,pose(**{**death_ground,'root':(0,-.05,-.195),'body':(66,0,-7)})),
              (78,death_ground),(91,death_ground)],note='Lose composure, knees buckle, soft forward collapse, small settle and final hold.')

meshes=[o for o in scene.objects if o.type=='MESH' and any(m.type=='ARMATURE' and m.object==rig for m in o.modifiers)]
for o in scene.objects:
    if o.type=='MESH' and o not in meshes:
        o.hide_render=True
rig.hide_render=True
scene.render.fps=30
scene.render.engine='BLENDER_WORKBENCH'
scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
scene.display.shading.light='STUDIO';scene.display.shading.studio_light='paint.sl'
scene.display.shading.color_type='TEXTURE';scene.display.shading.show_shadows=True
scene.display.shading.show_cavity=True;scene.display.shading.cavity_type='BOTH'
scene.display.shading.background_type='WORLD';scene.world.color=(.075,.10,.14)
bpy.ops.object.camera_add(location=(2.8,-4.5,2.55))
camera=bpy.context.object;camera.name='MC_ReviewCamera'
focus=Vector((0,.05,.72));camera.rotation_euler=(focus-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=2.65;scene.camera=camera
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.005))
floor=bpy.context.object;floor.name='MC_ReviewFloor';floor.color=(.13,.16,.21,1)

def show(record):
    reset();bind(bpy.data.actions[record['action']])
    scene.frame_start=record['start'];scene.frame_end=record['end'];scene.frame_set(record['start'])

# NLA showreel in a separate named track, with originals retained and muted.
review=rig.animation_data.nla_tracks.new();review.name='MC REVIEW | all 8 wizard clips'
cursor=1
for record in records:
    strip=review.strips.new(record['label'],cursor,bpy.data.actions[record['action']])
    strip.action_frame_start=record['start'];strip.action_frame_end=record['end']
    strip.frame_end=cursor+record['end']-record['start']
    record['review_start']=cursor;record['review_end']=int(strip.frame_end)
    strip.extrapolation='NOTHING'
    scene.timeline_markers.new(record['label'],frame=cursor)
    cursor=int(strip.frame_end)+10
review.mute=True

show(records[2]);scene.frame_set(56)
for obj in scene.objects:obj.select_set(False)
rig.hide_set(False);rig.select_set(True);bpy.context.view_layer.objects.active=rig
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.shading.type='SOLID';area.spaces.active.shading.color_type='TEXTURE'
            area.spaces.active.overlay.show_overlays=False
        elif area.type=='DOPESHEET_EDITOR':
            area.spaces.active.mode='ACTION'
text=bpy.data.texts.new('MC_ANIMATION_README')
text.write('Wizard style pass v1: 8 independent MC_Wizard_* actions.\nSelect actions in the Action Editor. Current Cast release is frame 56 / 55%.\nOriginal NUT_W_* and Mixamo references retained unchanged.\nNLA review track is muted; mute originals and clear active action before unmuting MC REVIEW.\nGame import not performed; runtime right-hand cast IK may reduce right-hand travel.\n')
scene['MC_AnimationStyle']='Smug theatrical wizard: held preparation, sharp release, delayed left hand/head and soft recovery.'
scene['MC_Source']=str(SOURCE)
scene['MC_GameIntegration']='Blender study only; existing Unreal packages and FBX exports retained.'
bpy.ops.file.pack_all()
blend=OUT/'NutWizard_Style.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend))

report={'status':'authored_blender_study','source':str(SOURCE),'source_sha256_before':source_hash,
        'source_sha256_after':digest(SOURCE),'source_preserved':digest(SOURCE)==source_hash,
        'rest_pose_preserved':rest_signature=={b.name:{'parent':b.parent.name if b.parent else None,'matrix':[list(row) for row in b.matrix_local]} for b in rig.data.bones},
        'original_actions_retained':all(n in bpy.data.actions and list(bpy.data.actions[n].frame_range)==f for n,f in source_actions.items()),
        'blend':str(blend),'fps':30,'actions':records,'checks':[],'game_imported':False}
for record in records:
    show(record)
    positions=[]
    for f in (record['start'],record['end']):
        scene.frame_set(f);bpy.context.view_layer.update()
        positions.append({n:list(rig.pose.bones[n].matrix.translation) for n in ('root.x','foot.l','foot.r','hand.l','hand.r','head.x')})
    seam=max((Vector(positions[0][n])-Vector(positions[1][n])).length for n in positions[0])
    report['checks'].append({'action':record['action'],'loop':record['loop'],'endpoint_position_delta_m':seam})
    if record['loop'] and seam>.0001:raise RuntimeError('Loop seam '+record['label'])
    for f in range(record['start'],record['end']+1):
        scene.frame_set(f)
        for b in rig.pose.bones:
            if not all(math.isfinite(v) for row in b.matrix for v in row):raise RuntimeError('Nonfinite bone '+b.name)
(OUT/'WizardStyleReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')

preview_dir=OUT/'Poses';preview_dir.mkdir(exist_ok=True)
for label,frames in [('Idle',[1]),('Walk',[1,13,25,37]),('Cast',[1,41,56,63,101]),
                     ('Summon',[33,56,63]),('Rain',[41,56,63]),('Death',[25,40,59,91])]:
    record=next(r for r in records if r['label']==label);show(record)
    for f in frames:
        scene.frame_set(f);scene.render.filepath=str(preview_dir/(label+'_'+str(f).zfill(3)+'.png'))
        bpy.ops.render.render(write_still=True)

if args.preview:
    frame_dir=ROOT/'Saved/NutAnimationStyle/WizardFrames';frame_dir.mkdir(parents=True,exist_ok=True)
    index=0;segments=[]
    scene.render.resolution_x=600;scene.render.resolution_y=600
    for record in records:
        show(record);begin=index/15
        for frame in range(record['start'],record['end']+1,2):
            scene.frame_set(frame);scene.render.filepath=str(frame_dir/('frame_%04d.png'%index))
            bpy.ops.render.render(write_still=True);index+=1
        segments.append((record['label'],begin,index/15))
    filters=["drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Wizard / %s':fontcolor=white:fontsize=22:box=1:boxcolor=black@0.65:boxborderw=8:x=16:y=16:enable='between(t,%.4f,%.4f)'"%(label,a,b) for label,a,b in segments]
    subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y','-framerate','15','-i',str(frame_dir/'frame_%04d.png'),'-vf',','.join(filters),'-c:v','libx264','-threads','2','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',str(OUT/'WizardStylePreview.mp4')],check=True)
print('WIZARD_STYLE_COMPLETE',str(blend),flush=True)
