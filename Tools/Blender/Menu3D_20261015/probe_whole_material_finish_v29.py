"""One isolated whole Open material render; run only with lead DCC grant.

No source save, mesh/depth/light edit, Pigment replacement, Painter or bake.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def input_of(node, name):
    return next(socket for socket in node.inputs if socket.name == name)


def clear_input(mat, socket):
    for link in list(socket.links):
        mat.node_tree.links.remove(link)


def set_input(mat, bs, name, value):
    socket = input_of(bs, name)
    clear_input(mat, socket)
    socket.default_value = value


def image_node(mat, path):
    image = bpy.data.images.load(str(path), check_existing=True)
    image.colorspace_settings.name = 'Non-Color'
    node = mat.node_tree.nodes.new('ShaderNodeTexImage')
    node.image, node.label = image, 'v29 PHYSICAL ' + Path(path).name
    uv = mat.node_tree.nodes.new('ShaderNodeUVMap')
    uv.uv_map = 'UV0'
    mat.node_tree.links.new(uv.outputs['UV'], node.inputs['Vector'])
    return node


def color_state():
    result = {}
    for name in ('MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue'):
        mat = bpy.data.materials[name]
        bs = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        socket = input_of(bs, 'Base Color')
        result[name] = {'default': list(socket.default_value),
            'links': [(link.from_node.name, link.from_socket.identifier) for link in socket.links],
            'image_nodes': sorted((n.name, bpy.path.abspath(n.image.filepath), sha(bpy.path.abspath(n.image.filepath))) for n in mat.node_tree.nodes if n.type == 'TEX_IMAGE' and n.image and n.image.colorspace_settings.name == 'sRGB')}
    return result


def apply_finish(binding):
    receipt = {}
    for name in binding.get('changed_materials', ('MC_Enamel', 'MC_Gum')):
        data = binding['materials'][name]
        mat = bpy.data.materials[name]
        bs = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        for key, socket_name in [('roughness', 'Roughness'), ('coat_weight', 'Coat Weight'), ('coat_roughness', 'Coat Roughness')]:
            node = image_node(mat, data[key])
            socket = input_of(bs, socket_name)
            clear_input(mat, socket)
            mat.node_tree.links.new(node.outputs['Color'], socket)
        constants = data['shader_constants']
        for key, socket_name in [('ior', 'IOR'), ('coat_ior', 'Coat IOR'), ('subsurface_scale', 'Subsurface Scale'), ('subsurface_radius', 'Subsurface Radius'), ('transmission_weight', 'Transmission Weight')]:
            if key in constants:
                set_input(mat, bs, socket_name, constants[key])
        tex = image_node(mat, data['subsurface'])
        mapped = mat.node_tree.nodes.new('ShaderNodeMapRange')
        mapped.clamp = True
        mapped.label = 'v29 anatomical SSS; no pigment change'
        mapped.inputs['From Min'].default_value = 0
        mapped.inputs['From Max'].default_value = 1
        mapped.inputs['To Min'].default_value, mapped.inputs['To Max'].default_value = constants['subsurface_weight_range']
        socket = input_of(bs, 'Subsurface Weight')
        clear_input(mat, socket)
        mat.node_tree.links.new(tex.outputs['Color'], mapped.inputs['Value'])
        mat.node_tree.links.new(mapped.outputs['Result'], socket)
        if constants.get('normal_strength') == 0:
            clear_input(mat, input_of(bs, 'Normal'))
        if data.get('physical_microdetail'):
            micro = data['physical_microdetail']
            tex = image_node(mat, micro['height_uv0'])
            bump = mat.node_tree.nodes.new('ShaderNodeBump')
            bump.label = 'INFERRED wet relief, world units; not a geometry bake'
            bump.inputs['Distance'].default_value = micro['bump_distance_world']
            bump.inputs['Strength'].default_value = micro['bump_strength']
            mat.node_tree.links.new(tex.outputs['Color'], bump.inputs['Height'])
            for socket_name in ('Normal', 'Coat Normal'):
                socket = input_of(bs, socket_name)
                clear_input(mat, socket)
                mat.node_tree.links.new(bump.outputs['Normal'], socket)
        receipt[name] = {'constants_applied': constants,
            'images': {key: {'path': data[key], 'sha256': sha(data[key])} for key in ('roughness', 'coat_weight', 'coat_roughness', 'subsurface')},
            'physical_microdetail': data.get('physical_microdetail')}
    return receipt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--binding', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--samples', type=int, default=48)
    options = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if not bpy.app.background:
        raise RuntimeError('Background task-owned scene only; unrelated live Blender is forbidden')
    candidate, binding_path, out = (Path(p).resolve() for p in (options.candidate, options.binding, options.out))
    root_path = Path(r'E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
    if not out.is_relative_to(root_path / 'Assembly/Review'):
        raise RuntimeError('Task-owned Review output required')
    out.mkdir(parents=True, exist_ok=True)
    if (out / 'DAY03_open_PhysicalFinish.png').exists():
        raise RuntimeError('Choose a fresh review folder; do not replace actual evidence')
    source_sha = sha(candidate)
    binding = json.loads(binding_path.read_text())
    bpy.ops.wm.open_mainfile(filepath=str(candidate))
    scene = bpy.context.scene
    root = bpy.data.objects['MC_JawsRoot']
    root.animation_data_clear()
    root['OpenAmount'] = 1.
    scene.frame_set(42)
    bpy.context.view_layer.update()
    before = color_state()
    light_before = [(o.name, list(o.location), list(o.rotation_euler), o.data.energy) for o in scene.objects if o.type == 'LIGHT']
    receipt = apply_finish(binding)
    assert color_state() == before, 'Source pigment nodes/images must remain identical'
    scene.render.resolution_x, scene.render.resolution_y = 963, 410
    scene.render.resolution_percentage = 100
    scene.render.threads_mode, scene.render.threads = 'FIXED', 2
    scene.render.engine = 'CYCLES'
    scene.cycles.samples, scene.cycles.seed = options.samples, 0
    scene.cycles.use_denoising = True
    scene.camera.data.type, scene.camera.data.ortho_scale = 'ORTHO', 16.
    scene.camera.location = (0., -30., 0.)
    scene.camera.rotation_euler = (math.pi/2, 0., 0.)
    if scene.view_settings.view_transform != 'Standard' or abs(scene.view_settings.exposure-.5) > 1e-7:
        raise RuntimeError('Exact native Standard+.5 camera management required')
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type = 'OPTIX'
    prefs.get_devices()
    for device in prefs.devices:
        device.use = device.type == 'OPTIX'
    if not any(device.use for device in prefs.devices):
        raise RuntimeError('No OPTIX; no unapproved CPU fallback')
    scene.cycles.device = 'GPU'
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '8'
    scene.render.filepath = str(out / 'DAY03_open_PhysicalFinish.png')
    bpy.ops.render.render(write_still=True)
    assert light_before == [(o.name, list(o.location), list(o.rotation_euler), o.data.energy) for o in scene.objects if o.type == 'LIGHT']
    assert color_state() == before
    assert source_sha == sha(candidate)
    report = {'status': 'ONE_ACTUAL_OPEN_PHYSICAL_MATERIAL_CANDIDATE_NOT_ACCEPTED',
        'candidate': str(candidate), 'source_sha256': source_sha, 'source_unchanged': True,
        'binding': str(binding_path), 'binding_sha256': sha(binding_path), 'actual_applied': receipt,
        'BaseColor_nodes_images_unchanged': before, 'geometry_edited': False, 'lights_edited': False,
        'frame': 42, 'samples': options.samples, 'new_meshmap_bake': False, 'Painter_mutated': False,
        'native_screenshot': scene.render.filepath,
        'limit': 'One Open view; no Closed/SSS optical truth/runtime/material acceptance. Actual visual compare required.'}
    (out / 'physical_finish_actual_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print('ONE_PHYSICAL_FINISH_ACTUAL_COMPLETE', scene.render.filepath, flush=True)


if __name__ == '__main__':
    main()
