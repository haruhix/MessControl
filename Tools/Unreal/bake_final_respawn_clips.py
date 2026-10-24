"""Root-invoked final sandbox body clips. Importing performs no Unreal calls.

Only preview mesh/skeleton and final clips under the sandbox Performance folder
are created/saved. Production clips, skeleton and mesh are read-only.
Root owns Editor identity, lifetime, authoring, rendering and visual acceptance.
"""
import json
import math
from pathlib import Path

DEST = '/Game/Tests/RespawnReplacement'
PERFORMANCE = DEST + '/Performance'
OUT = Path('E:/CodexWork/RespawnReplacementFinal_20261024')
MESH = '/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay'
OLD = '/Game/Gameplay/Character/Animations/Teeth3/A_Teeth_Tired'
NEW = '/Game/Gameplay/Character/Animations/Character2/A_Teeth_Happy_Jump'
FPS = 30

def _sat(x): return min(1.0, max(0.0, x))
def _smooth(a, b, x):
    t = _sat((x-a)/(b-a))
    return t*t*(3.0-2.0*t)
def _mix(a, b, t): return tuple(x+(y-x)*t for x,y in zip(a,b))
def _norm(q):
    m = math.sqrt(sum(v*v for v in q))
    assert m > 1e-10
    return tuple(v/m for v in q)
def _qb(a, b, t):
    if sum(x*y for x,y in zip(a,b)) < 0: b = tuple(-v for v in b)
    return _norm(_mix(a,b,t))
def _qm(a,b):
    x,y,z,w=a; X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)
def _qv(q,v):
    t=_qm(_qm(q,(*v,0.0)),(-q[0],-q[1],-q[2],q[3]))
    return t[:3]
def _source_time(frame):
    if frame <= 25: return 0.0
    if frame < 40: return .24*_smooth(25,40,frame)
    if frame < 63: return .24+(.95-.24)*(frame-40)/23.0
    if frame < 77: return .95+(1.25-.95)*_smooth(63,77,frame)
    return 1.25
def _blink(frame, start, peak, end):
    return _smooth(start,peak,frame)*(1.0-_smooth(peak,end,frame))
def _facial(name):
    # Matches the gameplay policy that keeps facial controls independent of emotes.
    return name.startswith('c_') and not any(
        finger in name for finger in ('index','middle','thumb','ring','pinky'))

def bake_clips(root_folder=DEST, end_frame=180):
    import unreal as u
    assert str(root_folder).rstrip('/') == DEST
    assert isinstance(end_frame,int) and end_frame >= 100
    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
    lib=u.EditorAssetLibrary
    mesh=lib.load_asset(MESH); assert isinstance(mesh,u.SkeletalMesh)
    source_skeleton=mesh.get_editor_property('skeleton')
    old=lib.load_asset(OLD); new=lib.load_asset(NEW)
    for source in (old,new):
        assert isinstance(source,u.AnimSequence) and source.get_editor_property('skeleton') == source_skeleton
    assert new.get_play_length() >= 1.25
    ref=source_skeleton.get_reference_pose(); names=list(ref.get_bone_names())
    assert {'root','root_x','foot_l','foot_r'}.issubset(set(map(str,names)))
    def numeric(tr):
        return (tuple(tr.translation.to_tuple()),tuple(tr.rotation.to_tuple()),
                tuple(tr.scale3d.to_tuple()))
    rest={str(n):numeric(ref.get_bone_pose(n,u.AnimPoseSpaces.LOCAL)) for n in names}
    options=u.AnimPoseEvaluationOptions()
    options.set_editor_property('optional_skeletal_mesh',mesh)
    options.set_editor_property('extract_root_motion',False)
    options.set_editor_property('evaluate_curves',False)
    cache={}
    def sample(source,time):
        key=(source.get_path_name(),round(time,8))
        if key not in cache:
            p=u.AnimPoseExtensions.get_anim_pose_at_time(source,time,options)
            assert u.AnimPoseExtensions.is_valid(p)
            cache[key]={str(n):numeric(p.get_bone_pose(n,u.AnimPoseSpaces.LOCAL))
                        for n in names}
        return cache[key]
    tired=sample(old,0.0)
    bone_editor=u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
    parents={str(n):str(bone_editor.get_bone_parent(mesh,n)) for n in names}
    def world_pose(pose):
        done={}
        def get(n):
            if n in done:return done[n]
            t,q,s=pose[n];parent=parents[n]
            if parent in pose:
                pt,pq,ps=get(parent);v=_qv(pq,tuple(a*b for a,b in zip(t,ps)))
                t=tuple(a+b for a,b in zip(pt,v));q=_qm(pq,q);s=tuple(a*b for a,b in zip(ps,s))
            done[n]=(t,q,s);return done[n]
        for n in pose:get(n)
        return done
    def settle_hands(pose,weight,dead=False):
        if weight<=0:return
        world=world_pose(pose)
        for side,sign in [('l',1),('r',-1)]:
            name='hand_'+side
            assert name in pose
            pt,pq,ps=world[parents[name]]
            target=(sign*44.,22. if dead else 7.,26. if dead else 41.)
            delta=tuple(a-b for a,b in zip(target,pt));local=_qv((-pq[0],-pq[1],-pq[2],pq[3]),delta)
            local=tuple(a/b for a,b in zip(local,ps))
            tr,rot,scale=pose[name];pose[name]=(_mix(tr,local,weight),rot,scale)
    old_frames=[]; new_frames=[]
    for frame in range(end_frame+1):
        sampled=sample(new,_source_time(frame))
        settle=_smooth(77,96,frame)
        old_pose={}; new_pose={}
        for name in map(str,names):
            if name in ('root','root_x'):
                old_pose[name]=rest[name];new_pose[name]=rest[name];continue
            a=sampled[name]; b=tired[name]
            if _facial(name):
                # Face is authored by the existing morphs below, not the emote rig.
                old_pose[name]=rest[name];new_pose[name]=rest[name];continue
            old_pose[name]=b
            new_pose[name]=(_mix(a[0],b[0],settle),_qb(a[1],b[1],settle),
                            _mix(a[2],b[2],settle))
        settle_hands(old_pose,_smooth(5,20,frame),True)
        settle_hands(new_pose,_smooth(77,96,frame),False)
        old_frames.append(old_pose);new_frames.append(new_pose)

    # Exact preview-only duplicates allow morph metadata without ever touching
    # the production skeleton, whose current metadata is intentionally empty.
    # Original mesh remains the evaluation mesh for every source pose above.
    skeleton_path=PERFORMANCE+'/SKEL_ReplacementPreview'
    preview_path=PERFORMANCE+'/SK_ReplacementPreview'
    sk=lib.load_asset(skeleton_path) if lib.does_asset_exist(skeleton_path) else lib.duplicate_asset(
        source_skeleton.get_path_name(),skeleton_path)
    preview=lib.load_asset(preview_path) if lib.does_asset_exist(preview_path) else lib.duplicate_asset(
        mesh.get_path_name(),preview_path)
    assert isinstance(sk,u.Skeleton) and sk != source_skeleton
    assert isinstance(preview,u.SkeletalMesh) and preview != mesh
    assert sk.get_path_name().split('.')[0] == skeleton_path
    assert preview.get_path_name().split('.')[0] == preview_path
    assert list(map(str,sk.get_reference_pose().get_bone_names())) == list(map(str,names))
    preview.call_method('SetSkeleton',args=(sk,))
    assert [str(s.material_interface.get_path_name()) if s.material_interface else None
            for s in preview.get_editor_property('materials')] == [
                str(s.material_interface.get_path_name()) if s.material_interface else None
                for s in mesh.get_editor_property('materials')]
    morphs={str(target.get_name()) for target in mesh.get_editor_property('morph_targets')}
    assert 'Eyes_Blink' in morphs, 'The required dead-body closed eyes need the production blink morph'
    assert 'Mouth_Smile' in morphs, 'The required alive-body expression needs the production smile morph'
    for name in ['Eyes_Blink','Eyes_Scary','Mouth_Smile','BlinkCancel_Mouth_Smile']:
        if name in morphs:
            u.AnimationLibrary.add_curve_meta_data(sk,name)
            u.AnimationLibrary.set_curve_meta_data_morph_target(sk,name,True)
    assert lib.save_loaded_asset(sk,only_if_is_dirty=False)
    assert lib.save_loaded_asset(preview,only_if_is_dirty=False)
    meta={str(n) for n in u.AnimationLibrary.get_curve_meta_data_names(sk)}
    allowed={n for n in morphs.intersection(meta)
             if u.AnimationLibrary.get_curve_meta_data_morph_target(sk,n)}
    skipped=[]
    def curves(is_old):
        result={}
        candidates=['Eyes_Blink','Eyes_Scary','Mouth_Smile',
                    'BlinkCancel_Mouth_Smile']
        for name in candidates:
            if name not in allowed:
                skipped.append(name);continue
            values=[]
            for f in range(end_frame+1):
                if is_old:
                    value=.97*_smooth(5,14,f) if name=='Eyes_Blink' else 0.0
                else:
                    blink=max(_blink(f,63,66,71),_blink(f,119,122,126))
                    smile=.3*_smooth(82,98,f)
                    if name=='Eyes_Blink': value=blink
                    elif name=='Eyes_Scary':
                        # These shapes intersect when combined at a half blink.
                        # Fade the expression fully out before either blink.
                        safe_blink=(1-_smooth(57,62,f)+_smooth(72,83,f))
                        safe_blink*=1-_smooth(113,118,f)+_smooth(127,136,f)
                        value=(.12+.12*_smooth(36,45,f)*(1-_smooth(65,83,f)))*safe_blink
                    elif name=='Mouth_Smile': value=smile
                    else: value=smile*blink
                values.append(value)
            result[name]=values
        return result

    assets=u.AssetToolsHelpers.get_asset_tools()
    def bake(name,frames,curve_values):
        path=PERFORMANCE+'/'+name
        clip=lib.load_asset(path) if lib.does_asset_exist(path) else None
        if not clip:
            factory=u.AnimSequenceFactory()
            factory.target_skeleton=sk;factory.preview_skeletal_mesh=preview
            clip=assets.create_asset(name,PERFORMANCE,u.AnimSequence,factory)
        assert isinstance(clip,u.AnimSequence) and clip.get_editor_property('skeleton') == sk
        assert clip.get_path_name().split('.')[0] == path
        c=clip.controller;c.open_bracket('Final replacement poses and recovery',False)
        try:
            c.remove_all_bone_tracks(False)
            c.set_frame_rate(u.FrameRate(FPS,1),False)
            c.set_number_of_frames(u.FrameNumber(end_frame),False)
            for n in names:
                data=[p[str(n)] for p in frames]
                assert c.add_bone_curve(n,False)
                assert c.set_bone_track_keys(n,[u.Vector(*v[0]) for v in data],
                    [u.Quat(*v[1]) for v in data],[u.Vector(*v[2]) for v in data],False)
        finally: c.close_bracket(False)
        u.AnimationLibrary.remove_all_curve_data(clip)
        times=[i/FPS for i in range(end_frame+1)]
        for n,values in curve_values.items():
            u.AnimationLibrary.add_curve(clip,n,u.RawCurveTrackTypes.RCT_FLOAT,False)
            u.AnimationLibrary.add_float_curve_keys(clip,n,times,values)
        clip.set_editor_property('enable_root_motion',False)
        clip.set_editor_property('force_root_lock',False)
        assert abs(clip.get_play_length()-end_frame/FPS) < .0001
        assert lib.save_loaded_asset(clip,only_if_is_dirty=False)
        return clip
    old_clip=bake('A_OldBody_Final',old_frames,curves(True))
    new_clip=bake('A_NewBody_Final',new_frames,curves(False))

    # Bone-based contact guide, not proof of skinned-mesh sole contact.
    editor=u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
    parents={str(n):str(editor.get_bone_parent(mesh,n)) for n in names}
    def component_pose(pose):
        done={}
        def get(n):
            if n in done:return done[n]
            t,q,s=pose[n]; parent=parents[n]
            if parent in pose:
                pt,pq,ps=get(parent)
                rt=_qv(pq,tuple(a*b for a,b in zip(t,ps)))
                t=tuple(a+b for a,b in zip(pt,rt));q=_qm(pq,q)
                s=tuple(a*b for a,b in zip(ps,s))
            done[n]=(t,q,s)
            return done[n]
        for n in pose:get(n)
        return done
    ref_world=component_pose(rest)
    foot_names=['foot_l','foot_r']
    ref_min=min(ref_world[n][0][2] for n in foot_names)
    contacts=[]
    for f,p in enumerate(new_frames):
        world=component_pose(p)
        minimum=min(world[n][0][2] for n in foot_names)
        contacts.append({'frame':f,'min_foot_bone_z':minimum,
                         'actor_z_correction_to_reference_feet':ref_min-minimum})
    OUT.mkdir(parents=True,exist_ok=True)
    report={'old':old_clip.get_path_name(),'new':new_clip.get_path_name(),
            'fps':FPS,'end_frame':end_frame,'new_recovery_frames':[77,96],
            'face_curves':sorted(allowed.intersection(
                {'Eyes_Blink','Eyes_Scary','Mouth_Smile','BlinkCancel_Mouth_Smile'})),
            'skipped_unsupported_morphs':sorted(set(skipped)),
            'preview_mesh':preview.get_path_name(),
            'preview_skeleton':sk.get_path_name(),
            'production_source_or_skeleton_saved':False,
            'contact_guide':contacts,
            'contact_guide_limit':'Bone points only. Root must visually check skinned soles, actor rotation and stage surface.',
            'validation':'Authored by root invocation; visual acceptance remains pending.'}
    (OUT/'FinalClips.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    u.log('MC_REPLACEMENT_FINAL_CLIPS_SAVED '+json.dumps({
        k:report[k] for k in ('old','new','face_curves','skipped_unsupported_morphs')}))
    return {'old_clip':old_clip,'new_clip':new_clip,
            'preview_mesh':preview,'preview_skeleton':sk}
