"""Three matched-radiance reflection-card probes; isolated candidate, no saves.

The existing wide Fang card is disabled in every probe. This is an isolated
single-card orientation diagnostic, not a reproduction of the full v05 rig.
Geometry, diffuse lights, material constants and fixed reference camera stay put.
"""
import argparse
import copy
import fnmatch
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import bpy
import numpy as np
from mathutils import Vector


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--profile', required=True)
p.add_argument('--binding', required=True)
p.add_argument('--out', required=True)
p.add_argument('--samples', type=int, default=24)
p.add_argument('--portrait-translations', type=float, nargs='+', default=None,
               help='Override default probes with B portrait translated in world Z; target moves identically')
p.add_argument('--receiver-profile', action='store_true',
               help='Recreate exact supplied logical lights and capture one combined receiver-specific probe')
a = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
if not bpy.app.background:
    raise RuntimeError('Only an isolated background Blender process is allowed')
candidate, profile_path, binding_path = map(Path, (a.candidate, a.profile, a.binding))
out = Path(a.out)
out.mkdir(parents=True, exist_ok=True)
source_hashes = {str(q.resolve()): sha(q) for q in (candidate, profile_path, binding_path)}
profile = json.loads(profile_path.read_text(encoding='utf8'))
binding = json.loads(binding_path.read_text(encoding='utf8'))['materials']['MC_Enamel']
bpy.ops.wm.open_mainfile(filepath=str(candidate.resolve()))
scene = bpy.context.scene
root = bpy.data.objects['MC_JawsRoot']
root.animation_data_clear()
root['OpenAmount'] = 1.
scene.frame_set(42)
bpy.context.view_layer.update()
if scene.camera.data.type != 'ORTHO' or abs(scene.camera.data.ortho_scale - 16.) > 1e-6:
    raise RuntimeError('The canonical width16 camera is required')
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
if not any(d.use for d in prefs.devices):
    raise RuntimeError('No OPTIX device; no unapproved fallback')
scene.cycles.device = 'GPU'
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
scene.view_settings.exposure = .5

# Replace only the enamel image resources, preserving the native shader graph.
enamel = bpy.data.materials['MC_Enamel']
paths = {Path(v).name: Path(v) for v in binding.values() if isinstance(v, str) and Path(v).is_file()}
rebound = []
for node in enamel.node_tree.nodes:
    if node.type != 'TEX_IMAGE' or not node.image:
        continue
    old = Path(bpy.path.abspath(node.image.filepath))
    target = paths.get(old.name)
    if target is None:
        continue
    color_space = node.image.colorspace_settings.name
    image = bpy.data.images.load(str(target.resolve()), check_existing=True)
    image.colorspace_settings.name = color_space
    node.image = image
    rebound.append({'node': node.name, 'old': str(old), 'new': str(target), 'sha256': sha(target)})
bsdf = next(n for n in enamel.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
bc_link = bsdf.inputs['Base Color'].links[0]
if bc_link.from_node.type != 'TEX_IMAGE' or Path(bpy.path.abspath(bc_link.from_node.image.filepath)).resolve() != Path(binding['base_color']).resolve():
    raise RuntimeError('Full32 enamel BaseColor did not bind directly and explicitly')

if a.receiver_profile:
    for obj in list(scene.objects):
        if obj.type == 'LIGHT':
            bpy.data.objects.remove(obj, do_unlink=True)
    for light in profile['lights']:
        data = bpy.data.lights.new(light['name'], 'AREA')
        data.shape = 'RECTANGLE' if light.get('size_y') else 'DISK'
        data.energy, data.color, data.size = light['energy'], light.get('color', [1., 1., 1.]), light['size']
        if light.get('size_y'):
            data.size_y = light['size_y']
        obj = bpy.data.objects.new(light['name'], data)
        scene.collection.objects.link(obj)
        obj.location = light['position']
        obj.rotation_euler = (Vector(light['target']) - obj.location).to_track_quat('-Z', 'Y').to_euler()
        obj.rotation_euler.rotate_axis('Z', math.radians(light.get('roll_degrees', 0.)))
        if 'use_shadow' in light:
            data.use_shadow = light['use_shadow']
        if light.get('receiver_objects') or light.get('receiver_materials'):
            receivers = bpy.data.collections.new(light['name'] + '_ProbeReceivers')
            for receiver in scene.objects:
                if receiver.type != 'MESH':
                    continue
                name_match = any(fnmatch.fnmatchcase(receiver.name, pattern) for pattern in light.get('receiver_objects', []))
                material_match = any(slot.material and slot.material.name in light.get('receiver_materials', []) for slot in receiver.material_slots)
                if name_match or material_match:
                    receivers.objects.link(receiver)
            if not receivers.objects:
                raise RuntimeError('No matched receivers for ' + light['name'])
            obj.light_linking.receiver_collection = receivers
        diffuse, specular = light.get('diffuse_factor', 1.), light.get('specular_factor', 1.)
        if diffuse != specular:
            data.energy = light['energy'] * diffuse
            obj.visible_glossy = False
            obj.visible_diffuse = diffuse > 0
            if specular > 0:
                reflection = obj.copy()
                reflection.data = obj.data.copy()
                reflection.name = light['name'] + '_CyclesGlossy'
                scene.collection.objects.link(reflection)
                reflection.data.energy = light['energy'] * specular
                reflection.visible_diffuse = False
                reflection.visible_glossy = True
                reflection.visible_transmission = False
                reflection.visible_volume_scatter = False
card = bpy.data.objects.get('Menu_Crown_ReflectionCard_CyclesGlossy')
fang = bpy.data.objects.get('Menu_Fang_ReflectionCard_CyclesGlossy')
if card is None or card.type != 'LIGHT' or (not a.receiver_profile and (fang is None or fang.type != 'LIGHT')):
    raise RuntimeError('Expected actual Cycles split reflection emitters missing')
if fang is not None:
    fang.hide_render = True
card.hide_render = False
if not card.visible_glossy or card.visible_diffuse:
    raise RuntimeError('Reflection card must be glossy-only via Cycles object ray visibility')
all_lights_before = [{'name': ob.name, 'energy': ob.data.energy,
                     'glossy': ob.visible_glossy, 'diffuse': ob.visible_diffuse,
                     'hide_render': ob.hide_render} for ob in scene.objects if ob.type == 'LIGHT']
reference_card = next(q for q in profile['lights'] if q['name'] == 'Menu_Crown_ReflectionCard')
reference_radiance = reference_card['energy'] / (math.pi * reference_card['size'] * reference_card['size_y'])
probes = [
    ('A_overhead', dict(position=reference_card['position'], target=reference_card['target'],
                        size=1.2, size_y=8., roll_degrees=26.6)),
    ('B_front_portrait', dict(position=[5., -7., 5.5], target=[0., 0., 1.],
                              size=.7, size_y=8., roll_degrees=0.)),
    ('C_high_portrait', dict(position=[5., -3., 7.5], target=[0., 0., 1.],
                             size=.7, size_y=8., roll_degrees=0.)),
]
if a.portrait_translations is not None:
    probes = []
    for dz in a.portrait_translations:
        probes.append((f'B_portrait_Zplus{dz:g}',
                       dict(position=[5., -7., 5.5 + dz], target=[0., 0., 1. + dz],
                            size=.7, size_y=8., roll_degrees=0.)))
if a.receiver_profile:
    probes = [('ReceiverSplit', None)]
materials = [bpy.data.materials[n] for n in ('MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue')]
original_inputs = []
for mat in materials:
    bs = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    socket = bs.inputs['Base Color']
    original_inputs.append((mat, socket, list(socket.default_value), [(link.from_socket, link.to_socket) for link in socket.links]))
character = [(ob, ob.hide_render) for ob in bpy.data.collections['Menu_ExistingCharacter'].all_objects if ob]
report = {'status': 'BOUNDED_ACTUAL_REFLECTION_CARD_DIAGNOSTIC_NOT_LOOK_ACCEPTANCE',
          'candidate': str(candidate.resolve()), 'source_hashes_before': source_hashes,
          'profile_basis': str(profile_path.resolve()), 'binding': str(binding_path.resolve()),
          'blender': bpy.app.version_string, 'samples': a.samples, 'device': 'OPTIX',
          'resolution': [W, H], 'exposure_stops': .5, 'view_transform': 'Standard',
          'reference_radiance_w_sr_m2': reference_radiance,
          'enamel_image_rebind': rebound, 'lights_before_probes': all_lights_before,
          'invariants': ['No mesh coordinate/topology/UV edits', 'No material constant changes',
                         'Diffuse rig unchanged', 'Receiver-specific cards only when explicitly requested',
                         'No source/candidate .blend saved'],
          'limitations': ['Matched emitter radiance does not equal identical incident irradiance at each crown.',
                         'Black-base renders keep full material specular/coat/roughness, and include changed indirect transport.',
                         'Character hidden only in black-response render; normal render retains existing character.',
                         'Source image has UI/character and requires semantic exclusion for per-crown metrics.'],
          'probes': []}


def checkpoint():
    (out / 'reflection_probe_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')


def capture(folder, label):
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '32'
    scene.render.filepath = str((folder / (label + '.exr')).resolve())
    started = time.perf_counter()
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath, check_existing=False)
    rgba = np.empty(W * H * 4, dtype=np.float32)
    image.pixels.foreach_get(rgba)
    rgba = rgba.reshape(H, W, 4)[::-1].copy()
    np.save(folder / (label + '_linear.npy'), rgba)
    bpy.data.images.remove(image)
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_depth = '8'
    bpy.data.images['Render Result'].save_render(str((folder / (label + '.png')).resolve()), scene=scene)
    return {'seconds': time.perf_counter() - started, 'linear_rgb_max': float(rgba[..., :3].max()),
            'display_png': str((folder / (label + '.png')).resolve()),
            'linear_npy': str((folder / (label + '_linear.npy')).resolve())}


for label, geometry in probes:
    folder = out / label
    folder.mkdir(exist_ok=True)
    logical = copy.deepcopy(reference_card)
    candidate_profile = copy.deepcopy(profile)
    candidate_profile['status'] = 'REFLECTION_CARD_ORIENTATION_PROBE_NOT_ACCEPTED'
    if geometry is not None:
        logical.update(geometry)
        logical['energy'] = reference_radiance * math.pi * geometry['size'] * geometry['size_y']
        card.location = geometry['position']
        card.rotation_euler = (Vector(geometry['target']) - card.location).to_track_quat('-Z', 'Y').to_euler()
        card.rotation_euler.rotate_axis('Z', math.radians(geometry['roll_degrees']))
        card.data.shape = 'RECTANGLE'
        card.data.size, card.data.size_y = geometry['size'], geometry['size_y']
        card.data.energy = logical['energy']
        candidate_profile['lights'] = [logical if q['name'] == logical['name'] else q for q in candidate_profile['lights'] if q['name'] != 'Menu_Fang_ReflectionCard']
        candidate_profile['notes'] = 'Single Crown-card isolation at matched emitter radiance. Wide Fang card disabled. Other diffuse lights unchanged.'
    (folder / 'look_profile.json').write_text(json.dumps(candidate_profile, indent=2), encoding='utf8')
    bpy.context.view_layer.update()
    print('REFLECTION_PROBE_BEGIN', label, logical['energy'], flush=True)
    record = {'name': label, 'logical_card': logical, 'world_long_axis': list(card.rotation_euler.to_matrix() @ Vector((0., 1., 0.)))}
    record['normal'] = capture(folder, 'normal')
    for mat, socket, value, links in original_inputs:
        for link in list(socket.links):
            mat.node_tree.links.remove(link)
        socket.default_value = (0., 0., 0., 1.)
    for obj, original_hide in character:
        obj.hide_render = True
    record['black'] = capture(folder, 'black')
    for mat, socket, value, links in original_inputs:
        socket.default_value = value
        for from_socket, to_socket in links:
            mat.node_tree.links.new(from_socket, to_socket)
    for obj, original_hide in character:
        obj.hide_render = original_hide
    report['probes'].append(record)
    checkpoint()
    print('REFLECTION_PROBE_DONE', label, flush=True)

report['source_hashes_after'] = {q: sha(q) for q in source_hashes}
report['source_files_unchanged'] = report['source_hashes_after'] == source_hashes
if not report['source_files_unchanged']:
    raise RuntimeError('Input source changed during diagnostic; inspect shared ownership')
checkpoint()
print('REFLECTION_PROBES_READY', len(probes), str(out), flush=True)
