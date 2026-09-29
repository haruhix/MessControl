"""Sculpt a smooth palatal root, narrow stalk and continuous rounded uvula tip.

Run in LivingThroat.blend. The previous mesh is kept in a hidden collection.
Coordinates retain the runtime contract: pivot at zero, tip at Z=-1 metre.
"""
import bpy
import math
import json
from pathlib import Path

root = Path('E:/DEVGAME/MessControl/ArtSource/LivingThroat')
obj = bpy.data.objects['SM_Uvula']
assert bpy.context.mode == 'OBJECT'
assert obj.type == 'MESH' and not obj.modifiers

archive = bpy.data.collections.get('MC_Uvula_Archive')
if archive is None:
    archive = bpy.data.collections.new('MC_Uvula_Archive')
    bpy.context.scene.collection.children.link(archive)
if 'SM_Uvula_PreAnatomy' not in bpy.data.objects:
    old = obj.copy()
    old.data = obj.data.copy()
    old.name = 'SM_Uvula_PreAnatomy'
    archive.objects.link(old)
    old.hide_render = True
    old.hide_set(True)
archive.hide_render = True
archive.hide_viewport = True

# Monotone cubic interpolation avoids ridges at the root/stalk/bulb joins.
profile = [(-1.6, 3.80), (-1.0, 1.80), (-.55, .72), (-.20, .37),
           (.16, .29), (.40, .32), (.60, .40), (.78, .46)]
secants = [(b[1]-a[1])/(b[0]-a[0]) for a,b in zip(profile, profile[1:])]
slopes = [secants[0]]
for i in range(1, len(profile)-1):
    left, right = secants[i-1], secants[i]
    if left*right <= 0:
        slopes.append(0.0)
    else:
        h0 = profile[i][0]-profile[i-1][0]
        h1 = profile[i+1][0]-profile[i][0]
        w0, w1 = 2*h1+h0, h1+2*h0
        slopes.append((w0+w1)/(w0/left+w1/right))
slopes.append(0.0)

def radius(t):
    for i in range(len(profile)-1):
        a, b = profile[i], profile[i+1]
        if t <= b[0]+1e-8:
            h = b[0]-a[0]
            s = (t-a[0])/h
            return ((2*s**3-3*s*s+1)*a[1] + (s**3-2*s*s+s)*h*slopes[i]
                    + (-2*s**3+3*s*s)*b[1] + (s**3-s*s)*h*slopes[i+1])
    return .46

def smooth(s):
    s = min(1.0, max(0.0, s))
    return s*s*(3-2*s)

sides = 96
rings = [( -1.6+2.38*j/160, None) for j in range(161)]
# An elliptical cap has a horizontal tangent at its widest point and no collar.
for j in range(1, 32):
    theta = (math.pi/2)*j/32
    rings.append((.78+.22*math.sin(theta), .46*math.cos(theta)))
verts, faces, face_uvs = [], [], []
for t, cap_radius in rings:
    r = radius(t) if cap_radius is None else cap_radius
    depth = .82 - .47*smooth((-t-.2)/1.4)
    bend = .035*math.sin((t+1.6)*math.pi/2.6)
    for i in range(sides):
        a = 2*math.pi*i/sides
        # Detail stays below the silhouette: the mucosa normal supplies pores.
        relief = 1+.004*math.sin(3*a+.6)*math.sin((t+1.6)*math.pi/2.6)**2
        verts.append((bend+math.cos(a)*r*depth*relief,
                      math.sin(a)*r*relief, -t))

for j in range(len(rings)-1):
    for i in range(sides):
        k = (i+1)%sides
        faces.append((j*sides+i, (j+1)*sides+i, (j+1)*sides+k, j*sides+k))
        v0, v1 = (rings[j][0]+1.6)/2.6, (rings[j+1][0]+1.6)/2.6
        face_uvs.append(((i/sides,v0),(i/sides,v1),((i+1)/sides,v1),((i+1)/sides,v0)))

top, tip = len(verts), len(verts)+1
verts.extend([(0,0,1.6), (0,0,-1.0)])
last = (len(rings)-1)*sides
for i in range(sides):
    k = (i+1)%sides
    faces.extend([(top,i,k), (last+i,tip,last+k)])
    face_uvs.extend([((.5,0),(i/sides,0),((i+1)/sides,0)),
                    ((i/sides,(rings[-1][0]+1.6)/2.6),((i+.5)/sides,1),
                     ((i+1)/sides,(rings[-1][0]+1.6)/2.6))])

mesh = bpy.data.meshes.new('Uvula_Anatomical_Smooth')
mesh.from_pydata(verts, [], faces)
mesh.update()
for material in obj.data.materials:
    mesh.materials.append(material)
layer = mesh.uv_layers.new(name='UVMap')
for polygon, uvs in zip(mesh.polygons, face_uvs):
    polygon.use_smooth = True
    for loop, uv in zip(polygon.loop_indices, uvs):
        layer.data[loop].uv = uv
obj.data = mesh
obj['shape_version'] = 'anatomical_2026_09_29'
obj['profile'] = json.dumps(profile)
obj['runtime_tip_z_cm'] = -100.0

# Validate closed topology and outward normals before exporting.
import bmesh
bm = bmesh.new()
bm.from_mesh(mesh)
assert all(e.is_manifold for e in bm.edges), 'Uvula must be watertight'
volume = bm.calc_volume(signed=True)
assert volume > 0, 'Uvula normals must face outwards'
bm.free()

for item in bpy.context.selected_objects:
    item.select_set(False)
obj.hide_set(False)
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
matrix = obj.matrix_world.copy()
obj.location = (0,0,0)
obj.rotation_euler = (0,0,0)
obj.scale = (1,1,1)
bpy.context.view_layer.update()
bpy.ops.export_scene.fbx(filepath=str(root/'SM_Uvula.fbx'), use_selection=True,
                         object_types={'MESH'}, apply_unit_scale=True,
                         axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE',
                         bake_anim=False)
obj.matrix_world = matrix
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(root/'LivingThroat.blend'))
result = {'mesh': obj.name, 'vertices':len(verts), 'triangles':sum(len(f)-2 for f in faces),
          'watertight':True, 'signed_volume_m3':volume, 'profile':profile,
          'export':str(root/'SM_Uvula.fbx')}
(root/'UvulaAnatomyReport.json').write_text(json.dumps(result, indent=2))
