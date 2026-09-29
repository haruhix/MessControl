"""Replace the throat profile with broad, nested palatal arches from the reference.

Keeps the existing skeleton, UV topology, three gameplay morphs and preview action.
The valve closes deeper inside the arches so the entrance keeps its rounded shape.
"""
import bpy
import math
import json
from pathlib import Path

root = Path('E:/DEVGAME/MessControl/ArtSource/LivingThroat')
obj = bpy.data.objects['SK_LivingThroat']
rig = obj.parent
assert bpy.context.mode == 'OBJECT' and rig.type == 'ARMATURE'
assert len(obj.data.vertices) == 113*193
assert {'Basis','SwallowOpen','Breath','Peristalsis'} <= set(obj.data.shape_keys.key_blocks.keys())

archive = bpy.data.collections.get('MC_Throat_Archive')
if archive is None:
    archive = bpy.data.collections.new('MC_Throat_Archive')
    bpy.context.scene.collection.children.link(archive)
if 'SK_Throat_PreReference' not in bpy.data.objects:
    previous = obj.copy()
    previous.data = obj.data.copy()
    previous.name = 'SK_Throat_PreReference'
    archive.objects.link(previous)
    previous.hide_set(True)
    previous.hide_render = True
archive.hide_viewport = True
archive.hide_render = True

# X depth, transverse radius, vertical radius in metres before runtime scaling.
# Each backward turn in X makes a rounded overhanging lip, followed by a recess.
profile = [(-1.25,11.40,8.70),(-.85,10.65,8.10),(-.80,9.85,7.50),
           (-1.00,9.05,7.00),(-.72,8.40,6.65),(.05,7.98,6.35),
           (1.15,7.75,6.12),(1.55,7.42,5.93),(1.30,6.95,5.70),
           (1.52,6.34,5.35),(2.32,5.90,5.05),(3.10,5.61,4.81),
           (3.60,5.20,4.48),(3.42,4.80,4.18),(4.10,4.22,3.82),
           (5.20,3.87,3.48),(6.80,3.40,3.02),(8.40,2.72,2.40),
           (10.40,1.72,1.48),(12.50,.06,.05)]

def spline(t, axis):
    q = t*(len(profile)-1)
    i = min(len(profile)-2,int(q))
    f = q-i
    p0,p1,p2,p3 = [profile[k][axis] for k in (max(0,i-1),i,i+1,min(len(profile)-1,i+2))]
    return .5*(2*p1+(-p0+p2)*f+(2*p0-5*p1+4*p2-p3)*f*f+(-p0+3*p1-3*p2+p3)*f*f*f)

def points(opening=0, breath=0, contraction=0):
    vertices = []
    for j in range(113):
        t = j/112
        x0,ry,rz = [spline(t,k) for k in range(3)]
        closure = math.exp(-((t-.825)/.072)**2)*(1-opening)
        # The narrow closed slit remains behind two visible palatal arches.
        ry *= 1-.50*closure
        rz *= 1-.9985*closure
        wave = math.sin(t*math.pi)
        for i in range(193):
            a = 2*math.pi*i/192
            ca,sa = math.cos(a),math.sin(a)
            edge_blend = math.exp(-(t/.06)**2)
            old_outline = .045*math.sin(3*a)+.025*math.cos(5*a)+.012*math.sin(11*a)
            organic = 1+edge_blend*old_outline+(1-edge_blend)*(.021*math.sin(2*a+t*1.2)+.014*math.cos(3*a-t*.7))
            exponent = 1-.12*(1-edge_blend)
            y = math.copysign(abs(ca)**exponent,ca)*ry*organic
            # Match the existing cheek shell's entire boundary, including its
            # seven-lobed vertical offset, to avoid a slit of sky at the seam.
            z = math.copysign(abs(sa)**exponent,sa)*rz*(organic+.02*math.sin(7*a)*edge_blend)
            # Sparse gentle mucosal furrows along the pillars; no angular lobes.
            pillar = abs(ca)**3
            ridge = .045*pillar*math.sin(11*a+.7*math.sin(t*5))*wave
            y += ca*ridge*(1-.50*closure)
            z += sa*ridge*(1-.9985*closure)
            x = x0+(.06*math.sin(3*a+t*3)+.065*pillar*math.sin(13*a+t*5))*wave
            y += .30*wave
            z += .10*math.sin(t*math.pi*1.5)
            y *= 1+breath*.009*wave-contraction*.075*math.exp(-((t-.77)/.17)**2)
            z *= 1+breath*.014*wave-contraction*.085*math.exp(-((t-.77)/.17)**2)
            vertices.append((x,y,z))
    return vertices

keys = obj.data.shape_keys.key_blocks
for name,coords in [('Basis',points()),('SwallowOpen',points(1)),
                    ('Breath',points(0,1)),('Peristalsis',points(0,0,1))]:
    for vertex,co in zip(keys[name].data,coords):
        vertex.co = co
    if name == 'Basis':
        for vertex,co in zip(obj.data.vertices,coords):
            vertex.co = co
obj.data.update()
obj['shape_version'] = 'reference_arches_2026_09_29'
obj['reference_profile'] = json.dumps(profile)

for item in bpy.context.selected_objects:
    item.select_set(False)
obj.hide_set(False)
rig.hide_set(False)
obj.select_set(True)
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
matrix = obj.matrix_world.copy()
values = {k.name:k.value for k in keys}
for k in keys:
    k.value = 0
obj.location = (0,0,0)
obj.rotation_euler = (0,0,0)
obj.scale = (1,1,1)
bpy.context.view_layer.update()
bpy.ops.export_scene.fbx(filepath=str(root/'SK_LivingThroat.fbx'),use_selection=True,
    object_types={'MESH','ARMATURE'},add_leaf_bones=False,use_armature_deform_only=True,
    apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',
    use_mesh_modifiers=False,mesh_smooth_type='FACE',bake_anim=False)
obj.matrix_world = matrix
for k in keys:
    k.value = values[k.name]
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(root/'LivingThroat.blend'))
result = {'vertices':len(obj.data.vertices), 'triangles':len(obj.data.polygons)*2,
          'morphs':[k.name for k in keys], 'profile':profile, 'closure_depth_m':spline(.825,0)}
(root/'ReferenceThroatReport.json').write_text(json.dumps(result,indent=2))
