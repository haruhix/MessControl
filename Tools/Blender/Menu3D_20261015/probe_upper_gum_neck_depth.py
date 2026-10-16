"""Actual three-position upper gingiva contact diagnostic on an isolated copy.

Exactly six captures: native PBR and constant linear.18 BaseColor, at each
world-Y offset -.04/0/+.04. All other shader properties, lights and geometry stay
unchanged. No UV/BC collar masks and no source/candidate blend save.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

import bpy
import numpy as np


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def mesh_digest(obj):
    vertices = np.empty(len(obj.data.vertices) * 3, dtype=np.float32)
    obj.data.vertices.foreach_get('co', vertices)
    return hashlib.sha256(vertices.tobytes()).hexdigest()


def light_state():
    return {ob.name: {'matrix': [float(v) for row in ob.matrix_world for v in row],
                      'energy': ob.data.energy, 'color': list(ob.data.color),
                      'shape': ob.data.shape if ob.data.type == 'AREA' else None,
                      'size': ob.data.size if ob.data.type == 'AREA' else None,
                      'size_y': ob.data.size_y if ob.data.type == 'AREA' else None,
                      'visible_glossy': ob.visible_glossy, 'visible_diffuse': ob.visible_diffuse,
                      'hide_render': ob.hide_render}
            for ob in bpy.context.scene.objects if ob.type == 'LIGHT'}


p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--out', required=True)
p.add_argument('--samples', type=int, default=24)
a = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
if not bpy.app.background:
    raise RuntimeError('This diagnostic refuses live Blender')
candidate = Path(a.candidate).resolve()
out = Path(a.out).resolve()
out.mkdir(parents=True, exist_ok=True)
candidate_hash = digest(candidate)
bpy.ops.wm.open_mainfile(filepath=str(candidate))
scene = bpy.context.scene
root = bpy.data.objects['MC_JawsRoot']
root.animation_data_clear()
root['OpenAmount'] = 1.
scene.frame_set(42)
bpy.context.view_layer.update()
gum = bpy.data.objects.get('MC_DAY03_Upper_Gum')
if gum is None or gum.type != 'MESH':
    raise RuntimeError('Exact upper gum mesh MC_DAY03_Upper_Gum required; no automatic alias')
baseline_matrix = gum.matrix_world.copy()
baseline_geometry_hash = mesh_digest(gum)
baseline_lights = light_state()
baseline_others = {ob.name: [float(v) for row in ob.matrix_world for v in row]
                   for ob in scene.objects if ob.type == 'MESH' and ob != gum}
camera = scene.camera
if camera is None or camera.data.type != 'ORTHO' or abs(camera.data.ortho_scale - 16) > 1e-6:
    raise RuntimeError('Canonical source-width16 camera required')
W, H = 963, 410
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
    raise RuntimeError('No OPTIX device; explicit fallback grant required')
scene.cycles.device = 'GPU'
if scene.view_settings.view_transform != 'Standard' or abs(scene.view_settings.exposure - .5) > 1e-7:
    raise RuntimeError('Expected unchanged v05 Standard exposure+.5 baseline')
material_state = []
for name in ('MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue'):
    mat = bpy.data.materials[name]
    bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    socket = bsdf.inputs['Base Color']
    material_state.append((mat, socket, list(socket.default_value),
                           [(link.from_socket, link.to_socket) for link in socket.links]))
character = [(ob, ob.hide_render) for ob in bpy.data.collections['Menu_ExistingCharacter'].all_objects if ob]
report = {'status': 'ACTUAL_BOUNDED_UPPER_GUM_WORLD_Y_SWEEP_NOT_ACCEPTANCE',
          'candidate': str(candidate), 'candidate_sha256_before': candidate_hash,
          'blender': bpy.app.version_string, 'device': 'OPTIX', 'samples': a.samples,
          'target_object': gum.name, 'target_parent': gum.parent.name if gum.parent else None,
          'baseline_world_matrix': [list(row) for row in baseline_matrix],
          'gum_mesh_geometry_hash_before': baseline_geometry_hash,
          'resolution': [W, H], 'camera_world_matrix': [list(row) for row in camera.matrix_world],
          'view_transform': scene.view_settings.view_transform, 'exposure': scene.view_settings.exposure,
          'lights_before': baseline_lights,
          'invariants': ['Only MC_DAY03_Upper_Gum world Y changes between geometric variants.',
                         'Gray control disconnects only Base Color on all four mouth materials.',
                         'Roughness/specular/normal/SSS/coat remain native to the candidate.',
                         'Existing character hidden only for gray control.',
                         'No geometry-coordinate, topology, UV, source-image or collar-mask edits.',
                         'No candidate/source .blend saves.'],
          'captures': []}


def checkpoint():
    (out / 'neck_depth_sweep_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')


def capture(folder, label):
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '32'
    scene.render.filepath = str(folder / (label + '.exr'))
    started = time.perf_counter()
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath, check_existing=False)
    rgba = np.empty(W * H * 4, dtype=np.float32)
    image.pixels.foreach_get(rgba)
    np.save(folder / (label + '_linear.npy'), rgba.reshape(H, W, 4)[::-1].copy())
    bpy.data.images.remove(image)
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_depth = '8'
    bpy.data.images['Render Result'].save_render(str(folder / (label + '.png')), scene=scene)
    return {'seconds': time.perf_counter() - started, 'png': str(folder / (label + '.png')),
            'linear_npy': str(folder / (label + '_linear.npy'))}


for label, dy in [('Yminus004', -.04), ('Yzero', 0.), ('Yplus004', .04)]:
    folder = out / label
    folder.mkdir(exist_ok=True)
    matrix = baseline_matrix.copy()
    matrix.translation.y += dy
    gum.matrix_world = matrix
    bpy.context.view_layer.update()
    actual_delta = gum.matrix_world.translation - baseline_matrix.translation
    if abs(actual_delta.y - dy) > 1e-6 or abs(actual_delta.x) > 1e-7 or abs(actual_delta.z) > 1e-7:
        raise RuntimeError('Upper-gum world-Y-only placement failed')
    print('NECK_SWEEP_BEGIN', label, list(actual_delta), flush=True)
    record = {'name': label, 'upper_gum_world_y_delta': dy,
              'actual_world_translation': list(gum.matrix_world.translation)}
    record['pbr'] = capture(folder, 'pbr')
    for mat, socket, default, links in material_state:
        for link in list(socket.links):
            mat.node_tree.links.remove(link)
        socket.default_value = (.18, .18, .18, 1.)
    for obj, original_hide in character:
        obj.hide_render = True
    record['gray_basecolor_control'] = capture(folder, 'gray')
    for mat, socket, default, links in material_state:
        socket.default_value = default
        for from_socket, to_socket in links:
            mat.node_tree.links.new(from_socket, to_socket)
    for obj, original_hide in character:
        obj.hide_render = original_hide
    report['captures'].append(record)
    checkpoint()
    print('NECK_SWEEP_DONE', label, flush=True)

gum.matrix_world = baseline_matrix
bpy.context.view_layer.update()
report['gum_mesh_geometry_hash_after'] = mesh_digest(gum)
report['gum_mesh_vertices_unchanged'] = report['gum_mesh_geometry_hash_after'] == baseline_geometry_hash
report['lights_unchanged'] = light_state() == baseline_lights
report['other_mesh_world_transforms_unchanged'] = baseline_others == {
    ob.name: [float(v) for row in ob.matrix_world for v in row]
    for ob in scene.objects if ob.type == 'MESH' and ob != gum}
report['candidate_sha256_after'] = digest(candidate)
report['candidate_file_unchanged'] = report['candidate_sha256_after'] == candidate_hash
if not all(report[key] for key in ('gum_mesh_vertices_unchanged', 'lights_unchanged',
                                   'other_mesh_world_transforms_unchanged', 'candidate_file_unchanged')):
    raise RuntimeError('Neck-depth sweep invariant failed; inspect manifest')
checkpoint()
print('NECK_DEPTH_SIX_CAPTURES_READY', str(out), flush=True)
