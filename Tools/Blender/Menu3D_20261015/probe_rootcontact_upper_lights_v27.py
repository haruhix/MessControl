"""Exactly two upper6-only reflection profiles on the clean v26 candidate.

Native materials/images are preserved. v05 diffuse rig and the original two
reflection cards on the other26 crowns+gingiva stay unchanged. Only the six
upper semantic receivers use the disjoint A/B portraits. No geometry edits,
source saves, Painter or mesh-map bake. Four actual renders total.
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


def material_state():
    result = {}
    for name in ('MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue'):
        mat = bpy.data.materials[name]
        bs = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        values = {}
        for socket in bs.inputs:
            if hasattr(socket, 'default_value'):
                value = socket.default_value
                values[socket.name] = value if isinstance(value, (int, float, str, bool)) else list(value)
        result[name] = {'principled_defaults': values,
                        'links': sorted((l.from_node.name, l.from_socket.identifier, l.to_node.name, l.to_socket.identifier) for l in mat.node_tree.links),
                        'images': sorted((n.name, bpy.path.abspath(n.image.filepath), n.image.colorspace_settings.name)
                                         for n in mat.node_tree.nodes if n.type == 'TEX_IMAGE' and n.image)}
    return result


def build_profile(profile):
    for obj in list(bpy.context.scene.objects):
        if obj.type == 'LIGHT':
            bpy.data.objects.remove(obj, do_unlink=True)
    receivers_audit = {}
    for light in profile['lights']:
        data = bpy.data.lights.new(light['name'], 'AREA')
        data.energy, data.color, data.size = light['energy'], light.get('color', [1., 1., 1.]), light['size']
        data.shape = 'RECTANGLE' if light.get('size_y') else 'DISK'
        if light.get('size_y'):
            data.size_y = light['size_y']
        if 'use_shadow' in light:
            data.use_shadow = light['use_shadow']
        obj = bpy.data.objects.new(light['name'], data)
        bpy.context.scene.collection.objects.link(obj)
        obj.location = light['position']
        obj.rotation_euler = (Vector(light['target']) - obj.location).to_track_quat('-Z', 'Y').to_euler()
        obj.rotation_euler.rotate_axis('Z', math.radians(light.get('roll_degrees', 0.)))
        if light.get('receiver_objects') or light.get('receiver_materials'):
            collection = bpy.data.collections.new(light['name'] + '_ProbeReceivers')
            for receiver in bpy.context.scene.objects:
                if receiver.type != 'MESH':
                    continue
                match = any(fnmatch.fnmatchcase(receiver.name, pattern) for pattern in light.get('receiver_objects', []))
                match |= any(slot.material and slot.material.name in light.get('receiver_materials', []) for slot in receiver.material_slots)
                if match:
                    collection.objects.link(receiver)
            if not collection.objects:
                raise RuntimeError('No receiver: ' + light['name'])
            obj.light_linking.receiver_collection = collection
            receivers_audit[light['name']] = sorted(o.name for o in collection.objects)
        diffuse, specular = light.get('diffuse_factor', 1.), light.get('specular_factor', 1.)
        # Official Cycles5.1 ignores Light.diffuse/specular_factor. Use object
        # ray visibility and independent emitter energy, matching root assembly.
        if diffuse != specular:
            data.energy = light['energy'] * diffuse
            obj.visible_glossy = False
            obj.visible_diffuse = diffuse > 0
            if specular > 0:
                glossy = obj.copy()
                glossy.data = obj.data.copy()
                glossy.name = light['name'] + '_CyclesGlossy'
                bpy.context.scene.collection.objects.link(glossy)
                glossy.data.energy = light['energy'] * specular
                glossy.visible_glossy, glossy.visible_diffuse = True, False
                glossy.visible_transmission = False
                glossy.visible_volume_scatter = False
    return receivers_audit


p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--out', required=True)
p.add_argument('--samples', type=int, default=24)
a = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
if not bpy.app.background:
    raise RuntimeError('Isolated background only; no live GUI')
candidate, out = Path(a.candidate).resolve(), Path(a.out).resolve()
out.mkdir(parents=True, exist_ok=True)
candidate_sha = sha(candidate)
root_path = Path(r'E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
profile_path = root_path / 'Assembly/Look/source_balanced_v05.json'
base_profile = json.loads(profile_path.read_text())
parts = json.loads((root_path / 'Jaws/ContourHero_v14/Full32/uv_manifest.json').read_text())['parts']
upper6 = [part['object'] for part in parts[:6]]
middle4 = [name for name in upper6 if 'Fang' not in name]
fang2 = [name for name in upper6 if 'Fang' in name]
others = [part['object'] for part in parts if part['object'] not in upper6]
if len(others) != 26 or len(middle4) != 4 or len(fang2) != 2:
    raise RuntimeError('Unexpected fixed source semantic inventory')
bpy.ops.wm.open_mainfile(filepath=str(candidate))
scene = bpy.context.scene
root = bpy.data.objects['MC_JawsRoot']
root.animation_data_clear()
root['OpenAmount'] = 1.
scene.frame_set(42)
bpy.context.view_layer.update()
native_materials = material_state()
material_inputs = []
for name in native_materials:
    mat = bpy.data.materials[name]
    bs = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    socket = bs.inputs['Base Color']
    material_inputs.append((mat, socket, list(socket.default_value), [(l.from_socket, l.to_socket) for l in socket.links]))
character = [(ob, ob.hide_render) for ob in bpy.data.collections['Menu_ExistingCharacter'].all_objects if ob]
scene.render.resolution_x, scene.render.resolution_y = 963, 410
scene.render.resolution_percentage = 100
scene.render.threads_mode, scene.render.threads = 'FIXED', 2
scene.render.engine = 'CYCLES'
scene.cycles.samples = a.samples
scene.cycles.seed = 0
scene.cycles.use_denoising = True
scene.render.film_transparent = False
if scene.camera.data.type != 'ORTHO' or abs(scene.camera.data.ortho_scale - 16.) > 1e-7:
    raise RuntimeError('Canonical width16 camera required')
if scene.view_settings.view_transform != 'Standard' or abs(scene.view_settings.exposure - .5) > 1e-7:
    raise RuntimeError('Native Standard+.5 view required')
prefs = bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type = 'OPTIX'
prefs.get_devices()
for device in prefs.devices:
    device.use = device.type == 'OPTIX'
if not any(device.use for device in prefs.devices):
    raise RuntimeError('No OPTIX device; no unapproved fallback')
scene.cycles.device = 'GPU'


def portrait(name, position, target, receivers):
    return {'name': name, 'position': position, 'target': target, 'size': .7, 'size_y': 8.,
            'energy': 466.6666666666667, 'diffuse_factor': 0, 'specular_factor': 1,
            'use_shadow': False, 'roll_degrees': 0, 'receiver_objects': receivers}


profiles = []
for label, cards in [
    ('A_Bplus2_UpperSix', [portrait('Menu_UpperSix_ReflectionCard', [5, -7, 7.5], [0, 0, 3], upper6)]),
    ('B_SplitPortrait', [portrait('Menu_MiddleFour_ReflectionCard', [4, -3, 10], [-1, 4, 5.5], middle4),
                         portrait('Menu_TwoFangs_ReflectionCard', [3, -6, 10], [-2, 1, 5.5], fang2)]),
]:
    profile = copy.deepcopy(base_profile)
    for light in profile['lights']:
        if light['name'] in ('Menu_Crown_ReflectionCard', 'Menu_Fang_ReflectionCard'):
            light['receiver_objects'], light['receiver_materials'] = others, ['MC_Gum']
    profile['lights'] += cards
    profile['status'] = 'V26_NATIVE_CLEAN_MATERIAL_UPPER6_REFLECTION_PROBE_NOT_ACCEPTED'
    profile['notes'] = 'Only six upper crown glossy receivers changed; original two v05 reflections on other26crowns+Gum and full diffuse rig retained.'
    profiles.append((label, profile))
report = {'status': 'EXACT_TWO_PROFILES_FOUR_ACTUAL_CAPTURES_NOT_FINAL_LOOK',
          'candidate': str(candidate), 'candidate_sha256_before': candidate_sha,
          'native_materials_before': native_materials, 'old_maps_rebound': False,
          'geometry_edited': False, 'constants_changed': False,
          'samples': a.samples, 'blender': bpy.app.version_string,
          'basis_profile': str(profile_path), 'basis_profile_sha256': sha(profile_path),
          'view': 'Standard,None,exposure+.5', 'probes': [],
          'limitations': ['Black-base response changes indirect transport, so it is diagnostic specular evidence, not unique physical decomposition.',
                         'Compare centroid and sliver area/aspect jointly; no centroid-only acceptance.',
                         'No SSS/microdetail changes or new bakes are included.']}


def capture(folder, label):
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '32'
    scene.render.filepath = str(folder / (label + '.exr'))
    started = time.perf_counter()
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath, check_existing=False)
    rgba = np.empty(963 * 410 * 4, dtype=np.float32)
    image.pixels.foreach_get(rgba)
    array = rgba.reshape(410, 963, 4)[::-1].copy()
    np.save(folder / (label + '_linear.npy'), array)
    bpy.data.images.remove(image)
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_depth = '8'
    bpy.data.images['Render Result'].save_render(str(folder / (label + '.png')), scene=scene)
    return {'seconds': time.perf_counter() - started, 'unclipped_linear_rgb_max': float(array[..., :3].max())}


for label, profile in profiles:
    folder = out / label
    folder.mkdir(exist_ok=True)
    (folder / 'look_profile.json').write_text(json.dumps(profile, indent=2), encoding='utf8')
    receiver_audit = build_profile(profile)
    for name in upper6:
        matched = [l['name'] for l in profile['lights'] if l.get('specular_factor', 1) > 0 and name in receiver_audit.get(l['name'], [])]
        if len(matched) != 1:
            raise RuntimeError('Upper crown double/missing reflection: ' + name + '/' + str(matched))
    print('V27_PROFILE_BEGIN', label, flush=True)
    entry = {'name': label, 'receiver_audit': receiver_audit, 'normal': capture(folder, 'normal')}
    for mat, socket, default, links in material_inputs:
        for link in list(socket.links):
            mat.node_tree.links.remove(link)
        socket.default_value = (0., 0., 0., 1.)
    for obj, original_hide in character:
        obj.hide_render = True
    entry['black'] = capture(folder, 'black')
    for mat, socket, default, links in material_inputs:
        socket.default_value = default
        for source_socket, target_socket in links:
            mat.node_tree.links.new(source_socket, target_socket)
    for obj, original_hide in character:
        obj.hide_render = original_hide
    if material_state() != native_materials:
        raise RuntimeError('Native candidate material graph/image/default changed')
    report['probes'].append(entry)
    (out / 'v27_profile_probe_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')
report['candidate_sha256_after'] = sha(candidate)
report['candidate_unchanged'] = report['candidate_sha256_after'] == candidate_sha
report['native_materials_preserved'] = material_state() == native_materials
if not report['candidate_unchanged']:
    raise RuntimeError('Input candidate changed')
(out / 'v27_profile_probe_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('V27_TWO_PROFILES_FOUR_RENDERS_READY', str(out), flush=True)
