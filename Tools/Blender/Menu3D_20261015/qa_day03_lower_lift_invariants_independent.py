"""Read-only independently verify v06 -> v07 bounded lower-row translation."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy
import numpy as np

assert bpy.app.background
ap = argparse.ArgumentParser()
ap.add_argument('--before', required=True)
ap.add_argument('--after', required=True)
ap.add_argument('--manifest', required=True)
ap.add_argument('--output', required=True)
args = ap.parse_args(sys.argv[sys.argv.index('--') + 1:])
parts = json.loads(Path(args.manifest).read_text(encoding='utf-8'))['parts']

def digest(array):
    return hashlib.sha256(array.tobytes()).hexdigest()

def inspect(path):
    bpy.ops.wm.open_mainfile(filepath=str(path))
    ob = bpy.data.objects['SK_DAY03']
    me = ob.data
    result = {'source': str(path), 'sha256': hashlib.sha256(Path(path).read_bytes()).hexdigest(),
              'vertices': len(me.vertices), 'polygons': len(me.polygons),
              'scales': {o.name: list(o.scale) for o in bpy.data.objects}, 'uv': {}, 'poses': {}}
    for layer in me.uv_layers:
        uv = np.empty(len(layer.data) * 2, np.float32)
        layer.data.foreach_get('uv', uv)
        result['uv'][layer.name] = digest(uv)
    loops = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get('vertex_index', loops)
    result['loop_vertex_indices_sha256'] = digest(loops)
    mats = np.empty(len(me.polygons), np.int32)
    me.polygons.foreach_get('material_index', mats)
    result['polygon_material_indices_sha256'] = digest(mats)
    for k in me.shape_keys.key_blocks:
        co = np.empty(len(k.data) * 3, np.float32)
        k.data.foreach_get('co', co)
        result['poses'][k.name] = co.reshape(-1, 3).copy()
    base = result['poses']['Basis']
    for amount in (.125, .375, .625, .875):
        co = base.astype(np.float64).copy()
        for name, t in [('Pose_000', 0.), ('Pose_025', .25), ('Pose_050', .5), ('Pose_075', .75)]:
            co += max(0., 1 - abs(amount - t) / .25) * (result['poses'][name].astype(np.float64) - base)
        result['poses']['Hat_' + str(amount)] = co
    return result

before = inspect(args.before)
after = inspect(args.after)
assert before['vertices'] == after['vertices']
lower = np.zeros(after['vertices'], bool)
for p in parts:
    if '_Lower_' in p['mesh']:
        lower[p['vertex_start']:p['vertex_start'] + p['vertex_count']] = True
poses = {}
amounts = {'Basis': 1., 'Pose_000': 0., 'Pose_025': .25, 'Pose_050': .5, 'Pose_075': .75,
           'Hat_0.125': .125, 'Hat_0.375': .375, 'Hat_0.625': .625, 'Hat_0.875': .875}
for name, amount in amounts.items():
    a = before['poses'][name].astype(np.float64)
    b = after['poses'][name].astype(np.float64)
    delta = b - a
    expected = np.zeros_like(delta)
    expected[lower, 2] = .8 * amount
    poses[name] = {'openness': amount,
                   'whole_closed_exact': bool(np.array_equal(a, b)) if amount == 0 else None,
                   'upper_coordinates_exact': bool(np.array_equal(a[~lower], b[~lower])),
                   'lower_xy_exact': bool(np.array_equal(a[lower, :2], b[lower, :2])),
                   'lower_z_shift_min': float(delta[lower, 2].min()),
                   'lower_z_shift_max': float(delta[lower, 2].max()),
                   'max_error_vs_requested_translation': float(np.abs(delta - expected).max())}
for r in (before, after):
    del r['poses']
gates = {'same_vertex_count': before['vertices'] == after['vertices'],
         'same_polygon_count': before['polygons'] == after['polygons'],
         'uv_coordinates_exact': before['uv'] == after['uv'],
         'topology_indices_exact': before['loop_vertex_indices_sha256'] == after['loop_vertex_indices_sha256'],
         'material_indices_exact': before['polygon_material_indices_sha256'] == after['polygon_material_indices_sha256'],
         'object_scales_exact': before['scales'] == after['scales'],
         'closed_whole_geometry_exact': poses['Pose_000']['whole_closed_exact'],
         'upper_all_nine_samples_exact': all(p['upper_coordinates_exact'] for p in poses.values()),
         'lower_xy_all_nine_samples_exact': all(p['lower_xy_exact'] for p in poses.values()),
         'only_requested_lower_z_translation_1e_minus_6_units': all(p['max_error_vs_requested_translation'] <= 1.e-6 for p in poses.values())}
report = {'independent': True, 'read_only': True, 'before': before, 'after': after,
          'poses': poses, 'gates': gates, 'passed': all(gates.values())}
out = Path(args.output)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({'output': str(out), 'gates': gates, 'passed': report['passed']}))
