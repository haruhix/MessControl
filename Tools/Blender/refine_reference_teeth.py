"""Refine the existing tooth silhouettes and UVs into smooth enamel crowns."""
import bpy, bmesh, math, json
from pathlib import Path
from mathutils import Vector
root=Path('E:/DEVGAME/MessControl')
out=root/'ArtSource/LivingThroat/Teeth'; out.mkdir(parents=True,exist_ok=True)
collection=bpy.data.collections.new('MC_Reference_Enamel'); bpy.context.scene.collection.children.link(collection)
with bpy.data.libraries.load(str(root/'ArtSource/ArenaGameplay/ArenaTeeth.blend'),link=False) as (source,target):
    target.objects=list(source.objects)
report=[]
for source in target.objects:
    collection.objects.link(source)
    name=source.name.split('.')[0].replace('SM_ArenaTooth','SM_ReferenceTooth')
    tooth=source; tooth.name=name; tooth.location=(0,0,0)
    for v in tooth.data.vertices:v.co*=.01
    old_min=Vector(tuple(min(v.co[a] for v in tooth.data.vertices) for a in range(3)))
    old_max=Vector(tuple(max(v.co[a] for v in tooth.data.vertices) for a in range(3)))
    for o in bpy.context.selected_objects:o.select_set(False)
    tooth.hide_set(False); tooth.select_set(True); bpy.context.view_layer.objects.active=tooth
    bm=bmesh.new(); bm.from_mesh(tooth.data)
    bmesh.ops.join_triangles(bm,faces=list(bm.faces),angle_face_threshold=.45,angle_shape_threshold=.45)
    bm.to_mesh(tooth.data); bm.free()
    mod=tooth.modifiers.new('Enamel smoothing','SUBSURF'); mod.levels=2; mod.render_levels=2
    bpy.ops.object.modifier_apply(modifier=mod.name)
    lo=Vector(tuple(min(v.co[a] for v in tooth.data.vertices) for a in range(3)))
    hi=Vector(tuple(max(v.co[a] for v in tooth.data.vertices) for a in range(3)))
    for v in tooth.data.vertices:
        for a in range(3):v.co[a]=old_min[a]+(v.co[a]-lo[a])/(hi[a]-lo[a])*(old_max[a]-old_min[a])
        u=(v.co.z-old_min.z)/(old_max.z-old_min.z)
        if u>.70:
            nx=v.co.x/max(abs(old_max.x),.01); ny=v.co.y/max(abs(old_max.y),.01)
            fissure=math.exp(-(nx/.15)**2)*math.exp(-(ny/.7)**4)+.5*math.exp(-(ny/.18)**2)*math.exp(-(nx/.65)**4)
            v.co.z-=.035*fissure*((u-.70)/.30)**2
    for p in tooth.data.polygons:p.use_smooth=True
    bpy.ops.export_scene.fbx(filepath=str(out/(name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',bake_anim=False)
    report.append({'mesh':name,'vertices':len(tooth.data.vertices),'triangles':sum(len(p.vertices)-2 for p in tooth.data.polygons)})
    tooth.location=Vector((8+len(report)*3,-12,0)); tooth.hide_render=True; tooth.hide_set(True)
(out/'TeethReport.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(root/'ArtSource/LivingThroat/LivingThroat.blend'))
result={'teeth':report}
