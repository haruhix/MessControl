"""Build a skinned tooth from the original workshop without changing the source scene.
Run in a separate Blender --background --factory-startup process.
"""
import bpy
import math
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
bpy.ops.wm.open_mainfile(filepath=str(ROOT / "ArtSource/MessControl.blend"))
scene = bpy.data.scenes["AssetWorkshop"]
bpy.context.window.scene = scene
source = bpy.data.objects["SM_ToothHero"]
hero = bpy.data.objects.new("SK_ToothHero", source.data.copy())
scene.collection.objects.link(hero)
hero.matrix_world = source.matrix_world.copy()

# Identify disconnected mittens; the enamel core remains a smooth, blended skin.
adj = [[] for _ in hero.data.vertices]
for edge in hero.data.edges:
    a,b=edge.vertices; adj[a].append(b); adj[b].append(a)
visited=set(); mittens=set()
for vertex in hero.data.vertices:
    if vertex.index in visited: continue
    pending=[vertex.index]; island=[]; visited.add(vertex.index)
    while pending:
        i=pending.pop(); island.append(i)
        for j in adj[i]:
            if j not in visited: visited.add(j); pending.append(j)
    center=sum((hero.data.vertices[i].co for i in island),Vector())/len(island)
    if abs(center.y)>38 and 30<center.z<54:
        for i in island:
            hero.data.vertices[i].co += Vector((2,math.copysign(12,center.y),-6))
            mittens.add(i)

armdata=bpy.data.armatures.new("ToothSkeleton")
rig=bpy.data.objects.new("ToothRig",armdata); scene.collection.objects.link(rig)
bpy.ops.object.select_all(action="DESELECT"); rig.select_set(True); bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode="EDIT")
bones=[("root",None,(0,0,0),(0,0,12)),("body","root",(0,0,58),(0,0,86))]
for side,sign in [("l",-1),("r",1)]:
    bones += [("arm_"+side,"body",(0,31*sign,62),(5,44*sign,47)),
              ("hand_"+side,"arm_"+side,(5,44*sign,47),(10,54*sign,36)),
              ("leg_"+side,"body",(0,20*sign,36),(0,20*sign,15)),
              ("foot_"+side,"leg_"+side,(0,20*sign,15),(12,20*sign,9))]
for name,parent,head,tail in bones:
    bone=armdata.edit_bones.new(name); bone.head=head; bone.tail=tail
    if parent: bone.parent=armdata.edit_bones[parent]
    bone.use_connect=False
bpy.ops.object.mode_set(mode="OBJECT")
for name,_,_,_ in bones: hero.vertex_groups.new(name=name)
for vertex in hero.data.vertices:
    p=vertex.co; side="l" if p.y<0 else "r"
    if vertex.index in mittens:
        hero.vertex_groups["hand_"+side].add([vertex.index],1,"REPLACE")
    elif p.z<44 and abs(p.y)>5:
        leg_weight=max(0,min(1,(44-p.z)/18))
        foot_weight=max(0,min(0.55,(18-p.z)/20))
        hero.vertex_groups["body"].add([vertex.index],1-leg_weight,"REPLACE")
        hero.vertex_groups["leg_"+side].add([vertex.index],leg_weight*(1-foot_weight),"REPLACE")
        hero.vertex_groups["foot_"+side].add([vertex.index],leg_weight*foot_weight,"REPLACE")
    else: hero.vertex_groups["body"].add([vertex.index],1,"REPLACE")

# Short rubbery arms connect the former floating mittens to the crown.
parts=[hero]
enamel=bpy.data.materials["Enamel"]
for name,parent,head,tail in bones:
    if not name.startswith(("arm_","hand_")): continue
    start=Vector(head); end=Vector(tail); midpoint=(start+end)/2
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=10,location=midpoint)
    obj=bpy.context.object; obj.name=name+"_skin"; obj.scale=(7,7,(end-start).length/2+5)
    obj.rotation_euler=(end-start).to_track_quat("Z","Y").to_euler()
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    obj.data.materials.append(enamel)
    for p in obj.data.polygons: p.use_smooth=True
    group=obj.vertex_groups.new(name=name); group.add(list(range(len(obj.data.vertices))),1,"REPLACE")
    parts.append(obj)
bpy.ops.object.select_all(action="DESELECT")
for obj in parts: obj.select_set(True)
bpy.context.view_layer.objects.active=hero; bpy.ops.object.join()

hero.shape_key_add(name="Basis")
for name,zscale in [("Squash",0.72),("Stretch",1.28)]:
    key=hero.shape_key_add(name=name)
    for point in key.data:
        point.co.z *= zscale
        point.co.x *= 1/math.sqrt(zscale); point.co.y *= 1/math.sqrt(zscale)
modifier=hero.modifiers.new("Tooth skin","ARMATURE"); modifier.object=rig
hero.parent=rig
rig.show_in_front=True

# A separate scene avoids clutter when opening the rig file.
rigscene=bpy.data.scenes.new("Tooth_Rig_Workshop")
rigscene.unit_settings.system="METRIC"; rigscene.unit_settings.scale_length=0.01
for obj in (hero,rig):
    for collection in list(obj.users_collection): collection.objects.unlink(obj)
    rigscene.collection.objects.link(obj)
bpy.context.window.scene=rigscene
bpy.ops.object.select_all(action="DESELECT"); hero.select_set(True); rig.select_set(True); bpy.context.view_layer.objects.active=rig
bpy.ops.export_scene.fbx(filepath=str(ROOT/"ArtSource/Exports/SK_ToothHero.fbx"),use_selection=True,
    object_types={"MESH","ARMATURE"},add_leaf_bones=False,use_armature_deform_only=False,
    apply_unit_scale=True,apply_scale_options="FBX_SCALE_NONE",axis_forward="-Y",axis_up="Z",
    use_mesh_modifiers=False,mesh_smooth_type="FACE",bake_anim=False)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/"ArtSource/ToothRig.blend"))
print("MC_RIG_COMPLETE: %d bones, %d vertices, 2 morph targets"%(len(bones),len(hero.data.vertices)))
