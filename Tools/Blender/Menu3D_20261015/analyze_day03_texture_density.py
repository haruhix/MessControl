"""Read-only, area-weighted texture density/anisotropy audit of a frozen packet.

Reports do not replace checker/atlas review. An automatic projection can have
reasonable density while fragmenting a hero facial surface into many charts.
"""
import argparse
import json
from pathlib import Path
import numpy as np


def weighted(values, area, q=(5, 50, 95)):
    mask = np.isfinite(values) & np.isfinite(area) & (area > 0)
    values = values[mask]; area = area[mask]
    if not len(values):
        return None
    order = np.argsort(values); values = values[order]; area = area[order]
    cdf = (np.cumsum(area) - .5 * area) / area.sum()
    return np.interp(np.asarray(q) / 100, cdf, values).tolist()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--handoff', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    packet = np.load(args.handoff / 'texture_mesh_packet.npz')
    meta = json.loads((args.handoff / 'unreal_mouth_handoff.json').read_text())
    loops = packet['loop_vertex_indices']; tri_loops = packet['triangle_loop_indices']
    tri_indices = loops[tri_loops]
    xyz = packet['vertices'][tri_indices].astype(np.float64)
    uv = packet['loop_uv'][tri_loops].astype(np.float64)
    e1 = xyz[:, 1] - xyz[:, 0]; e2 = xyz[:, 2] - xyz[:, 0]
    q1 = uv[:, 1] - uv[:, 0]; q2 = uv[:, 2] - uv[:, 0]
    cross = np.cross(e1, e2); twice_area = np.linalg.norm(cross, axis=1)
    area = twice_area * .5
    a = np.linalg.norm(e1, axis=1)
    b = np.einsum('ij,ij->i', e1, e2) / np.maximum(a, 1e-20)
    c = twice_area / np.maximum(a, 1e-20)
    jx = q1 / np.maximum(a[:, None], 1e-20)
    jy = (q2 - b[:, None] * jx) / np.maximum(c[:, None], 1e-20)
    aa = jx[:, 0] ** 2 + jy[:, 0] ** 2
    bb = jx[:, 0] * jx[:, 1] + jy[:, 0] * jy[:, 1]
    dd = jx[:, 1] ** 2 + jy[:, 1] ** 2
    discriminant = np.sqrt(np.maximum((aa - dd) ** 2 + 4 * bb * bb, 0))
    largest = np.sqrt(np.maximum((aa + dd + discriminant) * .5, 0))
    smallest = np.sqrt(np.maximum((aa + dd - discriminant) * .5, 0))
    anisotropy = largest / np.maximum(smallest, 1e-20)
    uv_area = np.abs(q1[:, 0] * q2[:, 1] - q1[:, 1] * q2[:, 0]) * .5
    density = 4096 * np.sqrt(uv_area / np.maximum(area, 1e-20))
    front = cross[:, 1] / np.maximum(twice_area, 1e-20) > .35
    report = {'schema': 'mesh_texture_density.v1', 'resolution': 4096,
              'source_units': 'Blender source units; density comparisons are relative.',
              'method': 'Triangle Jacobian singular values and world-area-weighted percentiles. Front means outward geometric normal +Y >0.35.',
              'limitations': 'This does not measure chart coherence or seam visibility; actual checker and final material review are required.',
              'parts': {}}
    for part in meta['parts']:
        st = part['vertex_start']; end = st + part['vertex_count']
        mask = (tri_indices[:, 0] >= st) & (tri_indices[:, 0] < end) & (area > 1e-8)
        item = {'material_slot': part['material_slot'], 'triangles': int(mask.sum())}
        for name, region in [('whole', mask), ('front', mask & front)]:
            item[name] = {'area': float(area[region].sum()),
                          'density_px_per_source_unit_p05_p50_p95': weighted(density[region], area[region]),
                          'anisotropy_p05_p50_p95': weighted(anisotropy[region], area[region])}
        report['parts'][part['mesh']] = item
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding='utf8')
    for slot, name in [(0, 'enamel'), (1, 'gum')]:
        region = (packet['triangle_material_indices'] == slot) & (area > 1e-8) & front
        print(name, 'front density', weighted(density[region], area[region]), 'anisotropy', weighted(anisotropy[region], area[region]))


if __name__ == '__main__':
    main()
