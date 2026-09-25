"""Small, replaceable prototype breakfast meshes. Does not touch the arena or hero."""
import bpy
import math
import random
from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = root / 'ArtSource' / 'Breakfast'
out.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = .01
colors = {'Green': (.08,.3,.018), 'Stem': (.3,.48,.08), 'EggWhite': (.95,.86,.64),
          'Yolk': (1,.4,.015), 'Bacon': (.5,.06,.025), 'Fat': (.95,.54,.25),
          'Carrot': (.95,.2,.015), 'Fibre': (.56,.34,.1)}
mats = {}
for name, color in colors.items():
    m = bpy.data.materials.new(name); m.diffuse_color = (*color, 1); mats[name] = m

def part(p, s, mat, cube=False):
    if cube: bpy.ops.mesh.primitive_cube_add(size=2, location=p)
    else: bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=10, location=p)
    o = bpy.context.object; o.scale = s; o.data.materials.append(mats[mat])
    for poly in o.data.polygons: poly.use_smooth = True
    return o

for kind in ['Broccoli', 'Egg', 'Bacon', 'Carrot', 'Fibre']:
    for fragment in [False, True]:
        for variant in range(3):
            random.seed(variant + 71)
            bpy.ops.object.select_all(action='DESELECT')
            before = set(bpy.data.objects)
            if kind == 'Broccoli':
                part((0,0,-15), (12,12,27), 'Stem')
                for i in range(5 if not fragment else 2):
                    a = i * math.tau / 5
                    part((math.cos(a)*23, math.sin(a)*23, 15+random.uniform(-6,6)), (25,25,23), 'Green')
            elif kind == 'Egg':
                part((0,0,-4), (49+variant*3,35,8), 'EggWhite')
                part((variant*4-4,0,4), (19,19,13), 'Yolk')
            elif kind == 'Bacon':
                for i in range(5):
                    x = i*19-38; z = math.sin(i+variant)*7
                    part((x,0,z), (12,23,5), 'Bacon', True)
                    part((x,-7,z+5), (12,5,2), 'Fat', True)
            elif kind == 'Carrot':
                part((0,0,0), (44,17+variant*2,17), 'Carrot')
                part((-35,0,0), (8,13,13), 'Stem')
            else:
                for i in range(3):
                    o=part((0,i*12-12,math.sin(i+variant)*8), (55,5,5), 'Fibre')
                    o.rotation_euler[2]=.15*(i-1)
            objects = list(set(bpy.data.objects)-before)
            for o in objects: o.select_set(True)
            bpy.context.view_layer.objects.active=objects[0]
            bpy.ops.object.join(); mesh=bpy.context.object
            bpy.context.scene.cursor.location=(0,0,0)
            bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
            mesh.name=f'SM_{kind}_{"Part" if fragment else "Whole"}_{chr(65+variant)}'
            bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
            bpy.ops.export_scene.fbx(filepath=str(out/(mesh.name+'.fbx')), use_selection=True,
                object_types={'MESH'}, apply_unit_scale=True, axis_forward='-Y', axis_up='Z', bake_anim=False,
                add_leaf_bones=False, use_mesh_modifiers=True, mesh_smooth_type='FACE')
            mesh.hide_set(True)
bpy.ops.wm.save_as_mainfile(filepath=str(root/'ArtSource'/'Breakfast.blend'))
print('MC_BREAKFAST_MESHES_PASS')
