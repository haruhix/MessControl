"""Author the third boss phase on the colleague's original exported skeleton.

Run Blender in a separate background process. No source package or source FBX
is modified, and the open artist scene is never used.
"""
import bpy
import hashlib
import json
import math
import sys
from pathlib import Path
from mathutils import Matrix, Vector, Euler

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/BossPhase3'
SOURCE = OUT / 'SK_Boss_stady3_Source.fbx'

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(SOURCE), use_anim=False,
                         automatic_bone_orientation=False)
scene = bpy.context.scene
rigs = [o for o in scene.objects if o.type == 'ARMATURE']
assert len(rigs) == 1, [o.name for o in rigs]
rig = rigs[0]
meshes = [o for o in scene.objects if o.type == 'MESH']
rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
parents = {b.name: b.parent.name if b.parent else None for b in rig.data.bones}

def values(v):
    return [round(float(x), 6) for x in v]

def inspection():
    bounds = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
    report = {
        'source': str(SOURCE), 'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'blender_version': bpy.app.version_string,
        'rig': rig.name, 'rig_world_matrix': [values(row) for row in rig.matrix_world],
        'bone_count': len(rest),
        'bounds_world': {'min': [min(v[i] for v in bounds) for i in range(3)],
                         'max': [max(v[i] for v in bounds) for i in range(3)]},
        'meshes': [{'name':o.name, 'vertices':len(o.data.vertices),
                    'shape_keys':[{'name':k.name,'value':k.value} for k in o.data.shape_keys.key_blocks] if o.data.shape_keys else [],
                    'world_matrix':[values(row) for row in o.matrix_world],
                    'vertex_groups':[g.name for g in o.vertex_groups],
                    'weights':{g.name:sum(1 for v in o.data.vertices if any(w.group==g.index and w.weight>.001 for w in v.groups)) for g in o.vertex_groups}}
                   for o in meshes],
        'bones': [{'name':b.name,'parent':parents[b.name], 'head':values(b.head_local),
                   'tail':values(b.tail_local), 'matrix':[values(row) for row in b.matrix_local],
                   'deform': b.use_deform} for b in rig.data.bones]
    }
    components=[]
    for obj in meshes:
        adjacent=[[] for v in obj.data.vertices]
        for edge in obj.data.edges:
            a,b=edge.vertices
            adjacent[a].append(b); adjacent[b].append(a)
        unseen=set(range(len(adjacent)))
        while unseen:
            pending=[unseen.pop()]; ids=[]
            while pending:
                i=pending.pop(); ids.append(i)
                for n in adjacent[i]:
                    if n in unseen:
                        unseen.remove(n); pending.append(n)
            pts=[obj.matrix_world @ obj.data.vertices[i].co for i in ids]
            components.append({'count':len(ids), 'ids':ids,
                'min':[min(v[i] for v in pts) for i in range(3)],
                'max':[max(v[i] for v in pts) for i in range(3)],
                'centroid':[sum(v[i] for v in pts)/len(pts) for i in range(3)]})
    report['components']=sorted(components,key=lambda c:-c['count'])
    (OUT/'RigInspection.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return report

facts=inspection()
print('MC_PHASE3_RIG_INSPECTION', json.dumps({k:v for k,v in facts.items() if k not in ('bones','components')}))
if '--inspect' in sys.argv:
    scene.render.engine='BLENDER_WORKBENCH'
    scene.render.resolution_x=900
    scene.render.resolution_y=900
    scene.render.resolution_percentage=100
    scene.display.shading.light='STUDIO'
    scene.display.shading.color_type='SINGLE'
    scene.display.shading.single_color=(.55,.64,.60)
    scene.display.shading.show_shadows=True
    scene.display.shading.show_cavity=True
    scene.display.shading.background_type='WORLD'
    scene.world=bpy.data.worlds.new('Phase3SourcePreview')
    scene.world.color=(.02,.03,.05)
    camera_data=bpy.data.cameras.new('InspectCamera')
    camera=bpy.data.objects.new('InspectCamera',camera_data)
    scene.collection.objects.link(camera)
    camera_data.type='ORTHO'
    camera_data.ortho_scale=13.5
    scene.camera=camera
    for label,point in [('Front',(0,-25,0)),('Side',(25,0,0)),('Top',(0,0,25))]:
        camera.location=point
        camera.rotation_euler=(-camera.location).to_track_quat('-Z','Y').to_euler()
        scene.render.filepath=str(OUT/('Source_'+label+'.png'))
        bpy.ops.render.render(write_still=True)
    sys.exit(0)

source_hash=facts['source_sha256']
source_min_z=facts['bounds_world']['min'][2]
normalization=2.4/(facts['bounds_world']['max'][2]-source_min_z)
def point(p):
    return Vector((p[0]*normalization,p[1]*normalization,(p[2]-source_min_z)*normalization))

# The colleague export is a rigid one-bone morph mesh. The derived asset gains
# articulation; the colleague source is kept exactly as exported.
original_positions={}
for obj in meshes:
    source_matrix=obj.matrix_world.copy()
    original_positions[obj.name]=[source_matrix @ v.co for v in obj.data.vertices]
    # Unreal's morph-capable FBX contains a Basis shape even when no morphs are
    # active. Normalize every shape alongside the basis so evaluation cannot
    # reintroduce the original centimeter coordinates.
    if obj.data.shape_keys:
        for block in obj.data.shape_keys.key_blocks:
            for vertex in block.data: vertex.co=point(source_matrix @ vertex.co)
    for vertex,p in zip(obj.data.vertices,original_positions[obj.name]):
        vertex.co=point(p)
    obj.parent=None
    obj.matrix_parent_inverse=Matrix.Identity(4)
    obj.matrix_basis=Matrix.Identity(4)
    obj.matrix_world=Matrix.Identity(4)
    for modifier in list(obj.modifiers):
        if modifier.type=='ARMATURE': obj.modifiers.remove(modifier)
    obj.vertex_groups.clear()
bpy.data.objects.remove(rig,do_unlink=True)
bpy.ops.object.armature_add(enter_editmode=True,location=(0,0,0))
rig=bpy.context.object
rig.name='Armature'
rig.data.name='BossPhase3Skeleton'
rig.data.edit_bones.remove(rig.data.edit_bones[0])
definitions=[
    ('root',None,(0,0,-3.123374),(0,0,-2.90)),
    ('pelvis','root',(0,.25,-1.35),(0,.2,-.55)),
    ('spine','pelvis',(0,.2,-.55),(0,.15,.60)),
    ('chest','spine',(0,.15,.60),(0,.10,1.55)),
    ('head','chest',(0,.10,1.55),(0,.05,2.9)),
    ('jaw','head',(0,-1.30,.98),(0,-1.82,.75)),
    ('brow_l','head',(.60,-2.04,1.82),(.90,-2.04,2.02)),
    ('brow_r','head',(-.60,-2.04,1.82),(-.90,-2.04,2.02)),
]
for side,sign in [('l',1),('r',-1)]:
    definitions.extend([
        ('clavicle_'+side,'chest',(sign*.85,.2,1.55),(sign*2.25,.30,1.65)),
        ('upperarm_'+side,'clavicle_'+side,(sign*2.25,.30,1.65),(sign*3.65,.30,.25)),
        ('forearm_'+side,'upperarm_'+side,(sign*3.65,.30,.25),(sign*4.65,.15,-.70)),
        ('hand_'+side,'forearm_'+side,(sign*4.65,.15,-.70),(sign*5.65,.10,-1.55)),
        ('thigh_'+side,'pelvis',(sign*.88,.25,-1.25),(sign*.90,.25,-2.22)),
        ('calf_'+side,'thigh_'+side,(sign*.90,.25,-2.22),(sign*.90,.15,-2.95)),
        ('foot_'+side,'calf_'+side,(sign*.90,.15,-2.95),(sign*.90,-.50,-3.03)),
    ])
for name,parent,head,tail in definitions:
    bone=rig.data.edit_bones.new(name)
    bone.head=point(head); bone.tail=point(tail)
    if parent: bone.parent=rig.data.edit_bones[parent]
    bone.use_deform=True
bpy.ops.object.mode_set(mode='OBJECT')
rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
parents={b.name:b.parent.name if b.parent else None for b in rig.data.bones}
scene.render.fps=30
for bone in rig.pose.bones: bone.rotation_mode='QUATERNION'

def clamp(x): return min(1.,max(0.,x))
def smooth(x):
    x=clamp(x); return x*x*(3-2*x)
def pulse(t,a,b,c):
    return smooth((t-a)/(b-a)) if t<b else 1-smooth((t-b)/(c-b))
def rotation(x=0,y=0,z=0):
    return Euler(tuple(math.radians(v) for v in (x,y,z)),'XYZ').to_matrix().to_4x4()
def pivot(p,transform):
    return Matrix.Translation(p) @ transform @ Matrix.Translation(-p)
def at(name,transform):
    return pivot(rest[name].translation,transform)

brow_ids={}
for comp in facts['components']:
    # The angular eyebrow strips are separate UV islands at the front of the
    # face. Coordinate tests also treat all coincident seam vertices equally.
    if comp['max'][1] < -2.0 and comp['min'][2] > 1.4:
        side='l' if comp['centroid'][0]>0 else 'r'
        for index in comp['ids']: brow_ids[index]='brow_'+side

def skin_weights(p,index):
    x,y,z=p; ax=abs(x); side='l' if x>=0 else 'r'
    if index in brow_ids: return {brow_ids[index]:1.}
    if ax>2.04:
        arm=smooth((ax-2.04)/.74)
        forearm=smooth((ax-3.32)/.90)
        hand=smooth((ax-4.46)/.74)
        return {'chest':1-arm,'upperarm_'+side:arm*(1-forearm),
                'forearm_'+side:arm*forearm*(1-hand),'hand_'+side:arm*forearm*hand}
    if z<-1.12:
        leg=smooth((-z-1.12)/.78)
        calf=smooth((-z-2.03)/.65)
        foot=smooth((-z-2.82)/.26)
        return {'pelvis':1-leg,'thigh_'+side:leg*(1-calf),
                'calf_'+side:leg*calf*(1-foot),'foot_'+side:leg*calf*foot}
    chest=smooth((z+.70)/1.20)
    head=smooth((z-.65)/1.0)
    jaw=(smooth((-y-1.20)/.60)*(1-smooth(ax/.96)) *
         smooth((z-.34)/.32)*(1-smooth((z-.95)/.32)))
    # The lower lip and nearby skin follow the soft jaw; upper teeth stay on
    # the upper face. The embedded head has no artificial visible neck.
    return {'pelvis':1-chest,'spine':chest*(1-head)*.35,
            'chest':chest*(1-head)*.65,'head':chest*head*(1-jaw),
            'jaw':chest*head*jaw}

for obj in meshes:
    groups={name:obj.vertex_groups.new(name=name) for name in rest}
    for vertex,p in zip(obj.data.vertices,original_positions[obj.name]):
        weights={n:w for n,w in skin_weights(p,vertex.index).items() if w>1e-5}
        total=sum(weights.values())
        assert total>.999
        for name,weight in weights.items(): groups[name].add([vertex.index],weight/total,'REPLACE')
    modifier=obj.modifiers.new('BossPhase3Articulation','ARMATURE')
    modifier.object=rig
    obj.parent=rig
    obj.matrix_parent_inverse=Matrix.Identity(4)
    obj.matrix_basis=Matrix.Identity(4)
    obj.name='BossPhase3Mesh'
    assert max(abs(obj.matrix_world[i][j]-(1 if i==j else 0)) for i in range(4) for j in range(4))<1e-6
    assert abs(max(v.co.z for v in obj.data.vertices)-2.4)<1e-5

def select_export(with_mesh):
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    if with_mesh:
        for obj in meshes: obj.select_set(True)
    bpy.context.view_layer.objects.active=rig

select_export(True)
scene.frame_start=1; scene.frame_end=1
bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_BossPhase3.fbx'),use_selection=True,
    object_types={'ARMATURE','MESH'},add_leaf_bones=False,
    use_armature_deform_only=False,bake_anim=False,mesh_smooth_type='FACE',
    axis_forward='-Y',axis_up='Z')
rig_manifest={
    'source_sha256':source_hash,'source_bones':1,'source_vertices':4662,
    'derived_bones':len(rest),'derived_height_cm':240,
    'derived_width_cm':(facts['bounds_world']['max'][0]-facts['bounds_world']['min'][0])*normalization*100,
    'forward_axis':'-Y','up_axis':'+Z','units':'Blender meters / FBX centimeters',
    'uniform_normalization':normalization,'ground_offset_source_units':-source_min_z,
    'root_translation_cm':[0,0,0],
    'mesh_file':'SK_BossPhase3.fbx','original_source_preserved':True,
    'bone_hierarchy':[{'name':n,'parent':parents[n],
                      'head_cm':values(rest[n].translation*100)} for n in rest],
}
(OUT/'DerivedRigReport.json').write_text(json.dumps(rig_manifest,indent=2),encoding='utf8')

def pose(kind,t,duration):
    phase=t*2*math.pi/2.4
    breath=math.sin(phase)
    body=Matrix.Translation((0,0,.013*breath)) @ at('pelvis',rotation(2+breath,0,breath*.7))
    torso=body @ at('spine',rotation(1,0,breath*.5))
    head_transform=torso @ at('head',rotation(-1+breath*.8,0,math.sin(phase)*.7))
    upper={'l':[-22,-22],'r':[-18,-20]}
    lower={'l':[-4,-30],'r':[-7,-34]}
    hand_angles={'l':[-4,0],'r':[-4,0]}
    hips={'l':-3.,'r':-3.}; knees={'l':6.,'r':6.}
    jaw=0.; brow=0.
    if kind=='Walk':
        a=t*2*math.pi/duration; step=math.sin(a)
        body=Matrix.Translation((.032*step,0,.020*math.cos(2*a))) @ at('pelvis',rotation(4,0,3*step))
        torso=body @ at('spine',rotation(2,0,-2*step))
        head_transform=torso @ at('head',rotation(-2,0,-2*step))
        for side,sign in [('l',1),('r',-1)]:
            v=step*sign; hips[side]=-27*v; knees[side]=4+40*max(0,-v)
            upper[side][0]+=7*v; upper[side][1]+=7*v
    elif kind in ('PunchLeft','PunchRight'):
        side='l' if kind=='PunchLeft' else 'r'; other='r' if side=='l' else 'l'
        sign=1 if side=='l' else -1
        impact=.85 if side=='l' else .95
        windup=smooth(t/(impact-.20))*(1-smooth((t-(impact-.20))/.20))
        strike=pulse(t,impact-.20,impact,impact+.32)
        upper[side]=[-22-13*windup-15*strike,-22+34*windup-58*strike]
        lower[side]=[-4-13*windup+4*strike,-30-33*windup+30*strike]
        hand_angles[side]=[-8+12*strike,-5]
        upper[other]=[-22,-27]; lower[other]=[-8,-43]
        body=Matrix.Translation((sign*.022*strike,-.065*strike,-.022*windup)) @ at('pelvis',rotation(2+9*strike,0,sign*(-8*windup+12*strike)))
        torso=body @ at('spine',rotation(2+3*strike,0,sign*(-4*windup+6*strike)))
        head_transform=torso @ at('head',rotation(-3,0,-sign*4*windup))
        jaw=2*strike; brow=3*windup
    elif kind=='Kick':
        chamber=pulse(t,.10,.70,1.40); kick=pulse(t,.80,1.12,1.44)
        hips['r']=-45*chamber-42*kick
        knees['r']=68*chamber*(1-kick)+5
        hips['l']=3*chamber; knees['l']=6+10*chamber
        body=Matrix.Translation((.060*chamber,0,-.045*chamber)) @ at('pelvis',rotation(2-13*kick,0,-4*chamber))
        torso=body @ at('spine',rotation(-4*kick,0,-3*chamber))
        head_transform=torso @ at('head',rotation(4*kick))
        upper['l']=[-27,-33-8*chamber]; upper['r']=[-23,-24+22*chamber]
    elif kind=='Hurt':
        flinch=pulse(t,0,.16,duration)
        body=Matrix.Translation((0,.070*flinch,-.035*flinch)) @ at('pelvis',rotation(2-13*flinch,0,-6*flinch))
        torso=body @ at('spine',rotation(-5*flinch,0,-4*flinch))
        head_transform=torso @ at('head',rotation(-5*flinch,0,5*flinch))
        upper['l'][0]-=14*flinch; upper['r'][0]-=19*flinch
        knees={s:6+16*flinch for s in knees}; jaw=4*flinch; brow=-3*flinch
    elif kind=='Roar':
        inhale=pulse(t,0,.72,1.50); roar=pulse(t,.75,1.5,4.55)
        shake=math.sin(t*27)*roar
        body=Matrix.Translation((0,.03*roar,-.060*inhale+.020*roar)) @ at('pelvis',rotation(2+9*inhale-8*roar,0,shake*.65))
        torso=body @ at('spine',rotation(4*inhale-6*roar,0,shake*.8))
        head_transform=torso @ at('head',rotation(-3*roar+shake*.5,0,shake*.6))
        for side in ('l','r'):
            upper[side]=[-22-23*roar,-22+14*roar]
            lower[side]=[-4-8*roar,-30+20*roar]
        knees={s:6+16*inhale for s in knees}; jaw=9*roar; brow=5*roar
    elif kind=='Death':
        buckle=pulse(t,0,.55,1.1)
        fall=smooth((t-.40)/1.25)
        body=Matrix.Translation((0,.10*fall,.085*fall)) @ pivot(Vector((0,0,.05)),rotation(-88*fall,0,10*fall))
        torso=body @ at('spine',rotation(8*buckle-4*fall,0,-7*fall))
        head_transform=torso @ at('head',rotation(-4*fall,0,6*fall))
        upper={'l':[-22+37*fall,-22+15*fall],'r':[-18+43*fall,-20+24*fall]}
        lower={'l':[-4+13*fall,-30+15*fall],'r':[-7+18*fall,-34+23*fall]}
        hips={'l':-3+12*fall,'r':-3-9*fall}; knees={'l':6+28*buckle+17*fall,'r':6+21*buckle+21*fall}
        jaw=3*fall; brow=-3*fall

    transforms={'root':Matrix.Identity(4),'pelvis':body,'spine':torso,'chest':torso,'head':head_transform,
                'jaw':head_transform @ at('jaw',rotation(jaw)),
                'brow_l':head_transform @ at('brow_l',rotation(0,0,-brow)),
                'brow_r':head_transform @ at('brow_r',rotation(0,0,brow))}
    for side,sign in [('l',1),('r',-1)]:
        transforms['clavicle_'+side]=torso
        arm=torso @ at('upperarm_'+side,rotation(0,sign*upper[side][0],sign*upper[side][1]))
        elbow=arm @ at('forearm_'+side,rotation(0,sign*lower[side][0],sign*lower[side][1]))
        transforms['upperarm_'+side]=arm
        transforms['forearm_'+side]=elbow
        transforms['hand_'+side]=elbow @ at('hand_'+side,rotation(hand_angles[side][0],0,sign*hand_angles[side][1]))
        thigh=body @ at('thigh_'+side,rotation(hips[side]))
        calf=thigh @ at('calf_'+side,rotation(knees[side]))
        transforms['thigh_'+side]=thigh; transforms['calf_'+side]=calf
        transforms['foot_'+side]=calf @ at('foot_'+side,rotation(-(hips[side]+knees[side])*.65))
    return {n:transforms[n] @ rest[n] for n in rest}

def apply_pose(desired):
    for bone in rig.pose.bones:
        parent=bone.parent
        bone.matrix_basis=bone.bone.convert_local_to_pose(
            desired[bone.name],rest[bone.name],
            parent_matrix=desired[parent.name] if parent else Matrix.Identity(4),
            parent_matrix_local=rest[parent.name] if parent else Matrix.Identity(4),invert=True)
    bpy.context.view_layer.update()

def keep_on_floor(desired):
    apply_pose(desired)
    deps=bpy.context.evaluated_depsgraph_get()
    lowest=min((obj.evaluated_get(deps).matrix_world @ v.co).z
               for obj in meshes for v in obj.evaluated_get(deps).data.vertices)
    if lowest<.004:
        lift=Matrix.Translation((0,0,.004-lowest))
        desired={n:(lift @ matrix if n!='root' else matrix) for n,matrix in desired.items()}
        apply_pose(desired)

clips=[('Idle',2.4,None,True),('Walk',1.8,None,True),
       ('PunchLeft',1.6,.85,False),('PunchRight',1.7,.95,False),
       ('Kick',2.,1.12,False),('Hurt',.8,None,False),('Roar',5.,None,False),
       ('Death',3.,None,False)]
records=[]; actions=[]
for name,duration,impact,loop in clips:
    action=bpy.data.actions.new('AN_BossPhase3_'+name)
    action.use_fake_user=True
    rig.animation_data_create(); rig.animation_data.action=action
    end=round(duration*30)+1
    for frame in range(1,end+1):
        keep_on_floor(pose(name,(frame-1)/30,duration))
        for bone in rig.pose.bones:
            bone.keyframe_insert(data_path='location',frame=frame,group=bone.name)
            bone.keyframe_insert(data_path='rotation_quaternion',frame=frame,group=bone.name)
            bone.keyframe_insert(data_path='scale',frame=frame,group=bone.name)
    scene.frame_start=1; scene.frame_end=end; scene.frame_set(1)
    select_export(False)
    filename=action.name+'.fbx'
    bpy.ops.export_scene.fbx(filepath=str(OUT/filename),use_selection=True,
        object_types={'ARMATURE'},add_leaf_bones=False,use_armature_deform_only=False,
        bake_anim=True,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,
        bake_anim_simplify_factor=0,bake_anim_step=1,axis_forward='-Y',axis_up='Z')
    records.append({'name':action.name,'file':filename,'frames':end,'fps':30,
                    'duration_seconds':duration,'impact_seconds':impact,'loop':loop,
                    'root_motion':False,'windup_end_seconds':impact-.2 if impact else None,
                    'recovery_begin_seconds':impact+.32 if impact else None})
    actions.append(action)
    print('MC_PHASE3_CLIP_READY',filename)

rig.animation_data.action=None
track=rig.animation_data.nla_tracks.new(); track.name='Phase 3 Animation Review'
timeline=1
for action,record in zip(actions,records):
    strip=track.strips.new(action.name,timeline,action)
    strip.action_frame_start=1; strip.action_frame_end=record['frames']
    strip.blend_type='REPLACE'; strip.extrapolation='HOLD_FORWARD'
    scene.timeline_markers.new(record['name'],frame=timeline)
    if record['impact_seconds']:
        scene.timeline_markers.new(record['name']+' impact',frame=timeline+round(record['impact_seconds']*30))
    record['review_start_frame']=timeline; timeline+=record['frames']+8
scene.frame_start=1; scene.frame_end=timeline-8; scene.frame_set(1)

# Neutral look for the editable authoring file; the in-game profile assigns
# Guardian's existing skin material after the derived mesh is imported.
scene.render.engine='BLENDER_EEVEE'
scene.render.resolution_x=1000;scene.render.resolution_y=800
scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('BossPhase3ReviewWorld');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.022,.035,.05,1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.35
preview_material=bpy.data.materials.new('Phase3AuthoringClay');preview_material.use_nodes=True
preview_material.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.22,.37,.30,1)
preview_material.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.72
for obj in meshes:
    obj.data.materials.clear();obj.data.materials.append(preview_material)
target=Vector((0,-.05,1.3))
camera_data=bpy.data.cameras.new('Phase3ReviewCamera')
camera=bpy.data.objects.new('Phase3ReviewCamera',camera_data);scene.collection.objects.link(camera)
camera.location=(4.7,-8.0,3.2);camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera_data.type='ORTHO';camera_data.ortho_scale=5.9;scene.camera=camera
for name,position,power,size,color in [('Key',(1.5,-4,6),1100,5,(.8,.9,1)),
                                      ('Fill',(-4,-2,3),650,4,(1,.64,.44)),
                                      ('Rim',(1,3,4),1400,4,(.43,1,.75))]:
    data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='DISK';data.size=size;data.color=color
    obj=bpy.data.objects.new(name,data);scene.collection.objects.link(obj);obj.location=position
    obj.rotation_euler=(target-obj.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.008))
floor=bpy.context.object;floor.name='Phase3ReviewFloor'
floor_material=bpy.data.materials.new('Phase3ReviewFloor');floor_material.diffuse_color=(.025,.035,.05,1)
floor.data.materials.append(floor_material)
select_export(False)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'BossPhase3Animations.blend'))
report={**rig_manifest,'clips':records,'review_blend':'BossPhase3Animations.blend',
        'review_timeline_frames':scene.frame_end,'root_motion':False,
        'derived_reference_pose_unchanged':all(max(abs(rig.data.bones[n].matrix_local[i][j]-rest[n][i][j]) for i in range(4) for j in range(4))<1e-6 for n in rest),
        'death_final_pose_hold_begin_seconds':1.65,
        'original_topology_and_uvs_preserved':True}
assert report['derived_reference_pose_unchanged']
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_hash
(OUT/'AnimationReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if '--preview' in sys.argv:
    for record in records:
        sample=record['impact_seconds'] if record['impact_seconds'] else .60
        if record['name'].endswith('Death'):sample=2.4
        if record['name'].endswith('Roar'):sample=1.65
        scene.frame_set(record['review_start_frame']+round(sample*30))
        scene.render.filepath=str(OUT/(record['name']+'.png'))
        bpy.ops.render.render(write_still=True)
print('MC_PHASE3_ANIMATIONS_PASS',json.dumps(records))
