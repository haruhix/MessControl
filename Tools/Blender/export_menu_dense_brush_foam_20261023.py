"""Build a dense closed foam cap fitted to the current imported brush bone."""
import bpy,json,math,random,hashlib
import numpy as np
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'ArtSource/UI/MenuDenseBrushFoam20261023';OUT.mkdir(parents=True,exist_ok=True)
frame=json.loads((ROOT/'Saved/MenuDenseBrushFoam20261023/BrushBone.json').read_text());origin=Vector(json.loads((ROOT/'ArtSource/UI/CharacterMenu20261023/ExportReport.json').read_text())['origin_offset_m'])
s=bpy.context.scene;s.frame_set(1);bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get();brush=bpy.data.objects['SM_MeshaBrush.001'];evaluated=brush.evaluated_get(deps)
q=frame['rotation'];bone=Matrix.Translation(Vector(frame['translation']))@Quaternion((q[3],q[0],q[1],q[2])).to_matrix().to_4x4()@Matrix.Diagonal((*frame['scale'],1.));inv=bone.inverted()
def to_bone_cm(v):
    p=evaluated.matrix_world@v.co-origin;ue=Vector((p.x*100,-p.y*100,p.z*100));return np.array(inv@ue)*100
points={}
for name in ['brush','head']:
    group=brush.vertex_groups[name];ids=[v.index for v in brush.data.vertices if any(w.group==group.index and w.weight>.5 for w in v.groups)];points[name]=np.array([to_bone_cm(evaluated.data.vertices[i]) for i in ids])
cloud=points['brush'];center=cloud.mean(axis=0);normal=center-points['head'].mean(axis=0);normal/=np.linalg.norm(normal)
planar=cloud-center;planar-=np.outer(planar@normal,normal);_,vectors=np.linalg.eigh(planar.T@planar);uaxis=vectors[:,-1];vaxis=np.cross(normal,uaxis);vaxis/=np.linalg.norm(vaxis)
ucoords=(cloud-center)@uaxis;vcoords=(cloud-center)@vaxis;ncoords=(cloud-center)@normal
rx=max(abs(ucoords.min()),abs(ucoords.max()))+1.0;ry=max(abs(vcoords.min()),abs(vcoords.max()))+1.0;tip=float(ncoords.max());rng=random.Random(231023)
def pos(u,v,h):
    p=center+u*uaxis+v*vaxis+h*normal;return Vector((float(p[0])*.01,-float(p[1])*.01,float(p[2])*.01))
if bpy.context.object and bpy.context.object.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.object.select_all(action='DESELECT');parts=[]
# The continuous base fills the bristle field; small embedded caps add foam texture.
bpy.ops.mesh.primitive_uv_sphere_add(segments=48,ring_count=24,location=(0,0,0))
base=bpy.context.object;base.name='DenseFoamCore'
for vert in base.data.vertices:
    x,y,z=vert.co;w=.18*math.sin(x*11+y*15+z*7);vert.co=pos(x*rx,y*ry,1.+z*18.+w)
parts.append(base)
for i in range(185):
    a=rng.random()*math.tau;zz=rng.uniform(-1.,1.);r=math.sqrt(1-zz*zz);uu=math.cos(a)*r*rx;vv=math.sin(a)*r*ry
    radius=rng.uniform(.7,2.25) if i%8 else rng.uniform(2.3,3.1)
    height=1.+zz*18.-zz*radius*.32
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2 if radius>1.6 else 1,radius=1.,location=(0,0,0))
    obj=bpy.context.object
    for vert in obj.data.vertices:
        p=vert.co;vert.co=pos(uu+p.x*radius,vv+p.y*radius,height+p.z*radius*.83)
    parts.append(obj)
bpy.ops.object.select_all(action='DESELECT')
for obj in parts:obj.select_set(True)
bpy.context.view_layer.objects.active=base;bpy.ops.object.join()
remesh=base.modifiers.new('Joined cream and microbubble surface','REMESH');remesh.mode='VOXEL';remesh.voxel_size=.0038;remesh.use_smooth_shade=True;bpy.ops.object.modifier_apply(modifier=remesh.name)
smooth=base.modifiers.new('Soft foam junctions','SMOOTH');smooth.factor=.35;smooth.iterations=2;bpy.ops.object.modifier_apply(modifier=smooth.name)
decimate=base.modifiers.new('Menu triangle budget','DECIMATE');decimate.ratio=.22;bpy.ops.object.modifier_apply(modifier=decimate.name)
for f in base.data.polygons:f.use_smooth=True
color=base.data.color_attributes.new(name='FoamShading',type='FLOAT_COLOR',domain='POINT')
for v,c in zip(base.data.vertices,color.data):
    p=v.co;shade=.95+.05*math.sin(p.x*49+p.y*61+p.z*53);c.color=(shade,shade,shade,1.)
base.name='SM_MenuDenseBrushFoam';base.data.name=base.name;file=OUT/(base.name+'.fbx')
bpy.ops.object.select_all(action='DESELECT');base.select_set(True);bpy.context.view_layer.objects.active=base
bpy.ops.export_scene.fbx(filepath=str(file),use_selection=True,object_types={'MESH'},add_leaf_bones=False,bake_anim=False,axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='FACE',use_mesh_modifiers=True)
base.data.calc_loop_triangles();verts=[v.co for v in base.data.vertices]
report={'source':bpy.data.filepath,'source_sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'fbx':str(file),'fbx_sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'vertices':len(verts),'triangles':len(base.data.loop_triangles),'bubble_caps':185,'radii_cm':[float(rx),float(ry)],'normal_in_bone':normal.tolist(),'center_in_bone_cm':center.tolist(),'bristle_tip_normal_cm':tip,'bone_attachment_scale':.01,'bounds_unreal_cm':[[min(v.x for v in verts)*100,-max(v.y for v in verts)*100,min(v.z for v in verts)*100],[max(v.x for v in verts)*100,-min(v.y for v in verts)*100,max(v.z for v in verts)*100]]}
(OUT/'ExportReport.json').write_text(json.dumps(report,indent=2),encoding='utf8');print('DENSE_BRUSH_FOAM_EXPORTED',json.dumps(report),flush=True)



