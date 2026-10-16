"""Deterministic reference-B tooth worker. Background-only; no live DCC changes.

Run: blender.exe -b --factory-startup --threads 2 -P build_worker.py -- --preview
Geometry faces +Y; backpack faces -Y; local ground plane is Z=0.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

ROOT = Path(r"E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1")
OUT = ROOT / "Worker"
PI = math.pi
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
parser = argparse.ArgumentParser()
parser.add_argument("--preview", action="store_true")
OPT = parser.parse_args(ARGS)


def material(name, color, roughness, coat=0.0, subsurface=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    p = mat.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Roughness"].default_value = roughness
    p.inputs["Coat Weight"].default_value = coat
    p.inputs["Coat Roughness"].default_value = 0.18
    p.inputs["Subsurface Weight"].default_value = subsurface
    mat.diffuse_color = (*color, 1)
    return mat


def mesh_object(name, verts, faces, mat, parent=None):
    mesh = bpy.data.meshes.new(name + "_Mesh")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.parent = parent or WORKER
    mesh.materials.append(mat)
    for poly in mesh.polygons:
        poly.use_smooth = True
    return obj


def catmull(points, steps=5, closed=False):
    ps = [Vector(p) for p in points]
    extended = [ps[-1]] + ps + [ps[0], ps[1]] if closed else [ps[0]] + ps + [ps[-1]]
    out = []
    count = len(ps) if closed else len(ps) - 1
    for i in range(count):
        p0, p1, p2, p3 = extended[i:i+4]
        for j in range(steps):
            t = j / steps
            out.append(0.5 * ((2*p1) + (-p0+p2)*t + (2*p0-5*p1+4*p2-p3)*t*t + (-p0+3*p1-3*p2+p3)*t*t*t))
    if not closed:
        out.append(ps[-1])
    return out


def ellipsoid(name, center, scale, mat, exponent=1.0, segments=32, rings=18):
    def power(v):
        return math.copysign(abs(v)**exponent, v)
    verts = [tuple(Vector(center) + Vector((0, 0, scale[2])))]
    for i in range(1, rings):
        th = PI*i/rings
        for j in range(segments):
            ph = 2*PI*j/segments
            verts.append(tuple(Vector(center) + Vector((scale[0]*power(math.sin(th)*math.cos(ph)), scale[1]*power(math.sin(th)*math.sin(ph)), scale[2]*power(math.cos(th))))))
    verts.append(tuple(Vector(center) - Vector((0,0,scale[2]))))
    faces = []
    for j in range(segments):
        faces.append((0, 1+j, 1+(j+1)%segments))
    for i in range(rings-2):
        a, b = 1+i*segments, 1+(i+1)*segments
        for j in range(segments):
            k = (j+1)%segments
            faces.append((a+j,b+j,b+k,a+k))
    pole = len(verts)-1
    a = 1+(rings-2)*segments
    for j in range(segments):
        faces.append((a+j,pole,a+(j+1)%segments))
    return mesh_object(name, verts, faces, mat)


def tube(name, points, radius, mat, sides=12, steps=5):
    path = catmull(points, steps=steps, closed=False)
    verts, faces = [], []
    radii = radius if isinstance(radius, (tuple, list)) else (radius, radius)
    prev = Vector((0,1,0))
    for i, p in enumerate(path):
        tangent = (path[min(i+1,len(path)-1)]-path[max(i-1,0)]).normalized()
        normal = prev - tangent*prev.dot(tangent)
        if normal.length < 1e-5:
            normal = Vector((1,0,0)) - tangent*tangent.x
        normal.normalize()
        other = tangent.cross(normal).normalized()
        prev = normal
        for j in range(sides):
            a = 2*PI*j/sides
            verts.append(tuple(p + radii[0]*math.cos(a)*normal + radii[1]*math.sin(a)*other))
    for i in range(len(path)-1):
        for j in range(sides):
            k=(j+1)%sides
            faces.append((i*sides+j,(i+1)*sides+j,(i+1)*sides+k,i*sides+k))
    faces += [tuple(reversed(range(sides))),tuple((len(path)-1)*sides+j for j in range(sides))]
    return mesh_object(name, verts, faces, mat)


def ribbon(name, points, width, thickness, mat, closed=False):
    path=catmull(points, steps=5,closed=closed)
    verts, faces=[],[]
    # Thin rounded rectangular webbing cross section: X is strap width.
    section=[(-.5,-.24),(-.42,-.5),(.42,-.5),(.5,-.24),(.5,.24),(.42,.5),(-.42,.5),(-.5,.24)]
    for i,p in enumerate(path):
        tang=(path[min(i+1,len(path)-1)]-path[max(i-1,0)]).normalized()
        across=Vector((1,0,0)); across=(across-tang*across.dot(tang)).normalized()
        normal=tang.cross(across).normalized()
        for a,b in section:
            verts.append(tuple(p + across*(a*width) + normal*(b*thickness)))
    for i in range(len(path)if closed else len(path)-1):
        next_i=(i+1)%len(path)
        for j in range(8):
            faces.append((i*8+j,next_i*8+j,next_i*8+(j+1)%8,i*8+(j+1)%8))
    if not closed:faces.extend([tuple(reversed(range(8))),tuple((len(path)-1)*8+j for j in range(8))])
    return mesh_object(name,verts,faces,mat)


def build_tooth():
    # Designed tooth outline: two rounded crown lobes, inward waist, two tapered
    # roots and a genuine inter-root notch. It is one continuous closed mesh.
    outline=[(0,0,1.87),(.18,0,2.015),(.43,0,2.045),(.63,0,1.95),(.73,0,1.73),(.73,0,1.45),(.65,0,1.10),(.57,0,.75),(.59,0,.24),(.52,0,.045),(.27,0,.038),(.16,0,.14),(.10,0,.39),(0,0,.465),(-.11,0,.38),(-.17,0,.14),(-.29,0,.03),(-.55,0,.047),(-.62,0,.24),(-.59,0,.72),(-.66,0,1.08),(-.75,0,1.43),(-.73,0,1.76),(-.59,0,1.98),(-.35,0,2.06),(-.15,0,2.02)]
    edge=catmull(outline, steps=4, closed=True)
    n=len(edge); rings=22; center=Vector((0,0,1.08)); depth=.385
    verts=[(0,-depth,1.08)]
    for i in range(1,rings):
        th=PI*i/rings
        s=math.sin(th)
        y=-depth*math.copysign(abs(math.cos(th))**.68,math.cos(th))
        for p in edge:
            # crown plane has controlled shallow enamel volume, without a pill
            # shoulder seam or disconnected root cylinders.
            v=center+(p-center)*s
            verts.append((v.x,y,v.z))
    verts.append((0,depth,1.08))
    faces=[]
    for j in range(n): faces.append((0,1+j,1+(j+1)%n))
    for i in range(rings-2):
        a=1+i*n; b=a+n
        for j in range(n): faces.append((a+j,b+j,b+(j+1)%n,a+(j+1)%n))
    bottom=len(verts)-1; a=1+(rings-2)*n
    for j in range(n):faces.append((a+j,bottom,a+(j+1)%n))
    obj=mesh_object("Worker_Tooth_ContinuousCrownAndRoots",verts,faces,ENAMEL)
    mod=obj.modifiers.new("SilhouettePolish",'SUBSURF'); mod.levels=1; mod.render_levels=1
    return obj


def fuse_enamel_body():
    """Continuous body/arm/palm skin: remove visible capped tube seams."""
    names=['Worker_Tooth_ContinuousCrownAndRoots','Worker_LeftArm','Worker_LeftMitten','Worker_RightArm','Worker_RightGripPalm']
    objects=[bpy.data.objects[n]for n in names]
    bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get()
    verts=[];faces=[]
    for obj in objects:
        eo=obj.evaluated_get(dg);me=eo.to_mesh();offset=len(verts)
        verts.extend([tuple(obj.matrix_world@v.co)for v in me.vertices])
        faces.extend([tuple(offset+i for i in p.vertices)for p in me.polygons]);eo.to_mesh_clear()
    for obj in objects:bpy.data.objects.remove(obj,do_unlink=True)
    obj=mesh_object('Worker_Tooth_ContinuousCrownAndRoots',verts,faces,ENAMEL)
    voxel=obj.modifiers.new('ContinuousShoulderAndWristFusion','REMESH');voxel.mode='VOXEL';voxel.voxel_size=.018;voxel.use_smooth_shade=True
    smooth=obj.modifiers.new('LocalJointSurfacePolish','SMOOTH');smooth.factor=.50;smooth.iterations=4
    bpy.context.view_layer.update();eo=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());me=bpy.data.meshes.new_from_object(eo);obj.modifiers.clear();obj.data=me
    me.calc_loop_triangles();n=len(me.loop_triangles)
    dec=obj.modifiers.new('SmoothRuntimeTopology','DECIMATE');dec.ratio=min(1.0,24500/max(n,1));dec.use_collapse_triangulate=True
    bpy.context.view_layer.update();eo=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());final=bpy.data.meshes.new_from_object(eo);obj.modifiers.clear();obj.data=final
    for p in final.polygons:p.use_smooth=True
    obj['construction']='One continuous manifold enamel body/roots/arms/palms via controlled voxel fusion; fine grip fingers remain separate fitted pieces.'


def bristle_bundle():
    """Dense short fine strands, all in one semantic mesh instead of beads."""
    verts=[];faces=[];sides=6
    for row in range(27):
        z=1.92+row*.018
        for col in range(12):
            x=1.18+(z-1.94)*.1+(col-5.5)*.017
            r=.0067;seed=row*37+col*13
            ytip=-.251-.008*math.sin(seed*1.73)
            offset=len(verts)
            for layer,y in enumerate((-.092,ytip)):
                for j in range(sides):
                    a=2*PI*j/sides
                    verts.append((x+r*math.cos(a)+(0.004*math.sin(seed)if layer else 0),y,z+r*math.sin(a)))
            for j in range(sides):faces.append((offset+j,offset+(j+1)%sides,offset+sides+(j+1)%sides,offset+sides+j))
            faces.extend([tuple(offset+j for j in reversed(range(sides))),tuple(offset+sides+j for j in range(sides))])
    return mesh_object('Brush_Bristles_DenseFineFilaments',verts,faces,BRISTLES)


def unwrap_all():
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for obj in meshes: obj.select_set(True)
    bpy.context.view_layer.objects.active=meshes[0]
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.02,area_weight=.3,correct_aspect=True,scale_to_bounds=True)
    bpy.ops.uv.pack_islands(rotate=True,margin_method='FRACTION',margin=.008)
    bpy.ops.object.mode_set(mode='OBJECT')
    for obj in meshes:
        obj.data.uv_layers.active.name="UV0"


def camera(name,position,target,scale):
    data=bpy.data.cameras.new(name);obj=bpy.data.objects.new(name,data);PREVIEW.objects.link(obj)
    obj.location=position;obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    data.type='ORTHO';data.ortho_scale=scale
    return obj


def area(name,position,power,size,target=(0,0,1)):
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='DISK';d.size=size
    o=bpy.data.objects.new(name,d);PREVIEW.objects.link(o);o.location=position;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()


def metrics():
    dg=bpy.context.evaluated_depsgraph_get();parts=[];allverts=[]
    for o in bpy.context.scene.objects:
        if o.type!='MESH':continue
        eo=o.evaluated_get(dg);m=eo.to_mesh();m.calc_loop_triangles()
        parts.append({'name':o.name,'vertices':len(m.vertices),'triangles':len(m.loop_triangles),'materials':[x.name for x in o.data.materials],'uv0':bool(o.data.uv_layers)})
        allverts.extend([o.matrix_world@v.co for v in m.vertices]);eo.to_mesh_clear()
    bounds=[[min(v[i] for v in allverts),max(v[i] for v in allverts)]for i in range(3)]
    return {'blender':bpy.app.version_string,'root':WORKER.name,'forward':'+Y','ground_z':0,'bounds_xyz':bounds,'evaluated_triangles':sum(p['triangles'] for p in parts),'parts':parts,'static_pose':'back-facing three-quarter; right hand grips standing toothbrush','uv_note':'UV0 multi-object smart-project with global island packing; distinct stable material slots.'}


print('Blender',bpy.app.version_string)
OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.context.collection.name='MenuWorker_Geometry'
WORKER=bpy.data.objects.new("Menu_ToothWorker_Root",None);bpy.context.collection.objects.link(WORKER)
WORKER["asset_role"]="Reference B small tooth worker; local root feet rest at Z0"
ENAMEL=material('MC_WorkerEnamel',(.88,.785,.65),.255,.28,.045)
MINT=material('MC_MintEquipment',(.028,.53,.39),.31,.18)
YELLOW=material('MC_YellowStraps',(.95,.60,.055),.38,.08)
BRISTLES=material('MC_BrushBristles',(.93,.91,.82),.43,.05)
ACCENTS=material('MC_Accents',(.035,.09,.085),.38,.12)
build_tooth()
ellipsoid('Backpack_MainFittedShell',(0,-.46,.98),(.44,.215,.355),MINT,.48)
ellipsoid('Backpack_FrontPocket',(0,-.667,.89),(.315,.052,.192),MINT,.52,32,16)
ellipsoid('Backpack_TopFold',(0,-.56,1.295),(.4,.12,.082),MINT,.65,32,12)
for x in (-.255,.255):
    ribbon('Backpack_YellowHarness_L' if x<0 else 'Backpack_YellowHarness_R',[(x,-.728,.67),(x,-.73,.91),(x,-.717,1.17),(x,-.65,1.30),(x,-.48,1.342),(x,-.30,1.29),(x,-.245,1.10),(x,-.25,.77),(x,-.40,.635),(x,-.60,.635)],.095,.035,YELLOW,closed=True)
    ellipsoid('Backpack_StrapEnd_L' if x<0 else 'Backpack_StrapEnd_R',(x,-.753,.745),(.061,.032,.083),YELLOW,.55,20,12)
# Rounded shoulder/forearm volumes remain physically connected to body and hands.
tube('Worker_LeftArm',[(-.57,.0,1.08),(-.76,-.015,.96),(-.82,-.07,.77)],(.105,.115),ENAMEL,16)
ellipsoid('Worker_LeftMitten',(-.82,-.075,.755),(.13,.115,.16),ENAMEL,.85,24,16)
tube('Worker_RightArm',[(.57,-.01,1.10),(.75,-.07,.99),(.90,-.13,1.075),(.985,-.115,1.14)],(.105,.11),ENAMEL,16)
ellipsoid('Worker_RightGripPalm',(.99,-.105,1.14),(.13,.10,.16),ENAMEL,.95,24,16)
fuse_enamel_body()
# Toothbrush is leaning outward as in B, attached to a closed finger grip.
brush_bottom=Vector((.90,-.075,.075));brush_top=Vector((1.23,-.045,2.34))
tube('Brush_LongMintHandle',[tuple(brush_bottom),(.98,-.066,.61),(1.08,-.055,1.3),(1.17,-.05,1.94)],(.053,.065),MINT,16)
tube('Brush_HandleGripInset',[(.93,-.13,.27),(.97,-.123,.54),(1.00,-.117,.77)],(.032,.021),ACCENTS,10)
head=ellipsoid('Brush_MintHead',(1.205,-.045,2.15),(.125,.074,.30),MINT,.58,28,18)
# Head geometry is authored in worker-local coordinates; rotating its object
# would orbit it around the worker root and detach it from the bristles.
ellipsoid('Brush_WhiteBristleBed',(1.205,-.116,2.153),(.104,.024,.255),BRISTLES,.50,28,18)
bristle_bundle()
for z in (1.065,1.135,1.205):
    cx=.90+(z-.075)*(1.17-.90)/(1.94-.075)
    points=[]
    for k in range(8):
        a=math.radians(150+240*k/7)
        points.append((cx+.086*math.cos(a),-.059+.086*math.sin(a),z))
    tube(f'Worker_GripFinger_{z:.3f}',points,.034,ENAMEL,10,2)
tube('Worker_GripThumb',[(.93,-.15,1.25),(1.025,-.19,1.245),(1.12,-.14,1.21)],.043,ENAMEL,12,4)
# A small face is inferred for reverse inspection; it is hidden in the B pose.
for x in (-.22,.22):
    ellipsoid('Worker_Eye_L' if x<0 else 'Worker_Eye_R',(x,.377,1.37),(.043,.022,.067),ACCENTS,1,20,12)
tube('Worker_Smile',[(-.12,.389,1.17),(0,.40,1.135),(.12,.389,1.17)],.013,ACCENTS,8,4)
# Place the evaluated root-foot minimum precisely on local Z0 while keeping
# the semantic parent itself at identity for assembly placement.
bpy.context.view_layer.update()
body=bpy.data.objects['Worker_Tooth_ContinuousCrownAndRoots']
evbody=body.evaluated_get(bpy.context.evaluated_depsgraph_get());evmesh=evbody.to_mesh()
ground=min(v.co.z for v in evmesh.vertices);evbody.to_mesh_clear()
for child in WORKER.children_recursive:
    if child.type=='MESH':
        for vertex in child.data.vertices:vertex.co.z-=ground
unwrap_all()
WORKER.rotation_euler.z=math.radians(-13)
WORKER['static_pose']='reference-B back view, turned 13 degrees'
scene=bpy.context.scene
PREVIEW=bpy.data.collections.new('Worker_PreviewOnly');scene.collection.children.link(PREVIEW)
scene.render.engine='CYCLES';scene.cycles.samples=24;scene.cycles.use_denoising=True;scene.cycles.device='CPU'
scene.render.threads_mode='FIXED';scene.render.threads=2
scene.world.color=(.075,.075,.075)
scene.view_settings.view_transform='AgX'
scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
scene.world.use_nodes=True;scene.world.node_tree.nodes.get('Background').inputs[0].default_value=(.13,.17,.17,1);scene.world.node_tree.nodes.get('Background').inputs[1].default_value=.25
area('Preview_Key',(-3,-5,6),450,4)
area('Preview_Fill',(4,-2,3),250,3)
area('Preview_Rim',(1,4,5),500,3)
cam=camera('Worker_ReferenceB_BackThreeQuarter',(-3.4,-7.5,3.3),(.15,0,1.22),3.1)
scene.camera=cam
bpy.context.view_layer.update()
report=metrics();(OUT/'WorkerMetrics.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
# Export only authored root and child meshes; never cameras or lights.
bpy.ops.object.select_all(action='DESELECT')
WORKER.select_set(True)
for o in WORKER.children_recursive:
    if o.type=='MESH':o.select_set(True)
bpy.context.view_layer.objects.active=WORKER
bpy.ops.export_scene.gltf(filepath=str(OUT/'ToothWorker.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
bpy.ops.wm.obj_export(filepath=str(OUT/'ToothWorker.obj'),export_selected_objects=True,forward_axis='Y',up_axis='Z',apply_modifiers=True,export_materials=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'ToothWorker.blend'))
if OPT.preview:
    scene.render.filepath=str(OUT/'Worker_BackThreeQuarter.png');bpy.ops.render.render(write_still=True)
    scene.camera=camera('Worker_Front',(0, -0.1,0),(.0,0,1),3.0)
    scene.camera.location=(3.5,7.0,3.0);scene.camera.rotation_euler=(Vector((.15,0,1.2))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=str(OUT/'Worker_FrontThreeQuarter.png');bpy.ops.render.render(write_still=True)
print('WORKER_READY',json.dumps({'out':str(OUT),'triangles':report['evaluated_triangles'],'bounds':report['bounds_xyz']}))
