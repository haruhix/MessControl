"""CPU closed-camera source pigment/support transfer to actual unique UV0.

Input is an actual evaluated loop-triangle packet from the frozen endpoint.
No source-affine shortcuts, mesh edits, DCC, Painter or mesh-map bake. Source
appearance is lighting-confounded; output is an explicitly unaccepted pigment
draft, not recovered physical albedo or a prelit screenshot on a plane.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json

import cv2
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
PREP = ROOT / 'Textures/Revisions/v08_InverseResponse_v18/ClosedEndpointPrep_v01'
BASIS = ROOT / 'Textures/Revisions/v08_InverseResponse_v18/SmoothGain_Full32_v23/material_binding_continuous_gain.json'
W, H, WORLD_W, N = 963, 257, 14.2, 2048
cv2.setNumThreads(2)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def linear(rgb):
    value = np.asarray(rgb, np.float32)/255
    return np.where(value <= .04045, value/12.92, ((value+.055)/1.055)**2.4)


def srgb(rgb):
    value = np.clip(rgb, 0, 1)
    return np.where(value <= .0031308, value*12.92, 1.055*np.maximum(value, 0)**(1/2.4)-.055)


def scalar(path):
    return np.asarray(Image.open(path).convert('L'), np.float32)/255


def save_scalar(path, values):
    Image.fromarray(np.uint16(np.rint(np.clip(values, 0, 1)*65535))).save(path)


def save_color(path, values):
    bgr = np.uint16(np.rint(srgb(values)[..., ::-1]*65535))
    if not cv2.imwrite(str(path), bgr):
        raise RuntimeError('Could not save ' + str(path))


def projected(world):
    return np.stack(((world[..., 0]/WORLD_W+.5)*W, H/2-world[..., 2]/WORLD_W*W), axis=-1)


def fragments(triangles_xy, width, height, block_size=4096):
    """Vectorized exact pixel-center barycentrics, bounded temporary storage."""
    for start in range(0, len(triangles_xy), block_size):
        p = triangles_xy[start:start+block_size]
        e1, e2 = p[:, 1]-p[:, 0], p[:, 2]-p[:, 0]
        denominator = e1[:, 0]*e2[:, 1]-e1[:, 1]*e2[:, 0]
        low = np.maximum(np.ceil(p.min(axis=1)-.5).astype(np.int64), [0, 0])
        high = np.minimum(np.floor(p.max(axis=1)-.5).astype(np.int64), [width-1, height-1])
        spans = np.maximum(high-low+1, 0)
        counts = spans[:, 0]*spans[:, 1]
        counts[np.abs(denominator) < 1e-12] = 0
        total = int(counts.sum())
        if not total:
            continue
        if total > 4_000_000 and len(p) > 1:
            # A very large projected triangle is uncommon but must not exhaust
            # RAM. Split only the CPU raster batch, preserving triangle ids.
            for i, triangle in enumerate(p):
                for ids, pixels, weights in fragments(triangle[None], width, height, 1):
                    yield ids+start+i, pixels, weights
            continue
        local_ids = np.repeat(np.arange(len(p)), counts)
        offsets = np.arange(total)-np.repeat(np.cumsum(counts)-counts, counts)
        px = low[local_ids, 0]+offsets % spans[local_ids, 0]
        py = low[local_ids, 1]+offsets // spans[local_ids, 0]
        dx, dy = px+.5-p[local_ids, 0, 0], py+.5-p[local_ids, 0, 1]
        a = (dx*e2[local_ids, 1]-dy*e2[local_ids, 0])/denominator[local_ids]
        b = (e1[local_ids, 0]*dy-e1[local_ids, 1]*dx)/denominator[local_ids]
        inside = (a >= -1e-8) & (b >= -1e-8) & (a+b <= 1+1e-8)
        if not inside.any():
            continue
        weights = np.stack((1-a-b, a, b), axis=1)[inside]
        yield local_ids[inside]+start, py[inside]*width+px[inside], weights


def first_hits(world_triangles, uv_triangles, tri_object):
    depth = np.full(W*H, np.inf, np.float64)
    owner = np.full(W*H, -1, np.int32)
    triangle = owner.copy()
    uv = np.zeros((W*H, 2), np.float32)
    for ids, pixels, weights in fragments(projected(world_triangles), W, H):
        if not len(pixels):
            continue
        y = np.einsum('ij,ij->i', weights, world_triangles[ids, :, 1])
        order = np.lexsort((ids, y, pixels))
        ordered_pixels = pixels[order]
        keep = np.r_[True, ordered_pixels[1:] != ordered_pixels[:-1]]
        index = order[keep]
        index = index[y[index] < depth[pixels[index]]]
        chosen_pixels, chosen_ids, chosen_w = pixels[index], ids[index], weights[index]
        depth[chosen_pixels] = y[index]
        owner[chosen_pixels] = tri_object[chosen_ids]
        triangle[chosen_pixels] = chosen_ids
        uv[chosen_pixels] = np.einsum('ij,ijk->ik', chosen_w, uv_triangles[chosen_ids])
    return depth.reshape(H, W), owner.reshape(H, W), triangle.reshape(H, W), uv.reshape(H, W, 2)


def harmonic_source(target, actual_owner_domain, radiance=False):
    source = np.asarray(Image.open(target['observed_RGB']).convert('RGBA'))
    known = scalar(target['known']) > .5
    if radiance:
        source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_closed_source.png').convert('RGBA'))
        path = Path(target['original_source_mask'])
        if target['object'].endswith('_Upper_Gum'):
            path = ROOT / 'Evidence/SourceAnalysis/ClosedPoseFit/Gingiva/Masks/upper_flesh_visible.png'
        elif target['object'].endswith('_Lower_Gum'):
            path = ROOT / 'Evidence/SourceAnalysis/ClosedPoseFit/Gingiva/Masks/lower_front_pad_visible.png'
        # Radiance deliberately keeps OBSERVED highlights. It is never called
        # albedo or supplied to the physical PBR source-color fitter.
        known = scalar(path) > .5
        known &= ~(scalar(PREP / 'UI_NoTransfer.png') > .5)
    # The prep already excludes label/reflections/uncertain ownership; a further
    # two pixels avoid transferring contact shape/shading as pigment evidence.
    if not radiance:
        known = cv2.erode(known.astype(np.uint8), np.ones((5, 5), np.uint8)).astype(bool)
    # Observed masks exclude labels/glints. Their holes are not material or
    # geometric boundaries: continue through the actual same-object first-hit
    # footprint, while original known pixels remain the only color constraints.
    domain = (scalar(target['original_source_mask']) > .5) | actual_owner_domain
    if known.sum() < 8:
        return None
    colors = linear(source[..., :3])
    median = np.median(colors[known], axis=0)
    yy, xx = np.where(domain | known)
    x0, x1, y0, y1 = max(0, xx.min()-3), min(W, xx.max()+4), max(0, yy.min()-3), min(H, yy.max()+4)
    local_domain = domain[y0:y1, x0:x1] | known[y0:y1, x0:x1]
    local_known = known[y0:y1, x0:x1]
    fill = np.broadcast_to(median, (*local_domain.shape, 3)).copy()
    fill[local_known] = colors[y0:y1, x0:x1][local_known]
    kernel = np.array([[0, 1, 0], [1, 0, 1], [0, 1, 0]], np.float32)
    denominator = cv2.filter2D(local_domain.astype(np.float32), -1, kernel, borderType=cv2.BORDER_CONSTANT)
    for _ in range(1000):
        average = cv2.filter2D(fill*local_domain[..., None], -1, kernel, borderType=cv2.BORDER_CONSTANT)/np.maximum(denominator[..., None], 1e-6)
        fill[local_domain & ~local_known] = average[local_domain & ~local_known]
    field = np.broadcast_to(median, (H, W, 3)).copy()
    field[y0:y1, x0:x1][local_domain] = fill[local_domain]
    return {'RGB_linear': field, 'known': known, 'domain': domain, 'median_linear': median,
            'qualified_pixels': int(known.sum()), 'material': target['material']}


def remap(field, sx, sy, nearest=False):
    if sx.size > 30000 and (sx.shape[0] > 30000 or sx.shape[-1] > 30000):
        flat_x, flat_y = sx.reshape(-1), sy.reshape(-1)
        parts = [remap(field, flat_x[i:i+16384][None], flat_y[i:i+16384][None], nearest)[0]
                 for i in range(0, len(flat_x), 16384)]
        merged = np.concatenate(parts, axis=0)
        return merged.reshape((*sx.shape, *field.shape[2:]))
    return cv2.remap(np.asarray(field, np.float32), sx.astype(np.float32), sy.astype(np.float32),
                     cv2.INTER_NEAREST if nearest else cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--packet', required=True)
    parser.add_argument('--receipt', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--radiance-not-albedo', action='store_true', help='Alternative surface-baked lighting branch; preserves observed glints, never PBR albedo')
    args = parser.parse_args()
    packet_path, receipt_path, out = (Path(value).resolve() for value in (args.packet, args.receipt, args.out))
    if not out.is_relative_to(ROOT / 'Textures/Revisions'):
        raise RuntimeError('Task-owned new texture revision required')
    out.mkdir(parents=True, exist_ok=True)
    if (out / 'closed_source_transfer_receipt.json').exists():
        raise RuntimeError('Use a fresh revision; preserve previous CPU transfer evidence')
    data = np.load(packet_path)
    receipt = json.loads(receipt_path.read_text(encoding='utf8'))
    if receipt.get('status', '').startswith('CPU_RECONSTRUCTION'):
        raise RuntimeError('Draft fan packet refused; actual evaluated loop-triangle dump is required')
    if isinstance(receipt['objects'], dict):
        objects = list(receipt['objects'])
        vertex_parts, triangle_parts, uv_parts, owner_parts = [], [], [], []
        offset = 0
        for object_id, name in enumerate(objects):
            if receipt['objects'][name]['uv_layer'] != 'UV0':
                raise RuntimeError('Refusing mislabeled/active diagnostic UV layer: ' + name)
            key = 'closed::' + name + '::'
            points = data[key+'world_vertices']
            tris = data[key+'triangles'].astype(np.int64)
            vertex_parts.append(points)
            triangle_parts.append(tris+offset)
            uv_parts.append(data[key+'UV0_triangles'])
            owner_parts.append(np.full(len(tris), object_id, np.int32))
            offset += len(points)
        vertices, triangles = np.concatenate(vertex_parts), np.concatenate(triangle_parts)
        uv_triangles, tri_object = np.concatenate(uv_parts), np.concatenate(owner_parts)
    else:
        vertices = data['vertices']
        triangles = data['triangles'].astype(np.int64)
        uv_triangles = data['UV0_triangles']
        tri_object = data['tri_object'].astype(np.int32)
        objects = [entry['object'] if isinstance(entry, dict) else entry for entry in receipt['objects']]
    assert len(objects) == 34 and uv_triangles.shape == (len(triangles), 3, 2)
    world_triangles = vertices[triangles]
    depth, owner, source_triangle, source_uv = first_hits(world_triangles, uv_triangles, tri_object)
    np.savez_compressed(out / 'actual_closed_camera_first_hit_UV0.npz', depth_Y=depth, object_id=owner, triangle_id=source_triangle, UV0=source_uv)
    colors = np.random.default_rng(2900).integers(70, 230, (len(objects), 3), np.uint8)
    preview = np.zeros((H, W, 3), np.uint8)
    preview[owner >= 0] = colors[owner[owner >= 0]]
    Image.fromarray(preview).save(out / 'actual_closed_first_hit_owner.png')
    contract = json.loads((PREP / 'closed_look_transfer_contract.json').read_text())
    fields = {record['object']: harmonic_source(record, owner == objects.index(record['object']), args.radiance_not_albedo) for record in contract['targets']}
    fields = {name: field for name, field in fields.items() if field}
    binding = json.loads(BASIS.read_text())
    atlases, known_atlas, visible_atlas, changed_atlas, all_occupied, object_stats = {}, {}, {}, {}, {}, {}
    for material in ('MC_Enamel', 'MC_Gum'):
        original = cv2.imread(binding['materials'][material]['base_color'], cv2.IMREAD_UNCHANGED)
        encoded = original[..., ::-1].astype(np.float32)/(65535 if original.dtype == np.uint16 else 255)
        atlases[material] = np.where(encoded <= .04045, encoded/12.92, ((encoded+.055)/1.055)**2.4)
        if args.radiance_not_albedo:
            # Unobserved objects/backs receive declared material continuation,
            # not an Open albedo/gain field mislabeled as Closed radiance.
            medians = [field['median_linear'] for field in fields.values() if field['material'] == material]
            atlases[material][...] = np.median(medians, axis=0)
        known_atlas[material] = np.zeros((N, N), np.float32)
        visible_atlas[material] = np.zeros((N, N), np.float32)
        changed_atlas[material] = np.zeros((N, N), bool)
        all_occupied[material] = np.zeros((N, N), bool)
    # UV0 raster uses the actual deformed positions and exact loop triangles.
    # It never applies old sourceXY->UV affine transforms after corrective roots.
    xy = uv_triangles * [N, -N] + [0, N]
    for ids, pixels, weights in fragments(xy, N, N, 2048):
        world = np.einsum('ij,ijk->ik', weights, world_triangles[ids])
        source_xy = projected(world)
        sx, sy = source_xy[:, 0]-.5, source_xy[:, 1]-.5
        sampled_owner = remap(owner, sx[None], sy[None], True)[0].astype(np.int32)
        sampled_depth = remap(np.where(np.isfinite(depth), depth, 1e8), sx[None], sy[None], True)[0]
        in_frame = (sx >= 0) & (sx < W-1) & (sy >= 0) & (sy < H-1)
        # Geometry Y can vary within one camera pixel. Owner must match exactly;
        # depth tolerance is bounded to .02world (~1.36sourcepx), with original
        # source semantic uncertainty and contact masks still excluded below.
        visible = in_frame & (sampled_owner == tri_object[ids]) & (np.abs(world[:, 1]-sampled_depth) < .02)
        for object_id in np.unique(tri_object[ids]):
            name = objects[object_id]
            actual_material = 'MC_Gum' if name.endswith('_Gum') else 'MC_Enamel'
            object_selected = tri_object[ids] == object_id
            all_occupied[actual_material].reshape(-1)[pixels[object_selected]] = True
            if name not in fields:
                continue
            field = fields[name]
            material = field['material']
            selected = tri_object[ids] == object_id
            values = np.broadcast_to(field['median_linear'], (int(selected.sum()), 3)).copy()
            valid = visible[selected]
            uv_pixels = pixels[selected]
            xs, ys = sx[selected], sy[selected]
            sampled = remap(field['RGB_linear'], xs[None], ys[None])[0]
            domain = remap(field['domain'], xs[None], ys[None], True)[0] > .5
            known = (remap(field['known'], xs[None], ys[None], True)[0] > .5) & valid
            # UI/glint/unknown locations get an explicitly inferred neutral
            # same-part continuation, never an old Open-camera gain patch.
            values[valid & domain] = sampled[valid & domain]
            atlases[material].reshape(-1, 3)[uv_pixels] = values
            changed_atlas[material].reshape(-1)[uv_pixels] = True
            known_atlas[material].reshape(-1)[uv_pixels] = known
            visible_atlas[material].reshape(-1)[uv_pixels] = valid
            stats = object_stats.setdefault(name, {'UV0_texels': 0, 'qualified_source_texels': 0, 'visible_texels': 0})
            stats['UV0_texels'] += len(uv_pixels)
            stats['qualified_source_texels'] += int(known.sum())
            stats['visible_texels'] += int(valid.sum())
    closed_binding = copy.deepcopy(binding)
    if args.radiance_not_albedo:
        closed_binding = {'status': 'ALTERNATIVE_BAKED_SURFACE_RADIANCE_NOT_ALBEDO_NOT_APPLIED_NOT_ACCEPTED',
            'materials': {}, 'surface_mode': 'BAKED_RADIANCE_ON_REAL_UNIQUE_UV0_MESH',
            'view_contract': {'view_transform': 'Standard', 'look': 'None',
                'texture_encoding': 'sRGB encoding of source linear radiance, exposure0',
                'current_scene_exposure_stops': 0, 'emission_strength': 1.,
                'formula': 'Standard exposure0 and unit radiance strength; camera baked-light branch only, no added PBR lobes. Native glTF unlit uses factor1.'},
            'physical_relighting_supported': False, 'source_character': 'Existing original character retains its PBR material/animation separately.'}
    for material in atlases:
        folder = out / material
        folder.mkdir(exist_ok=True)
        label = 'Closed_Radiance_NOT_ALBEDO' if args.radiance_not_albedo else 'ClosedSourcePigment_LIGHTING_CONFOUNDED'
        path = folder / f'{material}_{label}_16bit.png'
        # New material edges need new same-island mip padding. Preserve all
        # other actual UV islands, including explicitly unobserved fallback.
        distance, labels = cv2.distanceTransformWithLabels((~changed_atlas[material]).astype(np.uint8), cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
        ys, xs = np.where(changed_atlas[material])
        lookup = np.zeros((len(ys)+1, 3), np.float32)
        lookup[1:] = atlases[material][ys, xs]
        padding = (distance <= 7) & ~all_occupied[material]
        atlases[material][padding] = lookup[labels[padding]]
        save_color(path, atlases[material])
        save_scalar(folder / f'{material}_ClosedSourceKnown.png', known_atlas[material])
        save_scalar(folder / f'{material}_ClosedVisibleSupport.png', visible_atlas[material])
        if args.radiance_not_albedo:
            closed_binding['materials'][material] = {'radiance_not_albedo': str(path), 'uv_map': 'UV0', 'colorspace': 'sRGB',
                'emission_strength': 1., 'status': 'CLOSED_BAKED_SURFACE_RADIANCE_NOT_ALBEDO',
                'source_glints_preserved': True, 'real_geometry_and_depth': True,
                'hidden_back_coverage': 'Explicit inferred same-part/material median radiance continuation, not observed source truth.'}
        else:
            closed_binding['materials'][material]['base_color'] = str(path)
            closed_binding['materials'][material]['status'] = 'CLOSED_CPU_SOURCE_PIGMENT_DRAFT_NOT_RECOVERED_ALBEDO'
    if not args.radiance_not_albedo:
        closed_binding['status'] = 'CLOSED_SOURCE_PIGMENT_CPU_TRANSFER_NOT_APPLIED_NOT_ACCEPTED'
    closed_binding['basis_binding'] = str(BASIS)
    closed_binding['limits'] = [('Source-lit surface radiance is deliberately baked for a fixed canonical menu camera; it is NOT physical albedo or relightable PBR.' if args.radiance_not_albedo else 'Source beauty remains lighting-confounded; actual fixed-light Closed calibration is required.'),
        ('Observed source glints are retained as BAKED LIGHTING only; UI is excluded; no glint becomes physical normal/height.' if args.radiance_not_albedo else 'No source glints/UI/character copied into pigment or converted to normal.'),
        'Only exact visible qualified objects receive closed source fields; other hidden objects retain explicit fallback.',
        'Closed source-known masks do not certify inferred back/occluded/cropped colors.',
        'Open maps unchanged. This is not an actual Painter bake or final material export.']
    binding_name = 'material_binding_closed_Radiance_NOT_ALBEDO.json' if args.radiance_not_albedo else 'material_binding_closed_SOURCE_DRAFT.json'
    (out / binding_name).write_text(json.dumps(closed_binding, indent=2), encoding='utf8')
    report = {'status': closed_binding['status'], 'packet': str(packet_path), 'packet_sha256': sha(packet_path),
        'receipt': str(receipt_path), 'receipt_sha256': sha(receipt_path), 'source_contract': str(PREP / 'closed_look_transfer_contract.json'),
        'objects': objects, 'object_stats': object_stats, 'triangles': len(triangles),
        'projection': {'width_world': WORLD_W, 'resolution': [W, H], 'center': [0, 0, 0], 'axis': '-Y -> +Y'},
        'first_hit_camera_pixels': int((owner >= 0).sum()), 'UV0_source_affine_used': False,
        'Open_binding_sha256_unchanged': sha(BASIS), 'DCC_Painter_or_bake_run': False,
        'radiance_not_albedo': args.radiance_not_albedo, 'observed_source_glints_retained': args.radiance_not_albedo}
    if args.radiance_not_albedo:
        white = (np.asarray(Image.open(contract['source']).convert('RGB')) > [235, 200, 185]).all(axis=-1)
        report['observed_highlight_known_pixels'] = {name: int((field['known'] & white).sum()) for name, field in fields.items()}
    (out / 'closed_source_transfer_receipt.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
