"""Bake approved inner-mesh semantic weights to UV masks; no beauty pixels.

Run only after the root's UV/hash release. Medium folds remain geometry. The
authored thin-tissue/density maps are shading support, not metric thickness.
"""
import argparse
import hashlib
import json
from pathlib import Path
import cv2
import numpy as np


def smooth(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--handoff', type=Path, required=True)
    p.add_argument('--expected-packet-sha256', required=True)
    args = p.parse_args()
    path = args.handoff / 'texture_mesh_packet.npz'
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    if sha != args.expected_packet_sha256.lower():
        raise RuntimeError('Approved packet hash mismatch; no masks written.')
    d = np.load(path)
    names = ['cavity_depth_weight', 'buccal_arch_weight', 'palate_weight', 'rugae_crest_weight',
             'floor_weight', 'tongue_dorsal_weight', 'tongue_sulcus_weight',
             'tongue_tip_weight', 'hidden_return_weight']
    if not all(k in d for k in names):
        raise RuntimeError(f'Missing geometric semantic field; actual keys: {list(d.keys())}')
    w = {k: np.clip(d[k], 0, 1) for k in names}
    deep = smooth((w['cavity_depth_weight'] - .55) / .40)
    crest = np.clip(.65 * w['rugae_crest_weight'] + .35 * w['buccal_arch_weight'], 0, 1) * (1 - deep)
    visible = 1 - w['hidden_return_weight']
    dorsal = w['tongue_dorsal_weight']; sulcus = w['tongue_sulcus_weight']; tip = w['tongue_tip_weight']
    interior_thin = np.clip(.30 + .35 * crest + .12 * w['palate_weight'] - .28 * deep, .1, .8) * (1 - .7 * w['hidden_return_weight'])
    tongue_thin = np.clip(.28 + .20 * dorsal + .25 * tip - .10 * sulcus, .2, .75)
    fields = {
        'MC_Interior': {'DeepRecess': deep, 'CrestPink': crest,
                        'InteriorWetZone': (.4 + .6 * crest) * (1 - deep) * visible,
                        'FineMucosaZone': (1 - .8 * deep) * visible,
                        'ThinTissueMask': interior_thin, 'ArtistDensityMask': 1 - interior_thin},
        'MC_Tongue': {'DorsalTongue': dorsal, 'PapillaeZone': dorsal * (1 - .85 * sulcus) * (1 - .4 * tip),
                     'SmoothSulcus': dorsal * sulcus, 'AnteriorTip': dorsal * tip,
                     'ThinTissueMask': tongue_thin, 'ArtistDensityMask': 1 - tongue_thin}}
    n = 4096
    out = args.handoff / 'Textures' / 'AnatomyMasks'; out.mkdir(parents=True, exist_ok=True)
    loops = d['loop_vertex_indices']; triangles = d['triangle_loop_indices']
    uv = d['loop_uv']; xy = np.column_stack([uv[:, 0] * (n - 1), (1 - uv[:, 1]) * (n - 1)]).astype(np.float64)
    report = {'schema': 'messcontrol.inner_semantic_uv_masks.v1', 'packet_sha256': sha,
              'resolution': n, 'padding_pixels': 16, 'projection': 'Geometry semantic attributes only',
              'lighting_baked': False, 'sets': {}}
    for slot, name in enumerate(['MC_Interior', 'MC_Tongue']):
        keys = list(fields[name]); values = np.column_stack([fields[name][k] for k in keys])
        region = triangles[d['triangle_material_indices'] == slot]
        atlas = np.zeros((n, n, len(keys)), np.uint8); coverage = np.zeros((n, n), np.uint8)
        # Barycentric interpolation preserves continuous anatomy gradients on
        # these broader, lower-density inner meshes. Triangle-constant colors
        # could otherwise reveal triangulation in the deep burgundy transition.
        for tri in region:
            pts = xy[tri]
            x0, y0 = np.maximum(0, np.floor(pts.min(0)).astype(int)); x1, y1 = np.minimum(n - 1, np.ceil(pts.max(0)).astype(int))
            if x1 < x0 or y1 < y0:
                continue
            a, b, c = pts
            determinant = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if abs(determinant) < 1e-12:
                continue
            xx, yy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
            w1 = ((xx - a[0]) * (c[1] - a[1]) - (yy - a[1]) * (c[0] - a[0])) / determinant
            w2 = ((b[0] - a[0]) * (yy - a[1]) - (b[1] - a[1]) * (xx - a[0])) / determinant
            w0 = 1 - w1 - w2
            valid = (w0 >= -1e-7) & (w1 >= -1e-7) & (w2 >= -1e-7)
            colors = values[loops[tri]]
            interpolated = w0[valid, None] * colors[0] + w1[valid, None] * colors[1] + w2[valid, None] * colors[2]
            atlas[yy[valid], xx[valid]] = np.rint(np.clip(interpolated, 0, 1) * 255).astype(np.uint8)
            coverage[yy[valid], xx[valid]] = 255
        _, labels = cv2.distanceTransformWithLabels(255 - coverage, cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
        padded = cv2.dilate(coverage, np.ones((33, 33), np.uint8))
        files = []
        for j in range(2):
            pack = atlas[:, :, j * 3:j * 3 + 3]
            lookup = np.zeros((int(labels.max()) + 1, 3), np.uint8)
            lookup[labels[coverage > 0]] = pack[coverage > 0]
            filled = cv2.GaussianBlur(lookup[labels], (0, 0), .7); filled[padded == 0] = 0
            for channel in range(3):
                role = keys[j * 3 + channel]; output = out / f'{name}_{role}.png'
                cv2.imwrite(str(output), filled[:, :, channel])
                files.append({'role': role, 'path': str(output.resolve()), 'sha256': hashlib.sha256(output.read_bytes()).hexdigest()})
        cv2.imwrite(str(out / f'{name}_Coverage.png'), coverage)
        report['sets'][name] = {'triangles': int(len(region)), 'uv_coverage_fraction': float(np.mean(coverage > 0)), 'files': files}
        print(name, len(region), flush=True)
    (out / 'anatomy_mask_receipt.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps({'out': str(out.resolve()), 'packet_sha256': sha}))


if __name__ == '__main__':
    main()
