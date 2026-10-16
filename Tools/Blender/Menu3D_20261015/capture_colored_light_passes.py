"""Actual colored Cycles passes, with fixed scene transport for look fitting.

Blender5.1 compositor API. Isolated process; never saves the loaded .blend.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--out', required=True)
p.add_argument('--samples', type=int, default=96)
p.add_argument('--openness', type=float, default=1.)
p.add_argument('--width', type=float, default=16.)
p.add_argument('--height', type=int, default=410)
opt = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
candidate = Path(opt.candidate).resolve()
out = Path(opt.out).resolve()
out.mkdir(parents=True, exist_ok=True)
before = hashlib.sha256(candidate.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(candidate))
scene = bpy.context.scene
root = bpy.data.objects['MC_JawsRoot']
root.animation_data_clear()
root['OpenAmount'] = opt.openness
root.update_tag()
scene.frame_set(42)
bpy.context.view_layer.update()
for obj in bpy.data.collections['Menu_ExistingCharacter'].all_objects:
    if obj:
        obj.hide_render = True
scene.render.resolution_x = 963
scene.render.resolution_y = opt.height
scene.camera.data.ortho_scale = opt.width
if opt.openness < .02:
    bpy.data.collections['MenuInterior_Geometry'].hide_render = True
scene.render.resolution_percentage = 100
scene.cycles.samples = opt.samples
scene.render.film_transparent = False
scene.render.use_compositing = True
scene.render.image_settings.file_format = 'OPEN_EXR'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '32'
prefs = bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type = 'OPTIX'
prefs.get_devices()
for device in prefs.devices:
    device.use = device.type == 'OPTIX'
scene.cycles.device = 'GPU'
layer = bpy.context.view_layer
for name in ['diffuse_color', 'diffuse_direct', 'diffuse_indirect',
             'glossy_color', 'glossy_direct', 'glossy_indirect',
             'transmission_color', 'transmission_direct', 'transmission_indirect',
             'emit', 'environment']:
    setattr(layer, 'use_pass_' + name, True)
group = bpy.data.node_groups.new('MC_ActualColoredPassEvidence', 'CompositorNodeTree')
scene.compositing_node_group = group
group.interface.new_socket(name='Image', in_out='OUTPUT', socket_type='NodeSocketColor')
render = group.nodes.new('CompositorNodeRLayers')
render.scene = scene
render.layer = layer.name
output = group.nodes.new('NodeGroupOutput')
group.links.new(render.outputs['Image'], output.inputs['Image'])
passes = ['Image', 'DiffCol', 'DiffDir', 'DiffInd', 'GlossCol', 'GlossDir', 'GlossInd',
          'TransCol', 'TransDir', 'TransInd', 'Emit', 'Env']
socket_names = dict(zip(passes, ['Image', 'Diffuse Color', 'Diffuse Direct', 'Diffuse Indirect',
                               'Glossy Color', 'Glossy Direct', 'Glossy Indirect',
                               'Transmission Color', 'Transmission Direct', 'Transmission Indirect',
                               'Emission', 'Environment']))
print('ACTUAL_COLORED_OUTPUTS', [s.name for s in render.outputs], flush=True)
for label in passes:
    socket = socket_names[label]
    if socket not in render.outputs:
        raise RuntimeError('Requested actual pass unavailable: ' + label)
    file = group.nodes.new('CompositorNodeOutputFile')
    file.directory = str(out / label)
    file.file_name = label
    file.format.media_type = 'IMAGE'
    file.format.file_format = 'OPEN_EXR'
    file.format.color_mode = 'RGBA'
    file.format.color_depth = '32'
    file.save_as_render = False
    file.file_output_items.new('RGBA', label)
    group.links.new(render.outputs[socket], file.inputs[label])
scene.render.filepath = str(out / 'colored_baseline.exr')
bpy.ops.render.render(write_still=True)
record = {}
for label in passes:
    files = list((out / label).glob('*.exr'))
    if len(files) != 1:
        raise RuntimeError(f'Expected one actual {label} EXR, got {files}')
    image = bpy.data.images.load(str(files[0]), check_existing=False)
    W, H = image.size
    rgba = np.empty(W * H * 4, np.float32)
    image.pixels.foreach_get(rgba)
    pixels = rgba.reshape(H, W, 4)[::-1].copy()
    np.save(out / (label + '_linear.npy'), pixels)
    bpy.data.images.remove(image)
    record[label] = {'file': str(files[0]), 'size': [W, H],
                     'min_rgb': pixels[:, :, :3].min(axis=(0, 1)).tolist(),
                     'max_rgb': pixels[:, :, :3].max(axis=(0, 1)).tolist()}
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_depth = '8'
bpy.data.images['Render Result'].save_render(str(out / 'colored_baseline.png'), scene=scene)
manifest = {
    'status': 'ACTUAL_COLORED_TRANSPORT_PASSES_NOT_LOOK_ACCEPTANCE',
    'candidate': str(candidate), 'candidate_sha256': before,
    'candidate_unchanged': before == hashlib.sha256(candidate.read_bytes()).hexdigest(),
    'blender': bpy.app.version_string, 'passes': record,
    'view_transform': scene.view_settings.view_transform,
    'exposure_stops': scene.view_settings.exposure,
    'OpenAmount':opt.openness,'camera_width':opt.width,'image_height':opt.height,
    'character_hidden_for_calibration_only': True,
    'source_or_candidate_saved': False,
    'note': 'Verify pass reconstruction against Image before fitting; indirect transport changes when albedo changes.',
}
(out / 'colored_pass_manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
print('COLORED_PASSES_READY', out, flush=True)
