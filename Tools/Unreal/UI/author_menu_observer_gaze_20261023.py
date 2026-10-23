"""Bake a calm standing observer: follow the cleaning, glance at camera, blink."""
import unreal as u,json,math,shutil,hashlib
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuObserverGaze20261023';OUT.mkdir(exist_ok=True,parents=True)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not ls.is_in_play_in_editor();world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world();assert world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
aa=list(es.get_all_level_actors());love=next(a for a in aa if 'MC_MenuLove' in [str(t) for t in a.tags]);cleaner=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);camera=next(a for a in aa if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
c=love.skeletal_mesh_component;mesh=c.skeletal_mesh_asset;sk=mesh.skeleton;clip=c.get_editor_property('animation_data').anim_to_play
assert clip.get_path_name().startswith('/Game/UI/MenuShowcase/AdditionalCharacters20261023/')
backup=OUT/'Before';backup.mkdir(exist_ok=True)
if not (backup/'L_MainMenu_ArtistState.umap').exists():
    shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_Disk.umap')
    shutil.copy2(ROOT/'Content/UI/MenuShowcase/AdditionalCharacters20261023/A_Menu_Love_Face_Loop.uasset',backup/'A_Menu_Love_Face_Loop.uasset')
    assert ls.save_current_level();shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_ArtistState.umap')
report={'actors_before':{a.get_path_name():str(a.get_actor_transform()) for a in aa},'camera':camera.get_path_name(),'fov':camera.camera_component.field_of_view,'lighting':{},'postprocess':{},'observer':love.get_path_name(),'clip':clip.get_path_name()}
for a in aa:
    if isinstance(a,u.Light):report['lighting'][a.get_path_name()]=u.ToolsetLibrary.get_object_properties(a.light_component,['Intensity','LightColor','Mobility'])
    if isinstance(a,u.PostProcessVolume):report['postprocess'][a.get_path_name()]=str(a.settings)
ref=sk.get_reference_pose();names=[str(n) for n in ref.get_bone_names()];sub=u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
parents={n:str(sub.get_bone_parent(mesh,n)) for n in names};local={n:ref.get_bone_pose(n,u.AnimPoseSpaces.LOCAL) for n in names};rest={n:ref.get_bone_pose(n,u.AnimPoseSpaces.WORLD) for n in names}
HEAD='head_x';EYES=['c_eye_l','c_eye_r'];assert all(n in names for n in [HEAD,*EYES,'foot_l','foot_r','hand_l','hand_r'])
# Pure numeric quaternion math avoids assumptions about imported bone axes.
def qm(a,b):
    x,y,z,w=a;X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)
def qi(q):return (-q[0],-q[1],-q[2],q[3])
def qv(q,v):return qm(qm(q,(*v,0.)),qi(q))[:3]
def axis(a,degrees):
    t=math.radians(degrees)/2;s=math.sin(t);return (a[0]*s,a[1]*s,a[2]*s,math.cos(t))
def norm(v):
    l=math.sqrt(sum(x*x for x in v));return tuple(x/l for x in v)
def sub3(a,b):return tuple(x-y for x,y in zip(a,b))
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def uq(q):return u.Quat(*q)
def qt(q):return tuple(q.to_tuple())
def limited(x,lo,hi):return min(hi,max(lo,x))
def smooth(a,b,t):
    v=limited((t-a)/(b-a),0.,1.);return v*v*(3-2*v)
def camera_weight(t):
    # Most of the cycle watches the work. Two differently spaced brief glances.
    return max(smooth(5.1,5.65,t)*(1-smooth(7.15,7.85,t)),smooth(13.0,13.5,t)*(1-smooth(14.65,15.4,t)))
clean_clip=cleaner.skeletal_mesh_component.get_editor_property('animation_data').anim_to_play
fps=30;frames=round(clean_clip.get_play_length()*2*fps);length=frames/fps
clean_world=cleaner.skeletal_mesh_component.get_world_transform();observer_world=c.get_world_transform();target_camera=camera.get_actor_location()
# +Y is the artist mesh's facial forward, -X is its right, +Z is up.
forward=(0.,1.,0.);right=(-1.,0.,0.);up=(0.,0.,1.)
tracks={n:[] for n in EYES};times=[];blink=[];delight=[];samples=[]
blink_centers=[2.3,5.32,9.0,12.95,17.0]
head_rest_q=qt(rest[HEAD].rotation);head_parent_q=qt(rest[parents[HEAD]].rotation)
for index in range(frames+1):
    t=index/fps;w=camera_weight(t)
    pose=u.AnimPoseExtensions.get_anim_pose_at_time(clean_clip,t%clean_clip.get_play_length(),u.AnimPoseEvaluationOptions())
    brush=pose.get_bone_pose('MenuBrush_brush',u.AnimPoseSpaces.WORLD).translation
    hand=pose.get_bone_pose('hand_l',u.AnimPoseSpaces.WORLD).translation
    work=clean_world.transform_location(brush*.7+hand*.3)+u.Vector(0,0,6)
    target=work*(1-w)+target_camera*w
    aim=tuple(observer_world.inverse_transform_location(target).to_tuple())
    center=tuple(((rest[EYES[0]].translation+rest[EYES[1]].translation)*.5).to_tuple());d=norm(sub3(aim,center))
    yaw=math.degrees(math.atan2(dot(d,right),dot(d,forward)));pitch=math.degrees(math.atan2(dot(d,up),math.hypot(dot(d,forward),dot(d,right))))
    # Keep the head in its reference pose; only the eyes follow attention.
    hy=0.;hp=0.
    delta=qm(axis(up,hy),axis(right,-hp));head_q=qm(qm(qi(head_parent_q),delta),head_rest_q)
    for eye in EYES:
        eye_rest_q=qt(rest[eye].rotation);eye_parent_q=qt(rest[parents[eye]].rotation)
        new_parent_q=qm(delta,eye_parent_q)
        eye_pos=tuple(rest[eye].translation.to_tuple());pivot=tuple(rest[HEAD].translation.to_tuple());rel=qv(delta,sub3(eye_pos,pivot));eye_pos=tuple(a+b for a,b in zip(pivot,rel))
        # Aim in the rotated head frame so its rotation is not applied twice.
        eye_d=qv(qi(delta),norm(sub3(aim,eye_pos)))
        ey=limited(math.degrees(math.atan2(dot(eye_d,right),dot(eye_d,forward))),-35.,35.)
        ep=limited(math.degrees(math.atan2(dot(eye_d,up),math.hypot(dot(eye_d,forward),dot(eye_d,right)))),-28.,28.)
        eye_delta=qm(delta,qm(axis(up,ey),axis(right,-ep)))
        eye_q=qm(qm(qi(new_parent_q),eye_delta),eye_rest_q);tracks[eye].append(eye_q)
    closure=max(math.sin(math.pi*limited((t-(center-.09))/.22,0.,1.))**2 if center-.09<=t<=center+.13 else 0. for center in blink_centers)
    times.append(t);blink.append(closure);delight.append((.94+.06*math.cos(t*math.tau/length))*(1-closure))
    if index%fps==0:samples.append({'t':t,'camera_weight':w,'head_yaw':hy,'head_pitch':hp,'target':list(target.to_tuple()),'work':list(work.to_tuple())})
# Close the final eight frames smoothly to the exact initial pose.
for n,values in tracks.items():
    correction=qm(qi(values[-1]),values[0])
    for step in range(1,9):
        k=len(values)-9+step;f=step/8;f=f*f*(3-2*f)
        sign=1. if correction[3]>=0 else -1.
        blended=tuple((1-f)*a+f*b*sign for a,b in zip((0.,0.,0.,1.),correction))
        values[k]=qm(values[k],norm(blended))
    values[-1]=values[0]
controller=clip.controller;controller.open_bracket('Menu observer: watch cleaning, glance at camera and blink',False)
try:
    controller.set_number_of_frames(u.FrameNumber(frames),False)
    for n in names:
        tr=local[n];rotations=[uq(v) for v in tracks[n]] if n in tracks else [tr.rotation]
        assert controller.set_bone_track_keys(n,[tr.translation]*len(rotations),rotations,[tr.scale3d]*len(rotations),False),n
    u.AnimationLibrary.remove_all_curve_data(clip)
    for n,values in [('Eyes_delight',delight),('Eyes_Blink',blink)]:
        u.AnimationLibrary.add_curve(clip,n,u.RawCurveTrackTypes.RCT_FLOAT,False);u.AnimationLibrary.add_float_curve_keys(clip,n,times,values)
finally:controller.close_bracket(False)
assert u.EditorAssetLibrary.save_loaded_asset(clip)
c.set_animation(clip);data=c.get_editor_property('animation_data');data.anim_to_play=clip;data.saved_playing=True;data.saved_looping=True;data.saved_position=0.;c.set_editor_property('animation_data',data);c.set_position(0.,False)
assert ls.save_current_level()
report.update(duration=length,frames=frames,animated_bones=EYES,blink_centers=blink_centers,camera_glances=[[5.1,7.85],[13.,15.4]],samples=samples,body_stationary=True,preload='Existing saved mesh and clip references retained; no new runtime dependencies.')
(OUT/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('MENU_OBSERVER_GAZE_SAVED',json.dumps({k:report[k] for k in ['duration','frames','animated_bones','camera_glances','body_stationary']}))




