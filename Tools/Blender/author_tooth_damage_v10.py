"""V10 adaptation of the existing seeded crown cuts. Run with --background --factory-startup.

Preserves the source pivot and all three UV layers. Separate scene/process/output
keep the artist's open Blender session and previous V2 damage variants untouched.
"""
import bpy, bmesh, json, math, random, hashlib,sys
from pathlib import Path
from mathutils import Vector

ROOT=Path('D:/PROJECT/GAME/WaifuTD/MessControl')
OUT=ROOT/'ContentSource/Meshes/ToothDamageV10'
sys.path.insert(0,str(ROOT/'Tools/Blender'))
from tooth_branch_cracks import add_branch_cracks
scene=bpy.context.scene
assert bpy.app.background, 'Use a separate background Blender process'
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene.name='MC_ToothDamageV10_Authoring'

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
    bm=bmesh.new();bm.from_mesh(original.data);source_volume=bm.calc_volume();bm.free()
    previous_volume=source_volume
    source_uvs=[layer.name for layer in original.data.uv_layers]
    for stage in (1,2):
        existing=scene.objects.get(name+('_Chipped' if stage==1 else '_Broken'))
        if existing:bpy.data.objects.remove(existing,do_unlink=True)
        variant=original.copy();variant.data=original.data.copy();variant.name=name+('_Chipped' if stage==1 else '_Broken')
        scene.collection.objects.link(variant);variant.hide_set(False);variant.hide_render=False
        seed=int.from_bytes(hashlib.sha256(name.encode()).digest()[:4],'little')
        lo,hi,volume,mirror=cut_crown(variant,stage,seed)
        assert 0 < volume < previous_volume*.995,(variant.name,volume,previous_volume)
        previous_volume=volume
        assert [layer.name for layer in variant.data.uv_layers]==source_uvs
        crack_stats=add_branch_cracks(variant,lo,hi,stage,seed,crack)
        # Pivot and dimensions stay in original local coordinates; no recentering of the broken crown.
        bpy.ops.object.select_all(action='DESELECT');variant.select_set(True);bpy.context.view_layer.objects.active=variant
        bpy.ops.export_scene.fbx(filepath=str(OUT/(variant.name+'.fbx')),use_selection=True,add_leaf_bones=False,bake_anim=False,
                                 axis_forward='-Y',axis_up='Z',use_mesh_modifiers=True)
        rows.append({'source':source['mesh'],'variant':variant.name,'stage':stage,'seed':seed,'closed_enamel_volume':volume,'original_volume':source_volume,'uv_layers':source_uvs,'vertices':len(variant.data.vertices),'polygons':len(variant.data.polygons),'fracture_faces':sum(p.material_index==1 for p in variant.data.polygons),'crack_faces':sum(p.material_index==2 for p in variant.data.polygons),'branch_stats':crack_stats})
        variant.hide_render=True;variant.hide_set(True)
(OUT/'Variants.json').write_text(json.dumps(rows,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'ToothDamageV10.blend'))
print('TOOTH_DAMAGE_VARIANTS_EXPORTED',len(rows),'unique seeds',len(set(r['seed'] for r in rows)))
