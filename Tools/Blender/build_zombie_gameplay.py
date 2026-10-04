"""Export the artist zombie as a clean skinned boss visual, without editing its source.

Run background Blender with Character_Zombie.blend open. Its root/thigh rest
transforms differ from TeethGameplay; export an independent skeleton rather than
silently overwriting the player reference pose or claiming clip compatibility.
"""
import bpy
import hashlib
import json
import sys
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/ZombieBoss'
OUT.mkdir(parents=True, exist_ok=True)
source_path = Path(bpy.data.filepath)
assert source_path.name == 'Character_Zombie.blend', 'Open the actual artist zombie file.'
source_hash = hashlib.sha256(source_path.read_bytes()).hexdigest()
source_rig = bpy.data.objects['rig']
sources = [bpy.data.objects[name] for name in ('SM_Character', 'SM_Character.001')]
assert [len(obj.data.vertices) for obj in sources] == [3353, 35]
bpy.context.view_layer.update()
source_rig_world = source_rig.matrix_world.copy()
source_rig_bones = len(source_rig.data.bones)

weights = {}
used = set()
authored = {}
for source in sources:
    world = Matrix.Diagonal(Vector((100, 100, 100, 1))) @ source.matrix_world
    basis = source.data.shape_keys.key_blocks[0].data if source.data.shape_keys else source.data.vertices
    authored[source.name] = {
        'Basis': [world @ point.co for point in basis],
        'shapes': {key.name: [world @ point.co for point in key.data]
                   for key in list(source.data.shape_keys.key_blocks)[1:]} if source.data.shape_keys else {}
    }
    rows = [[(source.vertex_groups[group.group].name, group.weight)
             for group in vertex.groups if group.weight > 1e-5]
            for vertex in source.data.vertices]
    weights[source.name] = rows
    used.update(name for row in rows for name, _ in row)
assert all(name in source_rig.data.bones for name in used), 'A weighted bone is absent from the artist rig.'
assert all(sum(weight for _, weight in row) > 0 for rows in weights.values() for row in rows), 'Unweighted zombie vertex.'

with bpy.data.libraries.load(str(ROOT / 'ArtSource/CharacterGameplay/TeethGameplay.blend'),
                             link=False) as (available, loaded):
    loaded.objects = available.objects
canonical = next(obj for obj in loaded.objects if obj and obj.type == 'ARMATURE')
bpy.context.scene.collection.objects.link(canonical)
bpy.context.view_layer.update()
assert len(canonical.data.bones) == 107

def target_name(name):
    converted = name[:-2] + '_' + name[-1] if name.endswith(('.l', '.r', '.x')) else name
    for candidate in (converted, 'c_' + converted,
                      converted[2:] if converted.startswith('c_') else converted):
        if candidate in canonical.data.bones:
            return candidate
    raise RuntimeError('Unmapped weighted zombie bone: ' + name)

mapping = {name: target_name(name) for name in sorted(used)}
assert len(set(mapping.values())) == len(mapping)
head_errors = {}
rotation_errors = {}
scale_to_cm = Matrix.Diagonal(Vector((100, 100, 100, 1)))
for name, target in mapping.items():
    bone = source_rig.data.bones[name]
    original = scale_to_cm @ source_rig_world @ bone.matrix_local
    game = canonical.data.bones[target].matrix_local
    head_errors[target] = (original.translation - game.translation).length
    rotation_errors[target] = original.to_quaternion().rotation_difference(game.to_quaternion()).angle
compatible = max(head_errors.values()) < .05 and max(rotation_errors.values()) < .005
source_rest_heads = {mapping[name]: scale_to_cm @ source_rig_world @ source_rig.data.bones[name].head_local
                     for name in mapping}

# Keep the source global reference matrices. Nonweighted control bones are removed;
# their nearest weighted ancestor retains the skin hierarchy, with one export root.
scene = bpy.data.scenes.new('ZombieBossGameplay')
armature = bpy.data.armatures.new('ZombieBossDeformRig')
rig = bpy.data.objects.new('Armature', armature)
scene.collection.objects.link(rig)
rig.matrix_world = Matrix.Diagonal(Vector((.01, .01, .01, 1)))
bpy.context.window.scene = scene
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
root = armature.edit_bones.new('root')
root.head, root.tail, root.use_deform = (0, 0, 0), (0, 0, 10), False
parents = {}
for name in sorted(used):
    source = source_rig.data.bones[name]
    bone = armature.edit_bones.new(mapping[name])
    original = scale_to_cm @ source_rig_world @ source.matrix_local
    rotation = original.to_quaternion().to_matrix().to_4x4()
    rotation.translation = original.translation
    bone.matrix = rotation
    bone.length = (scale_to_cm @ source_rig_world @ source.tail_local
                   - scale_to_cm @ source_rig_world @ source.head_local).length
    bone.use_deform = True
    parent = source.parent
    while parent and parent.name not in used:
        parent = parent.parent
    parents[mapping[name]] = mapping[parent.name] if parent else 'root'
for name, parent in parents.items():
    armature.edit_bones[name].parent = armature.edit_bones[parent]
    armature.edit_bones[name].use_connect = False
bpy.ops.object.mode_set(mode='OBJECT')
armature.pose_position = 'POSE'

objects = []
material_copies = {}
normalization_error = 0.
maximum_influences = 0
for index, source in enumerate(sources):
    obj = source.copy()
    obj.data = source.data.copy()
    obj.name = 'SK_ZombieBoss' if index == 0 else 'ZombieBossMouth'
    obj.animation_data_clear()
    if obj.data.shape_keys:
        obj.data.shape_keys.animation_data_clear()
    obj.parent = None
    obj.matrix_world = Matrix.Identity(4)
    for modifier in list(obj.modifiers):
        obj.modifiers.remove(modifier)
    for group in list(obj.vertex_groups):
        obj.vertex_groups.remove(group)
    groups = {name: obj.vertex_groups.new(name=name) for name in mapping.values()}
    for vertex_index, row in enumerate(weights[source.name]):
        total = sum(weight for _, weight in row)
        normalized = [(mapping[name], weight / total) for name, weight in row]
        maximum_influences = max(maximum_influences, len(normalized))
        for name, weight in normalized:
            groups[name].add([vertex_index], weight, 'REPLACE')
        normalization_error = max(normalization_error, abs(sum(weight for _, weight in normalized) - 1))
    for vertex, point in zip(obj.data.vertices, authored[source.name]['Basis']):
        vertex.co = point
    if obj.data.shape_keys:
        for key in obj.data.shape_keys.key_blocks:
            points = authored[source.name]['Basis'] if key == obj.data.shape_keys.key_blocks[0] else authored[source.name]['shapes'][key.name]
            for vertex, point in zip(key.data, points):
                vertex.co = point
            key.value = 0
    for slot_index, slot in enumerate(obj.material_slots):
        if not slot.material:
            continue
        original = slot.material
        if original.name not in material_copies:
            copy = original.copy()
            copy.name = 'Zombie_Body' if slot_index == 0 else 'Zombie_Bag'
            material_copies[original.name] = copy
        slot.material = material_copies[original.name]
    modifier = obj.modifiers.new('Zombie deform skeleton', 'ARMATURE')
    modifier.object = rig
    obj.parent = rig
    obj.matrix_parent_inverse = Matrix.Identity(4)
    obj.matrix_basis = Matrix.Identity(4)
    scene.collection.objects.link(obj)
    objects.append(obj)

material_face_counts = {}
used_materials = set()
for obj in objects:
    obj.data.calc_loop_triangles()
    counts = {slot.material.name: {'polygons': 0, 'triangles': 0}
              for slot in obj.material_slots if slot.material}
    for polygon in obj.data.polygons:
        counts[obj.material_slots[polygon.material_index].material.name]['polygons'] += 1
    for triangle in obj.data.loop_triangles:
        material = obj.material_slots[triangle.material_index].material.name
        counts[material]['triangles'] += 1
        used_materials.add(material)
    material_face_counts[obj.name] = counts

texture_records = []
for material in material_copies.values():
    if material.name not in used_materials:
        continue
    exported = set()
    selected_images = {}
    for node in material.node_tree.nodes:
        if node.type != 'TEX_IMAGE' or not node.image:
            continue
        image = node.image
        kind = next((name for name in ('BaseColor', 'Normal', 'OcclusionRoughnessMetallic')
                     if name in image.name), None)
        if not kind:
            continue
        if kind in selected_images:
            selected = selected_images[kind]
            if image.packed_file and selected.packed_file and image.packed_file.data == selected.packed_file.data:
                node.image = selected
            continue
        filename = material.name + '_' + kind + '.png'
        image.filepath_raw = str(OUT / filename)
        image.file_format = 'PNG'
        image.save()
        texture_records.append({'material': material.name, 'kind': kind, 'file': filename,
                                'source_image': image.name, 'size': list(image.size),
                                'sha256': hashlib.sha256((OUT / filename).read_bytes()).hexdigest()})
        exported.add(kind)
        selected_images[kind] = image
    assert exported == {'BaseColor', 'Normal', 'OcclusionRoughnessMetallic'}, 'Incomplete zombie material textures.'

all_points = [point for data in authored.values() for point in data['Basis']]
rest_head_error = max((armature.bones[name].head_local - head).length
                      for name, head in source_rest_heads.items())
assert rest_head_error < .001, 'Export changed the original weighted bone reference heads.'
for obj in list(bpy.data.objects):
    if obj not in (rig, *objects):
        bpy.data.objects.remove(obj, do_unlink=True)
for old_scene in list(bpy.data.scenes):
    if old_scene != scene:
        bpy.data.scenes.remove(old_scene)
scene.frame_set(0)
scene.render.fps = 24
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.object.select_all(action='DESELECT')
for obj in (rig, *objects):
    obj.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.context.view_layer.update()
assert normalization_error < 1e-6
bpy.data.orphans_purge(do_recursive=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ZombieBoss.blend'))
bpy.ops.export_scene.fbx(filepath=str(OUT / 'SK_ZombieBoss.fbx'), use_selection=True,
                         object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                         use_armature_deform_only=False, bake_anim=False, mesh_smooth_type='FACE')
report = {'source': str(source_path), 'source_sha256': source_hash,
          'canonical': 'ArtSource/CharacterGameplay/TeethGameplay.blend',
          'canonical_sha256': hashlib.sha256((ROOT / 'ArtSource/CharacterGameplay/TeethGameplay.blend').read_bytes()).hexdigest(),
          'canonical_animation_compatible': compatible,
          'export_skeleton': 'Independent artist reference pose; never overwrite the player skeleton.',
          'source_rig_bones': source_rig_bones, 'weighted_source_bones': len(used), 'export_bones': len(armature.bones),
          'vertices': [len(obj.data.vertices) for obj in objects], 'maximum_influences': maximum_influences,
          'weight_normalization_error': normalization_error,
          'export_reference_head_error_cm': rest_head_error,
          'bounds_cm': {'min': [min(point[axis] for point in all_points) for axis in range(3)],
                        'max': [max(point[axis] for point in all_points) for axis in range(3)]},
          'materials': sorted(used_materials), 'material_face_counts': material_face_counts,
          'triangles': sum(count['triangles'] for counts in material_face_counts.values() for count in counts.values()),
          'textures': texture_records, 'weight_mapping': mapping,
          'reference_head_error_cm': head_errors, 'reference_rotation_error_radians': rotation_errors,
          'shape_keys': {obj.name: [key.name for key in obj.data.shape_keys.key_blocks]
                         if obj.data.shape_keys else [] for obj in objects}}
assert hashlib.sha256(source_path.read_bytes()).hexdigest() == source_hash
(OUT / 'ZombieBossReport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('MC_ZOMBIE_BUILD_PASS', json.dumps({key: report[key] for key in
      ('canonical_animation_compatible', 'export_bones', 'vertices', 'weight_normalization_error', 'bounds_cm')}))

if '--preview' in sys.argv:
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x, scene.render.resolution_y = 720, 720
    scene.render.resolution_percentage = 100
    scene.world = bpy.data.worlds.new('ZombiePreviewWorld')
    scene.world.color = (.07, .07, .07)
    camera_data = bpy.data.cameras.new('PreviewCamera')
    camera = bpy.data.objects.new('PreviewCamera', camera_data)
    scene.collection.objects.link(camera)
    camera.location = (1.4, -2.6, 1.5)
    target = Vector((0, 0, .57))
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.lens = 55
    scene.camera = camera
    for name, location, energy, size in (
            ('Key', (2, -3, 4), 600, 3), ('Fill', (-2, -1, 2), 300, 3), ('Rim', (0, 2, 3), 500, 2)):
        data = bpy.data.lights.new(name, 'AREA')
        light = bpy.data.objects.new(name, data)
        scene.collection.objects.link(light)
        light.location = location
        light.rotation_euler = (target - light.location).to_track_quat('-Z', 'Y').to_euler()
        data.energy, data.shape, data.size = energy, 'DISK', size
    scene.render.filepath = str(ROOT / 'Saved/RogueReview/ZombiePreview.png')
    bpy.ops.render.render(write_still=True)
