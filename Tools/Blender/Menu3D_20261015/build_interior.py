"""Dimensional reference-B oral cavity and tongue; isolated background build."""
import argparse
import json
import math
import sys
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

ROOT=Path(r"E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1")
OUT=ROOT/'Interior'
PI=math.pi
ARGS=sys.argv[sys.argv.index('--')+1:]if'--'in sys.argv else[]
if '--v03' in ARGS:
    # V2 is retained as the rejected comparison baseline. New anatomy writes
    # only Interior/V03 until the lead accepts it in the master assembly.
    import runpy
    ARGS.remove('--v03')
    sys.argv=[sys.argv[0],'--']+ARGS
    runpy.run_path(str(OUT/'Source'/'build_interior_v03.py'),run_name='__main__')
    raise SystemExit(0)
parser=argparse.ArgumentParser();parser.add_argument('--preview',action='store_true');OPT=parser.parse_args(ARGS)


def material(name,color,roughness,coat,subsurface):
    m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=roughness
    p.inputs['Coat Weight'].default_value=coat;p.inputs['Coat Roughness'].default_value=.22
    p.inputs['Subsurface Weight'].default_value=subsurface;m.diffuse_color=(*color,1)
    return m


def mesh_object(name,verts,faces,mat):
    me=bpy.data.meshes.new(name+'_Mesh');me.from_pydata(verts,[],faces);me.update()
    bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(me);bm.free()
    ob=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(ob);ob.parent=INTERIOR;me.materials.append(mat)
    for p in me.polygons:p.use_smooth=True
    return ob


def build_cavity():
    # Open-front continuous concave shell. Its front ring is behind the jaw
    # surfaces, so no cheek rim can occlude the enamel at y≈0.
    around=128;depth=42;verts=[]
    for i in range(depth):
        ph=(PI*.5)*i/depth
        s=math.cos(ph);y=.82+7.35*math.sin(ph)
        for j in range(around):
            a=2*PI*j/around
            organic=1+.010*math.sin(3*a+ph*2)+.007*math.cos(7*a-ph*3)
            x=7.86*s*math.cos(a)*organic
            z=.08+3.30*s*math.sin(a)*organic
            # Slight asymmetric cheek bulge; smooth low-frequency geometry.
            yy=y+.10*math.sin(a*2)*s*s
            verts.append((x,yy,z))
    verts.append((0,8.17,.08));pole=len(verts)-1;faces=[]
    for i in range(depth-1):
        for j in range(around):
            k=(j+1)%around
            faces.append((i*around+j,i*around+k,(i+1)*around+k,(i+1)*around+j))
    a=(depth-1)*around
    for j in range(around):faces.append((a+j,a+(j+1)%around,pole))
    obj=mesh_object('Interior_ContinuousCheeksAndPalate',verts,faces,MUCOSA)
    # Reverse to face the interior; solidify seals outer return surface.
    for poly in obj.data.polygons:poly.flip()
    mod=obj.modifiers.new('MucosaWallThickness','SOLIDIFY');mod.thickness=.14;mod.offset=1
    # Bake wall construction into the editable mesh BEFORE global UV packing.
    # Leaving Solidify live duplicates the inner UVs onto the hidden exterior,
    # which causes conflicting AO/normal bakes in Painter.
    bpy.context.view_layer.update()
    evaluated=obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    thick_mesh=bpy.data.meshes.new_from_object(evaluated)
    thick_mesh.name='Interior_ContinuousCheeksAndPalate_ClosedWallMesh'
    obj.modifiers.clear();obj.data=thick_mesh
    return obj


def fold(name,y):
    # Shallow broad folds follow the upper palate, attached to its actual shell.
    segments=64;sides=10;verts=[];faces=[]
    ph=math.asin((y-.82)/7.35);scale=math.cos(ph)
    for i in range(segments):
        a=.13+(PI-.26)*i/(segments-1)
        center=Vector((7.86*scale*math.cos(a),y,.08+3.30*scale*math.sin(a)))
        inward=Vector((-math.cos(a)/7.86,0,-math.sin(a)/3.30)).normalized()
        center+=inward*.035
        for j in range(sides):
            t=2*PI*j/sides
            verts.append(tuple(center+Vector((0,1,0))*(.105*math.cos(t))+inward*(.065*math.sin(t))))
    for i in range(segments-1):
        for j in range(sides):faces.append((i*sides+j,(i+1)*sides+j,(i+1)*sides+(j+1)%sides,i*sides+(j+1)%sides))
    faces.extend([tuple(reversed(range(sides))),tuple((segments-1)*sides+j for j in range(sides))])
    return mesh_object(name,verts,faces,MUCOSA)


def build_tongue():
    nx=56;ny=36;verts=[]
    def top(u,v):
        y=-.56+7.15*v
        width=6.86+.30*math.sin(v*PI)-2.5*(v**3)
        # broad soft front with lightly receding center; cheeks remain readable
        xx=(2*u-1)*width
        yy=y+.86*abs(2*u-1)**3.2*(1-v)**6-.08*(1-v)**4*(1-(2*u-1)**2)
        z=-2.03+.64*math.sin(v*PI)**.7*(1-(2*u-1)**2)
        z+=.12*abs(2*u-1)**3
        z-=.055*math.exp(-((2*u-1)/.085)**2)*math.sin(v*PI)**.7
        z+=.018*math.sin(xx*.75+v*2)*math.sin(v*PI)
        return(xx,yy,z)
    for layer in (0,1):
        for j in range(ny+1):
            for i in range(nx+1):
                x,y,z=top(i/nx,j/ny)
                if layer:z-=.50+.18*math.sin(j/ny*PI)
                verts.append((x,y,z))
    count=(nx+1)*(ny+1);faces=[];uvfaces=[]
    def index(i,j,layer=0):return layer*count+j*(nx+1)+i
    for layer in(0,1):
        for j in range(ny):
            for i in range(nx):
                ids=(index(i,j,layer),index(i+1,j,layer),index(i+1,j+1,layer),index(i,j+1,layer))
                uv=[(.04+.92*i/nx,(.035+.73*j/ny)if layer==0 else(.835+.13*j/ny)),(.04+.92*(i+1)/nx,(.035+.73*j/ny)if layer==0 else(.835+.13*j/ny)),(.04+.92*(i+1)/nx,(.035+.73*(j+1)/ny)if layer==0 else(.835+.13*(j+1)/ny)),(.04+.92*i/nx,(.035+.73*(j+1)/ny)if layer==0 else(.835+.13*(j+1)/ny))]
                if layer:ids=tuple(reversed(ids));uv=list(reversed(uv))
                faces.append(ids);uvfaces.append(uv)
    border=[index(i,0)for i in range(nx+1)]+[index(nx,j)for j in range(1,ny+1)]+[index(i,ny)for i in range(nx-1,-1,-1)]+[index(0,j)for j in range(ny-1,0,-1)]
    # Explicit rounded perimeter rolls replace the thin straight plate edge.
    side_rings=[border]
    for t in (1/3,2/3):
        ring=[]
        for k,a in enumerate(border):
            p=Vector(verts[a]);q=Vector(verts[a+count]);prev=Vector(verts[border[k-1]]);nxt=Vector(verts[border[(k+1)%len(border)]])
            tangent=nxt-prev;outward=Vector((tangent.y,-tangent.x,0)).normalized()
            point=p.lerp(q,t)+outward*(.12*math.sin(PI*t))
            ring.append(len(verts));verts.append(tuple(point))
        side_rings.append(ring)
    side_rings.append([a+count for a in border])
    for r in range(3):
        for k,a in enumerate(side_rings[r]):
            kk=(k+1)%len(border);b=side_rings[r][kk];c=side_rings[r+1][kk];d=side_rings[r+1][k]
            faces.append((a,b,c,d));u0=.04+.92*k/len(border);u1=.04+.92*(k+1)/len(border);v0=.788+.024*r/3;v1=.788+.024*(r+1)/3
            uvfaces.append([(u0,v0),(u1,v0),(u1,v1),(u0,v1)])
    obj=mesh_object('Interior_BroadDorsalTongue',verts,faces,TONGUE)
    uv=obj.data.uv_layers.new(name='UV0')
    # from_pydata preserves polygon order, recalc normals may reverse winding.
    # Derive each corner's map by vertex identity, so orientation stays correct.
    for p,ids,mapping in zip(obj.data.polygons,faces,uvfaces):
        lookup=dict(zip(ids,mapping))
        for li in p.loop_indices:uv.data[li].uv=lookup[obj.data.loops[li].vertex_index]
    obj['uv_contract']='dorsal U left-right [.04,.96], V front-back [.035,.765]; underside V [.835,.965]; perimeter [.788,.812]'
    mod=obj.modifiers.new('TongueSilhouettePolish','SUBSURF');mod.levels=1;mod.render_levels=1
    return obj


def unwrap_mucosa():
    objects=[o for o in INTERIOR.children_recursive if o.type=='MESH' and o.data.materials[0]==MUCOSA]
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(75),island_margin=.025,area_weight=.4,correct_aspect=True,scale_to_bounds=True)
    bpy.ops.uv.pack_islands(rotate=True,margin_method='FRACTION',margin=.012);bpy.ops.object.mode_set(mode='OBJECT')
    for o in objects:o.data.uv_layers.active.name='UV0'


def area(name,pos,power,size):
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='DISK';d.size=size;o=bpy.data.objects.new(name,d);PREVIEW.objects.link(o);o.location=pos;o.rotation_euler=(Vector((0,3,-.2))-o.location).to_track_quat('-Z','Y').to_euler()


print('Blender',bpy.app.version_string);OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.context.collection.name='MenuInterior_Geometry'
INTERIOR=bpy.data.objects.new('Menu_MouthInterior_Root',None);bpy.context.collection.objects.link(INTERIOR)
INTERIOR['asset_role']='Static dimensional oral interior behind independently translating jaws'
MUCOSA=material('MC_Interior',(.17,.009,.018),.32,.20,.06)
TONGUE=material('MC_Tongue',(.56,.062,.091),.29,.24,.075)
build_cavity()
for i,y in enumerate((2.4,4.2,5.7)):fold(f'Interior_AttachedPalatalFold_{i+1}',y)
build_tongue();unwrap_mucosa()
scene=bpy.context.scene;PREVIEW=bpy.data.collections.new('Interior_PreviewOnly');scene.collection.children.link(PREVIEW);scene.render.engine='CYCLES';scene.cycles.samples=20;scene.cycles.use_denoising=True;scene.cycles.device='CPU';scene.render.threads_mode='FIXED';scene.render.threads=2
scene.world.use_nodes=True;scene.world.node_tree.nodes.get('Background').inputs[0].default_value=(.045,.025,.03,1);scene.world.node_tree.nodes.get('Background').inputs[1].default_value=.18
scene.view_settings.view_transform='AgX';scene.render.resolution_x=640;scene.render.resolution_y=360;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG'
area('Interior_PreviewKey',(-5,-6,7),1300,7);area('Interior_PreviewFill',(6,-3,4),650,6);area('Interior_PreviewTongue',(0,-3,-.1),180,5)
data=bpy.data.cameras.new('Interior_ReferenceB_OpenFront');cam=bpy.data.objects.new(data.name,data);PREVIEW.objects.link(cam);cam.location=(0,-24,4.2);cam.rotation_euler=(Vector((0,3,-.1))-cam.location).to_track_quat('-Z','Y').to_euler();data.type='ORTHO';data.ortho_scale=17.6;scene.camera=cam
bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();parts=[];vv=[]
for o in INTERIOR.children_recursive:
    if o.type!='MESH':continue
    eo=o.evaluated_get(dg);m=eo.to_mesh();m.calc_loop_triangles();parts.append({'name':o.name,'triangles':len(m.loop_triangles),'vertices':len(m.vertices),'materials':[x.name for x in o.data.materials],'uv0':bool(o.data.uv_layers)});vv.extend([o.matrix_world@v.co for v in m.vertices]);eo.to_mesh_clear()
report={'blender':bpy.app.version_string,'root':INTERIOR.name,'bounds_xyz':[[min(v[i]for v in vv),max(v[i]for v in vv)]for i in range(3)],'evaluated_triangles':sum(p['triangles']for p in parts),'parts':parts,'tongue_uv':'Dorsal U left/right and V front/back, majority UV area. Separate underside and perimeter islands.','front_ring_y':.82,'closed_open_policy':'Interior static; hidden by jaw surfaces when closed; geometry recedes along +Y.'}
(OUT/'InteriorMetrics.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
bpy.ops.object.select_all(action='DESELECT');INTERIOR.select_set(True)
for o in INTERIOR.children_recursive:
    if o.type=='MESH':o.select_set(True)
bpy.context.view_layer.objects.active=INTERIOR
bpy.ops.export_scene.gltf(filepath=str(OUT/'MouthInterior.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
bpy.ops.wm.obj_export(filepath=str(OUT/'MouthInterior.obj'),export_selected_objects=True,forward_axis='Y',up_axis='Z',apply_modifiers=True,export_materials=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MouthInterior.blend'))
if OPT.preview:
    scene.render.filepath=str(OUT/'Interior_OpenFront.png');bpy.ops.render.render(write_still=True)
print('INTERIOR_READY',json.dumps({'out':str(OUT),'triangles':report['evaluated_triangles'],'bounds':report['bounds_xyz']}))
