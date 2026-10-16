"""Fixed-camera baked surface LIGHTING, deliberately NOT physical albedo.

CPU only: exact evaluated world triangles + named unique UV0, nearest-Y source
visibility, per-object source masks, same-object harmonic occlusion inference.
Existing original character is excluded and remains a separate PBR asset.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import time

import cv2
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
W, H, WORLD_W, N = 963, 410, 16., 2048
SOURCE = ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png'
INTERIOR = ROOT / 'Evidence/SourceAnalysis/CanonicalOpen/InteriorRefined'
GINGIVA = ROOT / 'Textures/Revisions/v05_SourceLocked/Gingiva'
JAW = ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json'
EXCLUSION = ROOT / 'Textures/Revisions/v08_EnamelFull32_v13/SourceFields/InteriorV03_UICharacterShadowExclusion.png'
cv2.setNumThreads(2)

spec = importlib.util.spec_from_file_location('closed_transfer_helpers', HERE / 'transfer_closed_source_to_uv0.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
helper.W, helper.H, helper.WORLD_W = W, H, WORLD_W


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def mask(path):
    value = np.asarray(Image.open(path).convert('L')) > 0
    if value.shape != (H, W):
        raise RuntimeError('Source mask size mismatch: ' + str(path))
    return value


def object_material(name, receipt):
    materials = receipt['objects'][name]['materials']
    if len(materials) != 1 or materials[0] not in ('MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue'):
        raise RuntimeError('Ambiguous actual material: ' + name)
    return materials[0]


def harmonic_field(source_linear, known, owner_domain):
    """Known radiance stays exact; unknown UI/character is never copied."""
    if known.sum() < 8:
        raise RuntimeError('Insufficient source observations for an object')
    domain = owner_domain | known
    median = np.median(source_linear[known], axis=0)
    yy, xx = np.where(domain)
    x0, x1 = max(0, xx.min()-3), min(W, xx.max()+4)
    y0, y1 = max(0, yy.min()-3), min(H, yy.max()+4)
    local_domain, local_known = domain[y0:y1, x0:x1], known[y0:y1, x0:x1]
    kernel = np.array([[0, 1, 0], [1, 0, 1], [0, 1, 0]], np.float32)
    # Coarse-to-fine propagation reaches the CENTER of large UI/worker holes;
    # a fixed short full-resolution Jacobi pass leaves a visible median plate.
    # Every level's constraints derive ONLY from observed pixels. The final
    # level restores every original known value exactly, without patch edges.
    local_source = source_linear[y0:y1, x0:x1]
    fill = None
    for divisor, iterations in ((8, 700), (4, 350), (2, 240), (1, 220)):
        width = max(2, int(np.ceil(local_domain.shape[1]/divisor)))
        height = max(2, int(np.ceil(local_domain.shape[0]/divisor)))
        size = (width, height)
        if divisor == 1:
            sd, sk, observed = local_domain, local_known, local_source
        else:
            sd = cv2.resize(local_domain.astype(np.float32), size, interpolation=cv2.INTER_AREA) > .05
            coverage = cv2.resize(local_known.astype(np.float32), size, interpolation=cv2.INTER_AREA)
            sk = coverage > .65
            observed = cv2.resize(local_source*local_known[..., None], size, interpolation=cv2.INTER_AREA)/np.maximum(coverage[..., None], 1e-6)
        if fill is None:
            fill = np.broadcast_to(median, (height, width, 3)).copy()
        else:
            fill = cv2.resize(fill, size, interpolation=cv2.INTER_LINEAR)
        fill[sk] = observed[sk]
        denominator = cv2.filter2D(sd.astype(np.float32), -1, kernel, borderType=cv2.BORDER_CONSTANT)
        unknown = sd & ~sk
        for _ in range(iterations):
            average = cv2.filter2D(fill*sd[..., None], -1, kernel, borderType=cv2.BORDER_CONSTANT)/np.maximum(denominator[..., None], 1e-6)
            fill[unknown] = average[unknown]
    field = np.broadcast_to(median, (H, W, 3)).copy()
    field[y0:y1, x0:x1][local_domain] = fill[local_domain]
    return {'RGB_linear': field, 'known': known, 'domain': domain, 'median_linear': median}


def fields_for(objects, owner, receipt, source_linear, excluded, recover_visible_interiors=False):
    records = {part['object']: Path(part['source_visible_front_mask']) for part in load(JAW)['parts']}
    records.update({
        'MC_DAY03_Upper_Gum': GINGIVA / 'upper_gingiva_visible.png',
        'MC_DAY03_Lower_Gum': GINGIVA / 'lower_gingiva_visible_strict.png',
        'Interior_OrganicBuccalPalate_Closed': INTERIOR / 'mucosa_visible_material.png',
        'Interior_BroadDorsalTongue': INTERIOR / 'tongue_visible_material.png',
    })
    if set(records) != set(objects):
        raise RuntimeError('Every actual surface needs its own source record')
    fields = {}
    observed_pixel_counts = {}
    for object_id, name in enumerate(objects):
        # The source masks are visibility evidence, not full physical surfaces.
        # Highlights are retained in this RADIANCE branch; labels and the
        # reference worker (including its old contact shadow) remain excluded.
        observed = mask(records[name])
        known = observed & ~excluded
        if recover_visible_interiors:
            # Endpoints have measured source ownership; stay two pixels inside
            # actual first-hit silhouettes to avoid importing a neighboring
            # material across residual source/mesh contour differences.
            interior_owner = cv2.erode((owner == object_id).astype(np.uint8), np.ones((5, 5), np.uint8)).astype(bool)
            known |= interior_owner & ~excluded
        if known.sum() < 8:
            observed_pixel_counts[name] = {'observed': int(known.sum()), 'status': 'INFERRED_SAME_MATERIAL_CONTINUATION'}
            continue
        field = harmonic_field(source_linear, known, (owner == object_id) | observed)
        field.update({'material': object_material(name, receipt), 'source_mask': str(records[name])})
        fields[name] = field
        observed_pixel_counts[name] = {'observed': int(known.sum()), 'status': 'OBSERVED_RADIANCE_PLUS_HARMONIC_OCCLUSION_INFERENCE',
                                      'source_mask': str(records[name]), 'source_mask_sha256': sha(records[name])}
    return fields, observed_pixel_counts


def render_cpu_review(out, binding, names, receipt, owner, source_uv, fields, source):
    sx = (source_uv[..., 0]*N-.5).astype(np.float32)
    sy = ((1-source_uv[..., 1])*N-.5).astype(np.float32)
    result = np.zeros((H, W, 3), np.float32)
    for material, values in binding['materials'].items():
        raw = cv2.imread(values['radiance_not_albedo'], cv2.IMREAD_UNCHANGED)[..., ::-1]
        encoded = raw.astype(np.float32)/65535
        linear = np.where(encoded <= .04045, encoded/12.92, ((encoded+.055)/1.055)**2.4)
        sampled = cv2.remap(linear, sx, sy, cv2.INTER_LINEAR)
        ids = [i for i, name in enumerate(names) if object_material(name, receipt) == material]
        selected = np.isin(owner, ids)
        result[selected] = sampled[selected]
    rgb8 = np.uint8(np.rint(helper.srgb(result)*255))
    Image.fromarray(rgb8).save(out / 'open_first_hit_UV0_Radiance_NOT_ALBEDO_CPU.png')
    records = []
    for object_id, name in enumerate(names):
        if name not in fields:
            continue
        qualified = fields[name]['known'] & (owner == object_id)
        if not qualified.any():
            continue
        error = np.abs(rgb8[qualified].astype(np.float32)-source[qualified])
        records.append({'object': name, 'material': fields[name]['material'], 'qualified_shared_firsthit_pixels': int(qualified.sum()),
                        'RGB8_abs_error_p50_p90': np.percentile(error, [50, 90]).tolist(),
                        'source_RGB8_median': np.median(source[qualified], axis=0).tolist(),
                        'roundtrip_RGB8_median': np.median(rgb8[qualified], axis=0).tolist()})
    report = {'status': 'CPU_RADIANCE_NOT_ALBEDO_UV0_ROUNDTRIP_ONLY_NOT_NATIVE_ACCEPTANCE', 'records': records,
              'limits': ['Not an actual Blender or runtime shader render.', 'UI/worker/old-worker-shadow and hidden backs are inferred, never measured truth.',
                         'Native fixed-camera radiance shader and geometric pose transition still require actual review.']}
    (out / 'open_source_roundtrip_review.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--packet', required=True)
    parser.add_argument('--receipt', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--exclusion', help='New source-inspected tight UI/worker/shadow radiance mask')
    parser.add_argument('--recover-visible-interiors', action='store_true', help='Recover source pixels inside eroded actual endpoint owners, outside tight exclusions')
    args = parser.parse_args()
    packet_path, receipt_path, out = (Path(value).resolve() for value in (args.packet, args.receipt, args.out))
    if not out.is_relative_to(ROOT / 'Textures/Revisions'):
        raise RuntimeError('Task-owned new revision required')
    if out.exists() and any(out.iterdir()):
        raise RuntimeError('Fresh revision required; preserve prior candidates')
    out.mkdir(parents=True, exist_ok=True)
    started = time.time()
    receipt = load(receipt_path)
    if not receipt.get('source_unchanged') or not isinstance(receipt.get('objects'), dict):
        raise RuntimeError('Actual evaluated packet with source-unchanged receipt required')
    names = list(receipt['objects'])
    if len(names) != 36:
        raise RuntimeError('Full36 packet required')
    camera = receipt['camera']['open']
    if camera['pixels'] != [W, H] or camera['width'] != WORLD_W:
        raise RuntimeError('Canonical open camera mismatch')
    data = np.load(packet_path)
    world_parts, uv_parts, owner_parts = [], [], []
    for object_id, name in enumerate(names):
        info = receipt['objects'][name]
        if info['uv_layer'] != 'UV0':
            raise RuntimeError('Diagnostic projection is not unique UV0: ' + name)
        prefix = 'open::' + name + '::'
        triangles = data[prefix+'triangles'].astype(np.int64)
        world_parts.append(data[prefix+'world_vertices'][triangles])
        uv_parts.append(data[prefix+'UV0_triangles'])
        owner_parts.append(np.full(len(triangles), object_id, np.int32))
    world_triangles = np.concatenate(world_parts)
    uv_triangles, tri_object = np.concatenate(uv_parts), np.concatenate(owner_parts)
    if uv_triangles.shape != (len(world_triangles), 3, 2):
        raise RuntimeError('Actual loop UV0 shape mismatch')
    depth, owner, triangle, source_uv = helper.first_hits(world_triangles, uv_triangles, tri_object)
    np.savez_compressed(out / 'actual_open_camera_first_hit_UV0.npz', depth_Y=depth, object_id=owner, triangle_id=triangle, UV0=source_uv)
    source = np.asarray(Image.open(SOURCE).convert('RGB'))
    if args.exclusion:
        excluded = mask(Path(args.exclusion).resolve())
    else:
        excluded = mask(EXCLUSION) | mask(INTERIOR / 'ui_occlusion.png') | mask(INTERIOR / 'character_occlusion.png')
        # One pixel of conservative antialias protection around UI/worker silhouette.
        excluded = cv2.dilate(excluded.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool)
    Image.fromarray(np.uint8(excluded)*255).save(out / 'Source_UI_Worker_OldShadow_NoTransfer.png')
    fields, source_stats = fields_for(names, owner, receipt, helper.linear(source), excluded, args.recover_visible_interiors)
    materials = ['MC_Enamel', 'MC_Gum', 'MC_Interior', 'MC_Tongue']
    atlases, occupied, source_known_atlas, visible_atlas, object_stats = {}, {}, {}, {}, {}
    for material in materials:
        medians = [field['median_linear'] for field in fields.values() if field['material'] == material]
        if not medians:
            raise RuntimeError('No observed source data for ' + material)
        atlases[material] = np.broadcast_to(np.median(medians, axis=0), (N, N, 3)).copy()
        occupied[material] = np.zeros((N, N), bool)
        source_known_atlas[material] = np.zeros((N, N), np.float32)
        visible_atlas[material] = np.zeros((N, N), np.float32)
    xy = uv_triangles * [N, -N] + [0, N]
    for ids, pixels, weights in helper.fragments(xy, N, N, 2048):
        world = np.einsum('ij,ijk->ik', weights, world_triangles[ids])
        projected = helper.projected(world)
        sx, sy = projected[:, 0]-.5, projected[:, 1]-.5
        sampled_owner = helper.remap(owner, sx[None], sy[None], True)[0].astype(np.int32)
        sampled_depth = helper.remap(np.where(np.isfinite(depth), depth, 1e8), sx[None], sy[None], True)[0]
        in_frame = (sx >= 0) & (sx < W-1) & (sy >= 0) & (sy < H-1)
        visible = in_frame & (sampled_owner == tri_object[ids]) & (np.abs(world[:, 1]-sampled_depth) < .02)
        for object_id in np.unique(tri_object[ids]):
            name, selected = names[object_id], tri_object[ids] == object_id
            material = object_material(name, receipt)
            chosen = pixels[selected]
            occupied[material].reshape(-1)[chosen] = True
            if name not in fields:
                continue
            field = fields[name]
            values = np.broadcast_to(field['median_linear'], (len(chosen), 3)).copy()
            valid, xs, ys = visible[selected], sx[selected], sy[selected]
            domain = helper.remap(field['domain'], xs[None], ys[None], True)[0] > .5
            sampled = helper.remap(field['RGB_linear'], xs[None], ys[None])[0]
            supported = valid & domain
            values[supported] = sampled[supported]
            known = (helper.remap(field['known'], xs[None], ys[None], True)[0] > .5) & valid
            atlases[material].reshape(-1, 3)[chosen] = values
            source_known_atlas[material].reshape(-1)[chosen] = known
            visible_atlas[material].reshape(-1)[chosen] = valid
            stats = object_stats.setdefault(name, {'UV0_texels': 0, 'known_source_texels': 0, 'visible_texels': 0})
            stats['UV0_texels'] += len(chosen)
            stats['known_source_texels'] += int(known.sum())
            stats['visible_texels'] += int(valid.sum())
    binding = {'status': 'ALTERNATIVE_OPEN_BAKED_SURFACE_RADIANCE_NOT_ALBEDO_NOT_NATIVE_ACCEPTED',
        'materials': {}, 'surface_mode': 'BAKED_RADIANCE_ON_REAL_UNIQUE_UV0_MESH',
        'view_contract': {'view_transform': 'Standard', 'look': 'None', 'current_scene_exposure_stops': 0,
                          'texture_encoding': 'sRGB encoding of source linear radiance, exposure0', 'emission_strength': 1.,
                          'formula': 'Standard exposure0 and unit radiance. Camera surface branch; no additive PBR lobes.'},
        'physical_relighting_supported': False,
        'source_character': 'Existing original character remains real PBR with its original mesh/textures/actions.',
        'limits': ['Real 3D geometry, depth, occlusion and unique UV0; no image planes.',
                   'Source wet glints are intentionally BAKED LIGHTING, not albedo, roughness or normal evidence.',
                   'UI/reference worker/its old contact shadow excluded and harmonically inferred inside same-object domain.',
                   'Hidden backs and unobserved surfaces receive explicit material/part median radiance continuation.',
                   'No PBR maps, original geometry, SPP, or source scenes changed. Actual shader/runtime/pose review is pending.']}
    for material in materials:
        folder = out / material
        folder.mkdir(exist_ok=True)
        # Seven-pixel gutter dilation stops at every real UV island, preserving
        # separate front/back and neighboring-part ownership.
        distance, labels = cv2.distanceTransformWithLabels((~occupied[material]).astype(np.uint8), cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
        ys, xs = np.where(occupied[material])
        lookup = np.zeros((len(ys)+1, 3), np.float32)
        lookup[1:] = atlases[material][ys, xs]
        padding = (distance <= 7) & ~occupied[material]
        atlases[material][padding] = lookup[labels[padding]]
        path = folder / f'{material}_Open_Radiance_NOT_ALBEDO_16bit.png'
        helper.save_color(path, atlases[material])
        helper.save_scalar(folder / f'{material}_ObservedRadianceConfidence.png', source_known_atlas[material])
        helper.save_scalar(folder / f'{material}_ActualOpenVisibleSupport.png', visible_atlas[material])
        binding['materials'][material] = {'radiance_not_albedo': str(path), 'uv_map': 'UV0', 'colorspace': 'sRGB', 'emission_strength': 1.,
            'source_glints_preserved': True, 'real_geometry_and_depth': True,
            'hidden_back_coverage': 'Inferred same-part/material median radiance, not source truth.'}
    binding_path = out / 'material_binding_open_Radiance_NOT_ALBEDO.json'
    binding_path.write_text(json.dumps(binding, indent=2), encoding='utf8')
    review = render_cpu_review(out, binding, names, receipt, owner, source_uv, fields, source)
    report = {'status': binding['status'], 'packet': str(packet_path), 'packet_sha256': sha(packet_path),
        'receipt': str(receipt_path), 'receipt_sha256': sha(receipt_path), 'source': str(SOURCE), 'source_sha256': sha(SOURCE),
        'objects': names, 'actual_triangles': len(world_triangles), 'UV0_source_affine_used': False,
        'firsthit_camera_pixels': int((owner >= 0).sum()), 'UI_worker_oldshadow_excluded_pixels': int(excluded.sum()),
        'known_radiance_pixels': {name: int(field['known'].sum()) for name, field in fields.items()},
        'source_records': source_stats, 'object_stats': object_stats, 'DCC_Painter_GPU_used': False,
        'tight_exclusion': str(Path(args.exclusion).resolve()) if args.exclusion else None,
        'eroded_actual_visible_owner_recovery': args.recover_visible_interiors,
        'elapsed_seconds': time.time()-started}
    (out / 'open_radiance_transfer_receipt.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps({'binding': str(binding_path), 'objects': len(names), 'triangles': len(world_triangles),
                      'seconds': report['elapsed_seconds'], 'roundtrip': review['records']}, indent=2), flush=True)


if __name__ == '__main__':
    main()
