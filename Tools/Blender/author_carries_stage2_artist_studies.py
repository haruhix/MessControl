"""Artist-motion studies for the asymmetric stage-two boss, Blender only.

The original stacked ARP pose stays frozen. Only the dedicated BoneBrushRig
animates the body/limbs. Existing native wrist motion supplies slam/idle timing
and arcs; contacts, proportions and the broad sweep are authored for this form.
"""
import json,math,sys
from pathlib import Path
import bpy
from mathutils import Vector,Quaternion,Matrix

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).parent))
import author_wizard_mixamo_pool as helpers

SOURCE=Path('C:/Users/user/Downloads/Telegram Desktop/cARRIES_sTADY2.blend')
MOTION_JSON=ROOT/'Saved/Codex/CarriesArtistMotionSamples.json'
DEST=ROOT/'ArtSource/CarriesStage2/ArtistReview'
BLEND=DEST/'Carries_Stage2_ArtistStudies_v2.blend'
REPORT=DEST/'AuthorReport_v2.json'
KEEP={'SM_Character','SM_Character.001','SM_Character_006_002_003.004','SM_Character_006_002_003.005'}
AXES=[Vector(v)for v in ((1,0,0),(0,1,0),(0,0,1))]


def envelope(frame,keys):
    return helpers.author.remap(frame,keys)


def native_point(motions,action,frame,bone='hand.l'):
    seq=motions['actions'][action]['frames']
    start=motions['actions'][action]['start']
    index=max(0,min(len(seq)-1,frame-start))
    lo=int(math.floor(index));hi=min(lo+1,len(seq)-1)
    return Vector(seq[lo]['bones'][bone]['head']).lerp(Vector(seq[hi]['bones'][bone]['head']),index-lo)


def main():
    assert bpy.app.background and not BLEND.exists(),'Preserve previous revisions'
    source_hash=helpers.author.sha(SOURCE)
    motions=json.loads(MOTION_JSON.read_text(encoding='utf8'))
    artist_source=Path(motions['source'])
    artist_hash=helpers.author.sha(artist_source)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene,rig,primary,mesh=bpy.context.scene,bpy.data.objects['BoneBrushRig'],bpy.data.objects['rig'],bpy.data.objects['SM_Character']
    scene.frame_set(13)
    defaults=helpers.defaults(rig)
    originals={a.name:helpers.signature(a)for a in bpy.data.actions}
    for action in bpy.data.actions:action.use_fake_user=True
    frozen=helpers.defaults(primary)
    helpers.author.bind(primary,None,frozen)
    primary['MC_stage2_body_pose_frozen']=True
    primary['MC_stage2_frozen_source_frame']=13
    # Small separate face pieces must follow the same body deformation as skin.
    for name in KEEP-{'SM_Character'}:
        obj=bpy.data.objects[name]
        group=obj.vertex_groups.get('segment_001')or obj.vertex_groups.new(name='segment_001')
        if obj.data.vertices:group.add(list(range(len(obj.data.vertices))),1.,'REPLACE')
        mod=obj.modifiers.new('MC Stage2 face follow','ARMATURE');mod.object=rig
    for obj in bpy.data.objects:
        if obj.type=='MESH':obj.hide_render=obj.name not in KEEP
        if obj.name in KEEP:obj.hide_set(False)
    controls=[p.name for p in rig.pose.bones if p.name.startswith('CTRL-')]
    bases={n:defaults[n]['basis'].decompose()for n in controls}
    world_rot={n:(rig.matrix_world@rig.pose.bones[n].matrix).to_quaternion()for n in controls}
    base_head={n:rig.matrix_world@rig.pose.bones[n].head for n in controls}
    basis_world={n:(rig.matrix_world@rig.pose.bones[n].matrix@defaults[n]['basis'].inverted()).to_3x3()for n in controls}
    dg=bpy.context.evaluated_depsgraph_get()
    foot_ids=[v.index for v in mesh.data.vertices if sum(g.weight for g in v.groups if mesh.vertex_groups[g.group].name.startswith(('segment_006','segment_007')))> .5]
    hand_ids=[v.index for v in mesh.data.vertices if sum(g.weight for g in v.groups if mesh.vertex_groups[g.group].name.startswith(('segment_008','segment_009','segment_010','segment_011')))> .55]
    assert foot_ids and hand_ids
    evaluated=mesh.evaluated_get(dg)
    floor=min((evaluated.matrix_world@evaluated.data.vertices[i].co).z for i in foot_ids)
    wrist=Vector((.8156,-1.05,.42))
    configs=[
        dict(name='Idle',labelRU='Живая стойка',useRU='Тело дышит, тяжёлая кисть мягко догоняет движение.',frames=120,event_frames={'breath':37,'hand_lag':62},source_motion='idle'),
        dict(name='GroundSlam',labelRU='Тяжёлый удар ладонью',useRU='Медленный замах, быстрый удар, удержание контакта и тяжёлый возврат.',frames=120,event_frames={'load':32,'apex':46,'contact':55,'press':61,'recover':91},source_motion='hit_atack_01'),
        dict(name='Sweep',labelRU='Широкий боковой взмах',useRU='Замах корпусом, удар по широкой дуге и инерционный перебег руки.',frames=108,event_frames={'load':34,'strike':48,'follow':62,'recover':89},source_motion='atack_aoe'),
        dict(name='Hurt',labelRU='Реакция на попадание',useRU='Корпус принимает удар первым; большая рука запаздывает и успокаивается.',frames=84,event_frames={'impact':14,'hand_lag':19,'overshoot':30,'settle':65},source_motion=None),
    ]
    contact_adjust=0.;reach_clamps=[];records_by_name={};foot_positions={}

    def make_pose(case,frame):
        body=Vector();angles=Vector();target=wrist.copy();hand_angles=Vector();grip=0.
        kind=case['name']
        if kind=='Idle':
            sf=(frame-1)*150/(case['frames']-1)
            blend=helpers.author.smooth(max(0,(frame-105)/15))
            hp=native_point(motions,'idle',sf);h0=native_point(motions,'idle',0)
            hp=hp.lerp(h0,blend)
            target+=(hp-h0)*.13
            bp=native_point(motions,'idle',sf,'head.x')-native_point(motions,'idle',0,'head.x')
            body=Vector((bp.x*.2,bp.y*.12,bp.z*.3))*(1-blend)
            angles=Vector((1.1*math.sin((frame-1)*math.tau/119),.6*math.sin((frame-1)*math.tau/119),0))
        elif kind=='GroundSlam':
            sf=envelope(frame,[(1,37),(39,37),(46,39),(55,46),(61,46),(101,84),(120,84)])
            src=native_point(motions,'hit_atack_01',sf)
            end=native_point(motions,'hit_atack_01',84)
            high=max(0,(src.z-end.z)/(8.575-end.z))
            low=max(0,min(1,(end.z-src.z)/(end.z-.128222)))
            desired=Vector((wrist.x+.055*(src.x-end.x),wrist.y+1.15*high+.055*(src.y-end.y),wrist.z+1.3*high-.25*low))
            active=envelope(frame,[(1,0),(12,0),(39,1),(101,1),(120,0)])
            target=wrist.lerp(desired,active)
            target.x+=.30*high*active
            # Calibration comes from actual skin contact, rather than wrist Z.
            target.z+=contact_adjust*envelope(frame,[(1,0),(49,0),(55,1),(61,1),(72,0),(120,0)])
            body.y=envelope(frame,[(1,0),(34,.055),(47,.04),(58,-.065),(66,-.05),(91,.012),(120,0)])
            body.z=envelope(frame,[(1,0),(36,.025),(48,.02),(58,-.048),(66,-.025),(90,.014),(120,0)])
            angles.x=envelope(frame,[(1,0),(34,-8),(46,-6),(58,11),(66,8),(91,-2),(120,0)])
            angles.y=envelope(frame,[(1,0),(37,-3),(58,4),(82,1),(120,0)])
            hand_angles.x=envelope(frame,[(1,0),(38,8),(46,5),(54,-18),(63,-18),(85,4),(120,0)])
            grip=envelope(frame,[(1,0),(37,.015),(48,0),(55,0),(64,0),(78,.02),(120,0)])
        elif kind=='Sweep':
            phi=math.radians(envelope(frame,[(1,-72),(14,-72),(35,-15),(40,-15),(54,-138),(65,-155),(104,-72),(108,-72)]))
            radius=envelope(frame,[(1,1.108),(14,1.108),(35,1.0),(65,1.0),(104,1.108),(108,1.108)])
            # Polar interpolation guarantees a wide front arc, never a chord
            # passing through the face. Native AOE adds a small vertical accent.
            sf=envelope(frame,[(1,0),(35,48),(54,80),(65,96),(104,176),(108,199)])
            src=native_point(motions,'atack_aoe',sf)
            z=envelope(frame,[(1,.42),(14,.42),(35,.73),(49,.85),(64,.7),(104,.42),(108,.42)])
            accent=math.sin(math.pi*max(0,min(1,(frame-14)/90)))
            target=Vector((.463+radius*math.cos(phi),.009+radius*math.sin(phi),z+.025*(src.z-2.5)*accent))
            if frame>=104:target=wrist.copy()
            body.x=envelope(frame,[(1,0),(35,-.025),(57,.032),(72,.022),(108,0)])
            body.z=envelope(frame,[(1,0),(35,-.02),(53,.01),(70,-.01),(108,0)])
            angles.z=envelope(frame,[(1,0),(35,-12),(42,-11),(62,18),(71,15),(108,0)])
            angles.x=envelope(frame,[(1,0),(35,-3),(57,6),(75,3),(108,0)])
            hand_angles.z=envelope(frame,[(1,0),(35,30),(54,-35),(65,-55),(105,0),(108,0)])
            hand_angles.x=envelope(frame,[(1,0),(35,-6),(55,-12),(74,-7),(108,0)])
        elif kind=='Hurt':
            body.y=envelope(frame,[(1,0),(10,0),(14,.075),(20,.055),(30,-.021),(60,0),(84,0)])
            body.z=envelope(frame,[(1,0),(10,0),(16,-.025),(28,.01),(64,0),(84,0)])
            angles.x=envelope(frame,[(1,0),(10,0),(14,-18),(20,-12),(30,5),(64,0),(84,0)])
            angles.y=envelope(frame,[(1,0),(11,0),(16,-7),(24,-4),(35,2),(63,0),(84,0)])
            target+=Vector((envelope(frame,[(1,0),(14,0),(20,.10),(30,-.025),(65,0),(84,0)]),
                envelope(frame,[(1,0),(14,0),(20,.14),(32,-.045),(67,0),(84,0)]),
                envelope(frame,[(1,0),(14,0),(20,.16),(33,-.04),(68,0),(84,0)])))
            hand_angles.x=envelope(frame,[(1,0),(14,0),(22,8),(34,-3),(67,0),(84,0)])
        pose={n:(v[0].copy(),v[1].copy(),v[2].copy())for n,v in bases.items()}
        for name,delta,degrees in [('CTRL-body',body,angles),('CTRL-ik_segment',target-base_head['CTRL-ik_segment'],hand_angles)]:
            loc,rot,scale=pose[name]
            q=Quaternion()
            for axis,angle in zip(AXES,degrees):q=Quaternion(axis,math.radians(angle))@q
            local=basis_world[name].to_quaternion().inverted()@q@world_rot[name]
            pose[name]=(loc+basis_world[name].inverted()@delta,local,scale)
        for name in ('CTRL-ik_segment.L','CTRL-ik_segment.L_01','CTRL-ik_segment.L_02'):
            loc,rot,scale=pose[name]
            # A very small local fingertip relaxation, inherited from wrist.
            pose[name]=(loc+Vector((0,0,grip)),rot,scale)
        helpers.author.bind(rig,None,defaults)
        for name,values in pose.items():rig.pose.bones[name].matrix_basis=Matrix.LocRotScale(*values)
        bpy.context.view_layer.update()
        shoulder=rig.matrix_world@rig.pose.bones['segment_002_01'].head
        distance=(target-shoulder).length
        if distance>1.36:
            limited=shoulder+(target-shoulder).normalized()*1.36
            name='CTRL-ik_segment';loc,rot,scale=pose[name]
            loc+=basis_world[name].inverted()@(limited-target)
            pose[name]=(loc,rot,scale);rig.pose.bones[name].matrix_basis=Matrix.LocRotScale(*pose[name])
            bpy.context.view_layer.update();reach_clamps.append(dict(clip=kind,frame=frame,requested=distance))
        return pose

    # One actual palm/fingertip surface measurement calibrates the slam contact.
    make_pose(configs[1],55)
    evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
    contact_min=min((evaluated.matrix_world@evaluated.data.vertices[i].co).z for i in hand_ids)
    contact_adjust=floor+.008-contact_min
    assert abs(contact_adjust)<.3,('Unexpected skin contact offset',contact_adjust)
    for case in configs:
        records=[]
        for frame in range(1,case['frames']+1):
            records.append((make_pose(case,frame),{}))
        action=bpy.data.actions.new('MC_Stage2_'+case['name']);action.use_fake_user=True
        helpers.bake(rig,action,defaults,records)
        case['action']=action.name
        case['source_method']='Native artist wrist arc adapted to custom IK and authored contacts/body timing' if case['source_motion'] else 'Authored body recoil and delayed heavy-hand recovery'
        records_by_name[case['name']]=records
        print('CARRIES_STUDY_READY',case['name'],case['frames'],flush=True)
    # Build two fixed cameras from all evaluated poses, including accessories.
    scene.render.engine='BLENDER_WORKBENCH';scene.display.render_aa='5'
    scene.display.shading.light='STUDIO';scene.display.shading.studio_light='paint.sl'
    scene.display.shading.color_type='TEXTURE';scene.display.shading.show_shadows=True
    scene.display.shading.show_cavity=True;scene.display.shading.cavity_type='BOTH'
    scene.display.shading.background_type='WORLD';scene.world.color=(.10,.12,.15)
    scene.render.resolution_x,scene.render.resolution_y=800,600;scene.render.resolution_percentage=100
    scene.render.fps=30
    bounds=[]
    for case in configs:
        helpers.author.bind(rig,bpy.data.actions[case['action']],defaults)
        for frame in range(1,case['frames']+1):
            scene.frame_set(frame)
            dg=bpy.context.evaluated_depsgraph_get()
            for name in KEEP:
                obj=bpy.data.objects[name].evaluated_get(dg)
                bounds += [obj.matrix_world@Vector(corner)for corner in obj.bound_box]
    lo=Vector([min(v[i]for v in bounds)for i in range(3)])
    hi=Vector([max(v[i]for v in bounds)for i in range(3)])
    focus=(lo+hi)*.5; cameras={}
    for view,offset in [('ThreeQuarter',(3,-7,2.4)),('Front',(0,-7,1.6))]:
        cam=bpy.data.objects.new('MC_Carries_'+view,bpy.data.cameras.new('MC_Carries_'+view));scene.collection.objects.link(cam)
        cam.location=focus+Vector(offset);cam.rotation_euler=(focus-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO'
        bpy.context.view_layer.update();inverse=cam.matrix_world.inverted()
        cam.data.ortho_scale=max(max(2*abs((inverse@v).x)/.9,2*abs((inverse@v).y)*(800/600)/.9)for v in bounds)
        cameras[view]=dict(name=cam.name,scale=cam.data.ortho_scale)
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,floor-.002))
    plane=bpy.context.object;plane.name='MC_Carries_ReviewFloor'
    mat=bpy.data.materials.new('MC_Carries_ReviewFloor');mat.diffuse_color=(.23,.26,.31,1);plane.data.materials.append(mat)
    scene.camera=bpy.data.objects[cameras['ThreeQuarter']['name']]
    helpers.author.bind(rig,None,defaults)
    track=rig.animation_data.nla_tracks.new();track.name='MC_Stage2_AllStudies'
    scene.timeline_markers.clear();cursor=1
    for case in configs:
        action=bpy.data.actions[case['action']]
        strip=track.strips.new(case['name'],cursor,action);strip.action_frame_start=1;strip.action_frame_end=case['frames']+1
        strip.frame_end=cursor+case['frames'];strip.action_slot=action.slots[0]
        strip.blend_type='REPLACE';strip.extrapolation='NOTHING'
        strip.blend_in=0;strip.blend_out=0;strip.use_auto_blend=False
        scene.timeline_markers.new(case['name'],frame=cursor)
        case['preview_start']=cursor;case['preview_end']=cursor+case['frames']-1;cursor+=case['frames']
    rig.animation_data.use_nla=True;track.mute=False
    scene.frame_start,scene.frame_end=1,cursor-1;scene.frame_set(1)
    note=bpy.data.texts.new('MC_STAGE2_REVIEW_README')
    note.write('Four Blender-only motion studies for the supplied stage-two boss.\n'
        'Original ARP body pose frozen at source13; animate only BoneBrushRig.\n'
        'Slam/idle use adapted native artist wrist motion from BossPhase3ARP.\n'
        'Sweep uses a contact-safe polar arc and subtle native AOE accent.\n'
        'These are not newly downloaded Mixamo clips or Unreal-integrated assets.\n'
        'Two fixed review cameras; NLA track plays all four editable Actions.\n')
    assert all(helpers.signature(bpy.data.actions[n])==sig for n,sig in originals.items())
    DEST.mkdir(parents=True,exist_ok=True);bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
    assert helpers.author.sha(SOURCE)==source_hash and helpers.author.sha(artist_source)==artist_hash
    report=dict(passed=True,source=str(SOURCE),source_sha256=source_hash,blend=str(BLEND),blend_sha256=helpers.author.sha(BLEND),
        artist_motion_source=str(artist_source),artist_motion_source_sha256=artist_hash,artist_motion_samples=str(MOTION_JSON),
        fps=30,clips=configs,cameras=cameras,primary_arp_pose_frozen=True,original_actions_preserved=True,
        original_files_unchanged=True,new_actions_on='BoneBrushRig',skin_contact_floor=floor,skin_contact_offset=contact_adjust,
        reach_limit=1.36,reach_clamps=reach_clamps,bounds=[list(lo),list(hi)],timeline_frames=cursor-1,
        scope='Blender-only stage-two artist-motion studies; review before approval and any Unreal integration.',
        limitations=['Custom asymmetric-body adaptation, not automatic full humanoid Mixamo retarget.',
                      'Preview motions and contacts; gameplay hitboxes and VFX are not integrated.'])
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('CARRIES_ARTIST_STUDIES_READY',str(BLEND),flush=True)


if __name__=='__main__':main()
