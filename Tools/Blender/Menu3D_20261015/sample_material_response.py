"""Bounded reference-camera inverse-look evidence on an isolated candidate.

Two actual Principled renders bracket diffuse BaseColor; all geometry, light,
roughness, normal, coat and specular controls remain unchanged. This is a local
response approximation, not proof that the source's physical albedo is known.
Never saves over the candidate or its editable sources.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--out', required=True)
p.add_argument('--samples', type=int, default=32)
args = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
candidate = Path(args.candidate).resolve()
out = Path(args.out).resolve()
out.mkdir(parents=True, exist_ok=True)
source_hash = hashlib.sha256(candidate.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(candidate))
scene = bpy.context.scene
root = bpy.data.objects['MC_JawsRoot']
root.animation_data_clear()
root['OpenAmount'] = 1.
scene.frame_set(42)
bpy.context.view_layer.update()
for obj in bpy.data.collections['Menu_ExistingCharacter'].all_objects:
    if obj:
        obj.hide_render = True

names = ['MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue']
materials = [bpy.data.materials[n] for n in names]
W, H = 963, 410
scene.render.resolution_x, scene.render.resolution_y = W, H
scene.render.resolution_percentage = 100
scene.cycles.samples = args.samples
scene.render.film_transparent = False
scene.render.image_settings.file_format = 'OPEN_EXR'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '32'
prefs = bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type = 'OPTIX'
prefs.get_devices()
for device in prefs.devices:
    device.use = device.type == 'OPTIX'
scene.cycles.device = 'GPU'

# Actual first-hit mesh/UV0 ownership, independent of source segmentation.
position_chunks, triangle_chunks, uv_chunks, material_chunks, object_chunks = [], [], [], [], []
object_names = []
offset = 0
dg = bpy.context.evaluated_depsgraph_get()
for obj in scene.objects:
    if obj.type != 'MESH' or obj.hide_render:
        continue
    slots = [s.material.name if s.material else '' for s in obj.material_slots]
    if not any(m in names for m in slots):
        continue
    evaluated = obj.evaluated_get(dg)
    mesh = evaluated.to_mesh()
    uv = mesh.uv_layers.get('UV0')
    if uv is None:
        raise RuntimeError('Required primary UV0 missing: ' + obj.name)
    mesh.calc_loop_triangles()
    xyz = np.empty((len(mesh.vertices), 3), dtype=np.float64)
    mesh.vertices.foreach_get('co', xyz.ravel())
    matrix = np.asarray(evaluated.matrix_world)
    xyz = xyz @ matrix[:3, :3].T + matrix[:3, 3]
    position_chunks.append(xyz)
    oi = len(object_names)
    object_names.append(obj.name)
    indices = np.empty((len(mesh.loop_triangles), 3), dtype=np.int32)
    loop_indices = np.empty_like(indices)
    material_indices = np.empty(len(indices), dtype=np.int32)
    mesh.loop_triangles.foreach_get('vertices', indices.ravel())
    mesh.loop_triangles.foreach_get('loops', loop_indices.ravel())
    mesh.loop_triangles.foreach_get('material_index', material_indices)
    all_uv = np.empty((len(uv.data), 2), dtype=np.float64)
    uv.data.foreach_get('uv', all_uv.ravel())
    lookup = np.array([names.index(n) if n in names else -1 for n in slots])
    ids = lookup[material_indices]
    selected = ids >= 0
    triangle_chunks.append(indices[selected] + offset)
    uv_chunks.append(all_uv[loop_indices[selected]])
    material_chunks.append(ids[selected])
    object_chunks.append(np.full(int(selected.sum()), oi, dtype=np.int16))
    offset += len(xyz)
    print('UV_RESPONSE_MESH', obj.name, len(indices), flush=True)
    evaluated.to_mesh_clear()
positions = np.concatenate(position_chunks)
triangles = np.concatenate(triangle_chunks)
tri_uv = np.concatenate(uv_chunks)
tri_material = np.concatenate(material_chunks)
tri_object = np.concatenate(object_chunks)
np.savez_compressed(out / 'world_geometry_uv0.npz', vertices=positions,
                    triangles=triangles, tri_uv=tri_uv,
                    tri_material=tri_material, tri_object=tri_object)
bvh = BVHTree.FromPolygons(positions.tolist(), triangles.tolist(), all_triangles=True)
uv_image = np.full((H, W, 2), -1, dtype=np.float32)
material_image = np.full((H, W), -1, dtype=np.int16)
object_image = np.full((H, W), -1, dtype=np.int16)
depth_image = np.full((H, W), np.nan, dtype=np.float32)
direction = Vector((0, 1, 0))
for y in range(H):
    z = (.5 - (y + .5) / H) * H / W * 16
    for x in range(W):
        origin = Vector((((x + .5) / W - .5) * 16, -30, z))
        loc, normal, index, distance = bvh.ray_cast(origin, direction)
        if index is None:
            continue
        v = positions[triangles[index]]
        # The front orthographic projection uses X/Z; solve barycentrics in it.
        a = np.stack((v[1, [0, 2]] - v[0, [0, 2]],
                      v[2, [0, 2]] - v[0, [0, 2]]), axis=1)
        det = np.linalg.det(a)
        if abs(det) < 1e-13:
            continue
        b = np.array([loc.x, loc.z]) - v[0, [0, 2]]
        bc = np.linalg.solve(a, b)
        bary = np.array([1 - bc.sum(), bc[0], bc[1]])
        uv_image[y, x] = bary @ tri_uv[index]
        material_image[y, x] = tri_material[index]
        object_image[y, x] = tri_object[index]
        depth_image[y, x] = loc.y
    if y % 100 == 0:
        print('UV_RESPONSE_ROW', y, flush=True)
np.savez_compressed(out / 'camera_uv0_ownership.npz', uv=uv_image,
                    material=material_image, object=object_image, depth=depth_image)

bsdfs = []
for mat in materials:
    bs = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    for link in list(bs.inputs['Base Color'].links):
        mat.node_tree.links.remove(link)
    bsdfs.append(bs)
for label, value in [('black', 0.), ('white', 1.)]:
    for bs in bsdfs:
        bs.inputs['Base Color'].default_value = (value, value, value, 1)
    scene.render.filepath = str(out / (label + '.exr'))
    bpy.ops.render.render(write_still=True)
    rendered = bpy.data.images.load(scene.render.filepath, check_existing=False)
    rgba = np.empty(W * H * 4, dtype=np.float32)
    rendered.pixels.foreach_get(rgba)
    # Blender image buffers start at bottom-left; all CPU evidence is top-left.
    np.save(out / (label + '_linear.npy'), rgba.reshape(H, W, 4)[::-1].copy())
    bpy.data.images.remove(rendered)
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_depth = '8'
    bpy.data.images['Render Result'].save_render(str(out / (label + '.png')), scene=scene)
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_depth = '32'
report = {
    'status': 'DIAGNOSTIC_LOCAL_RESPONSE_NOT_RECOVERED_PHYSICAL_ALBEDO',
    'candidate': str(candidate), 'candidate_sha256': source_hash,
    'candidate_unchanged': hashlib.sha256(candidate.read_bytes()).hexdigest() == source_hash,
    'blender': bpy.app.version_string, 'materials': names, 'objects': object_names,
    'camera': {'width': W, 'height': H, 'ortho_width': 16, 'axis': '-Y'},
    'exposure_stops': scene.view_settings.exposure,
    'view_transform': scene.view_settings.view_transform,
    'samples': args.samples,
    'limitations': ['Black/white simultaneous response includes changed indirect transport.',
                    'Specular mismatch and geometric contact faults must not be hidden by albedo.',
                    'Reference UI/character exclusions require explicit source-confidence fields.',
                    'Other poses and runtime response require independent actual review.'],
    'source_or_candidate_saved': False,
}
(out / 'response_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('MATERIAL_RESPONSE_READY', out, flush=True)
