"""Add Charge-specific editable actions and an explicit full Roll review to a v1 copy.

Never saves v1 or artist source. New helpers live in their own scene/collection.
Run Blender --background --factory-startup --python this.py.
"""
import copy,hashlib,importlib.util,json,math
from pathlib import Path
import bpy
from mathutils import Vector,Quaternion

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/NutAnimationStyle/Tank'
V1=OUT/'Nut_Tank_MC_Style.blend'
V2=OUT/'Nut_Tank_MC_Style_v2.blend'
ARTIST=Path('C:/Users/user/Downloads/Telegram Desktop/Nut_Tank.blend')
FPS=25
spec=importlib.util.spec_from_file_location('tank_style',Path(__file__).with_name('author_nut_tank_style.py'))
A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)

def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def action_fingerprint(action):
    rows=[(f.data_path,f.array_index,[(list(k.co),list(k.handle_left),list(k.handle_right),k.interpolation) for k in f.keyframe_points]) for f in A.fcurves(action)]
    return hashlib.sha256(json.dumps(rows,sort_keys=True).encode()).hexdigest()

def insert_action(rig,name,keys,loop=False):
    rig.animation_data.action=None
    action=bpy.data.actions.new(name);action.use_fake_user=True;rig.animation_data.action=action
    for f,p in keys:A.keypose(rig,p,f)
    for fc in A.fcurves(action):
        for k in fc.keyframe_points:
            k.handle_left_type='AUTO_CLAMPED';k.handle_right_type='AUTO_CLAMPED';k.interpolation='CONSTANT' if '["' in fc.data_path else 'BEZIER'
    action.use_frame_range=True;action.frame_start=keys[0][0];action.frame_end=keys[-1][0]
    action['mc_loop']=loop;action['mc_style_version']='2';action['mc_role']='Charge authoring asset; runtime slot addition required'
    return action

def sample(scene,rig,action,frame):
    rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0]
    scene.frame_set(math.floor(frame),subframe=frame-math.floor(frame));bpy.context.view_layer.update()
    return A.snapshot(rig)

def check_charge_floor(scene,rig,actions):
    """Check every visible skinned mesh at quarter-frame spacing before saving."""
    result={}
    for name,action in actions.items():
        rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0]
        minima={n:{'z_m':float('inf'),'frame':None} for n in A.VISIBLE}
        first,last=int(action.frame_start),int(action.frame_end)
        for i in range((last-first)*4+1):
            frame=first+i*.25
            scene.frame_set(math.floor(frame),subframe=frame-math.floor(frame));bpy.context.view_layer.update()
            dg=bpy.context.evaluated_depsgraph_get()
            for n in A.VISIBLE:
                evaluated=bpy.data.objects[n].evaluated_get(dg);mesh=evaluated.to_mesh()
                z=min((evaluated.matrix_world@v.co).z for v in mesh.vertices)
                evaluated.to_mesh_clear()
                if z<minima[n]['z_m']:minima[n]={'z_m':z,'frame':frame}
        result[action.name]={'samples':(last-first)*4+1,'per_mesh_floor_minima':minima}
    failed={a:{n:r for n,r in d['per_mesh_floor_minima'].items() if r['z_m']<-.0005} for a,d in result.items()}
    failed={a:d for a,d in failed.items() if d}
    print('TANK_V2_FLOOR_QA',json.dumps(result),flush=True)
    if failed:raise RuntimeError('Charge floor penetration: '+json.dumps(failed))
    return result

def mix_pose(a,b,t):
    result=copy.deepcopy(a)
    for name,parts in result.items():
        for attr in ('location','rotation_euler','rotation_quaternion','scale'):
            parts[attr]=[x*(1-t)+y*t for x,y in zip(a[name][attr],b[name][attr])]
    return result

def link_object(collection,name,data=None):
    o=bpy.data.objects.new(name,data);collection.objects.link(o);return o

def material(name,color):
    mat=bpy.data.materials.new(name);mat.diffuse_color=(*color,1);return mat

def plane(collection,name,points,mat):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(points,[],[(0,1,2,3)]);mesh.update()
    o=link_object(collection,name,mesh);mesh.materials.append(mat);return o

def build_review(scene,rig,base,brace,actions,report):
    review=bpy.data.scenes.new('MC_Tank_Abilities_v2_Review')
    c=bpy.data.collections.new('MC_Tank_Abilities_v2_Review');review.collection.children.link(c)
    review.render.engine='BLENDER_WORKBENCH';review.render.fps=FPS;review.render.fps_base=1
    review.render.resolution_x=760;review.render.resolution_y=640;review.render.resolution_percentage=100
    review.display.shading.light='STUDIO';review.display.shading.color_type='MATERIAL';review.display.shading.show_cavity=True;review.display.shading.show_shadows=True
    review.display.shading.background_type='WORLD';review.world=bpy.data.worlds.new('MCV2_ReviewWorld');review.world.color=(.04,.05,.07);review.view_settings.view_transform='Standard'
    floor=material('MCV2_ReviewFloor',(.20,.24,.30));grid=material('MCV2_ReviewGrid',(.42,.52,.64));wall_mat=material('MCV2_ReviewWall',(.42,.24,.12))
    plane(c,'MCV2_Ground',[(-28,-35,-.035),(28,-35,-.035),(28,12,-.035),(-28,12,-.035)],floor)
    # Fixed 1m grid makes translation and steering visible with a tracking camera.
    for x in range(-24,25):plane(c,f'MCV2_GridX_{x}',[(x-.012,-35,-.030),(x+.012,-35,-.030),(x+.012,12,-.030),(x-.012,12,-.030)],grid)
    for y in range(-34,13):plane(c,f'MCV2_GridY_{y}',[(-28,y-.012,-.030),(28,y-.012,-.030),(28,y+.012,-.030),(-28,y+.012,-.030)],grid)
    wall=plane(c,'MCV2_RicochetWall',[(9.6,-24,0),(9.6,-4,0),(9.6,-4,.5),(9.6,-24,.5)],wall_mat)
    course=link_object(c,'MCV2_PresentationMotion')
    char=link_object(c,'MCV2_WarriorScale');char.parent=course
    model_height=float(bpy.data.objects['SM_NutTank'].dimensions.z);model_scale=2.2/model_height
    char.scale=(model_scale,)*3
    rr=rig.copy();rr.data=rig.data.copy();rr.name='MCV2_ReviewRig';c.objects.link(rr);rr.parent=char;rr.matrix_parent_inverse.identity();rr.matrix_basis=rig.matrix_world.copy()
    # Internal constraints/drivers refer to this independent review instance.
    for pb in rr.pose.bones:
        for con in pb.constraints:
            if hasattr(con,'target') and con.target==rig:con.target=rr
    if rr.animation_data:
        for track in rr.animation_data.nla_tracks:track.mute=True
        rr.animation_data.use_nla=False
        for fc in rr.animation_data.drivers:
            for var in fc.driver.variables:
                for target in var.targets:
                    if target.id==rig:target.id=rr
    for name in A.VISIBLE:
        source=bpy.data.objects[name];obj=source.copy();obj.name='MCV2_'+name;c.objects.link(obj)
        obj.parent=char;obj.matrix_parent_inverse.identity();obj.matrix_basis=source.matrix_world.copy();obj.hide_render=False;obj.hide_viewport=False;obj.hide_set(False)
        for mod in obj.modifiers:
            if mod.type=='ARMATURE' and mod.object==rig:mod.object=rr
    source_ball=bpy.data.objects['stylized_walnut_game_ready.001']
    ball_mesh=source_ball.data.copy();ball_mesh.name='MCV2_RollBall_CenteredMesh'
    pts=[source_ball.matrix_world@v.co for v in source_ball.data.vertices]
    lower=Vector(tuple(min(p[i] for p in pts) for i in range(3)));upper=Vector(tuple(max(p[i] for p in pts) for i in range(3)));center=(lower+upper)*.5
    ball_scale=2.0/(upper.z-lower.z)
    for v,p in zip(ball_mesh.vertices,pts):v.co=(p-center)*ball_scale
    ball=link_object(c,'MCV2_RollBall',ball_mesh);ball.parent=course;ball.location=(0,0,1);ball.rotation_mode='QUATERNION'
    cd=bpy.data.cameras.new('MCV2_ReviewCamera');cam=link_object(c,'MCV2_ReviewCamera',cd);review.camera=cam;cd.type='ORTHO';cd.ortho_scale=5.4
    rr.animation_data.action=bpy.data.actions.new('MC_Tank_AbilityReview_Pose');rr.animation_data.action.use_fake_user=True
    rr.animation_data.action['mc_review_only']=True
    pose_action=rr.animation_data.action
    # Contact marker is an abstract review proxy, never another game-unit asset.
    marker_mesh=bpy.data.meshes.new('MCV2_ContactProxy');marker_mesh.from_pydata([(-.22,-.22,0),(.22,-.22,0),(.22,.22,0),(-.22,.22,0),(-.22,-.22,.45),(.22,-.22,.45),(.22,.22,.45),(-.22,.22,.45)],[],[(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]);marker_mesh.materials.append(material('MCV2_ContactMarker',(.70,.34,.18)))
    marker=link_object(c,'MCV2_ContactProxy',marker_mesh)
    # Kinematic course: 26m in 5s, bounded steering, one visible edge reflection.
    rollout=[];p=Vector((0,0,0));heading=0.;q=Quaternion();bounce=None
    for i in range(126):
        age=i/FPS
        desired=0 if age<.8 else math.radians(75) if bounce is None else math.radians(-35)
        delta=max(-math.radians(50)/FPS,min(math.radians(50)/FPS,desired-heading));heading+=delta
        direction=Vector((math.sin(heading),-math.cos(heading),0))
        if i>0:
            previous=p.copy();step=direction*(5.2/FPS);p+=step
            if p.x>8.55 and bounce is None:
                p.x=8.55;heading=-heading;direction=Vector((math.sin(heading),-math.cos(heading),0));bounce=age
            travel=p-previous
            if travel.length>.000001:q=(Quaternion(Vector((0,0,1)).cross(travel.normalized()),travel.length/1.0)@q).normalized()
        rollout.append((p.copy(),heading,q.copy()))
    hit_age=1.8;hit_index=round(hit_age*FPS);hit_p,hit_h,_=rollout[hit_index]
    outward=Vector((math.cos(hit_h),math.sin(hit_h),0));marker_anchor=hit_p+outward*.95
    charge_duration=11/6.5;charge_end=1.1+charge_duration;charge_total=charge_end+1.4
    # World-motion review cadence is matched to this short-legged authored stance.
    # This is presentation staging, not a change to the game's 650cm/s setting.
    charge_review_speed=.34*model_scale/(4/(FPS*2))
    charge_review_distance=charge_review_speed*charge_duration
    roll_start=charge_total+.6;total=roll_start+1.2+5+1.4
    count=round(total*FPS)+1;sections=[]
    for name,a,b in [('ChargeTell / locked direction',0,1.1),('ChargeLoop / illustrative motion',1.1,charge_end),('ChargeRecovery',charge_end,charge_total),('RollTransform / tuck',roll_start,roll_start+1.2),('Rolling / steer, contact proxy, ricochet',roll_start+1.2,roll_start+6.2),('Unfold / recovery',roll_start+6.2,total)]:sections.append({'label':name,'start_seconds':a,'end_seconds':b})
    last_loop=None
    for index in range(count):
        frame=index+1;t=index/FPS;weight=0;position=Vector((0,0,0));yaw=0;spin=Quaternion()
        if t<1.1:pose=sample(scene,rig,actions['ChargeTell'],1+t/1.1*27)
        elif t<charge_end:
            # New distinct braced sprint, shown at intended charge cadence.
            f=1+((t-1.1)*FPS*2)%8;pose=sample(scene,rig,actions['ChargeLoop'],f);last_loop=copy.deepcopy(pose)
            position.y=-(t-1.1)*charge_review_speed
        elif t<charge_total:
            age=t-charge_end;pose=sample(scene,rig,actions['ChargeRecovery'],1+min(1,age/1.4)*35)
            if age<.12 and last_loop:pose=mix_pose(last_loop,pose,age/.12)
            position.y=-charge_review_distance
        elif t<roll_start:pose=base;position.y=-charge_review_distance
        else:
            age=t-roll_start
            if age<1.2:
                pose=sample(scene,rig,bpy.data.actions['MC_Tank_Transform'],1+age/1.2*100)
                x=max(0,min(1,(age-1.02)/.18));weight=x*x*(3-2*x)
            elif age<6.2:
                ri=min(125,round((age-1.2)*FPS));position,yaw,spin=rollout[ri];weight=1;pose=sample(scene,rig,bpy.data.actions['MC_Tank_Transform'],101)
            else:
                r_age=age-6.2;position,yaw,spin=rollout[-1];pose=sample(scene,rig,bpy.data.actions['MC_Tank_Transform'],101-min(1,r_age/1.4)*100)
                x=max(0,min(1,r_age/.18));weight=1-x*x*(3-2*x)
        A.keypose(rr,pose,frame)
        course.location=position;course.rotation_euler=(0,0,yaw)
        # World spin converted into heading-relative child coordinates.
        ball.rotation_quaternion=Quaternion((0,0,1),-yaw)@spin
        ball.scale=(max(.0001,weight),)*3;ball.hide_render=weight<.001
        # Actual saved ball is slightly asymmetric; compensate its rotated support
        # radius against the review floor instead of assuming a perfect sphere.
        support=-min((spin@v.co).z for v in ball_mesh.vertices)
        ball.location.z=max(1.0,-.035+support*weight+.005)
        char.scale=(max(.0001,1-weight)*model_scale,)*3
        for o,attrs in [(course,('location','rotation_euler')),(ball,('location','rotation_quaternion','scale','hide_render')),(char,('scale',))]:
            for attr in attrs:o.keyframe_insert(data_path=attr,frame=frame)
        cam.location=position+Vector((3.8,-6.5,3.1));cam.rotation_euler=(position+Vector((0,0,1.0))-cam.location).to_track_quat('-Z','Y').to_euler()
        for attr in ('location','rotation_euler'):cam.keyframe_insert(data_path=attr,frame=frame)
        marker.hide_render=t<roll_start+1.2 or t>roll_start+6.2
        push_age=max(0,t-(roll_start+1.2+hit_age));marker.location=marker_anchor+outward*min(1.4,push_age*2.6)
        for attr in ('location','hide_render'):marker.keyframe_insert(data_path=attr,frame=frame)
    for obj in [course,ball,char,cam,marker]:
        if obj.animation_data and obj.animation_data.action:
            for fc in A.fcurves(obj.animation_data.action):
                for k in fc.keyframe_points:k.interpolation='CONSTANT' if fc.data_path=='hide_render' else 'LINEAR'
            obj.animation_data.action['mc_review_only']=True;obj.animation_data.action.use_fake_user=True
    for fc in A.fcurves(pose_action):
        for k in fc.keyframe_points:k.interpolation='CONSTANT' if '["' in fc.data_path else 'LINEAR'
    review.frame_start=1;review.frame_end=count;review.frame_set(1)
    for i,s in enumerate(sections):review.timeline_markers.new(s['label'],frame=round(s['start_seconds']*FPS)+1)
    report['review']={'scene':review.name,'frames':count,'fps':FPS,'duration_seconds':(count-1)/FPS,'sections':sections,
                      'ball_source':source_ball.name,'ball_source_mesh_vertices':len(source_ball.data.vertices),'ball_display_height_m':2.0,
                      'steer_limit_degrees_per_second':50,'roll_speed_mps':5.2,'roll_seconds':5,'edge_reflection_at_seconds':bounce,
                      'contact_proxy_at_roll_age_seconds':hit_age,'contact_proxy_scope':'Illustrative hit/push marker. No game target, hit detection or physics is simulated.',
                      'character_model_scale_for_review':model_scale,'charge_loop_preview_play_rate':2.0,
                      'charge_review_speed_mps':charge_review_speed,'charge_review_distance_m':charge_review_distance,'charge_motion_illustrative':True,
                      'review_wall_height_m':.5,'ball_support_floor_m':-.035,'ball_support_clearance_m':.005,'ball_spin_uses_actual_travel_over_radius':True}
    return review

def main():
    hashes={'v1':digest(V1),'artist':digest(ARTIST)}
    bpy.ops.wm.open_mainfile(filepath=str(V1));scene=bpy.context.scene;rig=bpy.data.objects['rig']
    old_actions={a.name:action_fingerprint(a) for a in bpy.data.actions}
    old_objects={o.name:tuple(sum((list(row) for row in o.matrix_world),[])) for o in bpy.data.objects}
    bind=[(b.name,b.parent.name if b.parent else None,tuple(sum((list(row) for row in b.matrix_local),[]))) for b in rig.data.bones]
    base=sample(scene,rig,bpy.data.actions['MC_Tank_Idle'],1)
    brace=A.pose(base,root=(0,-.065,.02),body=(.24,0,0),neck=(.04,0,0),head=(-.10,0,0),arm_r=(.18,-.08,.06),forearm_r=(.04,0,.12),hand_r=(.10,0,0),arm_l=(-.06,0,0),forearm_l=(0,0,-.08))
    # Right shoulder controller translates the existing FK club grip upward.
    # Leaves all bind transforms and limb scales intact, with clear floor clearance.
    A.add(brace,'c_shoulder.r','location',(0,0,.24))
    # Lift the shield grip together with its FK arm in the braced sprint.
    A.add(brace,'c_shoulder.l','location',(0,0,.16))
    loop=[]
    for frame,lf,rf,lh,rh,drop,lean in [(1,.17,-.17,0,0,-.015,.006),(2,.08,-.20,0,.065,-.020,.012),(3,-.02,-.06,0,.105,0,.015),(4,-.10,.095,0,.050,.012,.008),(5,-.17,.17,0,0,-.015,-.006),(6,-.20,.08,.065,0,-.020,-.012),(7,-.06,-.02,.105,0,0,-.015),(8,.095,-.10,.050,0,.012,-.008),(9,.17,-.17,0,0,-.015,.006)]:
        loop.append((frame,A.pose(brace,root=(lean,drop,0),foot_l=(0,lf,lh),foot_r=(0,rf,rh),arm_l=(.01,0,0),arm_r=(0,.015,0))))
    launch=loop[0][1]
    tell_early=A.pose(base,root=(0,-.03,0),body=(.07,0,0),forearm_l=(0,0,-.04),foot_l=(0,.065,.045))
    A.add(tell_early,'c_shoulder.r','location',(0,0,.16))
    A.add(tell_early,'c_shoulder.l','location',(0,0,.065))
    tell_mid=A.pose(base,root=(0,-.045,.005),body=(.14,0,0),forearm_l=(0,0,-.065),foot_l=(0,.10,0),foot_r=(0,-.065,.045))
    A.add(tell_mid,'c_shoulder.r','location',(0,0,.22))
    A.add(tell_mid,'c_shoulder.l','location',(0,0,.12))
    coil=A.pose(brace,root=(0,-.035,-.03),body=(-.025,0,0),foot_l=(0,.10,0),foot_r=(0,-.10,0))
    tell=[(1,base),(8,tell_early),(12,tell_mid),(17,coil),(23,coil),
          (24,A.pose(brace,foot_l=(0,.10,0),foot_r=(0,-.145,.040))),
          (26,A.pose(brace,foot_l=(0,.145,.045),foot_r=(0,-.17,0))),(28,launch)]
    recoil=A.pose(base,root=(0,-.025,.01),body=(-.075,0,0),neck=(-.045,0,0),foot_l=(0,.17,.005),foot_r=(0,-.17,.005))
    A.add(recoil,'c_shoulder.r','location',(0,0,.16))
    A.add(recoil,'c_shoulder.l','location',(0,0,.10))
    rear_step=A.pose(base,root=(0,.003,0),body=(-.025,0,0),foot_l=(0,.17,0),foot_r=(0,-.085,.045));A.add(rear_step,'c_shoulder.r','location',(0,0,.10))
    A.add(rear_step,'c_shoulder.l','location',(0,0,.065))
    rear_contact=A.pose(base,root=(0,.006,0),body=(.015,0,0),foot_l=(0,.17,0));A.add(rear_contact,'c_shoulder.r','location',(0,0,.055))
    A.add(rear_contact,'c_shoulder.l','location',(0,0,.04))
    front_step=A.pose(base,root=(0,.004,0),body=(.010,0,0),foot_l=(0,.085,.045));A.add(front_step,'c_shoulder.r','location',(0,0,.025))
    A.add(front_step,'c_shoulder.l','location',(0,0,.015))
    recovery=[(1,launch),(4,A.pose(brace,root=(0,-.08,.065),body=(.085,0,0),neck=(.065,0,0),foot_l=(0,.17,.010),foot_r=(0,-.17,.010))),
              (11,recoil),(15,rear_step),(19,rear_contact),(24,front_step),(29,base),(36,base)]
    actions={'ChargeTell':insert_action(rig,'MC_Tank_ChargeTell',tell),
             'ChargeLoop':insert_action(rig,'MC_Tank_ChargeLoop',loop,True),
             'ChargeRecovery':insert_action(rig,'MC_Tank_ChargeRecovery',recovery)}
    report={'schema':2,'status':'authoring','source_v1':str(V1),'source_artist':str(ARTIST),'source_sha256_before':hashes,
            'native_contracts':{'Charge':{'tell_seconds':1.1,'speed_cmps':650,'maximum_distance_cm':1100,'recovery_seconds':1.4,'locked_direction':True,'damage':22,'hit_per_hero':1,'push_horizontal':260,'push_vertical':45},
                                'Roll':{'tell_seconds':1.2,'speed_cmps':520,'active_seconds':5,'steering_degrees_per_second':50,'recovery_seconds':1.4,'damage':16,'same_hero_hit_gap_seconds':1.1,'push_horizontal':260,'push_vertical':55,'ball_crossfade_seconds':.18}},
            'source_evidence':['Source/MessControl/Private/MCNutBoss.cpp:255-297 (Charge still Walk2x)','Source/MessControl/Private/MCNutBoss.cpp:326-343 (warrior/ball presentation)','Source/MessControl/Private/MCNutBoss.cpp:434-450 (locked tells)','Source/MessControl/Private/MCNutBoss.cpp:511-580 (hits, push, steering, ricochet)','Source/MessControl/Private/MCNutBoss.cpp:623-629 (Charge execution, recovery1.4s)','Source/MessControl/Public/MCNutBossTypes.h:84-96 (defaults)'],
            'clips':[{'action':a.name,'start_frame':int(a.frame_start),'end_frame':int(a.frame_end),'duration_seconds':(a.frame_end-a.frame_start)/FPS,'loop':bool(a['mc_loop'])} for a in actions.values()],
            'limitations':['New Charge assets require runtime slots and export/import wiring; current game continues sampling Walk2x.','Review animation illustrates the existing kinematic Roll behavior; it does not run game collision, physics, target selection or network code.','World footlock and variable-speed gait blending remain unvalidated; no runtime packages were modified.']}
    report['charge_per_mesh_floor_qa']=check_charge_floor(scene,rig,actions)
    review=build_review(scene,rig,base,brace,actions,report)
    # Main scene returns to its v1 active state; all v1 authoring actions stay byte-equivalent.
    rig.animation_data.action=bpy.data.actions['MC_Tank_Idle'];rig.animation_data.action_slot=rig.animation_data.action.slots[0];scene.frame_set(1)
    report['v1_action_fcurves_unchanged']=all(action_fingerprint(bpy.data.actions[n])==h for n,h in old_actions.items())
    report['v1_object_transforms_unchanged']=all(tuple(sum((list(row) for row in bpy.data.objects[n].matrix_world),[]))==m for n,m in old_objects.items())
    report['rig_bind_unchanged']=bind==[(b.name,b.parent.name if b.parent else None,tuple(sum((list(row) for row in b.matrix_local),[]))) for b in rig.data.bones]
    report['charge_loop_pose_seam_error_m']=max((Vector(loop[0][1][n]['location'])-Vector(loop[-1][1][n]['location'])).length for n in loop[0][1])
    report['charge_tell_to_loop_exact_pose_match']=tell[-1][1]==loop[0][1]
    readme=bpy.data.texts.new('MC_Tank_Abilities_v2_Readme')
    readme.write('Tank abilities v2\nKeeps all v1 source/MC actions. Adds ChargeTell, ChargeLoop, ChargeRecovery.\nCharge:1.1slocked tell, straight650cm/s,max1100cm,1.4srecovery.\nRoll:1.2stuck, actual round-ball replacement,5sroll520cm/s, bounded50deg/ssteer,edge reflection,1.4sunfold.\nReview scene MC_Tank_Abilities_v2_Review is a presentation demonstration; abstract contact marker is not physics.\nNewCharge actions are not wired into game runtime slots. No FBX,gamepackages,builds touched.\n')
    bpy.ops.wm.save_as_mainfile(filepath=str(V2))
    report['source_sha256_after']={'v1':digest(V1),'artist':digest(ARTIST)};report['source_files_preserved']=report['source_sha256_after']==hashes
    report['status']='authored_review_pass';report['blend']=str(V2)
    (OUT/'Tank_Abilities_v2_Report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    if not all(report[k] for k in ('source_files_preserved','v1_action_fcurves_unchanged','v1_object_transforms_unchanged','rig_bind_unchanged')):raise RuntimeError('v1 preservation failed')
    print('TANK_V2_AUTHOR_PASS',json.dumps({'new_actions':list(actions),'review_frames':review.frame_end,'preserved':True}),flush=True)

if __name__=='__main__':main()
