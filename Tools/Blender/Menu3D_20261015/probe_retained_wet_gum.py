"""ONE whole Open Gum reflection/retained-relief probe. Lead DCC grant required."""
import argparse
import fnmatch
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('finish', HERE / 'probe_whole_material_finish_v29.py')
finish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(finish)


def build_profile(profile):
    for obj in list(bpy.context.scene.objects):
        if obj.type == 'LIGHT':
            bpy.data.objects.remove(obj, do_unlink=True)
    receipt = {}
    for light in profile['lights']:
        data = bpy.data.lights.new(light['name'], 'AREA')
        data.energy, data.color, data.size = light['energy'], light.get('color', [1., 1., 1.]), light['size']
        data.shape = 'RECTANGLE'
        data.size_y = light.get('size_y', light['size'])
        if 'use_shadow' in light:
            data.use_shadow = light['use_shadow']
        obj = bpy.data.objects.new(light['name'], data)
        bpy.context.scene.collection.objects.link(obj)
        obj.location = light['position']
        obj.rotation_euler = (Vector(light['target'])-obj.location).to_track_quat('-Z', 'Y').to_euler()
        obj.rotation_euler.rotate_axis('Z', math.radians(light.get('roll_degrees', 0.)))
        if light.get('receiver_objects') or light.get('receiver_materials'):
            receivers = bpy.data.collections.new(light['name']+'_ExactProbeReceivers')
            for receiver in bpy.context.scene.objects:
                if receiver.type != 'MESH':
                    continue
                match = any(fnmatch.fnmatchcase(receiver.name, pattern) for pattern in light.get('receiver_objects', []))
                match |= any(slot.material and slot.material.name in light.get('receiver_materials', []) for slot in receiver.material_slots)
                if match:
                    receivers.objects.link(receiver)
            if not receivers.objects:
                raise RuntimeError('No receiver: '+light['name'])
            obj.light_linking.receiver_collection = receivers
            receipt[light['name']] = sorted(receiver.name for receiver in receivers.objects)
        diffuse, specular = light.get('diffuse_factor', 1.), light.get('specular_factor', 1.)
        if diffuse != specular:
            data.energy = light['energy']*diffuse
            obj.visible_glossy = False
            obj.visible_diffuse = diffuse > 0
            if specular > 0:
                glossy = obj.copy()
                glossy.data = obj.data.copy()
                glossy.name = light['name']+'_CyclesGlossy'
                bpy.context.scene.collection.objects.link(glossy)
                glossy.data.energy = light['energy']*specular
                glossy.visible_glossy, glossy.visible_diffuse = True, False
                glossy.visible_transmission = glossy.visible_volume_scatter = False
    return receipt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--binding', required=True)
    parser.add_argument('--profile', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--samples', type=int, default=64)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if not bpy.app.background:
        raise RuntimeError('Isolated background only; no unrelated live Blender mutation')
    source, binding_path, profile_path, out = (Path(value).resolve() for value in (args.candidate, args.binding, args.profile, args.out))
    root_path = Path(r'E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
    if not out.is_relative_to(root_path / 'Assembly/Review'):
        raise RuntimeError('Task-owned Review output required')
    out.mkdir(parents=True, exist_ok=True)
    if (out / 'DAY03_open_RetainedWetGum.png').exists():
        raise RuntimeError('Choose a fresh actual review directory')
    source_sha = finish.sha(source)
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    root = bpy.data.objects['MC_JawsRoot']
    root.animation_data_clear()
    root['OpenAmount'] = 1.
    scene.frame_set(42)
    bpy.context.view_layer.update()
    before_color = finish.color_state()
    binding = json.loads(binding_path.read_text())
    assert binding['changed_materials'] == ['MC_Gum'], 'Keep all native enamel material inputs fixed'
    material_receipt = finish.apply_finish(binding)
    assert finish.color_state() == before_color
    profile = json.loads(profile_path.read_text())
    receiver_receipt = build_profile(profile)
    gum_lights = [light for light in profile['lights'] if light['name'].startswith(('Menu_UpperGum_', 'Menu_LowerGum_'))]
    assert gum_lights, 'Explicit physical Gum reflection field is required'
    for light in gum_lights:
        jaw = 'Upper' if light['name'].startswith('Menu_UpperGum_') else 'Lower'
        assert receiver_receipt[light['name']] == [f'MC_DAY03_{jaw}_Gum']
    scene.render.engine = 'CYCLES'
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 963, 410, 100
    scene.render.threads_mode, scene.render.threads = 'FIXED', 2
    scene.cycles.samples, scene.cycles.seed, scene.cycles.use_denoising = args.samples, 0, True
    scene.camera.data.type, scene.camera.data.ortho_scale = 'ORTHO', 16.
    scene.camera.location, scene.camera.rotation_euler = (0., -30., 0.), (math.pi/2, 0., 0.)
    assert scene.view_settings.view_transform == 'Standard' and abs(scene.view_settings.exposure-.5) < 1e-7
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type = 'OPTIX'
    prefs.get_devices()
    for device in prefs.devices:
        device.use = device.type == 'OPTIX'
    assert any(device.use for device in prefs.devices), 'No unapproved device fallback'
    scene.cycles.device = 'GPU'
    scene.render.image_settings.file_format, scene.render.image_settings.color_mode = 'PNG', 'RGBA'
    scene.render.image_settings.color_depth = '8'
    scene.render.filepath = str(out / 'DAY03_open_RetainedWetGum.png')
    bpy.ops.render.render(write_still=True)
    assert finish.color_state() == before_color
    assert finish.sha(source) == source_sha
    report = {'status': 'ONE_ACTUAL_WET_GUM_PHYSICAL_CANDIDATE_NOT_ACCEPTED', 'source': str(source),
        'source_sha256': source_sha, 'source_unchanged': True, 'binding': str(binding_path),
        'profile': str(profile_path), 'material': material_receipt, 'exact_receivers': receiver_receipt,
        'native_BaseColor_nodes_images_unchanged': before_color, 'enamel_materials_edited': False,
        'geometry_edited': False, 'new_bake_or_Painter': False, 'actual': scene.render.filepath,
        'limits': 'Open-only, inferred relief and lookdev reflection cards; actual whole-view source review required.'}
    (out / 'retained_wet_actual_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print('ONE_RETAINED_WET_GUM_ACTUAL_COMPLETE', scene.render.filepath, flush=True)


if __name__ == '__main__':
    main()
