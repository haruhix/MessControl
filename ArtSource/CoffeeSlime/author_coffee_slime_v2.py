"""Coffee slime v2, concept and repaired graybox explicitly approved by ROOT.

Stage author loads the frozen approved graybox, adds portable skin/material/actions.
Run isolated Blender5.1 --background --factory-startup --threads2.
Never mutates the user's open .blend or the v1 source/packages.
"""
import argparse, hashlib, json, math, sys
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

BASE=Path(__file__).resolve().parent/'v2'
OUT=BASE/'graybox'
OUT.mkdir(parents=True,exist_ok=True)
WIDTH,DEPTH,HEIGHT=1.68,1.58,.90
RX,RY=DEPTH/2,WIDTH/2
END=1.80
COS_END=math.cos(END)
NORMALIZE=Vector((1,1,1))
PARTS=[]
MATERIALS=['M_CoffeeSlimeV2_Body','M_CoffeeSlimeV2_Crema','M_CoffeeSlimeV2_Eyes']
CLIPS={
    'A_CoffeeSlime_Idle':(121,True,[1,31,61,91,121]),
    'A_CoffeeSlime_Hop':(91,True,[1,15,20,35,44,63,71,77,83,91]),
    'A_CoffeeSlime_Hurt':(29,False,[1,5,12,20,29]),
    'A_CoffeeSlime_PreBurst':(61,False,[1,16,31,46,61]),
    'A_CoffeeSlime_Burst':(19,False,[1,5,10,19]),
}

def select(obj):
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj

def mat(name,color,roughness,specular=.18):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Roughness'].default_value=roughness; p.inputs['Specular IOR Level'].default_value=specular
    p.inputs['Coat Weight'].default_value=0; p.inputs['Metallic'].default_value=0
    return m

def upper_radius(q):
    c=COS_END+(1-COS_END)*q
    return max(0,1-c*c)**.425

def radius(q):
    if q>=.15: return upper_radius(q)
    # Rounded lower roll occupies only the lower15%: narrow the contact rim by
    #14%, with a horizontal tangent into the floor and zero slope at the girth.
    t=max(0,min(1,1-q/.15))
    return upper_radius(.15)*(.86+.14*math.sqrt(max(0,1-t*t)))

def center(q): return (-.025*q*q,.012*q)

def raw_point(q,a):
    r=radius(q); lobe=1+.035*math.cos(3*a)*math.exp(-HEIGHT*q/.12)
    x,y=center(q)
    # The lower rim gently lifts between three contact lobes; the upper mass is
    # unchanged, and the lobes are part of the same surface, never attached feet.
    rim_lift=.025*(1-math.cos(3*a))*max(0,1-q/.22)**2
    return Vector((x+RX*r*lobe*math.cos(a),y+RY*r*lobe*math.sin(a),HEIGHT*q+rim_lift))

def point(q,a):
    v=raw_point(q,a)
    return Vector((v.x*NORMALIZE.x,v.y*NORMALIZE.y,v.z))

def front_x(y,z):
    q=max(0,min(1,z/HEIGHT)); lo=-math.pi/2; hi=math.pi/2
    for _ in range(35):
        a=(lo+hi)/2
        if point(q,a).y<y: lo=a
        else: hi=a
    return point(q,(lo+hi)/2).x

def top_z(x,y):
    rawx,rawy=x/NORMALIZE.x,y/NORMALIZE.y; lo=.30; hi=1.
    for _ in range(38):
        q=(lo+hi)/2; cx,cy=center(q)
        nx,ny=(rawx-cx)/RX,(rawy-cy)/RY
        a=math.atan2(ny,nx)
        expected=radius(q)*(1+.035*math.cos(3*a)*math.exp(-HEIGHT*q/.12))
        if math.hypot(nx,ny)<expected: lo=q
        else: hi=q
    return HEIGHT*(lo+hi)/2

def mesh(name,verts,faces,material):
    data=bpy.data.meshes.new(name); data.from_pydata(verts,[],faces); data.update()
    obj=bpy.data.objects.new(name,data); bpy.context.scene.collection.objects.link(obj); data.materials.append(material)
    for p in data.polygons: p.use_smooth=True
    PARTS.append(obj); return obj

def body(material):
    global NORMALIZE
    n=96; verts=[tuple(raw_point(1,0))]; faces=[]
    theta_roll=math.acos(COS_END+(1-COS_END)*.15)
    qs=[(math.cos(theta_roll*k/40)-COS_END)/(1-COS_END) for k in range(1,41)]
    qs.extend(.15*(1-k/18)**2 for k in range(1,19))
    rings=len(qs)
    for q in qs:
        for j in range(n): verts.append(tuple(raw_point(max(0,q),j*math.tau/n)))
    bottom=len(verts); verts.append((0,0,0))
    for j in range(n): faces.append((0,1+j,1+(j+1)%n))
    for k in range(rings-1):
        for j in range(n):
            a=1+k*n+j; b=1+k*n+(j+1)%n; faces.append((a,a+n,b+n,b))
    start=1+(rings-1)*n
    for j in range(n): faces.append((start+j,bottom,start+(j+1)%n))
    dimensions=[max(v[i] for v in verts)-min(v[i] for v in verts) for i in range(3)]
    NORMALIZE=Vector((DEPTH/dimensions[0],WIDTH/dimensions[1],1))
    verts=[(v[0]*NORMALIZE.x,v[1]*NORMALIZE.y,v[2]) for v in verts]
    return mesh('CoffeeSlimeV2_GrayBody',verts,faces,material)

def eye(side,material):
    cy=side*.225; cz=.43; tilt=math.radians(side*3)
    def vertex(r,a):
        dy=.063*r*math.cos(a); dz=.112*r*math.sin(a)
        y=cy+dy*math.cos(tilt)-dz*math.sin(tilt)
        z=cz+dy*math.sin(tilt)+dz*math.cos(tilt)
        return (front_x(y,z)+.0012,y,z)
    n=40; rings=7; verts=[vertex(0,0)]; faces=[]
    for k in range(1,rings+1):
        for j in range(n): verts.append(vertex(k/rings,j*math.tau/n))
    for j in range(n): faces.append((0,1+j,1+(j+1)%n))
    for k in range(rings-1):
        for j in range(n):
            a=1+k*n+j; b=1+k*n+(j+1)%n; faces.append((a,b,b+n,a+n))
    return mesh('CoffeeSlimeV2_FlatEye_'+('Left' if side<0 else 'Right'),verts,faces,material)

def crema(material):
    # One tapered surface crescent: no thickness/cap/tuft/cream geometry.
    n=48; bands=3; verts=[]; faces=[]
    def v(t,k):
        a=-1.85+3.25*t
        shrink=1-.76*math.sin(math.pi*t)*(k/bands)
        x=-.085+.10*shrink*math.cos(a); y=.015+.17*shrink*math.sin(a)
        return (x,y,top_z(x,y)+.0012)
    verts.append(v(0,0))
    for i in range(1,n):
        for k in range(bands+1): verts.append(v(i/n,k))
    end=len(verts); verts.append(v(1,0))
    for k in range(bands): faces.append((0,1+k,1+k+1))
    for i in range(n-2):
        for k in range(bands):
            a=1+i*(bands+1)+k; b=a+bands+1; faces.append((a,b,b+1,a+1))
    start=1+(n-2)*(bands+1)
    for k in range(bands): faces.append((start+k,end,start+k+1))
    return mesh('CoffeeSlimeV2_FlushCrema',verts,faces,material)

def camera(name,pos,target,scale):
    data=bpy.data.cameras.new(name); obj=bpy.data.objects.new(name,data)
    bpy.context.scene.collection.objects.link(obj); obj.location=pos
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    data.type='ORTHO'; data.ortho_scale=scale
    return obj

def graybox():
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene; scene.name='CoffeeSlimeV2.Graybox'; scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=1
    scene.render.engine='BLENDER_EEVEE'; scene.render.resolution_x=1200; scene.render.resolution_y=800; scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'; scene.view_settings.view_transform='AgX'
    materials=[mat('M_CoffeeSlimeV2_Body',(.34,.125,.036),.34,.20),mat('M_CoffeeSlimeV2_Crema',(.82,.59,.28),.48,.15),mat('M_CoffeeSlimeV2_Eyes',(.94,.81,.52),.52,.12)]
    obj=body(materials[0]); eye(-1,materials[2]); eye(1,materials[2]); crema(materials[1])
    for part in PARTS:
        select(part); bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.mesh.normals_make_consistent(inside=False); bpy.ops.object.mode_set(mode='OBJECT')
    # Body is closed; graphic surface marks intentionally remain open and nearly flush.
    export_col=bpy.data.collections.new('CoffeeSlimeV2.Graybox'); scene.collection.children.link(export_col)
    for part in PARTS:
        for c in list(part.users_collection): c.objects.unlink(part)
        export_col.objects.link(part)
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.002)); floor=bpy.context.object; floor.name='ReviewFloor'
    floor.data.materials.append(mat('ReviewNeutral',(.24,.25,.25),.78,.10))
    scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.24,.25,.25,1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.45
    for name,pos,energy,size,color in [('Key',(3,-4,5),550,4,(1,.90,.77)),('Fill',(3,4,3),250,4,(.85,.90,1)),('Rim',(-3,1,4),350,3,(1,.93,.82))]:
        data=bpy.data.lights.new(name,'AREA'); data.energy=energy; data.shape='DISK'; data.size=size; data.color=color
        light=bpy.data.objects.new(name,data); scene.collection.objects.link(light); light.location=pos
        light.rotation_euler=(Vector((0,0,.4))-light.location).to_track_quat('-Z','Y').to_euler()
    views={'three_quarter':((4.5,-5,2.7),2.85),'front':((5,0,.43),2.20),'side':((0,-5,.43),2.20),'back':((-5,0,.43),2.20)}
    for name,(pos,scale) in views.items():
        scene.camera=camera('Review_'+name,pos,(0,0,.40),scale)
        scene.render.filepath=str(OUT/f'{name}.png'); bpy.ops.render.render(write_still=True)
    scene.camera=bpy.data.objects['Review_three_quarter']
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'CoffeeSlimeV2_Graybox.blend'))
    bodytris=0
    for part in PARTS: part.data.calc_loop_triangles(); bodytris+=len(part.data.loop_triangles)
    metrics={'stage':'graybox','visual_acceptance':'pending ROOT GRAYBOX APPROVED','dimensions_m_xyz':list(obj.dimensions),
        'triangles_with_marks':bodytris,'materials':[m.name for m in materials],'armatures':0,'actions':0,'exports':0,
        'body_form':'one closed rounded volume with subtle integrated lowerlobes','marks':'open surface-conforming patches,1.2mmoffset,no protruding eyes/cap',
        'source_units':'meters','forward':'+X','up':'+Z','pivot':'floor','blender_version':bpy.app.version_string}
    assert bodytris<15000
    (OUT/'graybox_metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
    print('COFFEE_V2_GRAYBOX_READY',json.dumps(metrics))

def smooth(x):
    x=max(0,min(1,x)); return x*x*(3-2*x)

def curve(t,points):
    for (a,x),(b,y) in zip(points,points[1:]):
        if t<=b: return x+(y-x)*smooth((t-a)/(b-a))
    return points[-1][1]

def body_weights(z):
    # Constant weights over every eye/support triangle (z<.58) and every
    # crema/support triangle (z>.78). Thus marks and supporting skin use the
    # identical affine deformation, not separately moving face attachments.
    upper=.35*smooth((z-.58)/.20)
    face=.05*(1-upper); tip=.05*upper
    return {'gel_body':1-upper-face-tip,'gel_top':upper,'face':face,'foam_tip':tip}

def add_uv(obj):
    data=obj.data; uv=data.uv_layers.new(name='UVMap')
    for p in data.polygons:
        values=[((math.atan2(data.vertices[i].co.y/RY,data.vertices[i].co.x/RX)/math.tau)%1,
                 data.vertices[i].co.z/HEIGHT) for i in p.vertices]
        seam=max(u for u,v in values)-min(u for u,v in values)>.5
        for li,(u,v) in zip(p.loop_indices,values): uv.data[li].uv=(u+1 if seam and u<.5 else u,v)

def gradient(material):
    folder=BASE/'textures'; folder.mkdir(exist_ok=True)
    image=bpy.data.images.new('T_CoffeeSlimeV2_Body_BaseColor',width=512,height=512)
    image.colorspace_settings.name='sRGB'
    pixels=[]
    for y in range(512):
        t=smooth(y/511); bottom=(.22,.072,.017); top=(.38,.15,.046)
        color=tuple(a+(b-a)*t for a,b in zip(bottom,top))
        # Generated8-bit sRGB image buffers are raw encoded channel values.
        # Encode intended linear albedo exactly once; keep sRGB on import.
        encoded=tuple(12.92*c if c<=.0031308 else 1.055*c**(1/2.4)-.055 for c in color)
        pixels.extend((*encoded,1)*512)
    image.pixels.foreach_set(pixels); image.filepath_raw=str(folder/'T_CoffeeSlimeV2_Body_BaseColor.png'); image.file_format='PNG'; image.save(); image.pack()
    node=material.node_tree.nodes.new('ShaderNodeTexImage'); node.image=image
    material.node_tree.links.new(node.outputs['Color'],material.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])

def author():
    source=OUT/'CoffeeSlimeV2_Graybox.blend'
    if not source.exists(): raise RuntimeError('Run approved graybox stage first')
    frozen_hash=hashlib.sha256(source.read_bytes()).hexdigest()
    bpy.ops.wm.open_mainfile(filepath=str(source)); scene=bpy.context.scene
    scene.name='CoffeeSlimeV2.Source'; scene.render.fps=100
    parts=[bpy.data.objects[n] for n in ['CoffeeSlimeV2_GrayBody','CoffeeSlimeV2_FlatEye_Left','CoffeeSlimeV2_FlatEye_Right','CoffeeSlimeV2_FlushCrema']]
    eye_x=sum(v.co.x for p in parts[1:3] for v in p.data.vertices)/sum(len(p.data.vertices) for p in parts[1:3])
    material_specs={
        MATERIALS[0]:dict(color=[.30,.111,.032],roughness=.30,specular=.28,coat=.05,coat_roughness=.25,color_gain=1.,emissive_gain=0.,textures={'BaseColor':'textures/T_CoffeeSlimeV2_Body_BaseColor.png'}),
        MATERIALS[1]:dict(color=[.82,.59,.28],roughness=.42,specular=.20,coat=.02,coat_roughness=.25,color_gain=1.,emissive_gain=0.),
        MATERIALS[2]:dict(color=[.94,.81,.52],roughness=.48,specular=.15,coat=0.,coat_roughness=.25,color_gain=1.,emissive_gain=0.),
    }
    for name,spec in material_specs.items():
        m=bpy.data.materials[name]; p=m.node_tree.nodes.get('Principled BSDF')
        p.inputs['Roughness'].default_value=spec['roughness']; p.inputs['Specular IOR Level'].default_value=spec['specular']
        p.inputs['Coat Weight'].default_value=spec['coat']; p.inputs['Coat Roughness'].default_value=spec['coat_roughness']
    gradient(bpy.data.materials[MATERIALS[0]])
    for part in parts:
        add_uv(part)
        for name in ['gel_body','gel_top','face','foam_tip']: part.vertex_groups.new(name=name)
        for v in part.data.vertices:
            # Crema's1.2mm normal offset must not change its support weight region.
            z=v.co.z-(.0012 if 'Crema' in part.name else 0)
            for name,w in body_weights(z).items():
                if w>1e-7: part.vertex_groups[name].add([v.index],w,'REPLACE')
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts: part.select_set(True)
    bpy.context.view_layer.objects.active=parts[0]; bpy.ops.object.join(); skin=bpy.context.object
    skin.name='SK_CoffeeSlime'; skin.data.name='SK_CoffeeSlimeV2_Geometry'
    old=list(skin.data.materials); inds=[MATERIALS.index(old[p.material_index].name) for p in skin.data.polygons]
    skin.data.materials.clear()
    for n in MATERIALS: skin.data.materials.append(bpy.data.materials[n])
    for p,i in zip(skin.data.polygons,inds): p.material_index=i
    rigdata=bpy.data.armatures.new('CoffeeSlimeV2_Skeleton'); rig=bpy.data.objects.new('SK_CoffeeSlime_Rig',rigdata)
    scene.collection.objects.link(rig); select(rig); bpy.ops.object.mode_set(mode='EDIT')
    defs=[('root',None,(0,0,0),(0,0,.1)),('gel_body','root',(0,0,0),(0,0,.45)),
          ('gel_top','gel_body',(0,0,.65),(0,0,.86)),('face','gel_body',(eye_x,0,.43),(eye_x,0,.58)),
          ('foam_tip','gel_top',(0,0,.90),(0,0,1.0))]
    for name,parent,head,tail in defs:
        b=rigdata.edit_bones.new(name); b.head=head; b.tail=tail
        if parent: b.parent=rigdata.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT'); rig.show_in_front=True
    skin.parent=rig; mod=skin.modifiers.new('Shared gel skin','ARMATURE'); mod.object=rig
    col=bpy.data.collections.new('CoffeeSlimeV2.Export'); scene.collection.children.link(col)
    for obj in [skin,rig]:
        for c in list(obj.users_collection): c.objects.unlink(obj)
        col.objects.link(obj)
    animate(rig)
    # Same fixed neutral stage as approved graybox, plus required reverse/top and motion cameras.
    camera('Review_right',(0,5,.43),(0,0,.4),2.20); camera('Review_top',(0,0,5),(0,0,0),2.85)
    camera('Review_gameplay',(4,-3,4),(0,0,.4),3.)
    camera('Review_motion',(5,-2,2.6),(0,0,.75),4.2)
    manifest={'asset':'CoffeeSlime','schema_version':2,'revision':'v2','units':'meters','forward':'+X','up':'+Z','pivot':'floor',
        'expected_dimensions_m':[DEPTH,WIDTH,HEIGHT],'triangle_budget':15000,'max_skin_influences':4,
        'mesh_file':'exports/SK_CoffeeSlime.fbx','fbx_units':'centimeters','fbx_export':{'coordinate_scale_from_source':100,'scene_unit_scale_length':.01,'root_scale':1,'wrapper_object_name':'Armature'},
        'approved_graybox_sha256':frozen_hash,'required_skinned_meshes':['SK_CoffeeSlime'],'material_slots':MATERIALS,'materials':material_specs,
        'skeleton':{'name':rig.name,'required_bones':[d[0] for d in defs],'bone_parents':{d[0]:d[1] for d in defs},
                    'reference_positions_space':'UE_COMPONENT','reference_positions_cm':{d[0]:[d[2][0]*100,-d[2][1]*100,d[2][2]*100] for d in defs}},
        'clips':{n:{'file':f'exports/{n}.fbx','frame_start':1,'frame_end':end,'fps':100,'duration_seconds':(end-1)/100,'critical_frames':critical,'loop':loop} for n,(end,loop,critical) in CLIPS.items()},
        'skin_contract':'One common position-based4-influence function; passive face/foam_tip inherit respective parent skin matrices. Eye region below.58 and crema above.78 have constant support weights.'}
    manifest['clips']['A_CoffeeSlime_Hop']['min_gel_body_lift_cm']=30.
    (BASE/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    scene.camera=bpy.data.objects['Review_three_quarter']; scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=str(BASE/'CoffeeSlimeV2.blend'))
    print('COFFEE_V2_SOURCE_READY',json.dumps(manifest))

def animate(rig):
    rig.animation_data_create()
    for name,(end,loop,critical) in CLIPS.items():
        action=bpy.data.actions.new(name); action.use_fake_user=True; rig.animation_data.action=action
        for frame in range(1,end+1):
            t=(frame-1)/100; xyz=Vector((1,1,1)); lift=recoil=tilt=top_sway=top_stretch=yaw=0.
            if name.endswith('Idle'):
                z=1+.022*math.sin(math.tau*t/1.2); xyz=Vector((1/math.sqrt(z),1/math.sqrt(z),z))
                yaw=.012*math.sin(math.tau*t/1.2); top_sway=.025*math.sin(math.tau*t/1.2)
            elif name.endswith('Hop'):
                z=curve(t,[(0,1),(.14,.60),(.19,1.22),(.34,1.12),(.432,1.04),(.62,1.15),(.702,.58),(.765,.87),(.82,1.10),(.9,1)])
                xyz=Vector((1/math.sqrt(z),1/math.sqrt(z),z))
                if .162<t<.702: lift=.34*math.sin(math.pi*(t-.162)/.54)**.8
                top_sway=.07*math.sin(t*math.tau/.9-.5)*math.sin(math.pi*t/.9)
                top_stretch=.025*math.sin(t*math.tau/.9-1)*math.sin(math.pi*t/.9)
            elif name.endswith('Hurt'):
                pulse=curve(t,[(0,0),(.04,1),(.12,.70),(.28,0)])
                xyz=Vector((1-.12*pulse,1+.08*pulse,1+.07*pulse)); recoil=-.10*pulse; lift=.10*pulse; tilt=-.15*pulse
                top_sway=.08*pulse*math.sin(t*18)
            elif name.endswith('PreBurst'):
                u=t/.6; g=1+.83*smooth(u); envelope=math.sin(math.pi*u)*smooth(u)
                wob=.03*math.sin(t*math.tau*13)*envelope
                xyz=Vector((g*(1+wob),g*(1-wob),g*(1+.08*smooth(u))))
                top_sway=.08*math.sin(t*math.tau*11)*envelope
            else:
                u=smooth(t/.18); xyz=Vector((1.83+.20*u,1.83+.20*u,1.9764*(1-u)+.035*u))
            for p in rig.pose.bones:
                p.rotation_mode='XYZ'; p.location=(0,0,0); p.rotation_euler=(0,0,0); p.scale=(1,1,1)
            b=rig.pose.bones['gel_body']; b.scale=(xyz.x,xyz.z,xyz.y); b.location=(recoil,lift,0); b.rotation_euler.z=tilt; b.rotation_euler.y=yaw
            top=rig.pose.bones['gel_top']; top.rotation_euler.x=top_sway; top.scale.y=1+top_stretch
            # face and foam_tip are intentionally passive; no independent marker motion.
            for p in rig.pose.bones:
                for prop in ['location','rotation_euler','scale']: p.keyframe_insert(prop,frame=frame,group=p.name)
        for layer in action.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:
                    for fc in cb.fcurves:
                        for key in fc.keyframe_points: key.interpolation='LINEAR'
    rig.animation_data.action=bpy.data.actions['A_CoffeeSlime_Idle']; bpy.context.scene.frame_set(1)

def load_final():
    bpy.ops.wm.open_mainfile(filepath=str(BASE/'CoffeeSlimeV2.blend'))
    return bpy.data.objects['SK_CoffeeSlime_Rig'],bpy.data.objects['SK_CoffeeSlime']

def export():
    rig,skin=load_final(); scene=bpy.context.scene; folder=BASE/'exports'; folder.mkdir(exist_ok=True)
    select(rig); skin.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(folder/'CoffeeSlime.glb'),export_format='GLB',use_selection=True,export_animations=True,
        export_animation_mode='ACTIONS',export_frame_range=False,export_force_sampling=True,export_skins=True,export_def_bones=True,export_yup=True)
    rig.animation_data.action=None
    for p in rig.pose.bones: p.matrix_basis=Matrix.Identity(4)
    skin.data.transform(Matrix.Scale(100.,4)); rig.data.transform(Matrix.Scale(100.,4))
    for action in bpy.data.actions:
        for layer in action.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:
                    for fc in cb.fcurves:
                        if fc.data_path.startswith('pose.bones[') and fc.data_path.endswith('.location'):
                            for key in fc.keyframe_points: key.co.y*=100; key.handle_left.y*=100; key.handle_right.y*=100
        action.update_tag()
    scene.unit_settings.scale_length=.01; rig.name='Armature'; scene.frame_set(1)
    select(rig); skin.select_set(True)
    opts=dict(use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,use_armature_deform_only=True,
        axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',bake_space_transform=False,
        use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True,path_mode='COPY',embed_textures=True)
    bpy.ops.export_scene.fbx(filepath=str(folder/'SK_CoffeeSlime.fbx'),bake_anim=False,**opts)
    for name,(end,loop,critical) in CLIPS.items():
        scene.name=name; rig.animation_data.action=bpy.data.actions[name]; scene.frame_start=1; scene.frame_end=end; scene.frame_set(1); select(rig)
        bpy.ops.export_scene.fbx(filepath=str(folder/f'{name}.fbx'),bake_anim=True,bake_anim_use_all_bones=True,bake_anim_use_nla_strips=False,
            bake_anim_use_all_actions=False,bake_anim_force_startend_keying=True,bake_anim_step=1,bake_anim_simplify_factor=0,**opts)
    print('COFFEE_V2_EXPORT_READY')

def stills():
    rig,skin=load_final(); scene=bpy.context.scene; review=BASE/'review'; review.mkdir(exist_ok=True)
    for name in ['three_quarter','front','side','back','right','top','gameplay']:
        scene.camera=bpy.data.objects['Review_'+name]; scene.render.filepath=str(review/f'{name}.png'); bpy.ops.render.render(write_still=True)
    scene.render.resolution_x=720; scene.render.resolution_y=720; scene.camera=bpy.data.objects['Review_motion']
    for name,(end,loop,critical) in CLIPS.items():
        rig.animation_data.action=bpy.data.actions[name]
        for f in critical:
            scene.frame_set(f); scene.render.filepath=str(review/f'{name}_{f:03}.png'); bpy.ops.render.render(write_still=True)
    print('COFFEE_V2_STILLS_READY')

def hero():
    rig,skin=load_final(); scene=bpy.context.scene; review=BASE/'review'; review.mkdir(exist_ok=True)
    scene.camera=bpy.data.objects['Review_three_quarter']; scene.render.filepath=str(review/'hero.png'); bpy.ops.render.render(write_still=True)

def top():
    # Framing-only repair: keep the approved geometry/material/actions untouched.
    rig,skin=load_final(); scene=bpy.context.scene
    bpy.data.objects['Review_top'].data.ortho_scale=2.85
    bpy.ops.wm.save_as_mainfile(filepath=str(BASE/'CoffeeSlimeV2.blend'))
    scene.camera=bpy.data.objects['Review_top']; scene.render.filepath=str(BASE/'review'/'top.png')
    bpy.ops.render.render(write_still=True)

def motion():
    rig,skin=load_final(); scene=bpy.context.scene; review=BASE/'review'; folder=review/'motion_frames'; folder.mkdir(parents=True,exist_ok=True)
    scene.render.resolution_x=960; scene.render.resolution_y=720; scene.camera=bpy.data.objects['Review_motion']
    sequence=[('Idle',2),('Hop',3),('Hurt',2),('PreBurst',1),('Burst',1)]; index=0; ranges=[]
    for role,repeats in sequence:
        name='A_CoffeeSlime_'+role; rig.animation_data.action=bpy.data.actions[name]; end=CLIPS[name][0]; start=index
        for repeat in range(repeats):
            count=math.ceil((end-1)/100*24)
            for i in range(count):
                f=1+(end-1)*i/max(1,count-1); scene.frame_set(int(f),subframe=f-int(f))
                scene.render.filepath=str(folder/f'{index:04}.png'); bpy.ops.render.render(write_still=True); index+=1
        ranges.append([role,start,index-1])
    (review/'motion_sequence.json').write_text(json.dumps({'fps':24,'frames':index,'ranges':ranges}),encoding='utf-8')
    print('COFFEE_V2_MOTION_READY',index)

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--stage',choices=['graybox','author','export','hero','stills','motion','top'],default='graybox')
    opts=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    {'graybox':graybox,'author':author,'export':export,'hero':hero,'stills':stills,'motion':motion,'top':top}[opts.stage]()
