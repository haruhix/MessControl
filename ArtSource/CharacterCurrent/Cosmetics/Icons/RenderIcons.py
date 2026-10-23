"""Reproduce the two Work Cap icons without changing the verified source shape.

Use isolated Blender --factory-startup --threads 1, not the live artist scene.
Only the cap is appended. Approved glasses source/FBX/icon are never rewritten.
"""
import argparse
import hashlib
import json
import os
import sys
from array import array
from pathlib import Path
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'ArtSource/CharacterCurrent/Cosmetics/CharacterCosmetics.blend')
parser.add_argument('--out', type=Path, default=Path('E:/CodexWork/HubWardrobePolish_20261023/CapIconRepair'))
parser.add_argument('--samples', type=int, default=64)
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
assert args.out.drive.upper() == 'E:', 'Generated renders belong on E:.'
assert args.source.is_file()
args.out.mkdir(parents=True, exist_ok=True)
temp = Path(os.environ.get('TEMP', 'E:/CodexTemp/HubWardrobePolish_20261023'))
assert temp.drive.upper() == 'E:'
temp.mkdir(parents=True, exist_ok=True)
bpy.context.preferences.filepaths.temporary_directory = str(temp)
scene = bpy.context.scene
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
with bpy.data.libraries.load(str(args.source), link=False) as (available, loaded):
    assert 'SM_Cosmetic_WorkCap' in available.objects
    loaded.objects = ['SM_Cosmetic_WorkCap']
cap = loaded.objects[0]
assert cap and cap.type == 'MESH' and len(cap.data.vertices) == 1042
assert len(cap.data.materials) == 2 and not cap.modifiers
scene.collection.objects.link(cap)
cap.hide_render = False
cap.hide_viewport = False
cap.hide_set(False)
bpy.context.view_layer.update()
source_transform = [list(row) for row in cap.matrix_world]
source_scale = list(cap.matrix_world.to_scale())
assert max(source_scale) - min(source_scale) < 1.e-7, 'Nonuniform object scale distorts the source.'
source_vertices = [[*vertex.co] for vertex in cap.data.vertices]
source_vertex_hash = hashlib.sha256(json.dumps(source_vertices).encode()).hexdigest()
world_points = [cap.matrix_world @ vertex.co for vertex in cap.data.vertices]
bounds = [[min(point[axis] for point in world_points) for axis in range(3)],
          [max(point[axis] for point in world_points) for axis in range(3)]]
center = (Vector(bounds[0]) + Vector(bounds[1])) * .5
# Translation alone: retain source orientation, proportions, topology and smoothing.
cap.location -= center
bpy.context.view_layer.update()
scene.use_nodes = False
scene.render.use_compositing = False
scene.render.use_sequencer = False
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = args.samples
scene.cycles.use_denoising = True
scene.cycles.use_adaptive_sampling = True
scene.cycles.adaptive_threshold = .025
scene.cycles.max_bounces = 6
scene.render.threads_mode = 'FIXED'
scene.render.threads = 1
scene.render.resolution_x = scene.render.resolution_y = 512
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.image_settings.color_depth = '8'
scene.view_settings.view_transform = 'AgX'
scene.view_settings.look = 'AgX - Medium High Contrast'
scene.view_settings.exposure = 0
scene.view_settings.gamma = 1
world = bpy.data.worlds.new('CapIcon_NeutralSoftWorld')
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (.28, .28, .28, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = .4
scene.world = world
for name, position, power, size in [
        ('Key', (1.5, -2.0, 3.0), 180, 2.5),
        ('Fill', (-2.0, -1.0, 1.0), 70, 3.0),
        ('Rim', (0.5, 2.0, 2.5), 140, 2.0)]:
    data = bpy.data.lights.new('CapIcon_' + name, 'AREA')
    data.energy = power
    data.shape = 'DISK'
    data.size = size
    light = bpy.data.objects.new(data.name, data)
    scene.collection.objects.link(light)
    light.location = position
    light.rotation_euler = (-light.location).to_track_quat('-Z', 'Y').to_euler()
camera_data = bpy.data.cameras.new('CapIcon_ProductCamera')
camera = bpy.data.objects.new(camera_data.name, camera_data)
scene.collection.objects.link(camera)
scene.camera = camera
camera_data.type = 'ORTHO'
# Source-like three-quarter view: the opening and backwards brim remain visible.
camera.location = (2.6, -1.3, 1.3)
camera.rotation_euler = (-camera.location).to_track_quat('-Z', 'Y').to_euler()
bpy.context.view_layer.update()
view_points = [camera.matrix_world.inverted() @ (cap.matrix_world @ vertex.co) for vertex in cap.data.vertices]
projected_size = [max(point[axis] for point in view_points) - min(point[axis] for point in view_points)
                  for axis in (0, 1)]
camera_data.ortho_scale = max(projected_size) / .82
view_center = Vector(((min(p.x for p in view_points) + max(p.x for p in view_points)) * .5,
                      (min(p.y for p in view_points) + max(p.y for p in view_points)) * .5, 0))
camera.location += camera.matrix_world.to_quaternion() @ view_center

def material(name, color, roughness, metallic):
    result = bpy.data.materials.new(name)
    result.use_nodes = True
    result.diffuse_color = (*color, 1)
    bsdf = result.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color, 1)
    bsdf.inputs['Roughness'].default_value = roughness
    bsdf.inputs['Metallic'].default_value = metallic
    return result

def alpha_bounds(path):
    image = bpy.data.images.load(str(path), check_existing=False)
    width, height = image.size
    pixels = array('f', [0]) * (width * height * 4)
    image.pixels.foreach_get(pixels)
    left, top, right, bottom = width, height, -1, -1
    for index in range(3, len(pixels), 4):
        if pixels[index] > 0:
            pixel = (index - 3) // 4
            x, y = pixel % width, height - 1 - pixel // width
            left, top = min(left, x), min(top, y)
            right, bottom = max(right, x), max(bottom, y)
    bpy.data.images.remove(image)
    assert 0 < left <= right < width - 1 and 0 < top <= bottom < height - 1, 'Icon clipped at image border.'
    bounds = [left, top, right + 1, bottom + 1]
    padded = [max(0, left - 8), max(0, top - 8), min(width, right + 9), min(height, bottom + 9)]
    return bounds, padded, [padded[0] / width, padded[1] / height, padded[2] / width, padded[3] / height]

marine = material('CapIcon_DeepMarine', (.012, .035, .055), .36, .15)
variants = [('Hat_WorkCap', (.025, .47, .56), .05), ('Hat_WorkCapRed', (.66, .055, .035), .03)]
report = {'source': str(args.source), 'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
          'source_transform': source_transform, 'source_world_bounds_m': bounds,
          'source_vertex_sha256': source_vertex_hash, 'preserved_vertex_count': len(cap.data.vertices),
          'preserved_smooth_faces': sum(p.use_smooth for p in cap.data.polygons),
          'source_geometry_unchanged': True, 'camera': list(camera.location),
          'camera_ortho_scale': camera_data.ortho_scale, 'threads': 1,
          'glasses': 'Not appended or rendered; approved source and icon remain unchanged.', 'items': []}
for item, color, metallic in variants:
    cap.data.materials[0] = material('CapIcon_' + item, color, .52, metallic)
    cap.data.materials[1] = marine
    scene.render.filepath = str(args.out / (item + '.png'))
    assert scene.camera is not None
    bpy.ops.render.render(write_still=True)
    bounds_px, padded_px, crop_uv = alpha_bounds(scene.render.filepath)
    report['items'].append({'item': item, 'png': scene.render.filepath,
                            'base_color_linear': list(color), 'roughness': .52, 'metallic': metallic,
                            'alpha_bounds_px': bounds_px, 'padded_bounds_px': padded_px, 'crop_uv': crop_uv})
assert source_vertex_hash == hashlib.sha256(json.dumps([[*v.co] for v in cap.data.vertices]).encode()).hexdigest()
(args.out / 'Report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(args.out / 'CapIcons_Render.blend'))
print('MC_CAP_ICONS_PASS ' + str(args.out / 'Report.json'))
