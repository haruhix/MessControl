"""Read-only CPU ray audit of lower enamel vs authored mouth geometry.

This measures actual projected pixel visibility, not only vertex bounds. Native
interior, tongue, UI and neighboring actors must still be checked in Unreal.
"""
import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

assert bpy.app.background
ap = argparse.ArgumentParser()
ap.add_argument('--asset', required=True)
ap.add_argument('--manifest', required=True)
ap.add_argument('--output', required=True)
ap.add_argument('--scale-cm', type=float, default=54.)
ap.add_argument('--pixel-step', type=int, default=2)
ap.add_argument('--selected-day', type=int, choices=(1,2,3), default=3)
ap.add_argument('--lower-pattern', default='Central|Lateral|Canine')
args = ap.parse_args(sys.argv[sys.argv.index('--') + 1:])
source = Path(args.asset)
manifest = json.loads(Path(args.manifest).read_text(encoding='utf-8'))
out = Path(args.output)
out.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(source))
mouth_meshes=[o for o in bpy.data.objects if o.type=='MESH' and o.data.shape_keys]
assert len(mouth_meshes)==1,'Expected one native morph mouth'
ob = mouth_meshes[0]
me = ob.data
keys = me.shape_keys.key_blocks
parts = manifest['parts']
starts = np.array([p['vertex_start'] for p in parts])
polys = [tuple(p.vertices) for p in me.polygons]
owners = np.searchsorted(starts, np.array([p[0] for p in polys]), side='right') - 1
lower_ids = [i for i, p in enumerate(parts) if 'Lower_' in p['mesh'] and re.search(args.lower_pattern,p['mesh'])]
base = np.empty(len(me.vertices) * 3, np.float32)
keys['Basis'].data.foreach_get('co', base)
base = base.reshape(-1, 3).astype(np.float64)
pose_data = {}
for key in keys:
    a = np.empty(len(key.data) * 3, np.float32)
    key.data.foreach_get('co', a)
    pose_data[key.name] = a.reshape(-1, 3).astype(np.float64)
camera = Vector((0., 2200., 0.))
tan_h = math.tan(math.radians(39.97) / 2)
fpx = 800 / tan_h
samples = (0., .125, .25, .375, .5, .625, .75, .875, 1.)
report = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'independent': True, 'method': 'CPU BVH nearest visible surface rays at projected lower crown pixel grid',
          'pixel_step': args.pixel_step, 'design_resolution': [1600, 900], 'camera_cm': list(camera),
          'horizontal_fov_degrees': 39.97, 'source_units_to_native_cm_after_actor_scale': args.scale_cm,
          'arrangement': f'DAY0{args.selected_day} selected among three mouth rows; selected height246.667 to370, neighbors compress equally',
          'limitations': ['Native static interior and tongue not loaded', 'UI labels/buttons not included',
                         'Fixed actor scale for comparison; final runtime may refit width/height',
                         'Sampling measures projected enamel surface visibility, not art finish or biological anatomy'],
          'samples': []}
for amount in samples:
    # Native hats are deltas from Basis, not direct absolute pose blending.
    a = base.copy()
    for name, t in [('Pose_000', 0.), ('Pose_025', .25), ('Pose_050', .5), ('Pose_075', .75)]:
        a += max(0., 1 - abs(amount - t) / .25) * (pose_data[name] - base)
    height = 246.666667 + (370 - 246.666667) * amount
    center_z = (370-height/2) if args.selected_day==1 else 0. if args.selected_day==2 else height/2-370
    world = a * args.scale_cm
    world[:, 2] += center_z
    all_bvh = BVHTree.FromPolygons([Vector(v) for v in world], polys, all_triangles=False)
    sample = {'openness': amount, 'card_height_cm': height, 'center_z_cm': center_z, 'crowns': []}

    def ray_visible(origin, direction, clip):
        for _ in range(16):
            hit = all_bvh.ray_cast(origin, direction, 10000.)
            if hit[0] is None:
                return None
            if not clip or abs(hit[0].z - center_z) <= height / 2 + .0001:
                return hit
            origin = hit[0] + direction * .001
        return None

    for pi in lower_ids:
        p = parts[pi]
        start = p['vertex_start']
        stop = start + p['vertex_count']
        local_faces = [tuple(v - start for v in polys[i]) for i in np.where(owners == pi)[0]]
        pv = world[start:stop]
        crown_bvh = BVHTree.FromPolygons([Vector(v) for v in pv], local_faces, all_triangles=False)
        depth = 2200 - pv[:, 1]
        px = 800 + fpx * pv[:, 0] / depth
        py = 450 - fpx * pv[:, 2] / depth
        x0 = max(0, int(math.floor(px.min())))
        x1 = min(1599, int(math.ceil(px.max())))
        y0 = max(0, int(math.floor(py.min())))
        y1 = min(899, int(math.ceil(py.max())))
        potential = 0
        visible = 0
        visible_clipped = 0
        occluders = {}
        ys = []
        clipped_ys = []
        for y in range(y0, y1 + 1, args.pixel_step):
            for x in range(x0, x1 + 1, args.pixel_step):
                direction = Vector(((x + .5 - 800) / fpx, -1., -(y + .5 - 450) / fpx)).normalized()
                own = crown_bvh.ray_cast(camera, direction, 10000.)
                if own[0] is None:
                    continue
                potential += 1
                hit = ray_visible(camera.copy(), direction, False)
                if hit is not None and owners[hit[2]] == pi:
                    visible += 1
                    ys.append(y)
                elif hit is not None:
                    occ = parts[int(owners[hit[2]])]['mesh']
                    occluders[occ] = occluders.get(occ, 0) + 1
                clipped = ray_visible(camera.copy(), direction, True)
                if clipped is not None and owners[clipped[2]] == pi:
                    visible_clipped += 1
                    clipped_ys.append(y)
        sample['crowns'].append({'part': p['mesh'], 'projected_bbox_px': [x0, y0, x1, y1],
                                 'potential_surface_samples': potential, 'visible_surface_samples': visible,
                                 'visible_after_card_mask_samples': visible_clipped,
                                 'visible_fraction_of_isolated_crown': visible / potential if potential else 0,
                                 'visible_masked_fraction_of_isolated_crown': visible_clipped / potential if potential else 0,
                                 'visible_enamel_height_px': max(ys) - min(ys) + args.pixel_step if ys else 0,
                                 'masked_visible_enamel_height_px': max(clipped_ys) - min(clipped_ys) + args.pixel_step if clipped_ys else 0,
                                 'occluder_samples': occluders})
    report['samples'].append(sample)
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({'output': str(out), 'samples': len(report['samples']), 'source_sha256': report['source_sha256']}))
