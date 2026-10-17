"""Blender-only asymmetric gait: a planted palm carries two short steps.

Preserves the supplied art and previous reviews. Two local sole controls with
four skin bones keep foot orientation independent from the leg IK and preserve
the original thigh/calf bind matrices. Travel preview uses actual root
translation; the separately saved in-place cycle declares its matching speed.
"""
import json, math, sys
from pathlib import Path
import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import author_wizard_mixamo_pool as helpers

SOURCE = ROOT/'ArtSource/CarriesStage2/ArtistReview/Carries_Stage2_ArtistStudies_v2.blend'
USER_SOURCE = Path('C:/Users/user/Downloads/Telegram Desktop/cARRIES_sTADY2.blend')
SOURCE_REPORT = SOURCE.parent/'AuthorReport_v2.json'
DEST = ROOT/'ArtSource/CarriesStage2/HandSupportWalk'
BLEND = DEST/'Carries_Stage2_HandSupportWalk_v4.blend'
REPORT = DEST/'AuthorReport_v4.json'
STEP, PERIOD = .24, 60
AXES = [Vector(v) for v in ((1,0,0),(0,1,0),(0,0,1))]


def ease(value):
    value = max(0., min(1., value))
    return value*value*(3-2*value)


def curve(t, keys):
    return helpers.author.remap(t, keys)


def rotation(degrees):
    q = Quaternion()
    for axis, angle in zip(AXES, degrees):
        q = Quaternion(axis, math.radians(angle))@q
    return q


def main():
    assert bpy.app.background and not BLEND.exists(), 'Keep previous revisions'
    source_report = json.loads(SOURCE_REPORT.read_text(encoding='utf8'))
    sources = {p: helpers.author.sha(p) for p in (SOURCE, USER_SOURCE, Path(source_report['artist_motion_source']))}
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene, rig, mesh = bpy.context.scene, bpy.data.objects['BoneBrushRig'], bpy.data.objects['SM_Character']
    scene.frame_set(1)
    defaults = helpers.defaults(rig)
    helpers.author.bind(rig, None, defaults)
    original_actions = {a.name: helpers.signature(a) for a in bpy.data.actions}
    for a in bpy.data.actions: a.use_fake_user = True
    original_controls = [p.name for p in rig.pose.bones if p.name.startswith('CTRL-')]
    base_head = {n: rig.matrix_world@rig.pose.bones[n].head for n in original_controls}
    basis_world = {n: (rig.matrix_world@rig.pose.bones[n].matrix@defaults[n]['basis'].inverted()).to_3x3() for n in original_controls}
    world_rot = {n: (rig.matrix_world@rig.pose.bones[n].matrix).to_quaternion() for n in original_controls}
    # Use the already inspected, flat contact orientation of the palm.
    helpers.author.bind(rig, bpy.data.actions['MC_Stage2_GroundSlam'], defaults)
    scene.frame_set(55)
    contact_matrix = rig.matrix_world@rig.pose.bones['CTRL-ik_segment'].matrix
    palm_rotation = contact_matrix.to_quaternion()
    palm_z = contact_matrix.translation.z
    helpers.author.bind(rig, None, defaults)
    foot_specs = [
        dict(name='MC_Foot_PosX', ctrl='CTRL-ik_segment.R', calf='segment_006_02', thigh='segment_006_01', arp=('foot.l','toes_01.l'), lift=.40, land=.67, offset=-.012),
        dict(name='MC_Foot_NegX', ctrl='CTRL-ik_segment.R_01', calf='segment_007_02', thigh='segment_007_01', arp=('foot.r','toes_01.r'), lift=.10, land=.37, offset=.058),
    ]
    for spec in foot_specs:
        calf = rig.pose.bones[spec['calf']]
        spec['ankle'] = rig.matrix_world@calf.tail
        spec['length'] = calf.length+rig.pose.bones[spec['thigh']].length
        spec['skins']=[dict(name=spec['name'],source=spec['calf']),dict(name=spec['name']+'_UpperSkin',source=spec['thigh'])]
        for skin in spec['skins']:
            old=rig.pose.bones[skin['source']]
            skin['old_skin']=old.matrix@old.bone.matrix_local.inverted()
    source_eval=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
    source_z={v.index:(source_eval.matrix_world@v.co).z for v in source_eval.data.vertices}
    # Keep separate thigh/calf skin matrices under each sole control so localized
    # weight transfers preserve the original shape at bind.
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for spec in foot_specs:
        for skin in spec['skins']:
            bone = rig.data.edit_bones.new(skin['name'])
            bone.head = rig.data.edit_bones[spec['calf']].tail
            bone.tail = bone.head+Vector((0,-.09,0))
            bone.parent = rig.data.edit_bones[spec['ctrl']]
            bone.use_deform = True
    bpy.ops.object.mode_set(mode='OBJECT')
    bpy.context.view_layer.update()
    for spec in foot_specs:
        arp_ids = {mesh.vertex_groups[n].index for n in spec['arp'] if n in mesh.vertex_groups}
        leg_ids={mesh.vertex_groups[n].index for n in (spec['calf'],spec['thigh'])}
        mask={}
        for vertex in mesh.data.vertices:
            membership={g.group:g.weight for g in vertex.groups}
            if sum(membership.get(i,0)for i in leg_ids)>.7:
                native_foot=sum(membership.get(i,0)for i in arp_ids)>.025
                # The original BB paint also assigns the bottom of the foot to
                # the thigh. Extend the native foot mask through that low region,
                # fading at the ankle; keep separate bind matrices for both skins.
                mask[vertex.index]=1. if native_foot else ease((.29-source_z[vertex.index])/.07)
        moved=set()
        for skin in spec['skins']:
            bone = rig.pose.bones[skin['name']]
            bone.rotation_mode = 'QUATERNION'
            bone.matrix = skin['old_skin']@bone.bone.matrix_local
            bpy.context.view_layer.update()
            skin['base_world'] = rig.matrix_world@bone.matrix
            group = mesh.vertex_groups.new(name=skin['name'])
            old_group = mesh.vertex_groups[skin['source']]
            for vertex in mesh.data.vertices:
                membership = {g.group:g.weight for g in vertex.groups}
                amount = membership.get(old_group.index,0.)
                fraction=mask.get(vertex.index,0.)
                if amount>0 and fraction>0:
                    group.add([vertex.index],amount*fraction,'REPLACE')
                    if fraction>=1:old_group.remove([vertex.index])
                    else:old_group.add([vertex.index],amount*(1-fraction),'REPLACE')
                    if fraction>.95:moved.add(vertex.index)
            skin['skin_bind_error']=max(abs(v)for row in(bone.matrix@bone.bone.matrix_local.inverted()-skin['old_skin'])for v in row)
        assert moved, 'No native sole mask: '+spec['name']
        spec['ids']=list(moved)
        spec['skin_bind_error']=max(s['skin_bind_error']for s in spec['skins'])
        assert spec['skin_bind_error'] < 1e-4, (spec['name'],spec['skin_bind_error'])
    defaults = helpers.defaults(rig)
    bases = {n: defaults[n]['basis'].decompose() for n in original_controls}
    palm_delta = Vector((.76,-.78,palm_z))
    hand_ids = [v.index for v in mesh.data.vertices if sum(g.weight for g in v.groups if mesh.vertex_groups[g.group].name.startswith(('segment_008','segment_009','segment_010','segment_011')))>.95]
    arm_shell_ids = [v.index for v in mesh.data.vertices if sum(g.weight for g in v.groups if mesh.vertex_groups[g.group].name.startswith(('segment_002','segment_008','segment_009','segment_010','segment_011')))>.75]
    assert hand_ids
    floor = source_report['skin_contact_floor']
    sole_corrections = {s['name']:0. for s in foot_specs}
    palm_correction = 0.
    reach_rows = []
    contact_rows = []

    def make_pose(progress, travel=True, record=False):
        nonlocal palm_correction
        cycle = int(math.floor(progress+1e-9)); t = progress-cycle
        if t < 0: t = 0.
        root = Vector((0,-STEP*progress,0)) if travel else Vector()
        body = Vector((curve(t,[(0,.016),(.12,.034),(.52,.024),(.71,-.018),(.9,-.012),(1,.016)]),
                       curve(t,[(0,0),(.2,-.012),(.57,-.017),(.73,.007),(1,0)]),
                       curve(t,[(0,-.063),(.09,-.083),(.38,-.073),(.68,-.058),(.84,-.052),(1,-.063)])))
        angles = Vector((curve(t,[(0,9),(.12,14),(.43,12),(.68,6),(.9,7),(1,9)]),
                         curve(t,[(0,2),(.16,4),(.51,3),(.74,-2),(.92,0),(1,2)]),
                         curve(t,[(0,0),(.25,-2),(.59,2),(.8,1),(1,0)])))
        pose = {n:(v[0].copy(),v[1].copy(),v[2].copy()) for n,v in bases.items()}
        for n,delta,q in [('CTRL-global',root,Quaternion()),('CTRL-body',body,rotation(angles))]:
            loc,rot,scale = pose[n]
            pose[n] = (loc+basis_world[n].inverted()@delta,basis_world[n].to_quaternion().inverted()@q@world_rot[n],scale)
        hand_u = max(0.,min(1.,(t-.71)/.29))
        hand_step = ease(hand_u)
        hand_target = palm_delta+Vector((.05*math.sin(math.pi*hand_u)**2,STEP*t-STEP*hand_step,.24*math.sin(math.pi*hand_u)**2+palm_correction))
        hand_q = rotation((-13*math.sin(math.pi*hand_u),0,5*math.sin(math.pi*hand_u)))@palm_rotation
        n='CTRL-ik_segment';loc,rot,scale=pose[n]
        pose[n]=(loc+basis_world[n].inverted()@(hand_target-base_head[n]),basis_world[n].to_quaternion().inverted()@hand_q,scale)
        foot_targets = {}
        for spec in foot_specs:
            u=max(0.,min(1.,(t-spec['lift'])/(spec['land']-spec['lift'])))
            lift=.033*math.sin(math.pi*u)**2
            target=spec['ankle']+Vector((0,spec['offset']+STEP*t-STEP*ease(u),lift+sole_corrections[spec['name']]))
            foot_targets[spec['name']]=(target,u)
            n=spec['ctrl'];loc,rot,scale=pose[n]
            pose[n]=(loc+basis_world[n].inverted()@(target-base_head[n]),rot,scale)
        helpers.author.bind(rig,None,defaults)
        for name,values in pose.items(): rig.pose.bones[name].matrix_basis=Matrix.LocRotScale(*values)
        bpy.context.view_layer.update()
        # The supplied pole/pole angle puts the fleshy forearm below the floor
        # in a close planted pose. Solve a consistent outward/upward elbow
        # direction while preserving the pole angle and fixed palm target.
        shoulder=rig.matrix_world@rig.pose.bones['segment_002_01'].head
        elbow=rig.matrix_world@rig.pose.bones['segment_002_01'].tail
        wrist_world=rig.matrix_world@rig.pose.bones['CTRL-ik_segment'].head
        pole=rig.matrix_world@rig.pose.bones['CTRL-pole_segment'].head
        axis=(wrist_world-shoulder).normalized()
        def projected(v): return (v-axis*v.dot(axis)).normalized()
        pole_dir=projected(pole-shoulder);elbow_dir=projected(elbow-shoulder)
        bias=math.atan2(axis.dot(pole_dir.cross(elbow_dir)),pole_dir.dot(elbow_dir))
        wanted=projected(Vector((1,0,.6)))
        desired_pole=shoulder+(Quaternion(axis,-bias)@wanted)*1.5
        n='CTRL-pole_segment';loc,rot,scale=pose[n]
        pose[n]=(loc+basis_world[n].inverted()@(desired_pole-base_head[n]-root),rot,scale)
        rig.pose.bones[n].matrix_basis=Matrix.LocRotScale(*pose[n])
        bpy.context.view_layer.update()
        for spec in foot_specs:
            target,u=foot_targets[spec['name']]
            around_ankle=Matrix.Translation(spec['ankle'])@rotation((-7*math.sin(math.pi*u),0,0)).to_matrix().to_4x4()@Matrix.Translation(-spec['ankle'])
            for skin in spec['skins']:
                bone=rig.pose.bones[skin['name']]
                desired=Matrix.Translation(root+target-spec['ankle'])@around_ankle@skin['base_world']
                bone.matrix=rig.matrix_world.inverted()@desired
                bpy.context.view_layer.update()
                pose[bone.name]=bone.matrix_basis.decompose()
        if record:
            for spec in foot_specs:
                target,u=foot_targets[spec['name']]
                hip=rig.matrix_world@rig.pose.bones[spec['thigh']].head
                ratio=(hip-(target+root)).length/spec['length']
                reach_rows.append(dict(progress=progress,foot=spec['name'],ratio=ratio,swing=0<u<1))
            shoulder=rig.matrix_world@rig.pose.bones['segment_002_01'].head
            reach_rows.append(dict(progress=progress,foot='Palm',ratio=(shoulder-(hand_target+root)).length/1.45436,swing=t>.71))
            if t<.70:
                contact_rows.append(dict(progress=progress,cycle=cycle,wrist_world=list(rig.matrix_world@rig.pose.bones['CTRL-ik_segment'].head)))
        return pose

    # Calibrate actual skin soles once; retain fixed world orientation throughout
    # each planted phase. The palm surface is measured independently of wrist Z.
    make_pose(0.)
    evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
    for spec in foot_specs:
        low=min((evaluated.matrix_world@evaluated.data.vertices[i].co).z for i in spec['ids'])
        sole_corrections[spec['name']]=floor+.003-low
        assert abs(sole_corrections[spec['name']])<.08
    # The lowest load-bearing surface is the thick heel of this hand, which
    # blends wrist and forearm weights. Include that volume in contact fitting;
    # fitting only fully hand-weighted fingers would bury the fleshy wrist.
    for _ in range(4):
        make_pose(.4)
        evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
        palm_low=min((evaluated.matrix_world@evaluated.data.vertices[i].co).z for i in arm_shell_ids)
        difference=floor+.003-palm_low
        if abs(difference)<.001:break
        palm_correction+=difference
    assert abs(palm_correction)<.12
    configs=[('MC_Stage2_HandSupport_Walk',61,False),('MC_Stage2_HandSupport_Walk_RootMotion',61,True),('MC_Stage2_HandSupport_Walk_Preview',181,True)]
    for name,count,travel in configs:
        records=[(make_pose((frame-1)/PERIOD,travel,record=name.endswith('_Preview')), {})for frame in range(1,count+1)]
        action=bpy.data.actions.new(name);action.use_fake_user=True
        helpers.bake(rig,action,defaults,records)
        action['MC_stride_m']=STEP;action['MC_cycle_seconds']=PERIOD/30;action['MC_matching_speed_mps']=STEP/(PERIOD/30)
        action['MC_palm_stance_end']=.71;action['MC_root_motion']=travel
        if not travel:
            for fc in helpers.author.curves(action): fc.modifiers.new('CYCLES')
        print('CARRIES_HAND_WALK_ACTION',name,count,flush=True)
    max_leg=max(r['ratio']for r in reach_rows if r['foot']!='Palm')
    max_arm=max(r['ratio']for r in reach_rows if r['foot']=='Palm')
    assert max_leg<1.,('Unreachable short foot',max_leg)
    assert max_arm<.96,('Arm approaches straight IK',max_arm)
    slips=[]
    for cycle in range(3):
        planted=[Vector(r['wrist_world'])for r in contact_rows if r['cycle']==cycle]
        slips.append(max((p-planted[0]).length for p in planted))
    assert max(slips)<1e-4,('Palm slides during planted support',slips)
    # Keep all previous authored action curves unchanged in this derivative.
    assert all(helpers.signature(bpy.data.actions[n])==sig for n,sig in original_actions.items())
    scene.render.fps=30;scene.render.fps_base=1.
    preview=bpy.data.actions['MC_Stage2_HandSupport_Walk_Preview']
    helpers.author.bind(rig,preview,defaults)
    bounds=[];skin_minima=[]
    keep=('SM_Character','SM_Character.001','SM_Character_006_002_003.004','SM_Character_006_002_003.005')
    for frame in range(1,182,3):
        scene.frame_set(frame);dg=bpy.context.evaluated_depsgraph_get()
        for name in keep:
            obj=bpy.data.objects[name].evaluated_get(dg)
            bounds += [obj.matrix_world@Vector(corner)for corner in obj.bound_box]
            if name=='SM_Character':
                low,idx=min(((obj.matrix_world@v.co).z,v.index)for v in obj.data.vertices)
                skin_minima.append(low)
                if low<floor-.003:
                    print('WALK_LOW_SKIN',frame,low,idx,{mesh.vertex_groups[g.group].name:round(g.weight,4)for g in mesh.data.vertices[idx].groups if g.weight>.02},flush=True)
    lo=Vector([min(v[i]for v in bounds)for i in range(3)])
    hi=Vector([max(v[i]for v in bounds)for i in range(3)])
    assert min(skin_minima)>=floor-.003,('Skin below review floor',min(skin_minima),floor)
    focus=(lo+hi)*.5
    cameras={}
    for view,offset in [('ThreeQuarter',(3,-7,2.3)),('Side',(-7,-1.1,1.8))]:
        name='MC_Carries_'+view
        cam=bpy.data.objects.get(name)
        if cam is None:
            cam=bpy.data.objects.new(name,bpy.data.cameras.new(name));scene.collection.objects.link(cam)
        cam.location=focus+Vector(offset);cam.rotation_euler=(focus-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO'
        bpy.context.view_layer.update();inverse=cam.matrix_world.inverted()
        cam.data.ortho_scale=max(max(2*abs((inverse@v).x)/.89,2*abs((inverse@v).y)*(800/600)/.89)for v in bounds)
        cameras[view]=dict(name=cam.name,scale=cam.data.ortho_scale)
    # A fixed grid makes planted contacts and actual displacement visible.
    verts=[];faces=[]
    for axis in (0,1):
        for index in range(-16,17):
            v=index*.25;w=.0016
            coords=[(v-w,-4),(v+w,-4),(v+w,4),(v-w,4)]
            if axis:coords=[(y,x)for x,y in coords]
            first=len(verts);verts += [(x,y,floor+.0005)for x,y in coords];faces.append(tuple(range(first,first+4)))
    grid_mesh=bpy.data.meshes.new('MC_Walk_Grid');grid_mesh.from_pydata(verts,[],faces)
    grid=bpy.data.objects.new('MC_Walk_Grid',grid_mesh);scene.collection.objects.link(grid)
    grid_mat=bpy.data.materials.new('MC_Walk_Grid');grid_mat.diffuse_color=(.32,.36,.42,1);grid_mesh.materials.append(grid_mat)
    scene.camera=bpy.data.objects[cameras['ThreeQuarter']['name']]
    scene.timeline_markers.clear()
    for frame,label in [(1,'PALM_PLANTED'),(7,'FAR_FOOT_STEP'),(25,'NEAR_FOOT_STEP'),(43,'UNLOAD_PALM'),(49,'PALM_SWING'),(61,'NEXT_CYCLE'),(121,'THIRD_CYCLE')]:
        scene.timeline_markers.new(label,frame=frame)
    scene.frame_start,scene.frame_end=1,181;scene.frame_set(1)
    note=bpy.data.texts.new('MC_HAND_SUPPORT_WALK_README')
    note.write('Blender-only custom asymmetric hand-supported gait for approval.\n'
        'Palm carries weight for 71% of each 2 second cycle; two short steps follow.\n'
        '0.24 m root stride, expected matching speed 0.12 m/s at source model scale.\n'
        'Editable in-place, one-cycle root motion, and three-cycle preview actions.\n'
        'Two sole controls with four skin bones preserve both original leg bind matrices.\n'
        'Native ARP is still frozen; geometry and saved previous source files preserved.\n'
        'Custom gait study, not a downloaded Left4Dead or Mixamo clip.\n')
    DEST.mkdir(parents=True,exist_ok=True);bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
    assert all(helpers.author.sha(p)==digest for p,digest in sources.items())
    report=dict(passed=True,source=str(USER_SOURCE),source_sha256=sources[USER_SOURCE],blend=str(BLEND),blend_sha256=helpers.author.sha(BLEND),
        artist_motion_source=source_report['artist_motion_source'],artist_motion_source_sha256=source_report['artist_motion_source_sha256'],
        parent_review=str(SOURCE),parent_review_sha256=sources[SOURCE],fps=30,
        clips=[dict(name='HandSupportWalk',labelRU='Походка с опорой на руку',useRU='Опора на основание кисти, короткие шаги ногами, затем перенос руки вперёд. Три цикла с реальным продвижением.',
                    frames=180,action=preview.name,event_frames=dict(palm_contact=1,far_foot_swing=15,near_foot_swing=33,unload=43,palm_swing=50,next_contact=61))],
        cameras=cameras,original_files_unchanged=True,original_action_curves_preserved=True,
        source_method='Authored asymmetric planted-palm gait; no downloaded Mixamo or Left4Dead motion',
        step_m=STEP,cycle_frames=PERIOD,cycle_seconds=PERIOD/30,expected_speed_mps=STEP/(PERIOD/30),
        palm_stance_fraction=.71,palm_world_stance_slip_m=slips,max_leg_reach_ratio=max_leg,max_arm_reach_ratio=max_arm,
        foot_bones=[dict(name=s['name'],vertices=len(s['ids']),skin_bind_error=s['skin_bind_error'],sole_correction=sole_corrections[s['name']])for s in foot_specs],
        palm_correction=palm_correction,skin_contact_floor=floor,minimum_skin_z=min(skin_minima),bounds=[list(lo),list(hi)],
        scope='Blender-only hand-supported locomotion preview for approval before Unreal integration.',
        limitations=['Custom asymmetric gait, not a direct Mixamo or Left4Dead animation retarget.',
                     'Speed is calibrated to source model scale; gameplay speed and terrain IK need integration after approval.'])
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('CARRIES_HAND_WALK_READY',str(BLEND),max_leg,max_arm,slips,flush=True)


if __name__=='__main__': main()
