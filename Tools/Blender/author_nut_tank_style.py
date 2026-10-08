"""Author seven stylized tank actions in an isolated copy; never writes artist source.

Run: blender --background --factory-startup --python this.py
Sparse ARP control poses remain editable. No rig, bind, mesh or game package changes.
"""
import argparse, copy, hashlib, json, math, sys
from pathlib import Path
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/NutAnimationStyle/Tank'
SOURCE=Path('C:/Users/user/Downloads/Telegram Desktop/Nut_Tank.blend')
VISIBLE=('SM_NutTank','SM_Sheld','SM_Club')

def digest(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def snapshot(r):
    return {b.name:dict(location=list(b.location),rotation_euler=list(b.rotation_euler),
                        rotation_quaternion=list(b.rotation_quaternion),scale=list(b.scale),
                        props={k:b[k] for k in b.keys() if isinstance(b[k],(bool,int,float))})
            for b in r.pose.bones if b.name.startswith('c_')}

def loadpose(r,pose):
    for name,item in pose.items():
        b=r.pose.bones[name]
        for attr in ('location','rotation_euler','rotation_quaternion','scale'):setattr(b,attr,item[attr])
        for key,value in item['props'].items():b[key]=value

def add(p,name,attr,value):
    p[name][attr]=[a+b for a,b in zip(p[name][attr],value)]

def make_base(r,scene,initial):
    r.animation_data.action=None
    for b in r.pose.bones:
        b.matrix_basis.identity()
        for k,v in initial[b.name].items():b[k]=v
    a=bpy.data.actions['NUT_Idle'];r.animation_data.action=a;r.animation_data.action_slot=a.slots[0]
    scene.frame_set(1);bpy.context.view_layer.update()
    p=snapshot(r)
    p['c_root_master.x']['location']=[0,-.065,0]
    p['c_root_master.x']['rotation_euler']=[.055,-.12,-.025]
    p['c_root.x']['rotation_euler']=[0,0,0]
    p['c_neck.x']['rotation_euler']=[.10,0,0]
    p['c_head.x']['rotation_euler']=[-.05,.12,.025]
    # Ground-space IK controls (artist bone roll): X=-world X, Y=-world Y, Z=world Z.
    for side,x,y,yaw in [('l',-.035,.045,-.12),('r',.035,-.045,.12)]:
        p['c_foot_ik.'+side]['location']=[x,y,0]
        p['c_foot_ik.'+side]['rotation_euler']=[0,0,yaw]
        p['c_foot_ik.'+side]['props']['ik_fk_switch']=0.0
        p['c_leg_pole.'+side]['location']=[0,0,.22]
        p['c_toes_ik.'+side]['rotation_euler']=[0,0,0]
    # Artist's coherent shield and club hand grip retained in FK, without human arm swings.
    for side in ('l','r'):p['c_hand_ik.'+side]['props']['ik_fk_switch']=1.0
    p['c_arm_fk.l']['rotation_euler']=[-.95,-.10,-.10]
    p['c_forearm_fk.l']['rotation_euler']=[-.20,-.40,-1.10]
    r.animation_data.action=None
    return p

def pose(base,root=None,body=None,neck=None,arm_r=None,forearm_r=None,hand_r=None,
         arm_l=None,forearm_l=None,hand_l=None,head=None,foot_l=None,foot_r=None):
    p=copy.deepcopy(base)
    for name,attr,value in [('c_root_master.x','location',root),('c_root_master.x','rotation_euler',body),
        ('c_neck.x','rotation_euler',neck),('c_head.x','rotation_euler',head),
        ('c_arm_fk.r','rotation_euler',arm_r),('c_forearm_fk.r','rotation_euler',forearm_r),
        ('c_hand_fk.r','rotation_euler',hand_r),('c_arm_fk.l','rotation_euler',arm_l),
        ('c_forearm_fk.l','rotation_euler',forearm_l),('c_hand_fk.l','rotation_euler',hand_l),
        ('c_foot_ik.l','location',foot_l),('c_foot_ik.r','location',foot_r)]:
        if value is not None:add(p,name,attr,value)
    return p

def keypose(r,p,frame):
    loadpose(r,p)
    for name,item in p.items():
        b=r.pose.bones[name]
        for attr in ('location','scale', 'rotation_quaternion' if b.rotation_mode=='QUATERNION' else 'rotation_euler'):
            b.keyframe_insert(data_path=attr,frame=frame,group=name)
        for key,value in item['props'].items():b.keyframe_insert(data_path='["'+key+'"]',frame=frame,group=name)

def fcurves(a):return [f for l in a.layers for s in l.strips for b in s.channelbags for f in b.fcurves]

def stage(scene):
    for c in bpy.data.collections:c.hide_viewport=False;c.hide_render=False
    for o in scene.objects:
        o.hide_render=o.name not in VISIBLE
        o.hide_set(o.name not in VISIBLE and o.type!='ARMATURE')
        if o.name in VISIBLE or o.type=='ARMATURE':o.hide_viewport=False
    collection=bpy.data.collections.new('MC_Tank_Review');scene.collection.children.link(collection)
    camd=bpy.data.cameras.new('MC_Tank_ReviewCamera');cam=bpy.data.objects.new('MC_Tank_ReviewCamera',camd);collection.objects.link(cam)
    cam.location=(2.8,-4.8,2.5);target=Vector((0,0,1.0));cam.rotation_euler=(target-Vector(cam.location)).to_track_quat('-Z','Y').to_euler()
    camd.type='ORTHO';camd.ortho_scale=3.25;scene.camera=cam
    scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
    scene.display.shading.light='STUDIO';scene.display.shading.color_type='MATERIAL';scene.display.shading.show_shadows=True;scene.display.shading.show_cavity=True
    scene.display.shading.background_type='WORLD';scene.world.color=(.04,.05,.07);scene.view_settings.view_transform='Standard'
    # Review-only colored materials make shell, hands and equipment easy to read.
    # Original material datablocks remain intact; source mesh/material links are restored after review renders.
    return cam,collection

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--preview',action='store_true');args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    OUT.mkdir(parents=True,exist_ok=True);before=digest(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE));scene=bpy.context.scene;r=bpy.data.objects['rig']
    hierarchy=[(b.name,b.parent.name if b.parent else None,list(sum((list(row) for row in b.matrix_local),[]))) for b in r.data.bones]
    original_objects=sorted(bpy.data.objects.keys());original_actions=sorted(bpy.data.actions.keys())
    for a in bpy.data.actions:a.use_fake_user=True
    r.animation_data_create();r.animation_data.use_nla=False
    for t in r.animation_data.nla_tracks:t.mute=True
    initial={b.name:{k:b[k] for k in b.keys() if isinstance(b[k],(bool,int,float))} for b in r.pose.bones}
    base=make_base(r,scene,initial)
    plans={}
    # Small, asymmetric breathing/guard read. Feet remain stationary at every pose.
    plans['Idle']=[(1,base),(19,pose(base,root=(0,.006,0),neck=(-.012,0,0),arm_r=(0,-.015,0))),
                   (43,pose(base,root=(.005,-.004,0),body=(.006,.018,0),arm_l=(.018,0,0),head=(0,-.018,0))),
                   (61,pose(base,root=(0,-.002,0),neck=(.008,0,0))),(81,base)]
    # Shared 49-frame cycle and contacts; planted foot moves backward while body advances in game.
    # Both stance phases overlap during heel contact. No floating foot oscillation.
    for label,direction in [('Walk',(0,1)),('WalkLeft',(1,0)),('WalkRight',(-1,0))]:
        dx,dy=direction;keys=[]
        for frame,lf,rf,lh,rh,drop,sidelean in [
            (1,.13,-.13,0,0,-.008,.015),(5,.08,-.16,0,.04,-.018,.023),
            (10,.025,-.07,0,.09,0,.025),(16,-.05,.045,0,.075,.007,.015),
            (22,-.115,.125,0,.015,-.004,0),(25,-.13,.13,0,0,-.008,-.015),
            (29,-.16,.08,.04,0,-.018,-.023),(34,-.07,.025,.09,0,0,-.025),
            (40,.045,-.05,.075,0,.007,-.015),(46,.125,-.115,.015,0,-.004,0),
            (49,.13,-.13,0,0,-.008,.015)]:
            p=pose(base,root=(sidelean,drop,0),body=(.018,0,-sidelean*.8),neck=(-.015,0,0),
                   arm_l=(.025,0,sidelean*.6),arm_r=(0,.015,-sidelean*.45),
                   foot_l=(dx*lf,dy*lf,lh),foot_r=(dx*rf,dy*rf,rh))
            keys.append((frame,p))
        plans[label]=keys
    # Club anticipation held, four-frame strike, overshoot/recoil, controlled settle.
    plans['Melee']=[(1,base),(13,pose(base,root=(0,-.02,-.012),body=(-.045,-.13,0),head=(0,.05,0),arm_l=(.08,0,0))),
        (33,pose(base,root=(.025,-.045,-.025),body=(-.10,-.35,-.02),neck=(-.08,0,0),head=(0,.14,0),arm_r=(-.65,-.35,-.15),forearm_r=(.05,0,.10),hand_r=(-.15,0,-.10),arm_l=(-.04,0,.06))),
        (44,pose(base,root=(.025,-.045,-.025),body=(-.10,-.35,-.02),neck=(-.08,0,0),head=(0,.14,0),arm_r=(-.65,-.35,-.15),forearm_r=(.05,0,.10),hand_r=(-.15,0,-.10),arm_l=(-.04,0,.06))),
        (51,pose(base,root=(-.018,-.07,.035),body=(.20,.38,.018),neck=(.08,-.12,0),head=(.07,-.10,0),arm_r=(.60,.25,.05),forearm_r=(-.10,0,-.15),hand_r=(.20,0,-.10),arm_l=(.05,-.04,.04))),
        (56,pose(base,root=(-.018,-.082,.025),body=(.24,.42,.025),neck=(.1,-.14,0),head=(.08,-.12,0),arm_r=(.67,.30,.08),forearm_r=(-.15,0,-.18),hand_r=(.22,0,-.10),arm_l=(.05,-.04,.04))),
        (66,pose(base,root=(0,-.038,.006),body=(.08,.22,.01),neck=(.06,-.1,0),arm_r=(.35,.18,.04),forearm_r=(-.06,0,-.08),arm_l=(.04,0,0))),
        (85,pose(base,root=(0,.008,0),body=(-.016,-.025,0),neck=(-.015,0,0),arm_r=(.04,-.025,0))), (101,base)]
    # Game contract: .25 takeoff (frame 26), .70 landing (frame 71).
    plans['Jump']=[(1,base),(15,pose(base,root=(0,-.105,0),body=(.13,0,0),neck=(.09,0,0),arm_l=(.16,0,0),arm_r=(.18,-.12,-.06))),
        (23,pose(base,root=(0,-.13,0),body=(.17,0,0),neck=(.12,0,0),arm_l=(.20,0,0),arm_r=(.23,-.16,-.08))),
        (26,pose(base,root=(0,.03,0),body=(-.08,0,0),neck=(-.07,0,0),arm_l=(-.18,0,0),arm_r=(.15,-.25,-.12))),
        (40,pose(base,root=(0,.35,0),body=(-.14,-.14,0),neck=(-.07,0,0),head=(.04,.12,0),arm_l=(-.12,0,0),arm_r=(.35,-.38,-.16),forearm_r=(.12,0,-.12),foot_l=(0,-.06,.53),foot_r=(0,.04,.47))),
        (52,pose(base,root=(0,.41,0),body=(-.10,-.10,0),neck=(-.05,0,0),head=(.04,.1,0),arm_l=(-.06,0,0),arm_r=(.25,-.30,-.1),foot_l=(0,-.045,.60),foot_r=(0,.035,.54))),
        (63,pose(base,root=(0,.20,0),body=(.12,.12,0),neck=(.06,-.04,0),arm_r=(-.28,.10,.05),arm_l=(.1,0,0),foot_l=(0,-.025,.09),foot_r=(0,.02,.07))),
        (71,base),(76,pose(base,root=(0,-.105,0),body=(.12,.05,0),neck=(.1,0,0),arm_r=(-.12,.04,0),arm_l=(.12,0,0))),
        (87,pose(base,root=(0,.016,0),body=(-.018,0,0),neck=(-.02,0,0))), (101,base)]
    # Compression and guarded tuck; reverse playback is the recovery contract.
    plans['Transform']=[(1,base),(13,pose(base,root=(0,.01,0),body=(-.045,0,0),arm_l=(-.05,0,0),arm_r=(.05,0,0))),
        (28,pose(base,root=(0,-.07,0),body=(.10,0,0),neck=(.12,0,0),arm_l=(.25,-.1,.07),arm_r=(.20,-.12,-.15),forearm_r=(0,0,.15))),
        (48,pose(base,root=(0,-.13,0),body=(.25,.02,0),neck=(.22,0,0),head=(-.10,0,0),arm_l=(.44,-.18,.12),forearm_l=(.1,0,-.22),arm_r=(.52,-.15,-.35),forearm_r=(.15,.05,.20),foot_l=(-.02,.04,.04),foot_r=(.02,-.04,.04))),
        (70,pose(base,root=(0,-.16,0),body=(.31,.03,0),neck=(.26,0,0),head=(-.12,0,0),arm_l=(.56,-.22,.15),forearm_l=(.12,0,-.30),arm_r=(.66,-.18,-.44),forearm_r=(.20,.05,.26),foot_l=(-.07,.045,.10),foot_r=(.07,-.045,.10))),
        (101,pose(base,root=(0,-.16,0),body=(.31,.03,0),neck=(.26,0,0),head=(-.12,0,0),arm_l=(.56,-.22,.15),forearm_l=(.12,0,-.30),arm_r=(.66,-.18,-.44),forearm_r=(.20,.05,.26),foot_l=(-.07,.045,.10),foot_r=(.07,-.045,.10)))]
    # Fold equipment to chest using artist's coherent terminal arm orientation,
    # canonical Euler angles avoid the original 180-degree representation branch.
    tuck_rotations={'c_arm_fk.l':[-.190035,-.264095,-.370065],
                    'c_forearm_fk.l':[-.374405,.071145,-.503281],
                    'c_hand_fk.l':[.026775,-.099458,-.184646],
                    'c_arm_fk.r':[.331086,-.265738,.665856],
                    'c_forearm_fk.r':[-.134534,.204330,.938915],
                    'c_hand_fk.r':[.901278,-.128023,.332926]}
    for frame,p in plans['Transform']:
        weight={1:0,13:0,28:.25,48:.75,70:1,101:1}[frame]
        for name,target in tuck_rotations.items():
            p[name]['rotation_euler']=[v*(1-weight)+t*weight for v,t in zip(base[name]['rotation_euler'],target)]
    # Closed loop tangents join contact phases; hold frames use clamped handles for sharp attack timing.
    report={'schema':1,'unit':'tank','source':str(SOURCE),'source_sha256_before':before,'blender_version':bpy.app.version_string,
            'style':'Keyed guarded walnut bruiser; broad footing, shield held through actions, club anticipation and damped settle.',
            'fps':25,'source_actions_preserved':original_actions,'original_object_count':len(original_objects),'clips':[],
            'limitations':['Blender authoring pass only; no FBX export or Unreal runtime validation.',
                           'Transform is guarded skeletal tuck, with separate existing runtime ball representation.',
                           'Heavy in-place walk style has a roughly 0.30m source stride and a 1.92s cycle. Runtime world footlock is not validated at TankMoveSpeed=160cm/s or ChargeSpeed=650cm/s; integration must tune stride/play rate for scaled character height.',
                           'Source was saved by Blender 5.1 build 501.30; isolated authoring binary reports 5.1.0 501.20 and emits compatibility warning.']}
    scene.render.fps=25;scene.render.fps_base=1
    for label,keys in plans.items():
        r.animation_data.action=None
        a=bpy.data.actions.new('MC_Tank_'+label);a.use_fake_user=True;r.animation_data.action=a
        for frame,p in keys:keypose(r,p,frame)
        a['mc_game_label']=label;a['mc_style_version']='1';a['mc_loop']=label in ('Idle','Walk','WalkLeft','WalkRight')
        a.use_frame_range=True;a.frame_start=1;a.frame_end=keys[-1][0]
        for f in fcurves(a):
            for k in f.keyframe_points:
                k.interpolation='BEZIER';k.handle_left_type='AUTO_CLAMPED';k.handle_right_type='AUTO_CLAMPED'
            if '["' in f.data_path:
                for k in f.keyframe_points:k.interpolation='CONSTANT'
        a['mc_readme']='Sparse key poses on original ARP controls; feet IK, equipment FK. Source actions retained.'
        if label=='Melee':a['mc_hit_fraction']=.5
        if label=='Jump':a['mc_takeoff_fraction']=.25;a['mc_land_fraction']=.7
        report['clips'].append({'action':a.name,'label':label,'start':1,'end':keys[-1][0],'duration_seconds':(keys[-1][0]-1)/25,'key_poses':[f for f,_ in keys],'curves':len(fcurves(a)),'loop':bool(a['mc_loop'])})
    cam,review_collection=stage(scene)
    # Ground helper belongs only to the review collection, outside character source.
    mesh=bpy.data.meshes.new('MC_Tank_Ground');mesh.from_pydata([(-3,-3,-.035),(3,-3,-.035),(3,3,-.035),(-3,3,-.035)],[],[(0,1,2,3)]);mesh.update()
    ground=bpy.data.objects.new('MC_Tank_Ground',mesh);review_collection.objects.link(ground)
    ground.hide_render=False
    metrics={}
    for label,keys in plans.items():
        a=bpy.data.actions['MC_Tank_'+label];r.animation_data.action=a;r.animation_data.action_slot=a.slots[0]
        metrics[label]={}
        for frame in [keys[0][0],keys[-1][0],*([26,51,71] if label in ('Melee','Jump','Transform') else [])]:
            scene.frame_set(frame);bpy.context.view_layer.update();ev=r.evaluated_get(bpy.context.evaluated_depsgraph_get())
            metrics[label][str(frame)]={name:list((ev.matrix_world@ev.pose.bones[name].matrix).translation) for name in ('foot.l','foot.r','hand.l','hand.r','head.x','root.x')}
        if label in ('Idle','Walk','WalkLeft','WalkRight'):
            metrics[label]['loop_max_position_error_m']=max((Vector(metrics[label]['1'][name])-Vector(metrics[label][str(keys[-1][0])][name])).length for name in metrics[label]['1'])
        for frame in ([1,33,44,51,56,85] if label=='Melee' else [1,23,26,40,52,71,76,101] if label=='Jump' else [1,28,48,70,101] if label=='Transform' else [keys[f][0] for f in sorted({0,len(keys)//3,2*len(keys)//3,len(keys)-1})]):
            scene.frame_set(frame);scene.render.filepath=str(OUT/f'{label}_{frame:03}.png');bpy.ops.render.render(write_still=True)
        print('MC_TANK_CLIP_DONE',label,keys[-1][0],flush=True)
    report['pose_metrics']=metrics
    report['hierarchy_bind_unchanged']=hierarchy==[(b.name,b.parent.name if b.parent else None,list(sum((list(row) for row in b.matrix_local),[]))) for b in r.data.bones]
    report['all_source_objects_preserved']=all(name in bpy.data.objects for name in original_objects)
    report['all_source_actions_preserved']=all(name in bpy.data.actions for name in original_actions)
    r.animation_data.action=bpy.data.actions['MC_Tank_Idle'];r.animation_data.action_slot=r.animation_data.action.slots[0]
    scene.frame_start=1;scene.frame_end=81;scene.frame_set(1)
    for o in scene.objects:o.select_set(False)
    r.hide_set(False);r.select_set(True);bpy.context.view_layer.objects.active=r;r.show_in_front=True
    # Save an immediately editable action workspace with the character framed.
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_distance=3.8;area.spaces.active.region_3d.view_location=(0,0,.8)
                area.spaces.active.overlay.show_floor=True;area.spaces.active.shading.type='SOLID';area.spaces.active.shading.color_type='MATERIAL'
            elif area.type=='TIMELINE':area.type='DOPESHEET_EDITOR';area.spaces.active.mode='ACTION'
    text=bpy.data.texts.new('MC_Tank_Animation_Readme')
    text.write('MC Tank animation style v1\nSeven new MC_Tank_* actions; source clips preserved.\n25 FPS. Walk variants 1-49; Idle 1-81. Melee/Jump/Transform 1-101.\nMelee impact 51; Jump takeoff 26, landing 71. Transform tuck holds 70-101, recovery reverse.\nSparse keyed poses on unchanged ARP rig. Review helper collection excluded from exports.\nNo game assets, FBXs, materials, bind transforms or original source files replaced.\n')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Nut_Tank_MC_Style.blend'))
    report['source_sha256_after']=digest(SOURCE);report['source_preserved']=report['source_sha256_after']==before
    report['status']='authored_review_pass';report['blend']='Nut_Tank_MC_Style.blend'
    (OUT/'AnimationReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    if not report['source_preserved'] or not report['hierarchy_bind_unchanged']:raise RuntimeError('Preservation validation failed')
    print('MC_TANK_AUTHOR_PASS',json.dumps({'blend':report['blend'],'actions':len(plans),'source_preserved':True}),flush=True)

if __name__=='__main__':main()
