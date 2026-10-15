"""Original CoffeeSlime asset. Run only in isolated Blender --background --factory-startup.

Geometry, image materials, five-bone skin, portable baked actions and fixed review cameras.
No downloads, existing .blend mutation, Unreal import or project-package writes.
"""
import argparse, json, math, os, sys
from pathlib import Path
import bpy
from mathutils import Vector, Matrix

BASE = Path(__file__).resolve().parent / 'v1'
for folder in ('exports', 'textures', 'review'):
    (BASE / folder).mkdir(parents=True, exist_ok=True)
args = argparse.ArgumentParser()
args.add_argument('--stage', choices=['author', 'export', 'stills', 'motion'], default='author')
args.add_argument('--fbx-subdir', default='exports')
opt = args.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])

MATERIAL_NAMES = ['M_CoffeeGel', 'M_MilkFoam', 'M_Ivory', 'M_MauveLid', 'M_EspressoDark', 'M_CaramelLip']
CLIPS = {
    'A_CoffeeSlime_Idle': (121, True, [1, 31, 61, 91, 121]),
    'A_CoffeeSlime_Hop': (91, True, [1, 17, 27, 44, 61, 71, 77, 85, 91]),
    'A_CoffeeSlime_Hurt': (29, False, [1, 5, 12, 20, 29]),
    'A_CoffeeSlime_PreBurst': (61, False, [1, 16, 31, 46, 61]),
    'A_CoffeeSlime_Burst': (19, False, [1, 5, 10, 19]),
}

def select(*objs):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objs:
        obj.select_set(True)
    if objs:
        bpy.context.view_layer.objects.active = objs[0]

def collection(name):
    c = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    if c.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(c)
    return c

def move_collection(obj, target):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    target.objects.link(obj)

def material(name, color, roughness, coat=0):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Roughness'].default_value = roughness
    p.inputs['Coat Weight'].default_value = coat
    p.inputs['Coat Roughness'].default_value = .10
    return m

def image_pattern(name, color, rough=False):
    size = 512
    im = bpy.data.images.new(name, width=size, height=size)
    pixels = []
    for y in range(size):
        v = y / (size-1)
        for x in range(size):
            u = x / (size-1)
            wave = .5*math.sin(u*math.tau*5 + .6*math.sin(v*math.tau*3)) + .3*math.cos(v*math.tau*7+u*4)
            grain = math.sin(x*12.9898+y*78.233)*43758.5453
            grain = grain-math.floor(grain)-.5
            if rough:
                a = max(.08, min(.7, color[0] + .035*wave + .012*grain))
                pixels.extend((a,a,a,1))
            else:
                f = 1 + .08*wave + .014*grain
                pixels.extend(tuple(max(0,min(1,c*f)) for c in color)+(1,))
    # Changing colorspace after filling a generated image invalidates its pixel buffer.
    if rough:
        im.colorspace_settings.name = 'Non-Color'
    im.pixels.foreach_set(pixels)
    im.filepath_raw = str(BASE/'textures'/f'{name}.png')
    im.file_format = 'PNG'
    im.save()
    im.pack()
    return im

def image_material(mat, color, roughness):
    nodes=mat.node_tree.nodes; links=mat.node_tree.links
    p=nodes.get('Principled BSDF')
    a=nodes.new('ShaderNodeTexImage'); a.image=image_pattern(mat.name+'_BaseColor',color)
    links.new(a.outputs['Color'],p.inputs['Base Color'])
    b=nodes.new('ShaderNodeTexImage'); b.image=image_pattern(mat.name+'_Roughness',(roughness,)*3,True)
    links.new(b.outputs['Color'],p.inputs['Roughness'])

PARTS=[]
def mesh_part(name, verts, faces, mat, weight='gel_body', uv=None):
    m=bpy.data.meshes.new(name); m.from_pydata(verts,[],faces); m.update()
    o=bpy.data.objects.new(name,m); bpy.context.collection.objects.link(o)
    m.materials.append(mat)
    for p in m.polygons: p.use_smooth=True
    if uv:
        layer=m.uv_layers.new(name='UVMap')
        for p in m.polygons:
            ids=list(p.vertices)
            us=[uv[i][0] for i in ids]
            seam=max(us)-min(us)>.5
            for li,vi in zip(p.loop_indices,ids):
                u,v=uv[vi]
                layer.data[li].uv=(u+1 if seam and u<.5 else u,v)
    else:
        select(o)
        bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(island_margin=.02)
        bpy.ops.object.mode_set(mode='OBJECT')
    if weight:
        g=o.vertex_groups.new(name=weight); g.add(list(range(len(verts))),1,'REPLACE')
    PARTS.append(o)
    return o

def sphere(name, center, scale, mat, weight, segments=32, rings=18):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, location=center)
    o=bpy.context.object; o.name=name; o.scale=scale
    select(o); bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    o.data.materials.append(mat)
    for p in o.data.polygons: p.use_smooth=True
    g=o.vertex_groups.new(name=weight); g.add(list(range(len(o.data.vertices))),1,'REPLACE')
    PARTS.append(o); return o

def tube(name, points, radii, mat, weight, sides=10, closed=False):
    verts=[]; uv=[]; faces=[]; n=len(points)
    for i,p in enumerate(points):
        p=Vector(p)
        prev=Vector(points[(i-1)%n] if closed or i>0 else points[0])
        nxt=Vector(points[(i+1)%n] if closed or i<n-1 else points[-1])
        tangent=(nxt-prev).normalized()
        axis=Vector((1,0,0))
        if abs(tangent.dot(axis))>.95: axis=Vector((0,1,0))
        a=tangent.cross(axis).normalized(); b=tangent.cross(a).normalized()
        for j in range(sides):
            ang=j*math.tau/sides
            verts.append(tuple(p+radii[i]*(a*math.cos(ang)+b*math.sin(ang))))
            uv.append((j/sides,i/max(1,n-1)))
    for i in range(n if closed else n-1):
        for j in range(sides):
            faces.append((i*sides+j,i*sides+(j+1)%sides,((i+1)%n)*sides+(j+1)%sides,((i+1)%n)*sides+j))
    if not closed:
        faces.extend([tuple(reversed(range(sides))),tuple((n-1)*sides+j for j in range(sides))])
    return mesh_part(name,verts,faces,mat,weight,uv)

def catmull(points, count):
    ps=[Vector(points[0])]+[Vector(p) for p in points]+[Vector(points[-1])]
    out=[]
    for i in range(count):
        x=i/(count-1)*(len(points)-1); k=min(len(points)-2,int(x)); t=x-k
        p0,p1,p2,p3=ps[k:k+4]
        out.append(tuple(.5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t*t*t)))
    return out

PROFILE=[(0,0),(.023,.82),(.06,.91),(.13,1),(.23,.95),(.40,.84),(.65,.72),(.82,.60),(1.00,.42),(1.10,.18),(1.14,0)]
def radius(z):
    for i,((a,ra),(b,rb)) in enumerate(zip(PROFILE,PROFILE[1:])):
        if z<=b:
            t=(z-a)/(b-a)
            prev=PROFILE[max(0,i-1)]; nxt=PROFILE[min(len(PROFILE)-1,i+2)]
            ma=(rb-prev[1])/(b-prev[0])
            mb=(nxt[1]-ra)/(nxt[0]-a)
            if i==2: mb=0
            if i==3: ma=0
            return max(0,(2*t**3-3*t*t+1)*ra+(t**3-2*t*t+t)*ma*(b-a)+(-2*t**3+3*t*t)*rb+(t**3-t*t)*mb*(b-a))
    return 0

def front_surface(y,z):
    r=radius(z)
    return .79*r*math.sqrt(max(.01,1-(y/max(.01,.74*r))**2))+.025*z*z

def body(mat):
    n=80; rings=38; verts=[(0,0,0)]; uv=[(.5,0)]; faces=[]
    # Dense lower rings retain broad scalloped pooling without a pinched cone.
    zs=[.015]+[.03+(1.105-.03)*i/(rings-2) for i in range(rings-1)]
    for z in zs:
        for j in range(n):
            a=j*math.tau/n; r=radius(z)
            scallop=1+(.12*math.sin(7*a+.5)+.025*math.cos(11*a))*math.exp(-z/.14)
            asym=1+.022*math.sin(3*a+z*5)
            d=0
            for az,zz,amp in [(1.0,.40,.018),(2.8,.70,.011),(4.7,.30,.013),(5.9,.63,.015)]:
                da=math.atan2(math.sin(a-az),math.cos(a-az))
                d+=amp*math.exp(-(da/.085)**2-((z-zz)/.055)**2)
            verts.append(((.79*r*scallop*asym-d)*math.cos(a)+.025*z*z,
                          (.74*r*scallop*asym-d)*math.sin(a), z))
            uv.append((j/n,z/1.14))
    top=len(verts); verts.append((.035,0,1.14)); uv.append((.5,1))
    for j in range(n): faces.append((0,1+(j+1)%n,1+j))
    for k in range(len(zs)-1):
        for j in range(n):
            a=1+k*n+j; b=1+k*n+(j+1)%n
            faces.append((a,b,b+n,a+n))
    start=1+(len(zs)-1)*n
    for j in range(n): faces.append((start+j,start+(j+1)%n,top))
    o=mesh_part('Coffee scalloped gel',verts,faces,mat,None,uv)
    low=o.vertex_groups.new(name='gel_body'); high=o.vertex_groups.new(name='gel_top')
    for v in o.data.vertices:
        w=max(0,min(.68,(v.co.z-.48)/.6*.68))
        if w<1: low.add([v.index],1-w,'REPLACE')
        if w>0: high.add([v.index],w,'REPLACE')
    return o

def foam(mat):
    n=64; rings=14; verts=[(.02,0,1.09)]; uv=[(.5,0)]; faces=[]
    # Closed irregular dome embedded into body; creamy cap with front drips.
    for k in range(rings):
        theta=.08+(math.pi-.16)*k/(rings-1)
        for j in range(n):
            a=j*math.tau/n
            r=math.sin(theta); wave=1+.07*math.sin(5*a+.2)
            z=1.075+.145*math.cos(theta)
            z-=.045*math.exp(-((math.atan2(math.sin(a-.4),math.cos(a-.4)))/.22)**2)*r*r
            verts.append((.02+.405*r*wave*math.cos(a),.37*r*wave*math.sin(a),z))
            uv.append((j/n,k/(rings-1)))
    bottom=len(verts); verts.append((.02,0,.93)); uv.append((.5,1))
    for j in range(n): faces.append((0,1+j,1+(j+1)%n))
    for k in range(rings-1):
        for j in range(n):
            a=1+k*n+j; b=1+k*n+(j+1)%n
            faces.append((a,a+n,b+n,b))
    start=1+(rings-1)*n
    for j in range(n): faces.append((start+(j+1)%n,start+j,bottom))
    mesh_part('Milk crema cap',verts,faces,mat,'gel_top',uv)
    pts=catmull([(.04,-.06,1.15),(.01,-.21,1.28),(-.03,-.36,1.35),(-.04,-.50,1.32),(-.035,-.54,1.23),(-.01,-.43,1.205)],40)
    radii=[.115*(1-i/39)**.7+.012 for i in range(40)]
    tube('Crema curl',pts,radii,mat,'foam_tip',12)
    sphere('Curl drop',pts[-1],(.033,.038,.038),mat,'foam_tip',24,12)

def eyelid(center, scale, mat):
    # Upper half closed sphere, thick lid rather than a flat decal.
    verts=[]; faces=[]; n=32; rings=10
    for k in range(rings+1):
        theta=.02+(math.pi/2-.02)*k/rings
        for j in range(n):
            a=j*math.tau/n
            # Sphere top +Z, eye-forward +X. Slight angry downward roll.
            x=scale[0]*math.sin(theta)*math.cos(a)
            y=scale[1]*math.sin(theta)*math.sin(a)
            z=scale[2]*math.cos(theta)-.10*y
            verts.append((center[0]+x,center[1]+y,center[2]+z))
    faces.append(tuple(reversed(range(n))))
    for k in range(rings):
        for j in range(n):
            a=k*n+j; b=k*n+(j+1)%n
            faces.append((a,b,b+n,a+n))
    faces.append(tuple(rings*n+j for j in range(n)))
    return mesh_part('Heavy mauve eyelid',verts,faces,mat,'face')

def face(mats):
    ivory,lid,dark,lip=mats[2:]
    for i,y in enumerate([-.255,.255]):
        z=.785+(0.025 if i==0 else 0)
        sphere(f'Ivory eye {i}',(.54,y,z),(.165,.207,.213),ivory,'face')
        # Pupils visible below sleepy lids and inward-facing, no emissive shortcut.
        sphere(f'Pupil {i}',(.697,y-.026*(1 if y>0 else -1),z-.037),(.023,.063,.069),dark,'face',24,14)
        eyelid((.533,y,z+.047),(.183,.220,.192),lid)
        pts=catmull([(.62,y-.19,z+.15),(.63,y-.10,z+.225),(.60,y+.08,z+.235),(.56,y+.18,z+.18)],24)
        tube(f'Caramel brow {i}',pts,[.023+.016*math.sin(math.pi*k/23) for k in range(24)],lip,'face',8)
    cavity=sphere('Mouth dark cavity',(.648,0,.43),(.015,.269,.132),dark,'face',40,20)
    for v in cavity.data.vertices:
        v.co.x=front_surface(v.co.y,v.co.z+.43)+.014-.648+v.co.x
    pts=[]
    for i in range(56):
        a=i*math.tau/56; y=.268*math.cos(a); z=.43+.128*math.sin(a)
        x=front_surface(y,z)+.032
        pts.append((x,y,z))
    tube('Soft caramel lip rim',pts,[.023+.006*max(0,-math.sin(i*math.tau/56)) for i in range(56)],lip,'face',8,True)
    for i,y in enumerate([-.085,.056]):
        o=sphere(f'Tiny uneven tooth {i}',(front_surface(y,.503)+.045,y,.503),(.031,.039,.061 if i==0 else .045),ivory,'face',20,12)

def smooth(t): return t*t*(3-2*t)
def sample_curve(t, points):
    for (a,x),(b,y) in zip(points,points[1:]):
        if t<=b:
            u=max(0,min(1,(t-a)/(b-a))); return x+(y-x)*smooth(u)
    return points[-1][1]

def animate(rig):
    rig.animation_data_create()
    for name,(end,loop,critical) in CLIPS.items():
        a=bpy.data.actions.new(name); a.use_fake_user=True
        rig.animation_data.action=a
        for frame in range(1,end+1):
            t=(frame-1)/100
            body_scale=Vector((1,1,1)); lift=0; recoil=0; tilt=0; top=0; foam=0; squeeze=1
            if 'Idle' in name:
                s=.018*math.sin(math.tau*t/1.2); body_scale=Vector((1-s*.5,1-s*.5,1+s))
                top=.025*math.sin(math.tau*t/1.2+.5); foam=.035*math.sin(math.tau*t/1.2-.45)
            elif 'Hop' in name:
                sz=sample_curve(t,[(0,1),(.162,.64),(.24,1.24),(.432,1.06),(.65,1.16),(.702,.60),(.765,.82),(.82,1.12),(.9,1)])
                body_scale=Vector((1/math.sqrt(sz),1/math.sqrt(sz),sz))
                if .162<t<.702:
                    u=(t-.162)/.54; lift=.34*math.sin(math.pi*u)**.8
                top=.055*math.sin(t*math.tau/.9)*math.sin(math.pi*t/.9)
                foam=.11*math.sin(t*math.tau/.9-1)*math.sin(math.pi*t/.9)
            elif 'Hurt' in name:
                pulse=sample_curve(t,[(0,0),(.04,1),(.12,.7),(.28,0)])
                recoil=-.12*pulse; tilt=-.17*pulse; lift=.10*pulse
                body_scale=Vector((1-.13*pulse,1+.09*pulse,1+.08*pulse))
                squeeze=1-.12*pulse; foam=.16*pulse*math.sin(t*20)
            elif 'PreBurst' in name:
                g=1+.83*smooth(t/.6)
                wob=.027*math.sin(t*math.tau*13)*smooth(t/.6)
                body_scale=Vector((g*(1+wob),g*(1-wob),g*(1+.10*smooth(t/.6))))
                top=.085*math.sin(t*math.tau*12)*smooth(t/.6); foam=.12*math.sin(t*math.tau*10)*smooth(t/.6)
                squeeze=1-.13*smooth(t/.6)
            elif 'Burst' in name:
                u=smooth(t/.18)
                body_scale=Vector((1.83+.20*u,1.83+.20*u,2.013*(1-u)+.035*u))
                squeeze=1-.65*u
            for p in rig.pose.bones:
                p.location=(0,0,0); p.rotation_mode='XYZ'; p.rotation_euler=(0,0,0); p.scale=(1,1,1)
            b=rig.pose.bones['gel_body']
            # Vertical bone local Y is world Z; local Z is negative world Y.
            b.scale=(body_scale.x,body_scale.z,body_scale.y)
            b.location=(recoil,lift,0); b.rotation_euler.z=tilt
            rig.pose.bones['gel_top'].rotation_euler.x=top
            rig.pose.bones['foam_tip'].rotation_euler.x=foam
            rig.pose.bones['face'].scale=(1,squeeze,1)
            for p in rig.pose.bones:
                for prop in ('location','rotation_euler','scale'):
                    p.keyframe_insert(prop,frame=frame,group=p.name)
        # Baked samples use linear interpolation: no runtime modifiers or handlers.
        for layer in a.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:
                    for fc in cb.fcurves:
                        for key in fc.keyframe_points: key.interpolation='LINEAR'
        a['duration_seconds']=(end-1)/100; a['loop']=loop
        a['critical_frames']=critical
    rig.animation_data.action=bpy.data.actions['A_CoffeeSlime_Idle']
    bpy.context.scene.frame_set(1)

def camera(name, position, target, ortho=3.4):
    d=bpy.data.cameras.new(name); o=bpy.data.objects.new(name,d)
    bpy.context.collection.objects.link(o); o.location=position
    o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
    d.type='ORTHO'; d.ortho_scale=ortho; d.lens=65
    move_collection(o,collection('CoffeeSlime.Review')); return o

def lights():
    review=collection('CoffeeSlime.Review')
    for name,pos,energy,size,color in [
        ('Key',(3,-3,4),450,4,(1,.85,.70)),('Fill',(2,3,2.5),220,3,(.70,.82,1)),('Rim',(-2,0,3.4),450,2,(1,.92,.78))]:
        d=bpy.data.lights.new(name,'AREA'); d.energy=energy; d.shape='DISK'; d.size=size; d.color=color
        o=bpy.data.objects.new(name,d); bpy.context.collection.objects.link(o); o.location=pos
        o.rotation_euler=(Vector((0,0,.65))-o.location).to_track_quat('-Z','Y').to_euler(); move_collection(o,review)
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0,0,-.013))
    floor=bpy.context.object; floor.name='Review floor'
    floor.data.materials.append(material('Review slate',(.12,.16,.18),.75)); move_collection(floor,review)
    w=bpy.context.scene.world; w.use_nodes=True
    w.node_tree.nodes['Background'].inputs['Color'].default_value=(.23,.27,.30,1)
    w.node_tree.nodes['Background'].inputs['Strength'].default_value=.35

def author():
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    s=bpy.context.scene; s.name='CoffeeSlime.Source'; s.unit_settings.system='METRIC'; s.unit_settings.scale_length=1
    s.render.fps=100; s.render.engine='BLENDER_EEVEE'; s.render.resolution_x=1000; s.render.resolution_y=1000; s.render.resolution_percentage=100
    s.render.image_settings.file_format='PNG'; s.view_settings.view_transform='AgX'
    mats=[material(n,c,r,k) for n,c,r,k in zip(MATERIAL_NAMES,
        [(.075,.025,.009),(.80,.68,.43),(.88,.83,.68),(.26,.12,.15),(.006,.003,.002),(.23,.08,.02)],
        [.20,.46,.28,.36,.16,.27],[.40,0,.15,.08,.23,.25])]
    image_material(mats[0],(.075,.025,.009),.20); image_material(mats[1],(.80,.68,.43),.46)
    body(mats[0]); foam(mats[1]); face(mats)
    select(*PARTS); bpy.context.view_layer.objects.active=PARTS[0]; bpy.ops.object.join()
    mesh=bpy.context.object; mesh.name='SK_CoffeeSlime'; mesh.data.name='SK_CoffeeSlime_Geometry'
    # Ensure stable material order regardless of primitive/join implementation order.
    old=list(mesh.data.materials); indices=[MATERIAL_NAMES.index(old[p.material_index].name) for p in mesh.data.polygons]
    mesh.data.materials.clear()
    for m in mats: mesh.data.materials.append(m)
    for p,i in zip(mesh.data.polygons,indices): p.material_index=i
    # Triangulate only end caps; FBX tangents cannot be emitted for n-gons.
    import bmesh
    bm=bmesh.new(); bm.from_mesh(mesh.data)
    bmesh.ops.triangulate(bm,faces=[f for f in bm.faces if len(f.verts)>4])
    bm.to_mesh(mesh.data); bm.free()
    mins=Vector((min(v.co.x for v in mesh.data.vertices),min(v.co.y for v in mesh.data.vertices),min(v.co.z for v in mesh.data.vertices)))
    maxs=Vector((max(v.co.x for v in mesh.data.vertices),max(v.co.y for v in mesh.data.vertices),max(v.co.z for v in mesh.data.vertices)))
    dims=maxs-mins; wanted=Vector((1.68,1.58,1.30)); scale=Vector((wanted.x/dims.x,wanted.y/dims.y,wanted.z/dims.z))
    for v in mesh.data.vertices:
        v.co=Vector((v.co.x*scale.x,v.co.y*scale.y,(v.co.z-mins.z)*scale.z))
    # Recalculate closed-surface winding, retain smooth normals.
    select(mesh); bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.mesh.normals_make_consistent(inside=False); bpy.ops.object.mode_set(mode='OBJECT')
    rigdata=bpy.data.armatures.new('SK_CoffeeSlime_Skeleton'); rig=bpy.data.objects.new('SK_CoffeeSlime_Rig',rigdata)
    bpy.context.collection.objects.link(rig); select(rig); bpy.ops.object.mode_set(mode='EDIT')
    definitions=[('root',None,(0,0,0),(0,0,.10)),('gel_body','root',(0,0,0),(0,0,.65)),('gel_top','gel_body',(0,0,.74),(0,0,1.12)),('face','gel_body',(.58,0,.62),(.58,0,.84)),('foam_tip','gel_top',(0,-.08,1.10),(0,-.08,1.30))]
    for name,parent,head,tail in definitions:
        b=rigdata.edit_bones.new(name); b.head=head; b.tail=tail
        if parent: b.parent=rigdata.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT'); rig.show_in_front=True
    mesh.parent=rig; mod=mesh.modifiers.new('Coffee gel skin','ARMATURE'); mod.object=rig
    exp=collection('CoffeeSlime.Export'); move_collection(mesh,exp); move_collection(rig,exp)
    animate(rig); lights()
    targets=(0,0,.62)
    cams={
        'hero':((4,-3,2.5),3.0),'front':((5,0,.72),2.5),'back':((-5,0,.72),2.5),
        'left':((0,-5,.72),2.5),'right':((0,5,.72),2.5),'top':((0,0,5),2.5),
        'gameplay':((4,-2,4),3.0),'motion':((5,-2,2.5),5.4)}
    for name,(pos,ortho) in cams.items(): camera('Review_'+name,pos,(0,0,.90) if name=='motion' else targets,ortho)
    s.camera=bpy.data.objects['Review_hero']
    manifest={
        'asset':'CoffeeSlime','blender_version':bpy.app.version_string,'units':'meters','forward':'+X','up':'+Z','pivot':'floor',
        'expected_dimensions_m':[1.68,1.58,1.30], 'required_skinned_meshes':['SK_CoffeeSlime'],
        'fbx_export':{'coordinate_units':'centimeters','coordinate_scale_from_source':100,
                      'scene_unit_scale_length':.01,'fbx_unit_scale_factor':1,'object_and_reference_root_scale':1,
                      'wrapper_object_name':'Armature','canonical_directory':'exports'},
        'triangle_budget':25000,'max_skin_influences':4,'material_slots':MATERIAL_NAMES,
        'skeleton':{'name':'SK_CoffeeSlime_Rig','required_bones':[d[0] for d in definitions],'bone_parents':{d[0]:d[1] for d in definitions}},
        'clips':{n:{'frame_start':1,'frame_end':e,'fps':100,'critical_frames':c,'loop':l,'duration_seconds':(e-1)/100} for n,(e,l,c) in CLIPS.items()},
        'integration':{'ue_scale':1,'component_floor_offset_cm':'-BodyRadius-4','collision':'Keep native sphere radius72cm; optional static UCX is authoring reference only','remove_old_presentation':['green MID','separate sphere eyes','native procedural body squash/lift'],'gameplay_untouched':['wandering','paint trails','health','rupture','groundburst','droplets']},
    }
    (BASE/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(BASE/'CoffeeSlime.blend'))
    # Graybox views inspect form separately from wet highlights/material identity.
    gray=material('Graybox override',(.37,.37,.37),.65)
    s.view_layers[0].material_override=gray
    for view in ['front','left','hero']:
        s.camera=bpy.data.objects['Review_'+view]; s.render.filepath=str(BASE/'review'/f'graybox_{view}.png'); bpy.ops.render.render(write_still=True)
    s.view_layers[0].material_override=None
    s.camera=bpy.data.objects['Review_hero']; s.render.filepath=str(BASE/'review'/'hero.png'); bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(BASE/'CoffeeSlime.blend'))
    print('COFFEE_SOURCE_READY', json.dumps(manifest))

def load():
    bpy.ops.wm.open_mainfile(filepath=str(BASE/'CoffeeSlime.blend'))
    return bpy.data.objects['SK_CoffeeSlime_Rig'],bpy.data.objects['SK_CoffeeSlime']

def export():
    rig,mesh=load(); s=bpy.context.scene; rig.animation_data.action=None
    # Preserve the meter-space review/export contract independently of UE FBX.
    rig.animation_data.action=bpy.data.actions['A_CoffeeSlime_Idle']; s.frame_start=1; s.frame_end=121; s.frame_set(1); select(rig,mesh)
    if opt.fbx_subdir=='exports':
        bpy.ops.export_scene.gltf(filepath=str(BASE/'exports'/'CoffeeSlime.glb'),export_format='GLB',use_selection=True,
                                 export_animations=True,export_animation_mode='ACTIONS',export_frame_range=False,
                                 export_force_sampling=True,export_skins=True,export_def_bones=True,export_yup=True)
    rig.animation_data.action=None
    for p in rig.pose.bones: p.matrix_basis=Matrix.Identity(4)
    # A derived centimeter-space rig prevents UE's Blender-wrapper removal from
    # producing mesh reference scale100 but animation root scale1. The disk source
    # stays meters: only this disposable loaded scene/data and location keys change.
    convert=Matrix.Scale(100.,4)
    mesh.data.transform(convert); rig.data.transform(convert)
    for action in bpy.data.actions:
        for layer in action.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:
                    for fc in cb.fcurves:
                        if fc.data_path.startswith('pose.bones[') and fc.data_path.endswith('.location'):
                            for key in fc.keyframe_points:
                                key.co.y*=100.; key.handle_left.y*=100.; key.handle_right.y*=100.
        action.update_tag()
    s.unit_settings.scale_length=.01
    fbx_dir=(BASE/opt.fbx_subdir).resolve()
    if not fbx_dir.is_relative_to(BASE.resolve()): raise ValueError('FBX output must remain inside asset v1')
    fbx_dir.mkdir(exist_ok=True)
    bpy.context.view_layer.update()
    ref={b.name:{'translation_cm':list(b.matrix_local.translation),'scale':list(b.matrix_local.to_scale())} for b in rig.data.bones}
    assert all(abs(v-1)<1e-5 for v in rig.matrix_world.to_scale()),'Derived object scale must be1'
    assert all(abs(v-1)<1e-5 for v in ref['root']['scale']),'Derived reference root scale must be1'
    assert abs(ref['gel_top']['translation_cm'][2]-74)<.001 and abs(ref['face']['translation_cm'][0]-58)<.001
    rig.animation_data.action=bpy.data.actions['A_CoffeeSlime_Hop']; s.frame_set(44); bpy.context.view_layer.update()
    hop=list((rig.matrix_world@rig.pose.bones['gel_body'].matrix).translation)
    assert 33<hop[2]<35,'Derived hop must lift34cm'
    audit={'scene_unit_scale_length':.01,'fbx_coordinate_units':'centimeters','source_conversion_factor':100,
           'object_scale':list(rig.matrix_world.to_scale()),'reference_bones':ref,'hop_frame44_translation_cm':hop,
           'source_blend_unchanged':True,'base_color_textures_unchanged':True}
    (fbx_dir/'CentimeterExportAudit.json').write_text(json.dumps(audit,indent=2),encoding='utf-8')
    rig.animation_data.action=None
    # UE5.8 explicitly strips Blender's top Null wrapper only when named Armature.
    # Keep the source rig name for .blend/.glb, but use this compatibility name in FBX.
    source_rig_name=rig.name; rig.name='Armature'
    for p in rig.pose.bones: p.matrix_basis=Matrix.Identity(4)
    s.frame_set(1); select(rig,mesh)
    opts=dict(use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,use_armature_deform_only=True,
              axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',bake_space_transform=False,
              use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True,path_mode='COPY',embed_textures=True)
    bpy.ops.export_scene.fbx(filepath=str(fbx_dir/'SK_CoffeeSlime.fbx'),bake_anim=False,**opts)
    for name,(end,loop,crit) in CLIPS.items():
        s.name=name
        rig.animation_data.action=bpy.data.actions[name]; s.frame_start=1; s.frame_end=end; s.frame_set(1)
        select(rig)
        bpy.ops.export_scene.fbx(filepath=str(fbx_dir/f'{name}.fbx'),bake_anim=True,bake_anim_use_all_bones=True,
                                bake_anim_use_nla_strips=False,bake_anim_use_all_actions=False,bake_anim_force_startend_keying=True,
                                bake_anim_step=1,bake_anim_simplify_factor=0,**opts)
    s.name='CoffeeSlime.Source'
    rig.name=source_rig_name
    # Static neutral mesh, closed low-density convex UCX, separate from skeletal export.
    rig.animation_data.action=None
    for p in rig.pose.bones: p.matrix_basis=Matrix.Identity(4)
    deps=bpy.context.evaluated_depsgraph_get(); neutral=bpy.data.meshes.new_from_object(mesh.evaluated_get(deps),depsgraph=deps)
    static=bpy.data.objects.new('SM_CoffeeSlime',neutral); s.collection.objects.link(static)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,location=(0,0,65))
    hull=bpy.context.object; hull.name='UCX_SM_CoffeeSlime_00'; hull.scale=(72,72,65); select(hull); bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    select(static,hull)
    staticopts={k:v for k,v in opts.items() if k not in ('object_types','use_armature_deform_only','add_leaf_bones')}
    bpy.ops.export_scene.fbx(filepath=str(fbx_dir/'SM_CoffeeSlime.fbx'),bake_anim=False,object_types={'MESH'},**staticopts)
    print('COFFEE_EXPORT_READY')

def stills():
    rig,mesh=load(); s=bpy.context.scene; s.render.resolution_x=1000; s.render.resolution_y=1000
    rig.animation_data.action=bpy.data.actions['A_CoffeeSlime_Idle']; s.frame_set(1)
    for view in ['front','back','left','right','top','gameplay']:
        s.camera=bpy.data.objects['Review_'+view]; s.render.filepath=str(BASE/'review'/f'{view}.png'); bpy.ops.render.render(write_still=True)
    s.render.resolution_x=640; s.render.resolution_y=640; s.camera=bpy.data.objects['Review_motion']
    for name,(end,loop,crit) in CLIPS.items():
        rig.animation_data.action=bpy.data.actions[name]
        for f in crit:
            s.frame_set(f); s.render.filepath=str(BASE/'review'/f'{name}_{f:03}.png'); bpy.ops.render.render(write_still=True)
    print('COFFEE_STILLS_READY')

def motion():
    rig,mesh=load(); s=bpy.context.scene; s.render.resolution_x=960; s.render.resolution_y=720
    s.camera=bpy.data.objects['Review_motion']; folder=BASE/'review'/'motion_frames'; folder.mkdir(exist_ok=True)
    sequence=[('A_CoffeeSlime_Idle',2),('A_CoffeeSlime_Hop',3),('A_CoffeeSlime_Hurt',2),('A_CoffeeSlime_PreBurst',1),('A_CoffeeSlime_Burst',1)]
    idx=0
    for name,repeats in sequence:
        rig.animation_data.action=bpy.data.actions[name]; end=CLIPS[name][0]
        count=math.ceil((end-1)/100*24)
        for repeat in range(repeats):
            for j in range(count):
                f=1+(end-1)*j/max(1,count-1); s.frame_set(int(f),subframe=f-int(f))
                s.render.filepath=str(folder/f'{idx:04}.png'); bpy.ops.render.render(write_still=True); idx+=1
    (BASE/'review'/'motion_sequence.json').write_text(json.dumps({'fps':24,'frames':idx,'sequence':sequence}),encoding='utf-8')
    print('COFFEE_MOTION_READY',idx)

{'author':author,'export':export,'stills':stills,'motion':motion}[opt.stage]()
