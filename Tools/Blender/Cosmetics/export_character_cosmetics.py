"""Build cosmetic examples against the saved game mesh, preserving its skin and rig.

Run in a separate Blender process with the user's copied Character.blend open.
Creates only derived cosmetics under ArtSource/CharacterCurrent/Cosmetics.
All renderer/temp outputs use E:; the live unsaved menu scene is untouched.
"""
import bpy, bmesh, hashlib, json, math, os
from pathlib import Path
from mathutils import Matrix, Vector

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'ArtSource/CharacterCurrent/Cosmetics'
PREVIEW=Path(os.environ.get('MC_COSMETICS_PREVIEW','E:/CodexWork/HubSystems_20261023/Cosmetics/Preview'))
OUT.mkdir(parents=True,exist_ok=True);PREVIEW.mkdir(parents=True,exist_ok=True)
bpy.context.preferences.filepaths.temporary_directory='E:/CodexTemp/HubSystems_20261023/Cosmetics'
source_path=Path(os.environ.get('MC_COSMETICS_SOURCE',bpy.data.filepath or 'E:/CodexWork/HubSystems_20261023/Cosmetics/Character_Source.blend'))
cap_source=bpy.data.objects.get('Sphere.001')
if cap_source is None:
    # Factory-startup export appends only the needed object, preserving unrelated live Blender work.
    with bpy.data.libraries.load(str(source_path),link=False) as (available,loaded):
        loaded.objects=['Sphere.001']
    cap_source=loaded.objects[0]
    bpy.context.scene.collection.objects.link(cap_source)
    bpy.context.view_layer.update()
assert cap_source and cap_source.type=='MESH' and len(cap_source.data.vertices)==1042,'Source cap changed: inspect before exporting.'
assert not cap_source.modifiers,'Inspect evaluated source geometry before exporting a modified cap.'
cap_world=[cap_source.matrix_world@v.co for v in cap_source.data.vertices]

with bpy.data.libraries.load(str(ROOT/'ArtSource/CharacterCurrent/CharacterGameplay.blend'),link=False) as (available,loaded):
    loaded.objects=['SK_CharacterGameplay','root']
body=next(o for o in loaded.objects if o and o.type=='MESH')
rig=next(o for o in loaded.objects if o and o.type=='ARMATURE')
assert len(body.data.vertices)==4006 and len(rig.data.bones)==107,'Current runtime rig contract changed.'
body.name='CosmeticsFit_Character';rig.name='CosmeticsFit_Rig'
scene=bpy.data.scenes.new('CosmeticsFitValidation');bpy.context.window.scene=scene
scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
scene.collection.objects.link(body);scene.collection.objects.link(rig)
rig.animation_data_clear();body.animation_data_clear()
for bone in rig.pose.bones:bone.matrix_basis=Matrix.Identity(4)
for key in body.data.shape_keys.key_blocks:key.value=0
rig.data.pose_position='POSE';bpy.context.view_layer.update()

def mat(name,color,metal=0,rough=.45):
    m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1)
    p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
    return m
teal=mat('Cosmetics_WorkTeal',(.025,.47,.56),.05,.52)
dark=mat('Cosmetics_DeepMarine',(.012,.035,.055),.15,.36)
yellow=mat('Cosmetics_CreamGold',(.98,.64,.055),.2,.3)
red=mat('Cosmetics_Coral',(.66,.055,.035),.03,.52)

def eye_center(name):
    g=body.vertex_groups[name]
    ps=[body.matrix_world@v.co for v in body.data.vertices if any(x.group==g.index and x.weight>.99 for x in v.groups)]
    return (Vector(tuple(min(p[i] for p in ps) for i in range(3)))+Vector(tuple(max(p[i] for p in ps) for i in range(3))))*.5
eyes={s:eye_center('c_eye_'+s) for s in ('l','r')}

def new_mesh(name,verts,faces,materials):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata([v*100 for v in verts],[],faces);mesh.update()
    o=bpy.data.objects.new(name,mesh);scene.collection.objects.link(o);o.scale=(.01,)*3
    for m in materials:mesh.materials.append(m)
    for p in mesh.polygons:p.use_smooth=True
    return o

# The artist's tooth and runtime tooth have the same crown width/top. Preserve the
# intentionally smaller backwards cap exactly; fitting by independent axes flattens it.
# Its authored world transform already includes the correct tilt and rear-facing brim.
cap_points=[p.copy() for p in cap_world]
cap=new_mesh('SM_Cosmetic_WorkCap',cap_points,[tuple(p.vertices) for p in cap_source.data.polygons],[teal,dark])
# The underside keeps a darker material; the artist's topology is preserved.
for p,original in zip(cap.data.polygons,cap_source.data.polygons):
    p.use_smooth=original.use_smooth
    p.material_index=1 if p.normal.z<-.15 else 0
bpy.context.view_layer.update()
cap_shape_error=max((cap.matrix_world@v.co-original).length for v,original in zip(cap.data.vertices,cap_world))
assert cap_shape_error<1e-6,'Cap export must preserve the artist shape after uniform unit conversion.'

def tube(points,radius,material):
    # Closed smooth ring/tube with explicit topology, independent of Curve exporter settings.
    verts=[];faces=[];n=len(points);cyclic=(points[0]-points[-1]).length<1e-7
    if cyclic:points=points[:-1];n=len(points)
    for i,point in enumerate(points):
        before=points[(i-1)%n] if cyclic or i>0 else points[0]
        after=points[(i+1)%n] if cyclic or i<n-1 else points[-1]
        tangent=(after-before).normalized();normal=Vector((0,-1,0));side=tangent.cross(normal).normalized();normal=side.cross(tangent).normalized()
        for j in range(8):
            t=math.tau*j/8;verts.append(point+radius*(normal*math.cos(t)+side*math.sin(t)))
    for i in range(n if cyclic else n-1):
        for j in range(8):faces.append((i*8+j,i*8+(j+1)%8,((i+1)%n)*8+(j+1)%8,((i+1)%n)*8+j))
    if not cyclic:faces.extend([tuple(reversed(range(8))),tuple((n-1)*8+j for j in range(8))])
    return verts,faces,material

parts=[]
for s in ('l','r'):
    c=eyes[s]
    points=[Vector((c.x+.159*math.cos(t),-.374,c.z+.132*math.sin(t))) for t in [math.tau*i/48 for i in range(49)]]
    parts.append(tube(points,.012,0))
parts.append(tube([Vector((x,-.374,.824+.012*math.sin(math.pi*(x+.029)/.058))) for x in [-.029,-.015,0,.015,.029]],.011,1))
for sign in (-1,1):
    parts.append(tube([Vector((sign*.337,-.374,.823)),Vector((sign*.414,-.29,.824)),Vector((sign*.426,-.08,.813)),Vector((sign*.408,.15,.782))],.01,0))
verts=[];faces=[];material_ids=[]
for vs,fs,mi in parts:
    start=len(verts);verts.extend(vs);faces.extend([tuple(i+start for i in f) for f in fs]);material_ids.extend([mi]*len(fs))
glasses=new_mesh('SM_Cosmetic_SportGlasses',verts,faces,[dark,yellow])
for p,mi in zip(glasses.data.polygons,material_ids):p.material_index=mi

def bounds(o):
    points=[o.matrix_world@v.co for v in o.data.vertices]
    return [[min(p[i] for p in points)*100 for i in range(3)],[max(p[i] for p in points)*100 for i in range(3)]]
report={'blender':bpy.app.version_string,'source':str(source_path),'source_sha256':hashlib.sha256(source_path.read_bytes()).hexdigest(),
    'runtime_rig_source':str(ROOT/'ArtSource/CharacterCurrent/CharacterGameplay.blend'),'runtime_rig_sha256':hashlib.sha256((ROOT/'ArtSource/CharacterCurrent/CharacterGameplay.blend').read_bytes()).hexdigest(),
    'runtime_vertices_unchanged':len(body.data.vertices),'runtime_bones_unchanged':len(rig.data.bones),'bone':'head_x','mesh_space_fit':True,
    'notes':['Source file format is 502.45 (newer than available isolated Blender 5.1.0). Only the verified static cap topology is reused; original source never saved.','The cap preserves all source world vertices, proportions, polygon smooth flags and rear-facing brim; no per-axis fit or added 180-degree turn.','The test glasses have open lenses, keeping pupils, blink and mouth readable.','Accessory meshes are reference mesh space. Runtime converts via actual UE reference bone matrix, avoiding a guessed socket rotation.','Wardrobe palette/body skin/rig/expression shapes are not modified.'],
    'cap_source_shape':{'uniform_scale':1.0,'added_rotation_degrees':0.0,'translation_m':[0,0,0],'max_vertex_error_m':cap_shape_error},
    'items':[]}
for item,ids in [(cap,['Hat_WorkCap','Hat_WorkCapRed']),(glasses,['Face_SportGlasses'])]:
    bpy.ops.object.select_all(action='DESELECT');item.select_set(True);bpy.context.view_layer.objects.active=item
    # Match the saved character's metre scene and .01 object scale. FBX's unit
    # conversion yields centimetres in UE while retaining identical reference coordinates.
    bpy.ops.export_scene.fbx(filepath=str(OUT/(item.name+'.fbx')),use_selection=True,object_types={'MESH'},bake_anim=False,
        use_mesh_modifiers=True,mesh_smooth_type='FACE',add_leaf_bones=False)
    report['items'].append({'mesh':item.name,'ids':ids,'vertices':len(item.data.vertices),'triangles':sum(len(p.vertices)-2 for p in item.data.polygons),'bounds_cm':bounds(item),'materials':[m.name for m in item.data.materials]})

# A small editable accessory-only source, containing no replacement game skin.
bpy.data.libraries.write(str(OUT/'CharacterCosmetics.blend'),{cap,glasses},path_remap='RELATIVE',fake_user=True,compress=True)

# Preview deformation uses the same head weights as the runtime bone attachment.
for item in (cap,glasses):
    group=item.vertex_groups.new(name='head_x');group.add(list(range(len(item.data.vertices))),1,'REPLACE')
    mod=item.modifiers.new('ValidationHeadFollow','ARMATURE');mod.object=rig

scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=20
scene.render.resolution_x=720;scene.render.resolution_y=720;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('CosmeticsFitWorld');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.025,.04,.055,1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.45
scene.view_settings.view_transform='AgX'
for name,loc,power,size,col in [('Key',(-1,-2,3),220,2,(1,.86,.72)),('Fill',(2,-1,1.5),90,2,(.65,.86,1)),('Rim',(0,2,2.5),280,1.5,(.75,1,.95))]:
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='DISK';d.size=size;d.color=col
    o=bpy.data.objects.new(name,d);scene.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector((0,0,.7))-o.location).to_track_quat('-Z','Y').to_euler()
camera_data=bpy.data.cameras.new('FitCamera');camera=bpy.data.objects.new('FitCamera',camera_data);scene.collection.objects.link(camera);scene.camera=camera;camera_data.type='ORTHO';camera_data.ortho_scale=1.45
poses=[('front',(0,-3,.85),{}),('profile',(3,-.12,.85),{}),('three_quarter',(2,-3,1.05),{}),('smile',(1,-3,.95),{'Mouth_Smile':1}),('blink',(1,-3,.95),{'Eyes_Blink':1}),('head_turn',(1,-3,.95),{})]
for name,location,morphs in poses:
    for key in body.data.shape_keys.key_blocks:key.value=0
    for key,value in morphs.items():body.data.shape_keys.key_blocks[key].value=value
    rig.pose.bones['head_x'].rotation_mode='XYZ';rig.pose.bones['head_x'].rotation_euler=(0,math.radians(22) if name=='head_turn' else 0,0)
    camera.location=location;camera.rotation_euler=(Vector((0,0,.67))-camera.location).to_track_quat('-Z','Y').to_euler()
    bpy.context.view_layer.update();scene.render.filepath=str(PREVIEW/(name+'.png'));bpy.ops.render.render(write_still=True)
report['preview_poses']=[x[0] for x in poses]
(OUT/'CosmeticsReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('MC_COSMETICS_EXPORT_COMPLETE',json.dumps(report['items']))
