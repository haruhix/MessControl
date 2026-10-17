"""Author/ARP-export three cartoon tank clips in an isolated saved copy.

blender --background --factory-startup --disable-autoexec --python this.py -- --preview
No artist/source blend, Unreal package, rig bind, mesh or preferences are saved.
"""
import argparse, copy, hashlib, json, math, subprocess, sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_nut_tank_style as A
from export_nut_tank import SETTINGS, digest, enable_arp, fbx_inventory, select, sparse_pose_samples

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/Tank/Nut_Tank_MC_Style_v2.blend'
OUT = ROOT / 'ArtSource/NutAnimationStyle/TankCartoonV3'
VISIBLE = ('SM_NutTank', 'SM_Sheld', 'SM_Club')
FPS = 25


def signature(rig):
    return [(b.name, b.parent.name if b.parent else None, b.use_deform,
             [list(row) for row in b.matrix_local]) for b in rig.data.bones]


def bind_action(rig, action):
    rig.animation_data.action = action
    slots = [s for s in action.slots if s.target_id_type == 'OBJECT']
    assert len(slots) == 1, action.name
    rig.animation_data.action_slot = slots[0]


def make_plans(base):
    walk = []
    for frame, left, right, lift_l, lift_r, drop, lean in (
        (1,.255,-.255,0,0,-.018,.035), (5,.19,-.30,0,.035,-.060,.050),
        (10,.085,-.16,0,.16,-.018,.048), (15,-.025,.055,0,.14,.020,.026),
        (20,-.15,.23,0,.055,-.012,.008), (24,-.24,.255,0,0,-.017,-.025),
        (25,-.255,.255,0,0,-.018,-.035), (29,-.30,.19,.035,0,-.060,-.050),
        (34,-.16,.085,.16,0,-.018,-.048), (39,.055,-.025,.14,0,.020,-.026),
        (44,.23,-.15,.055,0,-.012,-.008), (48,.255,-.24,0,0,-.017,.025),
        (49,.255,-.255,0,0,-.018,.035)):
        p = A.pose(base, root=(lean,drop,0), body=(.07,lean*.50,-lean*1.7),
            neck=(-.04,0,lean*.4), head=(-.03,-lean*.8,lean*.65),
            arm_l=(.03,0,lean*.8), arm_r=(.018,.035,-lean*.55),
            foot_l=(-.035,left,lift_l), foot_r=(.035,right,lift_r))
        # Broad grounded guard, clean heel contact, fast swing with toe clearance.
        for side, lift in (('l',lift_l),('r',lift_r)):
            if lift > 0:
                A.add(p, 'c_foot_ik.'+side, 'rotation_euler', (-.18*lift/.16,0,0))
        walk.append((frame,p))
    coil = A.pose(base, root=(.035,-.082,-.04), body=(-.16,-.65,-.075),
        neck=(-.12,0,.04), head=(-.05,.28,.075), arm_r=(-.98,-.48,-.23),
        forearm_r=(.12,.04,.2), hand_r=(-.18,0,-.14), arm_l=(-.06,0,.1))
    melee = [(1,base), (12,A.pose(base,root=(0,-.035,-.008),body=(-.07,-.18,-.025),
        head=(0,.10,.03),arm_r=(-.2,-.12,-.03),arm_l=(.08,0,.04))),
        (32,coil), (38,coil),
        (41,A.pose(base,root=(.02,-.060,-.02),body=(-.08,-.38,-.04),
            neck=(-.06,0,.02),head=(-.05,.25,.06),arm_r=(-.76,-.36,-.18),
            forearm_r=(.08,.02,.16),hand_r=(-.15,0,-.12),arm_l=(-.04,0,.08))),
        (51,A.pose(base,root=(-.028,-.10,.045),body=(.32,.66,.065),
            neck=(.12,-.12,0),head=(.11,-.12,-.025),arm_r=(.91,.40,.095),
            forearm_r=(-.20,0,-.25),hand_r=(.28,0,-.12),arm_l=(.07,-.04,.055))),
        (55,A.pose(base,root=(-.040,-.115,.035),body=(.39,.76,.085),
            neck=(.17,-.16,0),head=(.14,-.20,-.04),arm_r=(1.03,.47,.12),
            forearm_r=(-.23,0,-.29),hand_r=(.31,0,-.12),arm_l=(.07,-.04,.055))),
        (65,A.pose(base,root=(-.020,-.055,.012),body=(.17,.37,.035),
            neck=(.10,-.10,0),head=(.07,-.13,-.025),arm_r=(.48,.26,.07),
            forearm_r=(-.10,0,-.13),arm_l=(.08,0,.02))),
        (78,A.pose(base,root=(0,-.025,0),body=(.08,.13,.018),neck=(.045,-.05,0),
            arm_r=(.25,.14,.02),arm_l=(.07,0,0))),
        (87,A.pose(base,root=(0,.012,0),body=(-.025,-.06,-.014),neck=(-.025,0,0),
            head=(-.015,.025,.01),arm_r=(-.06,-.045,-.012))), (101,base)]
    hit = [(1,base),
        (2,A.pose(base,root=(0,-.045,-.018),body=(-.10,-.04,.04),neck=(-.08,0,0),
            head=(-.09,.03,0),arm_r=(-.12,-.04,.05),arm_l=(.08,0,.04))),
        (3,A.pose(base,root=(0,-.080,-.042),body=(-.30,-.09,.075),neck=(-.16,0,0),
            head=(-.20,.10,-.02),arm_r=(-.32,-.12,.11),forearm_r=(.08,0,.08),
            arm_l=(.20,-.035,.08))),
        (5,A.pose(base,root=(0,.020,.012),body=(.115,.035,-.035),neck=(.07,0,0),
            head=(.14,-.04,.015),arm_r=(.12,.045,-.035),arm_l=(-.045,0,-.02))),
        (7,A.pose(base,root=(0,-.012,0),body=(-.042,-.012,.012),neck=(-.025,0,0),
            head=(-.04,.012,0),arm_r=(-.04,-.015,.01))), (11,base)]
    return {'Walk':walk,'Melee':melee,'Hit':hit}


def render_previews(scene, rig, actions):
    cache = OUT / 'PreviewFrames'
    cache.mkdir(parents=True, exist_ok=True)
    scene.render.resolution_x = scene.render.resolution_y = 384
    scene.render.resolution_percentage = 100
    frame_sets = {'Walk':((1.92,1,49),), 'Melee':((.65,1,51),(2.5,51,101)), 'Hit':((.4,1,11),)}
    movie_fps = 60
    movies = {}
    ffmpeg = 'C:/ffmpeg/ffmpeg.exe'
    for label in ('Walk','Melee','Hit'):
        versions = ('Old','New') if label != 'Hit' else ('New',)
        for version in versions:
            bind_action(rig, actions[(label,version)])
            number = 0
            for seconds, start, end in frame_sets[label]:
                count = max(2, round(seconds*movie_fps))
                for i in range(count):
                    frame = start+(end-start)*i/(count-1)
                    scene.frame_set(math.floor(frame),subframe=frame-math.floor(frame))
                    scene.render.filepath = str(cache/f'{label}_{version}_{number:03}.png')
                    bpy.ops.render.render(write_still=True)
                    number += 1
            movie = OUT / f'{label}_{version}.mp4'
            title = f'{label} | {version} | same rig and camera'
            filters = "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='"+title+"':x=10:y=12:fontsize=14:fontcolor=white:box=1:boxcolor=black@0.6"
            subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-y','-framerate',str(movie_fps),
                '-i',str(cache/f'{label}_{version}_%03d.png'),'-frames:v',str(number),'-vf',filters,
                '-c:v','libx264','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(movie)],check=True)
            movies[(label,version)] = movie
        if label != 'Hit':
            subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-y',
                '-i',str(movies[(label,'Old')]),'-i',str(movies[(label,'New')]),
                '-filter_complex','hstack=inputs=2','-c:v','libx264','-crf','19',
                '-pix_fmt','yuv420p','-movflags','+faststart',str(OUT/f'{label}_Old_New.mp4')],check=True)
    return {'fps':movie_fps,'timing_segments':frame_sets,
        'old_new':['Walk_Old_New.mp4','Melee_Old_New.mp4'],'new_hit':'Hit_New.mp4',
        'limitations':'Blender authored pose preview; native guard/hit/aim/secondary overlays and game collision are absent. No previous authored Tank Hit clip exists.'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preview',action='store_true')
    parser.add_argument('--no-export',action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    OUT.mkdir(parents=True,exist_ok=True)
    original_hash = digest(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    rig = bpy.data.objects['rig']
    scene = next(s for s in bpy.data.scenes if rig.name in s.objects)
    bpy.context.window.scene = scene
    required = ('c_root_master.x','c_root.x','c_neck.x','c_head.x','c_foot_ik.l','c_foot_ik.r',
        'c_arm_fk.l','c_arm_fk.r','c_forearm_fk.l','c_forearm_fk.r','c_hand_fk.l','c_hand_fk.r')
    assert all(n in rig.pose.bones for n in required)
    assert all(rig.pose.bones[n].rotation_mode=='XYZ' for n in required)
    rest = signature(rig)
    rig.animation_data_create()
    for track in rig.animation_data.nla_tracks: track.mute=True
    rig.animation_data.use_nla=False
    bind_action(rig,bpy.data.actions['MC_Tank_Idle'])
    scene.frame_set(1)
    bpy.context.view_layer.update()
    base = A.snapshot(rig)
    assert all(base['c_foot_ik.'+side]['props']['ik_fk_switch']==0 for side in ('l','r'))
    assert all(base['c_hand_ik.'+side]['props']['ik_fk_switch']==1 for side in ('l','r'))
    properties = {b.name:{k:b[k] for k in b.keys() if isinstance(b[k],(bool,int,float))} for b in rig.pose.bones}
    scene.render.fps=FPS
    scene.render.fps_base=1
    actions={}
    report={'schema':3,'source':str(SOURCE),'source_sha256_before':original_hash,
        'blender_version':bpy.app.version_string,'rig':'rig','fps':FPS,'clips':[],
        'style':'Cartoon walnut bruiser: grounded broad gait, held coil and fast club snap, damped recovery; no scale changes.',
        'native_contracts':{'Melee':{'impact_fraction':.5,'windup_seconds':.65,'recovery_seconds':2.5},'Hit':{'seconds':.4}},
        'limitations':['Authored contact coordinates are in place; moving-world foot contacts require Unreal validation.',
            'Tank Hit needs the new native asset slot; existing tank hit response was procedural.',
            'Native guard/aim/recoil overlays can change the final pose; no gameplay collider changes are made.']}
    for label, keys in make_plans(base).items():
        existing=bpy.data.actions.get('MC_Tank_'+label)
        if existing:
            existing.name='MC_Tank_V2_Reference_'+label
            actions[(label,'Old')]=existing
        rig.animation_data.action=None
        action=bpy.data.actions.new('MC_Tank_'+label)
        action.use_fake_user=True
        rig.animation_data.action=action
        for frame,p in keys: A.keypose(rig,p,frame)
        for curve in A.fcurves(action):
            for key in curve.keyframe_points:
                # Bone paths already contain ["bone"]. Only a custom-property
                # suffix such as ["ik_fk_switch"] requires discrete evaluation.
                key.interpolation='CONSTANT' if curve.data_path.endswith(']') else 'BEZIER'
                key.handle_left_type=key.handle_right_type='AUTO_CLAMPED'
        action.use_frame_range=True
        action.frame_start=1
        action.frame_end=keys[-1][0]
        action['mc_loop']=label=='Walk'
        action['mc_style_version']='TankCartoonV3'
        action['mc_authored_cartoon']=True
        if label=='Melee': action['mc_hit_fraction']=.5
        actions[(label,'New')]=action
        report['clips'].append({'label':label,'source_action':action.name,'start_frame':1,
            'end_frame':keys[-1][0],'fps':FPS,'duration_seconds':(keys[-1][0]-1)/FPS,
            'key_poses':[frame for frame,p in keys],'loop':label=='Walk',
            'unreal_float_curves':{'MCAuthoredCartoon':1.0}})
    A.stage(scene)
    scene.camera.data.ortho_scale=3.7
    scene.render.resolution_x=scene.render.resolution_y=640
    contact={}
    for label, frames in {'Walk':(1,5,10,25,29),'Melee':(32,44,51,55,65,87),'Hit':(2,3,5,7)}.items():
        bind_action(rig,actions[(label,'New')])
        feet=[]
        for frame in frames:
            scene.frame_set(frame)
            bpy.context.view_layer.update()
            evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
            feet.append({n:(evaluated.matrix_world@evaluated.pose.bones[n].matrix).translation.copy() for n in ('foot.l','foot.r')})
            scene.render.filepath=str(OUT/f'{label}_{frame:03}.png')
            bpy.ops.render.render(write_still=True)
        if label!='Walk':
            contact[label]={'foot_displacement_m':max((p[n]-feet[0][n]).length for p in feet for n in p)}
    report['contact_samples']=contact
    bind_action(rig,actions[('Walk','New')])
    scene.frame_start=1;scene.frame_end=49;scene.frame_set(1)
    blend=OUT/'Nut_Tank_Cartoon_v3.blend'
    assert signature(rig)==rest
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    report['blend']=str(blend)
    report['hierarchy_bind_unchanged']=signature(rig)==rest
    if not args.no_export:
        addon=enable_arp()
        for name,value in SETTINGS.items(): setattr(scene,name,value)
        meshes=[bpy.data.objects[n] for n in VISIBLE]
        assert all(any(m.type=='ARMATURE' and m.object==rig for m in mesh.modifiers) for mesh in meshes)
        legacy=json.loads((ROOT/'ArtSource/NutTank/ExportReport.json').read_text(encoding='utf8'))
        reference=next(f['fbx'] for f in legacy['files'] if f['file']=='SK_NutTank.fbx')
        bind={p['name']:p['matrix'] for p in reference['bind_poses']}
        export={'schema':2,'status':'exporting','unit':'Tank','source':str(blend),'arp':addon,
            'fps':FPS,'files':[],'clips':report['clips'],'source_pose_samples':{},
            'import_scope':'Animation only; existing SK_NutTank skeleton and reference pose retained.'}
        for clip in report['clips']:
            label=clip['label']
            rig.animation_data.action=None
            for bone in rig.pose.bones:
                bone.matrix_basis=Matrix.Identity(4)
                for key,value in properties[bone.name].items(): bone[key]=value
            bind_action(rig,actions[(label,'New')])
            scene.arp_bake_anim=True;scene.arp_frame_range_type='CUSTOM'
            scene.arp_export_start_frame=scene.frame_start=1
            scene.arp_export_end_frame=scene.frame_end=clip['end_frame']
            export['source_pose_samples'][label]=sparse_pose_samples(scene,rig,1,clip['end_frame'])
            scene.frame_set(1);bpy.context.view_layer.update()
            select([rig,*meshes],rig)
            path=OUT/f'A_NutTank_{label}.fbx'
            assert 'FINISHED' in bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
            info=fbx_inventory(path)
            current={p['name']:p['matrix'] for p in info['bind_poses']}
            assert info['hierarchy_sha256']==reference['hierarchy_sha256']
            assert info['root_animation_constant'] and set(current)==set(bind)
            delta=max(abs(a-b) for name in bind for a,b in zip(bind[name],current[name]))
            assert delta<.0001,(label,delta)
            clip['file']=path.name
            export['files'].append({'file':path.name,'bytes':path.stat().st_size,'sha256':digest(path),
                'fbx':info,'bind_max_delta':delta})
            print('TANK_CARTOON_EXPORTED',label,flush=True)
        export['status']='complete'
        export['source_preserved']=digest(SOURCE)==original_hash
        export['hierarchy_consistent']=export['reference_unchanged']=export['root_animation_constant']=True
        (OUT/'ExportReport.json').write_text(json.dumps(export,indent=2),encoding='utf8')
    if args.preview: report['preview']=render_previews(scene,rig,actions)
    report['source_sha256_after']=digest(SOURCE)
    report['source_preserved']=report['source_sha256_after']==original_hash
    assert report['source_preserved'] and report['hierarchy_bind_unchanged']
    report['status']='authored_exported_first_pass' if not args.no_export else 'authored_first_pass'
    (OUT/'AuthorReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print('TANK_CARTOON_PASS',json.dumps({'source_preserved':True,'clips':3,'out':str(OUT)}),flush=True)


if __name__=='__main__': main()
