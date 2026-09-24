"""Deterministic original prototype art. Run with Blender --background --factory-startup --python this_file.
All dimensions are centimeters. +X is character forward; origins sit on the floor.
Outputs editable .blend, individual FBX meshes and an art preview. No external assets.
"""
import bpy
import math
import random
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "ArtSource" / "Exports"
OUT.mkdir(parents=True, exist_ok=True)
(ROOT / "Artifacts").mkdir(exist_ok=True)
random.seed(17)
# This script is run in its own factory-startup background process.
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.name = "AssetWorkshop"
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 0.01

def material(name, color, roughness=0.45, metallic=0):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Roughness"].default_value = roughness
    p.inputs["Metallic"].default_value = metallic
    return m

enamel = material("Enamel", (0.94, 0.86, 0.65), 0.28)
dark = material("Ink", (0.024, 0.052, 0.061), 0.45)
pink = material("Blush", (0.96, 0.28, 0.29))
mint = material("Mint", (0.07, 0.59, 0.49), 0.32)
orange = material("Mango", (1.0, 0.48, 0.065))
bristle = material("Bristles", (0.53, 0.89, 0.81))
tongue_mat = material("Tongue", (0.66, 0.15, 0.21), 0.5)
gum = material("Gum", (0.51, 0.065, 0.11), 0.45)
cheek = material("Cheek", (0.31, 0.025, 0.053), 0.75)
throat = material("Throat", (0.055, 0.008, 0.02), 0.85)
coffee = material("Coffee", (0.17, 0.06, 0.025), 0.36)
green = material("Broccoli", (0.21, 0.34, 0.055), 0.7)

def smooth(obj):
    for p in obj.data.polygons: p.use_smooth = True
    return obj

def sphere(name, loc, scale, mat, segments=24, rings=16):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, location=loc)
    o = bpy.context.object; o.name = name; o.scale = scale
    o.data.materials.append(mat)
    return smooth(o)

def cube(name, loc, scale, bevel, mat):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object; o.name = name; o.dimensions = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.data.materials.append(mat)
    if bevel:
        b = o.modifiers.new("Soft toy edges", "BEVEL"); b.width = bevel; b.segments = 4
        bpy.ops.object.modifier_apply(modifier=b.name)
        n = o.modifiers.new("Weighted corner normals", "WEIGHTED_NORMAL")
        bpy.ops.object.modifier_apply(modifier=n.name)
    return o

def join(name, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects: o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    o = bpy.context.object; o.name = name
    scene.cursor.location = (0,0,0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return o

def crown(name, face=True):
    parts = [cube("crown", (0,0,64), (61,72,67), 18, enamel)]
    for y in (-20,20):
        for x in (-13,13):
            parts.append(sphere("cusp", (x,y,90), (20,22,18), enamel))
    for y in (-21,21):
        parts.append(sphere("root", (0,y,24), (20,16,25), enamel))
    body = join(name, parts)
    remesh = body.modifiers.new("Sculptable enamel union", "REMESH"); remesh.mode = "VOXEL"; remesh.voxel_size = 2.0
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    relax = body.modifiers.new("Smooth sculpt", "SMOOTH"); relax.factor = 1.3; relax.iterations = 5
    bpy.ops.object.modifier_apply(modifier=relax.name)
    smooth(body)
    parts = [body]
    if face:
        for y in (-14,14):
            parts.append(sphere("eye", (31,y,68), (2.8,4.2,7.3), dark, 16, 12))
            parts.append(sphere("eye glint", (33,y-1,71), (1,1.3,1.8), enamel, 12, 8))
            brow = cube("determined brow", (29,y,82), (4,16,4), 1.8, dark)
            brow.rotation_euler.x = math.radians(18 if y<0 else -18)
            parts.append(brow)
            parts.append(sphere("cheek", (30,y*1.55,55), (2,7,3.5), pink))
        parts.append(sphere("smile", (32,0,52), (2,4,2), dark))
        parts.append(cube("mint tool belt", (-2,0,36), (58,69,10), 4, mint))
        parts.append(cube("belt buckle", (29,0,36), (4,12,9), 2, orange))
        for y in (-42,42): parts.append(sphere("mitten", (8,y,42), (11,10,13), enamel))
    return join(name, parts)

hero = crown("SM_ToothHero")
prop = crown("SM_ToothProp", False)
parts = [cube("grip", (15,0,0), (60,10,12), 5, orange),
         cube("neck", (47,0,0), (32,7,8), 3, mint),
         cube("head", (70,0,0), (32,20,12), 6, mint)]
for x in (60,68,76,84):
    for y in (-6,0,6): parts.append(cube("bristle tuft", (x,y,-12), (4,4,17), 1.4, bristle))
brush = join("SM_Brush", parts)
food_parts = [cube("cheese", (0,0,27), (73,63,47), 10, orange)]
for i in range(9):
    food_parts.append(sphere("broccoli floret", (random.uniform(-35,35),random.uniform(-30,30),random.uniform(45,67)), (18,18,15),green,12,8))
food = join("SM_Food",food_parts)
coffee_parts = []
for i in range(9):
    a = i*math.tau/9
    coffee_parts.append(sphere("sticky stain", (math.cos(a)*45,math.sin(a)*37,3), (43,34,4), coffee))
stain = join("SM_Coffee", coffee_parts)

# Tongue top is intentionally almost level: visual shape and gameplay collision are separate.
verts=[(0,0,30)]; faces=[]; steps=80; rings=12
for r in range(1,rings+1):
    t=r/rings
    for i in range(steps):
        a=i*math.tau/steps
        x=1130*t*math.copysign(abs(math.cos(a))**0.65,math.cos(a))
        y=790*t*math.copysign(abs(math.sin(a))**0.75,math.sin(a))
        z=30-150*max(0,(t-0.88)/0.12)**2 + 1.5*math.sin(x*0.014)*math.cos(y*0.017)
        verts.append((x,y,z))
for i in range(steps): faces.append((0,1+i,1+(i+1)%steps))
for r in range(rings-1):
    for i in range(steps):
        a=1+r*steps+i; b=1+r*steps+(i+1)%steps
        faces.append((a,a+steps,b+steps,b))
mesh=bpy.data.meshes.new("TongueSurface"); mesh.from_pydata(verts,[],faces); mesh.update()
tongue=bpy.data.objects.new("SM_Tongue",mesh); scene.collection.objects.link(tongue); tongue.data.materials.append(tongue_mat); smooth(tongue)

shellparts=[]
for side in (-1,1):
    shellparts.append(sphere("cheek wall",(100,side*1260,440),(1540,390,790),cheek,32,24))
    shellparts.append(sphere("gum rail",(80,side*840,-30),(1270,210,160),gum,32,16))
    shellparts.append(sphere("upper gum",(100,side*930,930),(1250,300,180),gum,32,16))
shellparts.append(sphere("deep throat",(1380,0,510),(120,1060,980),throat,32,24))
shellparts.append(sphere("palate",(500,0,1210),(1200,1400,180),cheek,32,24))
shellparts.append(sphere("uvula",(1240,0,760),(80,88,150),gum,24,16))
shell=join("SM_MouthShell",shellparts)

assets=[hero,prop,brush,food,stain,tongue,shell]
for obj in assets:
    bpy.ops.object.select_all(action="DESELECT"); obj.select_set(True); bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.fbx(filepath=str(OUT/(obj.name+".fbx")),use_selection=True,object_types={"MESH"},
        apply_unit_scale=True,apply_scale_options="FBX_SCALE_NONE",axis_forward="-Y",axis_up="Z",use_mesh_modifiers=True,bake_anim=False)

# Separate art direction scene, with editable linked instances of all the exported meshes.
preview = bpy.data.scenes.new("Mouth_Preview")
preview.unit_settings.system="METRIC"; preview.unit_settings.scale_length=0.01
bpy.context.window.scene=preview
def instance(source, name, loc=(0,0,0), scale=(1,1,1), rot=(0,0,0)):
    o=bpy.data.objects.new(name,source.data); preview.collection.objects.link(o); o.location=loc; o.scale=scale; o.rotation_euler=rot; return o
instance(shell,"Mouth shell"); instance(tongue,"Tongue arena")
for side in (-1,1):
    for i in range(8):
        instance(prop,"Lower molar",(-890+i*265,side*(815+35*math.sin(i)),0),(2.15,2.15,2.45),(0,0,0.06*math.sin(i)))
        instance(prop,"Upper molar",(-740+i*275,side*865,990),(2.2,2.2,2.15),(math.pi,0,0))
for i,(x,y) in enumerate([(-620,0),(-530,-220),(-460,230)]):
    instance(hero,"Hero tooth",(x,y,35),(1.55,1.55,1.55),(0,0,math.pi+0.15*(i-1)))
    instance(brush,"Hero brush",(x-40,y-70,105),(1.55,1.55,1.55),(0,0,math.pi))
for x,y in [(50,300),(-80,-340),(570,-290),(650,410)]:
    instance(food,"Snack",(x,y,30),(1.2,1.2,1.2))
for x,y in [(-500,430),(340,0),(-170,-500)]: instance(stain,"Coffee stain",(x,y,32),(1.7,1.7,1.2))
world=bpy.data.worlds.new("Warm studio air"); world.use_nodes=True; world.node_tree.nodes["Background"].inputs[0].default_value=(0.19,0.10,0.12,1); world.node_tree.nodes["Background"].inputs[1].default_value=0.35; preview.world=world
def area(name,loc,energy,color,size):
    data=bpy.data.lights.new(name,"AREA"); data.energy=energy; data.color=color; data.shape="DISK"; data.size=size
    obj=bpy.data.objects.new(name,data); preview.collection.objects.link(obj); obj.location=loc
    obj.rotation_euler=(Vector((0,0,60))-obj.location).to_track_quat("-Z","Y").to_euler()
area("Warm softbox",(-950,-350,950),1500000,(1,0.76,0.58),900)
area("Mint fill",(-500,850,600),1000000,(0.56,0.88,1),650)
area("Throat rim",(850,-100,800),1700000,(1,0.38,0.20),550)
cam_data=bpy.data.cameras.new("ArenaCamera"); cam=bpy.data.objects.new("ArenaCamera",cam_data); preview.collection.objects.link(cam)
cam.location=(-2070,-40,1000); cam.rotation_euler=(Vector((180,0,190))-cam.location).to_track_quat("-Z","Y").to_euler(); cam_data.lens=26; cam_data.clip_end=10000; preview.camera=cam
preview.render.engine="CYCLES"; preview.cycles.samples=32
preview.render.resolution_x=1440; preview.render.resolution_y=960; preview.render.resolution_percentage=100
preview.view_settings.view_transform="AgX"
preview.view_settings.look="AgX - Medium High Contrast"
preview.view_settings.exposure=2.3
preview.render.image_settings.file_format="PNG"; preview.render.filepath=str(ROOT/"Artifacts"/"Mouth_ArtPreview.png")
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/"ArtSource"/"MessControl.blend"))
bpy.ops.render.render(write_still=True)
print("MC_ART_COMPLETE")
