"""Rasterize mesh-space anatomical pigment masks into the frozen v08 UV0.

No beauty/reference pixels or lighting are sampled. High-resolution mesh
triangles carry smooth root/tip attributes; per-triangle interpolation is
approximated by the triangle mean, then softened by 0.7 texel. Padding is a
nearest-island extension. Separate material atlases may share UV coordinates.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
HANDOFF = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish'


def main():
    global HANDOFF
    p = argparse.ArgumentParser()
    p.add_argument('--handoff', type=Path, default=HANDOFF)
    p.add_argument('--packet', type=Path)
    p.add_argument('--manifest', type=Path)
    p.add_argument('--resolution', type=int, default=4096)
    p.add_argument('--expected-packet-sha256', help='Reject a changed packet before rasterizing any masks.')
    args = p.parse_args()
    HANDOFF = args.handoff.resolve()
    args.packet = args.packet or HANDOFF / 'texture_mesh_packet.npz'
    args.manifest = args.manifest or HANDOFF / 'unreal_mouth_handoff.json'
    if args.expected_packet_sha256 and hashlib.sha256(args.packet.read_bytes()).hexdigest() != args.expected_packet_sha256.lower():
        raise RuntimeError('Approved packet hash mismatch; no masks written.')
    data = np.load(args.packet)
    # The packet writer's schema is intentionally explicit and checked here.
    required = ['vertices', 'loop_vertex_indices', 'loop_uv', 'triangle_loop_indices', 'triangle_material_indices']
    if not all(key in data for key in required):
        raise RuntimeError(f'Packet keys are {list(data.keys())}; expected {required}')
    vertices = data['vertices']
    loop_vertices = data['loop_vertex_indices']
    uvs = data['loop_uv']
    triangles = data['triangle_loop_indices']
    materials = data['triangle_material_indices']
    manifest = json.loads(args.manifest.read_text(encoding='utf8'))
    n = args.resolution
    attributes = np.zeros((len(vertices), 3), np.float32)
    # R: cervical warmth/pink gingiva. G: incisal enamel. B: thin-tissue artist
    # mask. The latter is a normalized shading mask, never a metric thickness.
    roots = {}
    for part in manifest['parts']:
        start = part['vertex_start']; end = start + part['vertex_count']
        c = vertices[start:end]
        lower = '_Lower_' in part['mesh']
        if part['material_slot'] == 0:
            root = c[:, 2].min() if lower else c[:, 2].max()
            tip = c[:, 2].max() if lower else c[:, 2].min()
            tip_fraction = np.clip((c[:, 2] - root) / (tip - root), 0, 1)
            neck = np.clip((.45 - tip_fraction) / .45, 0, 1)
            edge = np.clip((tip_fraction - .65) / .35, 0, 1)
            attributes[start:end, 0] = neck * neck * (3 - 2 * neck)
            attributes[start:end, 1] = edge * edge * (3 - 2 * edge)
            attributes[start:end, 2] = .18 + .70 * tip_fraction ** 1.5
            roots.setdefault(lower, []).append((float(np.abs(c[:, 0]).mean()), float(root)))
    for part in manifest['parts']:
        if part['material_slot'] != 1:
            continue
        start = part['vertex_start']; end = start + part['vertex_count']
        c = vertices[start:end]; lower = '_Lower_' in part['mesh']
        rr = sorted(roots[lower])
        cervical_z = np.interp(np.abs(c[:, 0]), [v[0] for v in rr], [v[1] for v in rr])
        # 0.55 source units is measured against a ~2-unit crown, not interpreted
        # as a literal biological millimeter scale.
        margin = np.exp(-((c[:, 2] - cervical_z) / .55) ** 2)
        attributes[start:end, 0] = margin
        attributes[start:end, 2] = .35 + .50 * margin
    # Prefer the geometry author's parameterized anatomy over reconstructed
    # bounding-box distances when the frozen handoff provides those fields.
    if all(k in data for k in ['crown_root_weight', 'crown_tip_weight', 'gum_margin_weight']):
        for part in manifest['parts']:
            start = part['vertex_start']; end = start + part['vertex_count']
            if part['material_slot'] == 0:
                root_weight = np.clip(data['crown_root_weight'][start:end], 0, 1)
                tip_weight = np.clip(data['crown_tip_weight'][start:end], 0, 1)
                attributes[start:end, 0] = root_weight
                attributes[start:end, 1] = tip_weight
                attributes[start:end, 2] = .18 + .70 * tip_weight
            else:
                margin_weight = np.clip(data['gum_margin_weight'][start:end], 0, 1)
                attributes[start:end, 0] = margin_weight
                attributes[start:end, 2] = .35 + .50 * margin_weight
    out = HANDOFF / 'Textures' / 'AnatomyMasks'
    out.mkdir(parents=True, exist_ok=True)
    report = {'schema': 'mesh_anatomy_uv_masks.v1', 'packet': str(args.packet.resolve()),
              'packet_sha256': hashlib.sha256(args.packet.read_bytes()).hexdigest(),
              'resolution': n, 'padding_pixels': 16, 'lighting_sampled': False,
              'method': __doc__, 'sets': {}}
    uv_pixels = np.rint(np.column_stack([uvs[:, 0] * (n - 1), (1 - uvs[:, 1]) * (n - 1)])).astype(np.int32)
    for slot, name in enumerate(['MC_Enamel', 'MC_Gum']):
        image = np.zeros((n, n, 3), np.uint8)
        coverage = np.zeros((n, n), np.uint8)
        ids = np.flatnonzero(materials == slot)
        for tri in triangles[ids]:
            points = uv_pixels[tri]
            color = np.rint(attributes[loop_vertices[tri]].mean(axis=0) * 255).astype(np.uint8)
            cv2.fillConvexPoly(image, points, tuple(int(v) for v in color), lineType=cv2.LINE_8)
            cv2.fillConvexPoly(coverage, points, 255, lineType=cv2.LINE_8)
        _, labels = cv2.distanceTransformWithLabels(255 - coverage, cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
        lut = np.zeros((int(labels.max()) + 1, 3), np.uint8)
        lut[labels[coverage > 0]] = image[coverage > 0]
        image = lut[labels]
        image = cv2.GaussianBlur(image, (0, 0), .7)
        padded = cv2.dilate(coverage, np.ones((33, 33), np.uint8))
        image[padded == 0] = 0
        files = []
        for i, role in enumerate(['Cervical', 'Incisal', 'ThinTissueMask']):
            path = out / f'{name}_{role}.png'
            cv2.imwrite(str(path), image[:, :, i])
            files.append({'role': role, 'path': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        cv2.imwrite(str(out / f'{name}_Coverage.png'), coverage)
        report['sets'][name] = {'triangles': int(len(ids)), 'uv_coverage_fraction': float(np.mean(coverage > 0)), 'files': files}
        print(name, len(ids), flush=True)
    (out / 'anatomy_mask_receipt.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps({'out': str(out.resolve()), 'status': 'mesh-derived masks; native look review required'}))


if __name__ == '__main__':
    main()
