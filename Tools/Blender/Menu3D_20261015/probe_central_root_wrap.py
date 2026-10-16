"""Granted-only isolated CentralR hidden-root/gingival-wrap actual proof.

Apply the CPU coordinate stage to copies of two candidate meshes, preserve native
shader RGB and all other shader inputs, perform actual compound ray ownership,
then exactly two renders: native PBR and constant linear.18 BaseColor control.
No original/candidate blend is saved. This is a geometric hypothesis, not a final
asset or UV0/source-projection acceptance.
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


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def world_vertices(obj):
    xyz = np.empty((len(obj.data.vertices), 3), dtype=np.float64)
    obj.data.vertices.foreach_get('co', xyz.ravel())
    matrix = np.asarray(obj.matrix_world)
    return xyz @ matrix[:3, :3].T + matrix[:3, 3]


def uv_digest(obj):
    digest = hashlib.sha256()
    for layer in obj.data.uv_layers:
        xy = np.empty(len(layer.data) * 2, dtype=np.float32)
        layer.data.foreach_get('uv', xy)
        digest.update(layer.name.encode())
        digest.update(xy.tobytes())
    return digest.hexdigest()


p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--stage', required=True)
p.add_argument('--out', required=True)
p.add_argument('--samples', type=int, default=24)
p.add_argument('--upper-six', action='store_true', help='Use the separately prepared v14 six-upper coordinate stage')
a = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
if not bpy.app.background:
    raise RuntimeError('Refusing live Blender; isolated background only')
candidate, stage, out = map(Path, (a.candidate, a.stage, a.out))
out.mkdir(parents=True, exist_ok=True)
receipt_path = stage.parent / ('upper6_root_wrap_v14_preparation.json' if a.upper_six else 'cpu_root_wrap_preparation.json')
receipt = json.loads(receipt_path.read_text())
candidate_hash = sha(candidate)
if candidate_hash != (receipt.get('basis_candidate_sha256') or receipt['original_basis_sha256']):
    raise RuntimeError('Exact v18 candidate hash required')
coordinates = np.load(stage)
bpy.ops.wm.open_mainfile(filepath=str(candidate.resolve()))
scene = bpy.context.scene
root = bpy.data.objects['MC_JawsRoot']
root.animation_data_clear()
root['OpenAmount'] = 1.
scene.frame_set(42)
bpy.context.view_layer.update()
W, H = 963, 410
if scene.camera.data.type != 'ORTHO' or abs(scene.camera.data.ortho_scale - 16) > 1e-7:
    raise RuntimeError('Canonical source camera required')
material_state = []
for name in ('MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue'):
    mat = bpy.data.materials[name]
    bs = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    socket = bs.inputs['Base Color']
    material_state.append((mat, socket, list(socket.default_value),
                           [(link.from_socket, link.to_socket) for link in socket.links]))
report = {'status': 'ACTUAL_SINGLE_CENTRAL_R_ROOT_WRAP_DIAGNOSTIC_NOT_ACCEPTED',
          'candidate': str(candidate.resolve()), 'candidate_sha256_before': candidate_hash,
          'coordinate_stage': str(stage.resolve()), 'coordinate_stage_sha256': sha(stage),
          'cpu_preparation': str(receipt_path),
          'parameters': receipt.get('parameters') or {'geometry_version': 'v14', 'root_helper': receipt['root_helper'], 'gum_helper': receipt['gum_helper']},
          'UV_contract': {'UV_arrays': 'UNCHANGED', 'world_XZ_to_UV0_affine_audit': 'STALE after this coordinate deformation',
                          'secondary_UV_SourceProjection': 'STALE diagnostic coordinates; not updated',
                          'new_texture_bake_or_projection_claim': False},
          'shader_contract': 'Native v18 maps/RGB/roughness/specular/SSS/normal/coat fixed; gray overrides only Base Color.',
          'changes': []}
target_names = [r['object'] for r in receipt['changes']] if a.upper_six else ['MC_DAY03_Upper_Central_R', 'MC_DAY03_Upper_Gum']
report['target_names'] = target_names
if a.upper_six:
    report['status'] = 'ACTUAL_V14_SIX_UPPER_ROOT_WRAP_DIAGNOSTIC_NOT_ACCEPTED'
for name in target_names:
    obj = bpy.data.objects[name]
    before, after = coordinates[name + '__before'], coordinates[name + '__after']
    if obj.data.shape_keys is not None or obj.modifiers:
        raise RuntimeError('This diagnostic requires the exact modifier-free v18 frozen mesh: ' + name)
    actual_before = world_vertices(obj)
    if actual_before.shape != before.shape or np.max(np.abs(actual_before - before)) > 2e-6:
        raise RuntimeError('Actual mesh world-coordinate/order mismatch: ' + name)
    uv_before = uv_digest(obj)
    parent_before = obj.parent.name if obj.parent else None
    obj.data = obj.data.copy()
    inverse = np.linalg.inv(np.asarray(obj.matrix_world))
    local = after @ inverse[:3, :3].T + inverse[:3, 3]
    obj.data.vertices.foreach_set('co', local.astype(np.float32).ravel())
    obj.data.update()
    actual_after = world_vertices(obj)
    if np.max(np.abs(actual_after - after)) > 2e-6 or uv_digest(obj) != uv_before:
        raise RuntimeError('Coordinate application or UV invariant failed: ' + name)
    report['changes'].append({'object': name, 'parent': parent_before,
                               'vertices': len(before), 'UV_sha256_before_after': uv_before,
                               'world_coordinate_max_apply_error': float(np.max(np.abs(actual_after - after))),
                               'parent_unchanged': (obj.parent.name if obj.parent else None) == parent_before})
bpy.context.view_layer.update()

# Actual evaluated scene geometry, independent of any source-color mask.
vertices, triangles, tri_owner = [], [], []
object_names = []
offset = 0
dg = bpy.context.evaluated_depsgraph_get()
for obj in scene.objects:
    if obj.type != 'MESH' or obj.hide_render:
        continue
    if not any(s.material and s.material.name in ('MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue') for s in obj.material_slots):
        continue
    evaluated = obj.evaluated_get(dg)
    mesh = evaluated.to_mesh()
    mesh.calc_loop_triangles()
    xyz = np.empty((len(mesh.vertices), 3), dtype=np.float64)
    mesh.vertices.foreach_get('co', xyz.ravel())
    matrix = np.asarray(evaluated.matrix_world)
    xyz = xyz @ matrix[:3, :3].T + matrix[:3, 3]
    indices = np.empty((len(mesh.loop_triangles), 3), dtype=np.int32)
    mesh.loop_triangles.foreach_get('vertices', indices.ravel())
    vertices.append(xyz)
    triangles.append(indices + offset)
    tri_owner.extend([len(object_names)] * len(indices))
    object_names.append(obj.name)
    offset += len(xyz)
    evaluated.to_mesh_clear()
vertices, triangles, tri_owner = np.concatenate(vertices), np.concatenate(triangles), np.asarray(tri_owner)
bvh = BVHTree.FromPolygons(vertices.tolist(), triangles.tolist(), all_triangles=True)
constraints_path = receipt.get('source_margin_constraints') or str(stage.parents[2] / 'V03/Constraints/source_locked_gums.json')
constraints = json.loads(Path(constraints_path).read_text())
margin = np.asarray(constraints['upper_margin_y_by_x'])
ray_rows = []
if a.upper_six:
    sample_pixels = sorted({(r['x'], y) for r in receipt['compound_ownership_columns']
                            for y in range(r['source_first_enamel_y'] - 3, r['source_first_enamel_y'] + 5)})
else:
    sample_pixels = [(x, y) for x in range(487, 568)
                     for y in range(int(np.floor(margin[x])) - 3, int(np.ceil(margin[x])) + 6)]
for x, y in sample_pixels:
        origin = Vector((((x + .5) / W - .5) * 16, -30, (205 - y - .5) * 16 / W))
        loc, normal, face, distance = bvh.ray_cast(origin, Vector((0, 1, 0)))
        ray_rows.append({'pixel_xy': [x, y], 'object': object_names[tri_owner[face]] if face is not None else None,
                         'world_Y': loc.y if face is not None else None,
                         'geometric_normal_xyz': list(normal) if face is not None else None})
report['actual_compound_first_hits'] = ray_rows
report['old_crack_pixel_525_26'] = next(r for r in ray_rows if r['pixel_xy'] == [525, 26])
report['old_crack_now_covered_by_mouth_surface'] = report['old_crack_pixel_525_26']['object'] in ('MC_DAY03_Upper_Central_R', 'MC_DAY03_Upper_Gum')
scene.render.resolution_x, scene.render.resolution_y = W, H
scene.render.resolution_percentage = 100
scene.render.threads_mode, scene.render.threads = 'FIXED', 2
scene.render.engine = 'CYCLES'
scene.cycles.samples = a.samples
scene.cycles.seed = 0
scene.cycles.use_denoising = True
scene.render.film_transparent = False
prefs = bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type = 'OPTIX'
prefs.get_devices()
for device in prefs.devices:
    device.use = device.type == 'OPTIX'
if not any(device.use for device in prefs.devices):
    raise RuntimeError('No OPTIX device; no unapproved fallback')
scene.cycles.device = 'GPU'
if scene.view_settings.view_transform != 'Standard' or abs(scene.view_settings.exposure - .5) > 1e-7:
    raise RuntimeError('Unchanged v05 Standard+.5 required')
character = [(ob, ob.hide_render) for ob in bpy.data.collections['Menu_ExistingCharacter'].all_objects if ob]
report['captures'] = []
for mode in ('pbr', 'gray'):
    if mode == 'gray':
        for mat, socket, default, links in material_state:
            for link in list(socket.links):
                mat.node_tree.links.remove(link)
            socket.default_value = (.18, .18, .18, 1.)
        for obj, original in character:
            obj.hide_render = True
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '32'
    scene.render.filepath = str((out / (mode + '.exr')).resolve())
    started = time.perf_counter()
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath, check_existing=False)
    rgba = np.empty(W * H * 4, dtype=np.float32)
    image.pixels.foreach_get(rgba)
    np.save(out / (mode + '_linear.npy'), rgba.reshape(H, W, 4)[::-1].copy())
    bpy.data.images.remove(image)
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_depth = '8'
    bpy.data.images['Render Result'].save_render(str((out / (mode + '.png')).resolve()), scene=scene)
    report['captures'].append({'mode': mode, 'seconds': time.perf_counter() - started})
report['candidate_sha256_after'] = sha(candidate)
report['candidate_unchanged'] = report['candidate_sha256_after'] == candidate_hash
report['source_or_candidate_blend_saved'] = False
(out / 'actual_root_wrap_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')
if not report['candidate_unchanged']:
    raise RuntimeError('Input candidate changed unexpectedly')
print('CENTRAL_ROOT_WRAP_TWO_CAPTURES_READY', str(out), flush=True)
