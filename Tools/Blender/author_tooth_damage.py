"""Run in Blender's dedicated MC_ToothDamage_Authoring scene. Never edits source meshes."""
import bpy, bmesh, json, math, random, hashlib
from pathlib import Path
from mathutils import Vector

ROOT=Path('D:/PROJECT/GAME/WaifuTD/MessControl')
OUT=ROOT/'ContentSource/Meshes/ToothDamage'
scene=bpy.context.scene
assert scene.name=='MC_ToothDamage_Authoring'

def material(name,color,roughness):
    mat=bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes=True
    node=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    node.inputs['Base Color'].default_value=(*color,1)
    node.inputs['Roughness'].default_value=roughness
    mat.diffuse_color=(*color,1)
    return mat

enamel=material('MC_Damage_Enamel',(.81,.9,.98),.27)
dentin=material('MC_Damage_Dentin',(.53,.68,.74),.72)
crack=material('MC_Damage_Crack',(.018,.055,.09),.85)

def cut_crown(obj,stage,seed):
    rng=random.Random(seed)
    lo=Vector(tuple(min(v.co[k] for v in obj.data.vertices) for k in range(3)))
    hi=Vector(tuple(max(v.co[k] for v in obj.data.vertices) for k in range(3)))
    size=hi-lo
    mirror=rng.choice((False,True))
    # Each entry removes a volume, rather than extruding a repeating saw profile.
    # The broken stage reuses every initial chip, so further damage never regrows enamel.
    chips=[(.5,rng.uniform(.78,.88),rng.uniform(1.00,1.07),1.05,rng.uniform(.30,.38),rng.uniform(.29,.36)),
           (rng.uniform(.35,.65),rng.uniform(.50,.66),rng.uniform(.91,.98),.85,rng.uniform(.07,.12),rng.uniform(.09,.15)),
           (rng.uniform(.35,.65),rng.uniform(.90,1.02),rng.uniform(.73,.85),.90,rng.uniform(.10,.17),rng.uniform(.07,.14))]
    if stage==2:
        chips.extend([(.5,rng.uniform(.34,.48),rng.uniform(.94,1.03),1.05,rng.uniform(.30,.42),rng.uniform(.44,.52)),
                      (rng.uniform(.35,.65),rng.uniform(.08,.22),rng.uniform(.87,.97),.90,rng.uniform(.10,.17),rng.uniform(.13,.23)),
                      (rng.uniform(.35,.65),rng.uniform(.52,.70),rng.uniform(.80,.93),.85,rng.uniform(.10,.16),rng.uniform(.08,.15))])
    for index,(cx,cy,cz,rx,ry,rz) in enumerate(chips):
        shape_rng=random.Random(seed+104729*(index+1))
        if mirror:cy=1-cy
        angle=shape_rng.uniform(-.45,.45)
        vertices=[]
        for _ in range(shape_rng.randint(28,42)):
            # Random hull directions and unequal facets remove repeated teeth/steps.
            direction=Vector((shape_rng.gauss(0,1),shape_rng.gauss(0,1),shape_rng.gauss(0,1))).normalized()
            radius=shape_rng.uniform(.83,1.15)
            dx=direction.x*rx*radius;dy=direction.y*ry*radius;dz=direction.z*rz*radius
            y=dy*math.cos(angle)-dz*math.sin(angle)
            z=dy*math.sin(angle)+dz*math.cos(angle)
            vertices.append((lo.x+(cx+dx)*size.x,lo.y+(cy+y)*size.y,lo.z+(cz+z)*size.z))
        # A crown chip must open to the outside, rather than drill a closed hole.
        top_x=lo.x+cx*size.x;top_y=lo.y+cy*size.y
        tilt=shape_rng.uniform(-.12,.12)*size.y
        vertices.extend([(top_x+(x-top_x)*1.5,top_y+(y-top_y)*1.9+tilt,lo.z+size.z*1.65) for x,y,z in list(vertices)])
        mesh=bpy.data.meshes.new('MC_OwnedCrownCutter');mesh.from_pydata(vertices,[],[])
        bm=bmesh.new();bm.from_mesh(mesh)
        bmesh.ops.convex_hull(bm,input=list(bm.verts),use_existing_faces=False)
        for v in list(bm.verts):
            if not v.link_faces:bm.verts.remove(v)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free();mesh.update()
        cutter=bpy.data.objects.new(mesh.name,mesh);scene.collection.objects.link(cutter)
        for m in (enamel,dentin,crack):cutter.data.materials.append(m)
        for p in cutter.data.polygons:p.material_index=1
        bpy.context.view_layer.objects.active=obj
        mod=obj.modifiers.new('Irregular crown chip %d'%index,'BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(cutter,do_unlink=True);bpy.data.meshes.remove(mesh)
    # Closed enamel volume is checked before adding the intentionally open crack ribbons.
    bm=bmesh.new();bm.from_mesh(obj.data)
    volume=bm.calc_volume();boundary=sum(not e.is_manifold for e in bm.edges);bm.free()
    assert boundary==0 and volume>0,(obj.name,boundary,volume)
    return lo,hi,volume,mirror

def add_cracks(obj,lo,hi,stage,seed,mirror):
    # Narrow branching ribbons projected onto all four faces of the surviving enamel.
    rng=random.Random(seed+stage*7919)
    vertices=[];faces=[];size=hi-lo
    for axis in (0,1):
        sideaxis=1-axis
        for sign in (-1,1):
            paths=[]
            for branch in range(1 if stage==1 else 2):
                side=rng.uniform(.57,.81) if stage==1 else rng.uniform(.18,.39) if branch==0 else rng.uniform(.65,.84)
                if mirror:side=1-side
                top=.72
                for step in range(42):
                    height=.99-step*.016
                    start=(lo+hi)*.5;start[axis]=hi[axis]+size[axis] if sign>0 else lo[axis]-size[axis]
                    start[sideaxis]=lo[sideaxis]+side*size[sideaxis];start.z=lo.z+height*size.z
                    direction=Vector((0,0,0));direction[axis]=-sign
                    hit,_,normal,_=obj.ray_cast(start,direction)
                    if hit and normal[axis]*sign>.15:top=height-.016;break
                path=[(side,top)]
                for j in range(rng.randint(3,5)):
                    path.append((max(.08,min(.92,path[-1][0]+rng.uniform(-.085,.085))),path[-1][1]-rng.uniform(.04,.075)))
                paths.append(path)
                split=path[rng.randint(1,len(path)-2)]
                paths.append([split,(split[0]+rng.uniform(-.12,.12),split[1]-.045),(split[0]+rng.uniform(-.16,.16),split[1]-.105)])
            for path in paths:
                path=[(a[0]+(b[0]-a[0])*j/8,a[1]+(b[1]-a[1])*j/8)
                      for a,b in zip(path,path[1:]) for j in range(8)]+[path[-1]]
                for segment in range(len(path)-1):
                    a,b=path[segment:segment+2]
                    tangent=Vector(((b[0]-a[0])*size[sideaxis],(b[1]-a[1])*size.z)).normalized()
                    perpendicular=Vector((-tangent.y,tangent.x))
                    width=.009*(1-.55*segment/max(1,len(path)-2))*(1.15 if stage==2 else 1)
                    quad=[]
                    for p,edge in ((a,-1),(a,1),(b,1),(b,-1)):
                        start=Vector((0,0,0));start[axis]=(hi[axis]+size[axis]) if sign>0 else (lo[axis]-size[axis])
                        start[sideaxis]=lo[sideaxis]+p[0]*size[sideaxis]+perpendicular.x*width*edge
                        start.z=lo.z+p[1]*size.z+perpendicular.y*width*edge
                        direction=Vector((0,0,0));direction[axis]=-sign
                        hit,point,normal,_=obj.ray_cast(start,direction)
                        if not hit:break
                        quad.append(tuple(point+normal*.002))
                    if len(quad)==4 and max((Vector(quad[(i+1)%4])-Vector(quad[i])).length for i in range(4))<.1 \
                            and (Vector(quad[1])-Vector(quad[0])).length<width*6 \
                            and (Vector(quad[2])-Vector(quad[3])).length<width*6:
                        base=len(vertices);vertices.extend(quad);faces.append(tuple(range(base,base+4)))
    data=bpy.data.meshes.new('MC_OwnedCrackRibbons');data.from_pydata(vertices,[],faces);data.materials.append(crack);data.update()
    ribbons=bpy.data.objects.new(data.name,data);scene.collection.objects.link(ribbons)
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);ribbons.select_set(True);bpy.context.view_layer.objects.active=obj
    bpy.ops.object.join()

rows=[]
for source in json.loads((OUT/'Sources.json').read_text()):
    name=source['name']
    original=scene.objects.get(name)
    if original is None:
        bpy.ops.import_scene.fbx(filepath=str(OUT/(name+'.fbx')))
        original=next(o for o in bpy.context.selected_objects if o.type=='MESH')
        bpy.context.view_layer.objects.active=original
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    original.data.materials.clear()
    original.location=(0,0,0)
    bm=bmesh.new();bm.from_mesh(original.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(original.data);bm.free()
    for mat in (enamel,dentin,crack):original.data.materials.append(mat)
    for poly in original.data.polygons:poly.material_index=0
    original.hide_render=True;original.hide_set(True)
    for stage in (1,2):
        existing=scene.objects.get(name+('_Chipped' if stage==1 else '_Broken'))
        if existing:bpy.data.objects.remove(existing,do_unlink=True)
        variant=original.copy();variant.data=original.data.copy();variant.name=name+('_Chipped' if stage==1 else '_Broken')
        scene.collection.objects.link(variant);variant.hide_set(False);variant.hide_render=False
        seed=int.from_bytes(hashlib.sha256(name.encode()).digest()[:4],'little')
        lo,hi,volume,mirror=cut_crown(variant,stage,seed);add_cracks(variant,lo,hi,stage,seed,mirror)
        # Pivot and dimensions stay in original local coordinates; no recentering of the broken crown.
        bpy.ops.object.select_all(action='DESELECT');variant.select_set(True);bpy.context.view_layer.objects.active=variant
        bpy.ops.export_scene.fbx(filepath=str(OUT/(variant.name+'.fbx')),use_selection=True,add_leaf_bones=False,bake_anim=False,
                                 axis_forward='-Y',axis_up='Z',use_mesh_modifiers=True)
        rows.append({'source':source['mesh'],'variant':variant.name,'stage':stage,'seed':seed,'closed_enamel_volume':volume,'vertices':len(variant.data.vertices),'polygons':len(variant.data.polygons),'fracture_faces':sum(p.material_index==1 for p in variant.data.polygons),'crack_faces':sum(p.material_index==2 for p in variant.data.polygons)})
        variant.hide_render=True;variant.hide_set(True)
(OUT/'Variants.json').write_text(json.dumps(rows,indent=2))
print('TOOTH_DAMAGE_VARIANTS_EXPORTED',len(rows),'unique seeds',len(set(r['seed'] for r in rows)))
