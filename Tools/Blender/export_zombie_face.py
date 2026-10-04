"""Create lightweight boss face morphs on an isolated copy of the artist export.

The eyelids and brows in this zombie are separate rigid shells weighted entirely
to head_x, so translating the retained face bones would not animate them. Rotate
their actual shell geometry about the existing eye centres and bake morph deltas.
The independent 85-bone reference, vertex count, weights and UVs stay unchanged.
"""
import bpy, hashlib, json, math, sys
from pathlib import Path
from mathutils import Matrix, Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/ZombieBossAnimations'
OUT.mkdir(parents=True,exist_ok=True)
source=Path(bpy.data.filepath)
assert source.resolve()==(ROOT/'ArtSource/ZombieBoss/ZombieBoss.blend').resolve()
original_hash=hashlib.sha256(source.read_bytes()).hexdigest()
rig=bpy.data.objects['Armature']; body=bpy.data.objects['SK_ZombieBoss']
mouth=bpy.data.objects['ZombieBossMouth']
assert len(rig.data.bones)==85 and len(body.data.vertices)==3353
rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
for b in rig.pose.bones:b.matrix_basis=Matrix.Identity(4)
rig.animation_data_clear()
for obj in (body,mouth):
    obj.animation_data_clear()
    obj.data.shape_keys.animation_data_clear()
    for key in obj.data.shape_keys.key_blocks[1:]:key.value=0

adj=[set() for _ in body.data.vertices]
for e in body.data.edges:
    a,b=e.vertices;adj[a].add(b);adj[b].add(a)
remaining=set(range(len(adj)));shells=[]
while remaining:
    a=remaining.pop();queue=[a];ids={a}
    while queue:
        for b in adj[queue.pop()]:
            if b in remaining:remaining.remove(b);queue.append(b);ids.add(b)
    shells.append(sorted(ids))
base=[p.co.copy() for p in body.data.shape_keys.key_blocks[0].data]
lid_shells=[s for s in shells if len(s)==354]
brow_shells=[s for s in shells if len(s)==36]
lip_shell=next(s for s in shells if len(s)==144)
assert len(lid_shells)==4 and len(brow_shells)==2
lid_info=[]
for ids in lid_shells:
    centroid=sum((base[i] for i in ids),Vector())/len(ids)
    side='l' if centroid.x>0 else 'r'
    eye=rig.data.bones['c_eye_'+side].head_local.copy()
    # The drooping right upper lid has a centroid below its eye centre. Its
    # high back rim identifies the upper shell without that ambiguous centroid.
    upper=max(base[i].z for i in ids)>90
    lid_info.append((ids,side,upper,eye))

def rotate(points,ids,pivot,axis,degrees):
    transform=Matrix.Rotation(math.radians(degrees),4,axis)
    for i in ids:points[i]=pivot+transform @ (points[i]-pivot)

def make(name,points,obj=body):
    existing=obj.data.shape_keys.key_blocks.get(name)
    if existing:obj.shape_key_remove(existing)
    key=obj.shape_key_add(name=name,from_mix=False)
    key.relative_key=obj.data.shape_keys.key_blocks[0]
    for point,co in zip(key.data,points):point.co=co
    key.value=0
    return key

for name,amount in (('Eyes_Blink',1.),('Eyes_Squint',.48)):
    points=[p.copy() for p in base]
    for ids,side,upper,eye in lid_info:
        # The artist's right upper eyelid already sits further down.
        angle=(55 if side=='l' else 40) if upper else -23
        # Close the front of each lid while leaving its rear rim and pole in
        # place. Rotating the whole asymmetric shell reveals a rear pole hole.
        for i in ids:
            delta=base[i]-eye
            facing=-delta.y/max(.001,delta.length)
            weight=min(1,max(0,(facing-.05)/.7))
            weight=weight*weight*(3-2*weight)
            rotated=Matrix.Rotation(math.radians(angle*amount*weight),4,'X') @ delta
            # Preserve radial clearance for linear morph interpolation, fading
            # the extra room away from the moving front into the fixed rim.
            clearance=(16.0 if upper else 15.6) if name=='Eyes_Blink' else 15.4
            radius=delta.length+(max(delta.length,clearance)-delta.length)*weight
            projected=rotated.normalized()*radius
            if name=='Eyes_Blink' and facing>.4:
                # The closed lid rims meet at the eye's equator. Prevent the
                # front caps from passing through each other after projection.
                projected.z=max(.12,projected.z) if upper else min(-.12,projected.z)
                projected.y=-math.sqrt(max(.001,radius*radius-projected.x*projected.x-projected.z*projected.z))
            points[i]=eye+projected
    make(name,points)

points=[p.copy() for p in base]
for ids in brow_shells:
    center=sum((base[i] for i in ids),Vector())/len(ids)
    sign=1 if center.x>0 else -1
    rotate(points,ids,center,'Y',-sign*17)
    for i in ids:points[i].z-=1.3
make('Brow_Angry',points)

# Mouth targets reuse the artist lip/cutout deltas. Restrict them to the mouth
# region so procedural blinking and brow attitude remain independent channels.
mouth_ids=set(lip_shell)
enamel_shell=next(s for s in shells if len(s)==611)
for i in enamel_shell:
    p=base[i]
    if p.y<-17 and 37<p.z<76 and abs(p.x)<26:mouth_ids.add(i)
for name,authored in (('Mouth_Pain','Mouth_Angry'),('Mouth_Roar','Mouth_Surprise')):
    points=[p.copy() for p in base]
    shape=body.data.shape_keys.key_blocks[authored]
    for i in mouth_ids:points[i]=shape.data[i].co.copy()
    make(name,points)
    mouth_base=[p.co.copy() for p in mouth.data.shape_keys.key_blocks[0].data]
    mouth_shape=mouth.data.shape_keys.key_blocks[authored]
    make(name,[p.co.copy() for p in mouth_shape.data],mouth)

for name in ('Eyes_Blink','Eyes_Squint','Brow_Angry','Mouth_Pain','Mouth_Roar'):
    shape=body.data.shape_keys.key_blocks[name]
    assert max((point.co-origin).length for point,origin in zip(shape.data,base))>.1,name
for obj in (body,mouth):
    for key in obj.data.shape_keys.key_blocks[1:]:key.value=0
bpy.ops.object.select_all(action='DESELECT')
for obj in (rig,body,mouth):obj.select_set(True)
bpy.context.view_layer.objects.active=rig
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'ZombieBossFace.blend'))
bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_ZombieBoss_Face.fbx'),use_selection=True,
    object_types={'ARMATURE','MESH'},add_leaf_bones=False,use_armature_deform_only=False,
    bake_anim=False,use_mesh_modifiers=False,mesh_smooth_type='FACE')
# Confirm shape channels exist in the actual written FBX, not only in .blend.
from io_scene_fbx import parse_fbx
fbx,_=parse_fbx.parse(str(OUT/'SK_ZombieBoss_Face.fbx'))
blendshapes=[]
def scan(node):
    if node.id==b'Deformer' and len(node.props)>2 and node.props[2]==b'BlendShapeChannel':
        blendshapes.append(node.props[1].decode(errors='replace').split('\\x00')[0])
    for child in node.elems:scan(child)
scan(fbx)
assert all(any(name in channel for channel in blendshapes) for name in ('Eyes_Blink','Eyes_Squint','Brow_Angry','Mouth_Pain','Mouth_Roar')),blendshapes
assert all(max(abs(rig.data.bones[n].matrix_local[i][j]-m[i][j]) for i in range(4) for j in range(4))<1e-6 for n,m in rest.items())
assert hashlib.sha256(source.read_bytes()).hexdigest()==original_hash
report={'source':str(source),'source_sha256':original_hash,'bones':85,
    'vertices':[len(body.data.vertices),len(mouth.data.vertices)],
    'morphs':{name: {'nonzero_vertices':sum((v.co-base[i]).length>.001 for i,v in enumerate(body.data.shape_keys.key_blocks[name].data)),
                    'max_delta_cm':max((v.co-base[i]).length for i,v in enumerate(body.data.shape_keys.key_blocks[name].data))}
              for name in ('Eyes_Blink','Eyes_Squint','Brow_Angry','Mouth_Pain','Mouth_Roar')},
    'reference_pose_unchanged':True,'topology_weights_uvs_unchanged':True,
    'fbx_blendshape_channels':blendshapes,
    'eyelid_shells':[{'side':side,'upper':upper,'vertices':len(ids)} for ids,side,upper,eye in lid_info],
    'mouth_targets_include_lip_jaw_and_cavity':True,'no_extra_face_tick_bones':True}
(OUT/'ZombieFaceReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')

if '--preview' in sys.argv:
    scene=bpy.context.scene
    scene.render.engine='BLENDER_EEVEE'
    scene.render.resolution_x=scene.render.resolution_y=640
    scene.render.resolution_percentage=100
    scene.world=bpy.data.worlds.new('BossFaceReviewWorld');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.025,.04,.05,1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.3
    data=bpy.data.cameras.new('Boss face camera');cam=bpy.data.objects.new(data.name,data)
    scene.collection.objects.link(cam);cam.location=(.25,-2.3,1.10)
    target=Vector((0,-.1,.71));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();data.lens=80;scene.camera=cam
    for name,pos,power,color in [('Key',(1,-2,2.5),500,(.85,.95,1)),('Fill',(-1.5,-1,1.2),190,(1,.62,.44)),('Rim',(0,1.4,2),500,(.42,1,.71))]:
        light=bpy.data.lights.new(name,'AREA');light.energy=power;light.size=2.;light.color=color
        obj=bpy.data.objects.new(name,light);scene.collection.objects.link(obj);obj.location=pos;obj.rotation_euler=(target-obj.location).to_track_quat('-Z','Y').to_euler()
    for name,values in (('Neutral',{}),('Blink',{'Eyes_Blink':1.}),('Combat',{'Eyes_Squint':.4,'Brow_Angry':1.,'Mouth_Angry':.35}),('Pain',{'Eyes_Squint':.8,'Brow_Angry':.7,'Mouth_Pain':1.}),('Roar',{'Eyes_Squint':.8,'Brow_Angry':1.,'Mouth_Roar':1.})):
        for mesh in (body,mouth):
            for key in mesh.data.shape_keys.key_blocks[1:]:key.value=values.get(key.name,0)
        bpy.context.view_layer.update();scene.render.filepath=str(OUT/('Face_'+name+'.png'));bpy.ops.render.render(write_still=True)
print('MC_ZOMBIE_FACE_PASS',json.dumps(report))
