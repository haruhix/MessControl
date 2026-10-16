"""Read-only independent source/fresh-FBX symmetry gates; run in isolated Blender.

No source is saved. Output metrics are measured, never copied from producer pass flags.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

assert bpy.app.background, "Read-only QA must run in an isolated background process"
ap = argparse.ArgumentParser()
ap.add_argument('--asset', required=True)
ap.add_argument('--manifest', required=True)
ap.add_argument('--output', required=True)
ap.add_argument('--source-reference')
args = ap.parse_args(sys.argv[sys.argv.index('--') + 1:])
asset = Path(args.asset)
manifest = json.loads(Path(args.manifest).read_text(encoding='utf-8'))
out = Path(args.output)
out.parent.mkdir(parents=True, exist_ok=True)
if asset.suffix.lower() == '.blend':
    bpy.ops.wm.open_mainfile(filepath=str(asset))
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(asset), use_anim=False)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
assert len(meshes) == 1, f'Expected joined native mouth, got {len(meshes)} meshes'
ob = meshes[0]
me = ob.data
keys = me.shape_keys.key_blocks if me.shape_keys else []
parts = manifest['parts']
arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
report = {
    'asset': str(asset), 'sha256': hashlib.sha256(asset.read_bytes()).hexdigest(),
    'blender_version': bpy.app.version_string, 'independent': True,
    'read_only': True, 'mesh': ob.name, 'vertices': len(me.vertices),
    'triangles': sum(len(p.vertices) - 2 for p in me.polygons),
    'morph_names': [k.name for k in keys],
    'armatures': [{'name': a.name, 'bones': [b.name for b in a.data.bones]} for a in arms],
    'object_scales': {o.name: list(o.scale) for o in bpy.data.objects},
    'parts': [], 'poses': {}, 'gates': {},
}
uv = {}
for layer in me.uv_layers:
    data = np.empty(len(layer.data) * 2, np.float32)
    layer.data.foreach_get('uv', data)
    uv[layer.name] = {'loops': len(layer.data), 'non_finite': int((~np.isfinite(data)).sum()),
                      'min': data.reshape(-1, 2).min(0).tolist(),
                      'max': data.reshape(-1, 2).max(0).tolist()}
report['uv_layers'] = uv
weights_ok = True
root_group = ob.vertex_groups.get('root')
if root_group is None:
    weights_ok = False
else:
    for v in me.vertices:
        assigned = [g for g in v.groups if g.group == root_group.index]
        if len(assigned) != 1 or abs(assigned[0].weight - 1.) > 1.e-6:
            weights_ok = False
            break
report['all_root_weights_one'] = weights_ok

def coords(key):
    data = np.empty(len(key.data) * 3, np.float32)
    key.data.foreach_get('co', data)
    a = data.reshape(-1, 3).astype(np.float64)
    m = np.asarray(ob.matrix_world, dtype=np.float64)
    return a @ m[:3, :3].T + m[:3, 3]

def nearest(a, b):
    tree = KDTree(len(b))
    for i, co in enumerate(b):
        tree.insert(Vector(co), i)
    tree.balance()
    distances = np.fromiter((tree.find(Vector(co))[2] for co in a), dtype=np.float64, count=len(a))
    return {'max': float(distances.max()), 'mean': float(distances.mean()),
            'p99': float(np.quantile(distances, .99)), 'count': len(distances)}

def reflected(a):
    b = a.copy()
    b[:, 0] *= -1
    return b

source_ref = np.load(args.source_reference) if args.source_reference else None
pose_arrays = {}
all_coordinates = {key.name: coords(key) for key in keys}
base_coordinates = all_coordinates['Basis']
for amount in (.125, .375, .625, .875):
    a = base_coordinates.copy()
    for name, t in [('Pose_000', 0.), ('Pose_025', .25), ('Pose_050', .5), ('Pose_075', .75)]:
        a += max(0., 1 - abs(amount - t) / .25) * (all_coordinates[name] - base_coordinates)
    all_coordinates['Hat_' + str(amount)] = a
for keyname, a in all_coordinates.items():
    pose_arrays[keyname] = a
    info = {'min': a.min(0).tolist(), 'max': a.max(0).tolist(),
            'non_finite': int((~np.isfinite(a)).sum()),
            'whole_mesh_reflection': nearest(reflected(a), a)}
    # Source manifests refer to the authored order; FBX may reorder vertices, so
    # do not pretend these ranges identify fresh-imported parts.
    if asset.suffix.lower() == '.blend':
        named = {p['mesh']: p for p in parts}
        info['part_reflection'] = []
        info['gum_reflection'] = []
        for name, p in named.items():
            if 'Gum' in name:
                gum = a[p['vertex_start']:p['vertex_start'] + p['vertex_count']]
                info['gum_reflection'].append({'name': name, 'distance': nearest(reflected(gum), gum)})
            if not re.search(r'_L(?=$|\d)', name):
                continue
            counterpart = re.sub(r'_L(?=$|\d)', '_R', name)
            q = named.get(counterpart)
            if q is None:
                info['part_reflection'].append({'left': name, 'right': counterpart, 'missing': True})
                continue
            aa = a[p['vertex_start']:p['vertex_start'] + p['vertex_count']]
            bb = a[q['vertex_start']:q['vertex_start'] + q['vertex_count']]
            info['part_reflection'].append({'left': name, 'right': counterpart,
                                            'left_count': len(aa), 'right_count': len(bb),
                                            'distance': nearest(reflected(aa), bb)})
    if source_ref is not None:
        expected = source_ref[keyname]
        info['fresh_vs_source_source_units'] = nearest(a, expected)
    report['poses'][keyname] = info

if asset.suffix.lower() == '.blend':
    a = pose_arrays['Basis']
    for p in parts:
        b = a[p['vertex_start']:p['vertex_start'] + p['vertex_count']]
        report['parts'].append({'name': p['mesh'], 'vertices': len(b),
                                'min': b.min(0).tolist(), 'max': b.max(0).tolist(),
                                'center': ((b.min(0) + b.max(0)) / 2).tolist()})
    np.savez_compressed(out.with_suffix('.npz'), **pose_arrays)
    report['source_pose_arrays'] = str(out.with_suffix('.npz'))

names = [p['mesh'] for p in parts]
crowns = [n for n in names if 'Gum' not in n]
counts = {}
for jaw in ('Upper', 'Lower'):
    selected = [n for n in crowns if '_' + jaw + '_' in n]
    counts[jaw] = {'total': len(selected),
                   'left': sum(bool(re.search(r'_L(?=$|\d)', n)) for n in selected),
                   'right': sum(bool(re.search(r'_R(?=$|\d)', n)) for n in selected)}
report['source_manifest_counts'] = counts
symmetry_max = max(x['whole_mesh_reflection']['max'] for x in report['poses'].values())
report['gates'] = {
    '32_crowns_8_pairs_per_row': len(crowns) == 32 and all(x == {'total': 16, 'left': 8, 'right': 8} for x in counts.values()),
    'two_gums': sum('Gum' in n for n in names) == 2,
    'native_shape_keys': [k.name for k in keys] == ['Basis', 'Pose_000', 'Pose_025', 'Pose_050', 'Pose_075'],
    'root_rig': len(arms) == 1 and [b.name for b in arms[0].data.bones] == ['root'],
    'root_weights': weights_ok,
    'uv_present_finite': len(uv) > 0 and all(v['non_finite'] == 0 for v in uv.values()),
    'all_pose_coordinates_finite': all(v['non_finite'] == 0 for v in report['poses'].values()),
    'source_symmetry_1e_minus_5_units': symmetry_max <= 1.e-5,
    'no_negative_object_scale': all(all(s >= 0 for s in o.scale) for o in bpy.data.objects),
}
if asset.suffix.lower() == '.blend':
    pair_checks = [p for v in report['poses'].values() for p in v['part_reflection']]
    gum_checks = [p for v in report['poses'].values() for p in v['gum_reflection']]
    report['gates']['all_16_named_crown_pairs_symmetric_in_all_9_samples'] = (
        len(pair_checks) == 16 * 9 and all(not p.get('missing') and p['left_count'] == p['right_count']
                                          and p['distance']['max'] <= 1.e-5 for p in pair_checks))
    report['gates']['both_gums_symmetric_in_all_9_samples'] = (
        len(gum_checks) == 2 * 9 and all(p['distance']['max'] <= 1.e-5 for p in gum_checks))
if source_ref is not None:
    report['gates']['fresh_fbx_source_match_1e_minus_3_cm'] = max(v['fresh_vs_source_source_units']['max'] for v in report['poses'].values()) * 100 <= .001
report['geometry_gates_pass'] = all(report['gates'].values())
report['visual_review_required'] = True
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({'report': str(out), 'gates': report['gates'], 'symmetry_max_source_units': symmetry_max}))
