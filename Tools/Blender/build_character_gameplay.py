"""Prepare Character.blend on the existing 107-bone gameplay skeleton.

Open the artist file in background Blender, then run this script. Only derived
files under ArtSource/CharacterCurrent are saved; the artist file stays intact.
"""
import bpy
import hashlib
import json
import math
from pathlib import Path
from mathutils import Matrix, Vector, geometry

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/CharacterCurrent'
OUT.mkdir(parents=True, exist_ok=True)
source_path = Path(bpy.data.filepath)
source = bpy.data.objects['SM_Character']
source_mesh = source.data
source_rig = bpy.data.objects['rig']
source_world = Matrix.Diagonal(Vector((100, 100, 100, 1))) @ source.matrix_world
assert len(source_mesh.vertices) == 4006, 'Review the new source topology.'
assert source_mesh.shape_keys and 'Eyes_Blink' in source_mesh.shape_keys.key_blocks
authored = {k.name: [source_world @ v.co for v in k.data]
            for k in source_mesh.shape_keys.key_blocks}
weights = [[(source.vertex_groups[g.group].name, g.weight)
            for g in v.groups if g.weight > 1e-5] for v in source_mesh.vertices]

# Identify the two actual eyeball islands, rather than every vertex with a
# small eye influence. The artist mesh also contains overlapping head weights.
adj = [set() for _ in source_mesh.vertices]
for edge in source_mesh.edges:
    a, b = edge.vertices
    adj[a].add(b)
    adj[b].add(a)
remaining = set(range(len(adj)))
components = []
while remaining:
    first = remaining.pop()
    part, queue = {first}, [first]
    while queue:
        for index in adj[queue.pop()]:
            if index in remaining:
                remaining.remove(index)
                part.add(index)
                queue.append(index)
    components.append(part)
eyes = {}
for side in ('l', 'r'):
    group = source.vertex_groups['c_eye.' + side].index
    candidates = [part for part in components if len(part) > 300
                  and all(any(g.group == group and g.weight > .99
                              for g in source_mesh.vertices[i].groups) for i in part)]
    assert len(candidates) == 1
    eyes[side] = candidates[0]
assert [len(eyes[s]) for s in ('l', 'r')] == [448, 448]
eye_ids = eyes['l'] | eyes['r']
blink_ids = {i for i, p in enumerate(authored['Basis'])
             if (authored['Eyes_Blink'][i] - p).length > .001}

with bpy.data.libraries.load(str(ROOT / 'ArtSource/CharacterGameplay/TeethGameplay.blend'),
                             link=False) as (available, loaded):
    loaded.objects = available.objects
rig = next(o for o in loaded.objects if o and o.type == 'ARMATURE')
bpy.context.scene.collection.objects.link(rig)
bpy.context.view_layer.update()
# The saved rig's object scale is animated in its original scene. Evaluate it
# before removing animation, just as the existing clip export pipeline does.
rig_world = rig.matrix_world.copy()
assert all(abs(s - .01) < 1e-6 for s in rig_world.to_scale())
rig.animation_data_clear()
rig.parent = None
rig.matrix_world = rig_world
rig.data.pose_position = 'POSE'
for bone in rig.pose.bones:
    bone.matrix_basis = Matrix.Identity(4)
assert len(rig.data.bones) == 107

obj = source.copy()
obj.data = source_mesh.copy()
obj.name = 'SK_CharacterGameplay'
obj.data.name = 'CharacterGameplaySkin'
obj.animation_data_clear()
obj.data.shape_keys.animation_data_clear()
obj.parent = None
obj.matrix_world = Matrix.Identity(4)
for modifier in list(obj.modifiers):
    obj.modifiers.remove(modifier)

def target_bone(name):
    mapped = name[:-2] + '_' + name[-1] if name.endswith(('.l', '.r', '.x')) else name
    for candidate in (mapped, 'c_' + mapped, mapped[2:] if mapped.startswith('c_') else mapped):
        if candidate in rig.data.bones:
            return candidate
    raise RuntimeError('Unmapped skin weight: ' + name)

# Rebind to the saved game skeleton without changing its reference matrices.
mapping = {name: target_bone(name) for row in weights for name, _ in row}
for group in list(obj.vertex_groups):
    obj.vertex_groups.remove(group)
groups = {name: obj.vertex_groups.new(name=name) for name in rig.data.bones.keys()}
for index, row in enumerate(weights):
    if index in eye_ids:
        row = [('c_eye_l' if index in eyes['l'] else 'c_eye_r', 1)]
    elif index in blink_ids:
        row = [('head_x', 1)]
    else:
        merged = {}
        for name, weight in row:
            mapped = mapping[name]
            merged[mapped] = merged.get(mapped, 0) + weight
        row = list(merged.items())
    total = sum(w for _, w in row)
    assert total > 0
    for name, weight in row:
        groups[name].add([index], weight / total, 'REPLACE')

basis = obj.data.shape_keys.key_blocks[0]
for index, p in enumerate(authored['Basis']):
    basis.data[index].co = p
for vertex in obj.data.vertices:
    vertex.co = authored['Basis'][vertex.index]
aliases = {'Mouth_Happy': 'Mouth_Smile', 'Mouth_Sad': 'Mouth_Frown'}
runtime_shapes = []
for key in list(obj.data.shape_keys.key_blocks)[1:]:
    name = key.name
    points = authored[name]
    for index, p in enumerate(points):
        key.data[index].co = p
    key.value = 0
    if name.startswith('Mouth_'):
        backup = obj.shape_key_add(name='Author_' + name, from_mix=False)
        for index, p in enumerate(points):
            backup.data[index].co = p
        key.name = aliases.get(name, name)
        runtime_shapes.append(key.name)
        # Runtime pupil size has one owner; emotional and speech forms must
        # not also resize or reshape the eyeballs.
        for index in eye_ids:
            key.data[index].co = authored['Basis'][index]

# Closed eyelids replace, rather than add to, the current expression. These
# corrections are weighted by expression_weight * blink_weight in C++.
for name in runtime_shapes:
    key = obj.data.shape_keys.key_blocks[name]
    if not any((key.data[i].co - basis.data[i].co).length > .01 for i in blink_ids):
        continue
    correction = obj.shape_key_add(name='BlinkCancel_' + name, from_mix=False)
    for index in blink_ids:
        correction.data[index].co = 2 * basis.data[index].co - key.data[index].co

mesh = obj.data
mesh.calc_loop_triangles()
uv = mesh.uv_layers.active.data
pupil_uv = Vector((.345, .8405))
eye_frames = []
for side in ('l', 'r'):
    ids = eyes[side]
    center = Vector([(min(basis.data[i].co[a] for i in ids)
                      + max(basis.data[i].co[a] for i in ids)) * .5 for a in range(3)])
    radius = max((basis.data[i].co - center).length for i in ids)
    pupil = None
    for tri in mesh.loop_triangles:
        if not all(i in ids and basis.data[i].co.y < center.y - radius * .25
                   for i in tri.vertices):
            continue
        tex = [uv[i].uv.copy() for i in tri.loops]
        if geometry.intersect_point_tri_2d(pupil_uv, *tex):
            pupil = geometry.barycentric_transform(
                pupil_uv.to_3d(), *(u.to_3d() for u in tex),
                *(basis.data[i].co for i in tri.vertices))
            break
    assert pupil is not None, 'Painted pupil UV was not found.'
    axis = (pupil - center).normalized()
    assert axis.y < -.8
    eye_frames.append((ids, center, axis))
radius_error = 0
for name, scale in [('Pupil_Dilate', 1.8), ('Pupil_Contract', .6)]:
    key = obj.shape_key_add(name=name, from_mix=False)
    for ids, center, axis in eye_frames:
        for index in ids:
            p = basis.data[index].co
            direction = p - center
            radius = direction.length
            direction.normalize()
            forward = direction.dot(axis)
            if forward <= 0:
                continue
            tangent = direction - axis * forward
            if tangent.length < 1e-6:
                continue
            theta = math.atan2(tangent.length * scale, forward)
            target = center + radius * (axis * math.cos(theta)
                                       + tangent.normalized() * math.sin(theta))
            key.data[index].co = target
            radius_error = max(radius_error, abs((target - center).length - radius))
    key.value = 0
assert radius_error < .0001

modifier = obj.modifiers.new('Gameplay skeleton', 'ARMATURE')
modifier.object = rig
obj.parent = rig
obj.matrix_parent_inverse = Matrix.Identity(4)
obj.matrix_basis = Matrix.Identity(4)
scene = bpy.data.scenes.new('CharacterGameplay')
scene.collection.objects.link(rig)
scene.collection.objects.link(obj)
bpy.context.window.scene = scene
for other in list(bpy.data.objects):
    if other not in (rig, obj):
        bpy.data.objects.remove(other, do_unlink=True)
for old_scene in list(bpy.data.scenes):
    if old_scene != scene:
        bpy.data.scenes.remove(old_scene)

# Export the artist's packed textures to deterministic local files.
texture_records = []
for slot_index, slot in enumerate(obj.material_slots):
    material = slot.material
    for node in material.node_tree.nodes:
        if node.type != 'TEX_IMAGE' or not node.image:
            continue
        image = node.image
        kind = next((name for name in ('BaseColor', 'Normal', 'OcclusionRoughnessMetallic')
                     if name in image.name), None)
        if not kind:
            continue
        filename = ('Character' if slot_index == 0 else 'Bag') + '_' + kind + '.png'
        image.filepath_raw = str(OUT / filename)
        image.file_format = 'PNG'
        image.save()
        record = {'slot': slot_index, 'kind': kind, 'file': filename}
        if record not in texture_records:
            texture_records.append(record)
scene.frame_set(0)
scene.render.fps = 24
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.context.view_layer.update()
bpy.data.orphans_purge(do_recursive=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'CharacterGameplay.blend'))
bpy.ops.export_scene.fbx(
    filepath=str(OUT / 'SK_CharacterGameplay.fbx'), use_selection=True,
    object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
    use_armature_deform_only=False, bake_anim=False, mesh_smooth_type='FACE')
report = {'source': str(source_path), 'source_sha256': hashlib.sha256(source_path.read_bytes()).hexdigest(),
          'vertices': len(mesh.vertices), 'bones': len(rig.data.bones),
          'shape_keys': [k.name for k in mesh.shape_keys.key_blocks],
          'morph_max_delta_cm': {k.name: max((v.co - b.co).length for v, b in zip(k.data, basis.data))
                                 for k in mesh.shape_keys.key_blocks[1:]},
          'eye_vertices': [len(eyes[s]) for s in ('l', 'r')],
          'blink_vertices': len(blink_ids), 'pupil_radius_error_cm': radius_error,
          'textures': texture_records, 'weight_mapping': mapping}
(OUT / 'CharacterReport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('MC_CHARACTER_BUILD_PASS', json.dumps({k: report[k] for k in
      ('vertices', 'bones', 'eye_vertices', 'blink_vertices', 'pupil_radius_error_cm')}))
