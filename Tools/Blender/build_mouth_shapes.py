"""Build the connected mouth, complete-pose shape keys, FBX and editable review scene.

Run in Blender with --background --factory-startup --python this_file.py.
The artist source and TeethGameplay base are never overwritten.
"""
import bpy, bmesh, math, json, runpy, sys
from pathlib import Path
from mathutils import Vector, Matrix

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/CharacterFace'
PREVIEW = ROOT / 'Saved/MouthPreview'
OUT.mkdir(parents=True, exist_ok=True)
PREVIEW.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'ArtSource/CharacterGameplay/TeethGameplay.blend'))
rig = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
obj = next(o for o in bpy.data.objects if o.type == 'MESH')
rig.animation_data_clear()
for bone in rig.pose.bones: bone.matrix_basis = Matrix.Identity(4)
runpy.run_path(str(ROOT/'Tools/Blender/audit_mouth_topology.py'))
audit = json.loads((ROOT/'Saved/MouthTopology.json').read_text())
assert len(obj.data.vertices) == 3138 and audit['components'][9]['welded'] == 144

# Preserve the artist's packed textures on all existing surfaces.
with bpy.data.libraries.load(str(ROOT/'ArtSource/CharacterAnimation/TeethAnimationLab.blend'), link=False) as (src,dst):
    dst.materials = ['Material']
enamel = dst.materials[0]
assert enamel and any(n.type == 'TEX_IMAGE' for n in enamel.node_tree.nodes)
obj.data.materials.clear(); obj.data.materials.append(enamel)
def material(name, color, roughness):
    mat = bpy.data.materials.new(name); mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*color, 1)
    shader.inputs['Roughness'].default_value = roughness
    mat.diffuse_color = (*color,1)
    obj.data.materials.append(mat)
    return len(obj.data.materials)-1
LIP = material('M_MouthLip', (.48,.13,.17), .43)
INNER = material('M_MouthCavity', (.055,.009,.017), .66)

bm = bmesh.new(); bm.from_mesh(obj.data); bm.verts.ensure_lookup_table()
shell = bm.verts.layers.int.new('mouth_shell')
kind = bm.verts.layers.int.new('mouth_part')
angle = bm.verts.layers.float.new('mouth_angle')
body = set(audit['components'][2]['indices'])
for v in bm.verts: v[shell] = 2 if v.index in body else 0
bmesh.ops.delete(bm, geom=[bm.verts[i] for i in audit['components'][9]['indices']], context='VERTS')
inside = [v for v in bm.verts if v[shell]==2 and abs(v.co.x)<16 and -24<v.co.y<-5 and 51<v.co.z<68]
faces = {f for v in inside for f in v.link_faces if all(abs(q.co.x)<17 and q.co.y<0 and 49<q.co.z<68 for q in f.verts)}
assert len(faces)==40
bmesh.ops.delete(bm, geom=list(faces), context='FACES_ONLY')
bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
bmesh.ops.remove_doubles(bm, verts=[v for v in bm.verts if v[shell]==2], dist=.0005)
rim_edges = [e for e in bm.edges if len(e.link_faces)==1 and all(v[shell]==2 and v.co.y<-24 and 48<v.co.z<68 for v in e.verts)]
assert len(rim_edges)==12
bmesh.ops.subdivide_edges(bm, edges=rim_edges, cuts=3, use_grid_fill=False)
rim_edges = [e for e in bm.edges if len(e.link_faces)==1 and all(v[shell]==2 and v.co.y<-24 and 48<v.co.z<68 for v in e.verts)]
rim = list({v for e in rim_edges for v in e.verts})
assert len(rim)==48 and len(rim_edges)==48
C = 58.5
rim.sort(key=lambda v: math.atan2((v.co.z-C)/8.1,v.co.x/14.4))
for v in rim: v[kind]=1; v[angle]=math.atan2((v.co.z-C)/8.1,v.co.x/14.4)
angles = [v[angle] for v in rim]
original_rim = [v.co.copy() for v in rim]

# The source bake contains colored streaks from the old mouth along its UV rim.
# Move only the enamel's mouth neighbourhood onto clean enamel in the same atlas.
# Keep one continuous radial mapping across all loops, including shared edges.
uv_layer=bm.loops.layers.uv.active
uv_center=Vector((.23037,.71503));uv_radius=Vector((.08338,.05153))
uv_adjusted=0
for f in bm.faces:
    if f.material_index!=0 or not all(v[shell]==2 for v in f.verts):continue
    for loop in f.loops:
        uv=loop[uv_layer].uv
        delta=uv-uv_center
        radius=math.hypot(delta.x/uv_radius.x,delta.y/uv_radius.y)
        if .65<radius<1.75:
            t=max(0,min(1,(radius-1)/.75));fade=1-t*t*(3-2*t)
            loop[uv_layer].uv=uv_center+delta*((radius+.26*fade)/radius)
            uv_adjusted+=1
assert uv_adjusted>48

# The original triangular enamel faces acquire five vertices on their cut edge.
# Fan each one from its opposite vertex so every new seam vertex participates
# in the surface. An ngon triangulator may discard collinear seam vertices.
rim_set=set(rim)
adjacent={f for e in rim_edges for f in e.link_faces}
assert len(adjacent)==12
for f in adjacent:
    opposite=[v for v in f.verts if v not in rim_set]
    assert len(opposite)==1
    apex=opposite[0];face_uv={loop.vert:loop[uv_layer].uv.copy() for loop in f.loops}
    for e in list(f.edges):
        if not all(v in rim_set for v in e.verts):continue
        triangle=bm.faces.new((apex,*e.verts));triangle.material_index=f.material_index;triangle.smooth=True
        for loop in triangle.loops:loop[uv_layer].uv=face_uv[loop.vert]
bmesh.ops.delete(bm,geom=list(adjacent),context='FACES_ONLY')
def original_boundary(a):
    # Interpolate the actual cutout, including its asymmetry; an ideal ellipse
    # can move neighbouring enamel vertices inside the new lip at the corners.
    for i,left in enumerate(angles):
        j=(i+1)%len(angles);right=angles[j]+(2*math.pi if j==0 else 0)
        test=a+(2*math.pi if a<angles[0] else 0)
        if left<=test<=right:
            return original_rim[i].lerp(original_rim[j],(test-left)/(right-left))
    raise AssertionError(a)
def new_vertex(part, a=0):
    v=bm.verts.new((0,0,0)); v[kind]=part; v[angle]=a
    return v
def face(verts, mat):
    f=bm.faces.new(verts); f.material_index=mat; f.smooth=True
    return f
# Every ring uses the same angles and winding; the enamel rim is reused, not copied.
rings=[rim]
for part in range(2,8):
    row=[new_vertex(part,a) for a in angles]
    for i in range(48):
        j=(i+1)%48
        face((rings[-1][i],rings[-1][j],row[j],row[i]), LIP if part<=4 else INNER)
    rings.append(row)
cap=new_vertex(8)
for i in range(48): face((rings[-1][i],rings[-1][(i+1)%48],cap),INNER)

# Each target is a complete mouth pose, including the cavity.
# H is HALF the inner opening, not the outer lip height; lips retain volume at closure.
# width, opening, corner lift, protrusion
POSES={
 'Basis':(11.8,.18,0,0),
 'Mouth_MBP':(11.8,.025,0,.2),
 'Mouth_A':(11.6,6.2,0,.1),
 'Mouth_E':(14.1,2.8,.65,-.25),
 'Mouth_I':(14.7,1.65,.9,-.35),
 'Mouth_O':(6.4,5.5,0,2.0),
 'Mouth_U':(5.1,3.25,0,2.5),
 'Mouth_FV':(11.7,1.0,0,.1),
 'Mouth_L':(11.6,3.65,.1,0),
 'Mouth_TH':(11.9,2.15,0,0),
 'Mouth_CH':(8.6,2.35,0,1.35),
 'Mouth_S':(13.6,.85,.3,-.15),
 'Mouth_D':(11.8,2.2,.1,.1),
 'Mouth_Smile':(14.5,2.5,2.6,-.1),
 'Mouth_Frown':(11.8,.25,-2,0),
 'Mouth_Angry':(12.4,1.0,-.8,0),
 'Mouth_Pain':(13.4,4.3,-1.25,.1),
 'Mouth_Surprise':(7.0,5.9,0,1.0),
 'Mouth_Effort':(12.5,.75,-.6,0),
}
rest_source={v:v.co.copy() for v in bm.verts}
rim_neighbors={v:[e.other_vert(v)[angle] for e in v.link_edges if e.other_vert(v)[kind]==1]
               for v in bm.verts if v[kind]==0 and v[shell]==2}
def ring_position(part,a,p):
    w,h,smile,purse=p
    # From the enamel junction to the cavity floor. Cavity widens behind the lips.
    dims={1:(w+2.1,h+2.0,-27.7), 2:(w+1.9,h+1.7,-29.5-purse*.7),
          3:(w+.9,h+.85,-30.6-purse),4:(w,h,-29.8-purse),
          5:(w+.35,h+.3,-26.5),
          6:(max(w+1.3,13.2),max(h+1.8,7.4),-18),
          7:(10,5.4,-11.8)}
    rx,rz,y=dims[part]
    x=rx*math.cos(a); s=math.sin(a)
    # Broad central lip line and soft corners, with preserved upper/lower order.
    z=C+rz*s+smile*math.cos(a)**2
    return Vector((x,y-.55*s,z))
def position(v,p,name):
    part=v[kind]; old=rest_source[v]
    if 1<=part<=7: return ring_position(part,v[angle],p)
    if part==8:return Vector((0,-10.6,C))
    if v[shell]==2 and old.y<-22:
        nx=old.x/14.4; nz=(old.z-C)/8.1; radius=math.sqrt(nx*nx+nz*nz)
        if radius<2.5:
            a=math.atan2(nz,nx)
            original=original_boundary(a)
            boundary_radius=math.sqrt((original.x/14.4)**2+((original.z-C)/8.1)**2)
            t=max(0,min(1,(radius-boundary_radius)/1.5)); weight=(1-t*t*(3-2*t))**2
            target=ring_position(1,a,p)
            delta=target-original
            result=old+weight*delta
            # Keep each neighbouring vertex beyond the seam's tangent planes.
            # Otherwise an edge from a sparse enamel vertex can cut across the
            # new curved rim despite that vertex itself being outside the hole.
            neighbors=rim_neighbors.get(v,[])
            if neighbors:
                w,h,smile,_=p;lift=smile*math.cos(a)**2
                px=result.x/(w+2.1);pz=(result.z-C-lift)/(h+2.0)
                projection=min(px*math.cos(q)+pz*math.sin(q) for q in neighbors)
                assert projection>.25
                scale=max(1,1.015/projection)
                result.x*=scale;result.z=C+lift+(result.z-C-lift)*scale
            return result
    return old.copy()

coords={name:{v:position(v,p,name) for v in bm.verts} for name,p in POSES.items()}
deform=bm.verts.layers.deform.verify(); head=obj.vertex_groups['head_x'].index
for v in bm.verts:
    old=rest_source[v]
    if v[kind] or (v[shell]==2 and old.y<-21 and abs(old.x)<29 and 36<old.z<76):
        v[deform].clear();v[deform][head]=1
    v.co=coords['Basis'][v]
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
bm.verts.index_update()
key_coords={name:[tuple(values[v]) for v in bm.verts] for name,values in coords.items()}
seam_indices=[v.index for v in rim]
assert all(len(e.link_faces)==2 for e in rim_edges), 'Mouth seam is not welded'
assert all(len(v[deform])>0 and abs(sum(v[deform].values())-1)<.002 for v in bm.verts)
bm.to_mesh(obj.data);bm.free();obj.data.update()
obj.name='SK_TeethFace';obj.data.name='TeethConnectedMouth'
for name,values in key_coords.items():
    key=obj.shape_key_add(name=name,from_mix=False)
    for v,co in zip(key.data,values):v.co=co
    key.slider_min=0;key.slider_max=1
pupil_report=runpy.run_path(str(ROOT/'Tools/Blender/pupil_shapes.py'))['add_pupil_shapes'](obj)
obj['MouthConstruction']='Shared enamel/lip/cavity vertices. Complete mouth poses; blend with total weight <= 1. All mouth geometry skinned to head_x.'

# Check endpoints here; check_mouth_shapes.py additionally checks pairwise transitions.
import numpy as np
arrays={n:np.asarray(c,dtype=np.float64) for n,c in key_coords.items()}
assert all(np.isfinite(a).all() for a in arrays.values())
min_gap=min(2*p[1] for p in POSES.values())
assert min_gap>=.049
report={'vertices':len(obj.data.vertices),'bones':len(rig.data.bones),'mouth_seam_shared_vertices':len(seam_indices),
        'clean_enamel_uv_loops':uv_adjusted,
        'shape_keys':list(POSES),'minimum_central_lip_gap_cm':min_gap,
        'pupils':pupil_report,
        'all_weights_normalized':True,'source':'ArtSource/CharacterGameplay/TeethGameplay.blend',
        'parameters':{k:dict(zip(['width','half_opening','corner_lift','purse'],v)) for k,v in POSES.items()}}
(OUT/'MouthShapes.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')

# Export before adding the review scene and animation. The skeleton is unchanged.
bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
bpy.context.view_layer.update()
bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_TeethFace.fbx'),use_selection=True,object_types={'ARMATURE','MESH'},
    add_leaf_bones=False,use_armature_deform_only=False,bake_anim=False,use_mesh_modifiers=False,mesh_smooth_type='FACE')

scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.resolution_x=800;scene.render.resolution_y=800;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('FaceStudio');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.08,.10,.15,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.4
focus=Vector((0,-.17,.66))
bpy.ops.object.camera_add(location=(.48,-2.8,.91));camera=bpy.context.object;camera.name='Face_ThreeQuarter'
camera.rotation_euler=(focus-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=1.18;scene.camera=camera
for name,pos,energy,size in [('Key',(1,-3,3),250,3),('Fill',(-2,-2,1),100,2),('Rim',(0,2,2),350,2)]:
    bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.name=name
    light.data.energy=energy;light.data.shape='DISK';light.data.size=size
    light.rotation_euler=(focus-light.location).to_track_quat('-Z','Y').to_euler()
scene.render.fps=30
keys=obj.data.shape_keys
for index,name in enumerate(list(POSES)[1:]):
    frame=1+index*30
    for key in keys.key_blocks[1:]:
        for f,value in [(frame,0),(frame+7,float(key.name==name)),(frame+20,float(key.name==name)),(frame+29,0)]:
            key.value=value;key.keyframe_insert('value',frame=f)
    scene.timeline_markers.new(name,frame=frame+7)
keys.animation_data.action.name='Mouth_AllShapes';keys.animation_data.action.use_fake_user=True
mouth_action=keys.animation_data.action;mouth_slot=keys.animation_data.action_slot
keys.animation_data.action=None
for frame,dilate,contract in [(1,0,0),(10,1,0),(24,1,0),(65,0,0),(80,0,.5),(110,0,0)]:
    for name,value in [('Pupil_Dilate',dilate),('Pupil_Contract',contract)]:
        keys.key_blocks[name].value=value;keys.key_blocks[name].keyframe_insert('value',frame=frame)
keys.animation_data.action.name='Pupils_Reaction';keys.animation_data.action.use_fake_user=True;keys.animation_data.action.asset_mark()
keys.animation_data.action=mouth_action;keys.animation_data.action_slot=mouth_slot
scene.frame_start=1;scene.frame_end=(len(POSES)-1)*30;scene.frame_set(1)
scene['README']='Select SK_TeethFace > Object Data > Shape Keys. Disable Mouth_AllShapes action to pose manually. Timeline markers show each complete mouth pose. Mix mouth keys with sum <= 1. Pupils_Reaction is a separate 110-frame action. Shared lip/enamel/cavity edge. Speech input is not audio recognition.'
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_location=focus;area.spaces.active.region_3d.view_distance=1.7
            area.spaces.active.shading.color_type='TEXTURE'
bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'TeethFaceLab.blend'))
# Initial review renders. A separate script can render every timeline marker.
action=keys.animation_data.action;keys.animation_data.action=None
for name in ([] if '--skip-render' in sys.argv else ['Basis','Mouth_A','Mouth_O','Mouth_Smile','Mouth_L','Mouth_MBP']):
    for key in keys.key_blocks[1:]:key.value=float(key.name==name)
    bpy.context.view_layer.update();scene.render.filepath=str(PREVIEW/(name+'.png'));bpy.ops.render.render(write_still=True)
print('MC_MOUTH_BUILD_PASS',json.dumps({k:report[k] for k in ['vertices','bones','mouth_seam_shared_vertices','minimum_central_lip_gap_cm']}),flush=True)
