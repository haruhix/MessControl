"""Fresh GLB import: actual geometry and authored endpoint deformation checks.

Keeps the candidate's authored lights/camera for a separate material comparison.
Never saves over the source. Per-object vertex order may change on GLB export,
so compare sampled nearest world positions plus whole bounds, not index order.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Matrix
from mathutils.kdtree import KDTree

p = argparse.ArgumentParser()
p.add_argument('--manifest', required=True)
p.add_argument('--out', required=True)
p.add_argument('--render', action='store_true')
p.add_argument('--samples', type=int, default=48)
opt = p.parse_args(sys.argv[sys.argv.index('--')+1:])
manifest_path = Path(opt.manifest).resolve()
manifest = json.loads(manifest_path.read_text(encoding='utf8'))
if opt.render and manifest['entry'].get('textureStates'):
    raise RuntimeError('Fresh-import geometry check supports pose textures through the real browser adapter; do not render misleading static-Open materials here')
out = Path(opt.out).resolve()
root = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1').resolve()
if not out.is_relative_to(root / 'Assembly/Review'):
    raise RuntimeError('Expected task-owned Review output directory')
out.mkdir(parents=True, exist_ok=True)
source = Path(manifest['source'])
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(source))
scene = bpy.context.scene
light_receivers = {obj.name: [receiver.name for receiver in obj.light_linking.receiver_collection.objects]
                   for obj in bpy.data.objects if obj.type == 'LIGHT' and obj.light_linking.receiver_collection}
for obj in list(bpy.data.objects):
    if obj.type not in {'LIGHT', 'CAMERA'}:
        bpy.data.objects.remove(obj, do_unlink=True)
for action in list(bpy.data.actions):
    bpy.data.actions.remove(action)
existing = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=manifest['glb'])
imported = [obj for obj in bpy.data.objects if obj not in existing]
by_name = {}
for obj in imported:
    original = obj.get('mcOriginalObjectName', obj.name)
    if original in by_name:
        raise RuntimeError('Ambiguous imported node: ' + original)
    by_name[original] = obj
for name, receivers in light_receivers.items():
    col = bpy.data.collections.new(name + '_FreshImportReceivers')
    for receiver in receivers:
        if receiver in by_name:
            col.objects.link(by_name[receiver])
    if not col.objects:
        raise RuntimeError('No fresh-import receiver for light: ' + name)
    bpy.data.objects[name].light_linking.receiver_collection = col
points = np.load(manifest_path.parent / 'source_endpoint_world_vertices.npz')
report = {'source': str(source), 'glb': manifest['glb'], 'states': {},
          'imported_objects': len(imported), 'actions': [a.name for a in bpy.data.actions],
          'renders': [], 'material_limit': 'Fresh GLB has no native Blender SSS.'}
channels = {channel['node']:channel for channel in manifest['entry']['pose']['morphs']}
for label, amount in [('closed', 0.), ('quarter', .25), ('half', .5), ('threequarter', .75), ('open', 1.)]:
    states = manifest['endpoint_states'][label]
    for name, record in states.items():
        obj = by_name[name]
        obj.animation_data_clear()
        # Match the real browser: position/scale lerp, quaternion slerp, and
        # linear morph weights from endpoints. Compare against the source's
        # actual intermediate driver evaluation rather than copying that pose.
        c = manifest['endpoint_states']['closed'][name]
        o = manifest['endpoint_states']['open'][name]
        cp, cq, cs = Matrix(c['raw_blender_matrix']).decompose()
        op, oq, os = Matrix(o['raw_blender_matrix']).decompose()
        obj.matrix_basis = Matrix.LocRotScale(cp.lerp(op, amount), cq.slerp(oq, amount), cs.lerp(os, amount))
        if record.get('morph_names'):
            if not obj.data.shape_keys:
                raise RuntimeError('Morphs missing on imported node: ' + name)
            if [key.name for key in obj.data.shape_keys.key_blocks][1:] != record['morph_names']:
                raise RuntimeError('Fresh-import morph name/order mismatch: ' + name)
            samples = channels[name].get('samples', [{'openness':0.,'weights':c['weights']}, {'openness':1.,'weights':o['weights']}])
            right = next((index for index in range(1,len(samples)) if samples[index]['openness'] >= amount), len(samples)-1)
            low, high = samples[right-1], samples[right]
            fraction = (amount-low['openness'])/(high['openness']-low['openness'])
            for index, key_name in enumerate(record['morph_names']):
                obj.data.shape_keys.key_blocks[key_name].value = low['weights'][index]*(1-fraction) + high['weights'][index]*fraction
    for name in manifest['entry']['openingOnly']:
        by_name[name].hide_render = amount < .02
    scene.frame_set(0)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    results = []
    for name in states:
        key = label + '::' + name
        if key not in points:
            continue
        obj = by_name[name].evaluated_get(dg)
        coords = np.empty(len(obj.data.vertices)*3, dtype=np.float32)
        obj.data.vertices.foreach_get('co', coords)
        coords = coords.reshape(-1, 3)
        M = np.asarray(obj.matrix_world)
        world = coords @ M[:3, :3].T + M[:3, 3]
        target = points[key].astype(np.float64)
        kd = KDTree(len(world))
        for i, point in enumerate(world):
            kd.insert(point, i)
        kd.balance()
        samples = target[::max(1, len(target)//2000)]
        distances = np.asarray([kd.find(point)[2] for point in samples])
        bounds_error = float(np.max(np.abs(np.stack([world.min(0), world.max(0)]) - np.stack([target.min(0), target.max(0)]))))
        result = {'node': name, 'source_vertices': len(target), 'imported_vertices': len(world),
                  'sample_count': len(samples), 'nearest_max': float(distances.max()),
                  'nearest_p95': float(np.percentile(distances, 95)), 'bounds_max': bounds_error}
        result['pass'] = result['nearest_max'] < 1e-4 and bounds_error < 1e-4
        results.append(result)
    report['states'][label] = results
    if opt.render and label in ('closed', 'open'):
        frame = manifest['entry']['framing'][label]
        scene.camera.data.ortho_scale = frame['width']
        scene.render.resolution_x = 963
        scene.render.resolution_y = 257 if label == 'closed' else 410
        scene.cycles.samples = opt.samples
        path = out / (label + '_fresh_glb.png')
        scene.render.filepath = str(path)
        bpy.ops.render.render(write_still=True)
        report['renders'].append(str(path))
report['source_unchanged'] = source_hash == hashlib.sha256(source.read_bytes()).hexdigest()
report['geometry_pass'] = all(row['pass'] for state in report['states'].values() for row in state)
(out / 'fresh_import_report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
if not report['geometry_pass'] or not report['source_unchanged']:
    raise RuntimeError('Fresh-import geometry/source preservation failed; inspect report')
print('FRESH_IMPORT_GEOMETRY_PASS', len(report['states']['open']), flush=True)
