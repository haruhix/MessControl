"""CPU full-scene first-hit check for the prepared seven-mesh v14 stage."""
from pathlib import Path
import json
import numpy as np

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Gums/Diagnostics/v14_UpperRootWrap'
receipt = json.loads((OUT / 'upper6_root_wrap_v14_preparation.json').read_text())
g = np.load(receipt['source_geometry_npz'])
V, T, O = g['vertices'].copy(), g['triangles'], g['tri_object']
response = ROOT / 'Assembly/Review/MaterialResponse_v18'
names = json.loads((response / 'response_manifest.json').read_text())['objects']
stage = np.load(receipt['coordinate_stage'])
for change in receipt['changes']:
    name = change['object']
    indices = np.unique(T[O == names.index(name)])
    if not np.array_equal(V[indices], stage[name + '__before']):
        raise RuntimeError('Original basis mismatch: ' + name)
    V[indices] = stage[name + '__after']
tri = V[T]
xmin, xmax = tri[:, :, 0].min(axis=1), tri[:, :, 0].max(axis=1)
zmin, zmax = tri[:, :, 2].min(axis=1), tri[:, :, 2].max(axis=1)


def owner(cut, owner_ids, x, z):
    a, b, c = cut[:, 0], cut[:, 1], cut[:, 2]
    u, v = b[:, [0, 2]] - a[:, [0, 2]], c[:, [0, 2]] - a[:, [0, 2]]
    d = np.array([x, z]) - a[:, [0, 2]]
    det = u[:, 0] * v[:, 1] - u[:, 1] * v[:, 0]
    good = np.abs(det) > 1e-14
    safe = np.where(good, det, 1)
    s = (d[:, 0] * v[:, 1] - d[:, 1] * v[:, 0]) / safe
    t = (u[:, 0] * d[:, 1] - u[:, 1] * d[:, 0]) / safe
    good &= (s >= -1e-7) & (t >= -1e-7) & (s + t <= 1 + 1e-7)
    if not good.any():
        return None, None
    indices = np.where(good)[0]
    depth = a[good, 1] + s[good] * (b[good, 1] - a[good, 1]) + t[good] * (c[good, 1] - a[good, 1])
    nearest = indices[np.argmin(depth)]
    return names[int(owner_ids[nearest])], float(depth.min())


columns, cracks = [], []
for source in receipt['compound_ownership_columns']:
    x, target_y, target = source['x'], source['source_first_enamel_y'], source['object']
    wx = ((x + .5) / 963 - .5) * 16
    hi_z, lo_z = (205 - target_y + 3.5) * 16 / 963, (205 - target_y - 5.5) * 16 / 963
    selected = (xmin <= wx) & (xmax >= wx) & (zmin <= hi_z) & (zmax >= lo_z)
    cut, owners = tri[selected], O[selected]
    first, rays = None, []
    for y in range(target_y - 3, target_y + 5):
        hit, depth = owner(cut, owners, wx, (205 - y - .5) * 16 / 963)
        rays.append({'y': y, 'object': hit, 'world_Y': depth})
        if hit == target and first is None:
            first = y
        if abs(y + .5 - target_y) <= 1.5 and (hit is None or 'Interior_' in hit):
            cracks.append({'x': x, 'y': y, 'object': hit, 'source_part': target})
    columns.append({'object': target, 'x': x, 'source_first_enamel_y': target_y,
                     'full_scene_first_target_enamel_y': first,
                     'error': first - target_y if first is not None else None, 'rays': rays})
bad = [r for r in columns if r['error'] is None or abs(r['error']) > 1]
report = {'status': 'CPU_FULL_SCENE_FIRST_HIT_AUDIT_NOT_ACTUAL_DCC_LOOK',
          'source_stage': receipt['coordinate_stage'], 'columns': len(columns),
          'within1px': len(columns) - len(bad), 'outside1px': bad,
          'near_cervical_interior_or_background_hits': cracks,
          'corner567': next(r for r in columns if r['x'] == 567),
          'all_first_hits': columns,
          'pass': not bad and not cracks,
          'limitation': 'Uses the exact original evaluated v18 triangle arrays with staged world coordinates. A fresh Blender evaluated-BVH and PBR/gray capture remain mandatory.'}
(OUT / 'full_scene_cpu_ownership_audit.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps({k: v for k, v in report.items() if k not in ('all_first_hits', 'corner567')}, indent=2))
