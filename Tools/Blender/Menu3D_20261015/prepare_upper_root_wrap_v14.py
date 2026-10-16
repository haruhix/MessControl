"""CPU v14 six-upper hidden-root continuation and source-owned gum wrap.

SourceV13 is immutable. The original v18 evaluated world arrays are the basis,
not an already corrected mesh. This tool fits local tissue wrap to actual
frontmost enamel/tissue depth at source cervical ownership curves. No Blender.
"""
from pathlib import Path
import hashlib
import json
import os

for key in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS'):
    os.environ[key] = '2'
import numpy as np
import cv2
from PIL import Image
cv2.setNumThreads(2)

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
RESPONSE = ROOT / 'Assembly/Review/MaterialResponse_v18'
OUT = ROOT / 'Gums/Diagnostics/v14_UpperRootWrap'
OUT.mkdir(parents=True, exist_ok=True)
SCALE = 16 / 963
MIN_WRAP, MAX_WRAP = -2., 5.


def smooth(t):
    t = np.clip(t, 0, 1)
    return t * t * t * (10 + t * (-15 + 6 * t))


def sample_c2(values, positions):
    p = np.clip(np.asarray(positions, dtype=float), 1, len(values) - 3.001)
    i = p.astype(int)
    t = p - i
    weights = ((1-t)**3/6, (3*t**3-6*t*t+4)/6,
               (-3*t**3+3*t*t+3*t+1)/6, t**3/6)
    return sum(w * values[i+j-1] for j, w in enumerate(weights))


def gaussian(values, sigma):
    return cv2.GaussianBlur(np.asarray(values, dtype=float)[None, :], (0, 0), sigma,
                            borderType=cv2.BORDER_REPLICATE)[0]


def front_depth(tri, source_x, source_y):
    x, z = (source_x / 963 - .5) * 16, (205 - source_y) * SCALE
    a, b, c = tri[:, 0], tri[:, 1], tri[:, 2]
    u, v = b[:, [0, 2]] - a[:, [0, 2]], c[:, [0, 2]] - a[:, [0, 2]]
    d = np.array([x, z]) - a[:, [0, 2]]
    det = u[:, 0] * v[:, 1] - u[:, 1] * v[:, 0]
    good = np.abs(det) > 1e-14
    safe = np.where(good, det, 1.)
    s = (d[:, 0] * v[:, 1] - d[:, 1] * v[:, 0]) / safe
    t = (u[:, 0] * d[:, 1] - u[:, 1] * d[:, 0]) / safe
    inside = good & (s >= -1e-7) & (t >= -1e-7) & (s + t <= 1 + 1e-7)
    if not inside.any():
        return float('inf')
    return float(np.min(a[inside, 1] + s[inside] * (b[inside, 1] - a[inside, 1]) + t[inside] * (c[inside, 1] - a[inside, 1])))


gpath = RESPONSE / 'world_geometry_uv0.npz'
geometry = np.load(gpath)
V, T, O = geometry['vertices'], geometry['triangles'], geometry['tri_object']
names = json.loads((RESPONSE / 'response_manifest.json').read_text())['objects']
uvpath = ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json'
parts = json.loads(uvpath.read_text())['parts'][:6]
gumpath = ROOT / 'Gums/V03/Constraints/source_locked_gums.json'
constraints = json.loads(gumpath.read_text())
margin_values = np.array(constraints['upper_margin_y_by_x'])
old_margin = lambda positions: sample_c2(margin_values, positions)
arcs = {a['crown']: a for a in constraints['upper_neck_ownership_intervals']}
gum_name = 'MC_DAY03_Upper_Gum'
gum_triangles = T[O == names.index(gum_name)]
gum_indices = np.unique(gum_triangles)
staged = V.copy()
arrays, source_curves, selected = {}, {}, {}
for part in parts:
    name, semantic = part['object'], part['part']
    indices = np.unique(T[O == names.index(name)])
    selected[name] = indices
    before = V[indices]
    px, py = (before[:, 0] / 16 + .5) * 963, 205 - before[:, 2] / SCALE
    q = py - old_margin(px)
    arc = arcs[semantic]
    # Visible arc ends are occlusion points, not physical root endpoints. Put
    # the2px C2fade3px outside that observed span; X coordinates stay fixed.
    wx = smooth((px - (arc['x0'] - 3.)) / 2.) * smooth(((arc['x1'] + 3.) - px) / 2.)
    # Two observed corner constraints require less hidden-UP growth; blend the
    # amplitude physically over6px rather than demand a tall tissue curtain.
    if semantic == 'Upper_Lateral_L':
        wx *= .5 + .5 * smooth((px - arc['x0']) / 6.)
    elif semantic == 'Upper_Lateral_R':
        wx *= .5 + .5 * smooth((arc['x1'] - px) / 6.)
    delta_py = -6. * smooth((12. - q) / 11.) * wx
    after = before.copy()
    after[:, 2] -= delta_py * SCALE
    staged[indices] = after
    arrays[name + '__before'], arrays[name + '__after'] = before, after
    mask = np.asarray(Image.open(part['source_visible_front_mask']).convert('L')) > 0
    curve = []
    for x in range(arc['x0'] + 1, arc['x1']):
        ys = np.where(mask[:, x])[0]
        if len(ys):
            curve.append([x, int(ys.min())])
    source_curves[name] = curve

gum_before = V[gum_indices]
gum_px = (gum_before[:, 0] / 16 + .5) * 963
gum_py = 205 - gum_before[:, 2] / SCALE
gum_q = gum_py - old_margin(gum_px)
gum_weight = smooth((12. + gum_q) / 12.)
raw_field = np.full(963, np.nan)
constraints_fit = []
tri_root = {name: staged[T[O == names.index(name)]] for name in source_curves}
tri_gum_original = V[gum_triangles]
tri_gum_weight = np.zeros(len(V))
tri_gum_weight[gum_indices] = gum_weight
gum_face_weight = tri_gum_weight[gum_triangles]

# For each observed cervical column, solve the local projected solid crossing.
# Tissue scalar is inferred depth/geometry, never a color or confidence mask.
for name, curve in source_curves.items():
    tooth_tri = tri_root[name]
    for x, target_y in curve:
        wx = ((x + .5) / 963 - .5) * 16
        tooth_cut = tooth_tri[(tooth_tri[:, :, 0].min(axis=1) <= wx) & (tooth_tri[:, :, 0].max(axis=1) >= wx)]
        gum_keep = (tri_gum_original[:, :, 0].min(axis=1) <= wx) & (tri_gum_original[:, :, 0].max(axis=1) >= wx)
        gum_cut, weight_cut = tri_gum_original[gum_keep], gum_face_weight[gum_keep]
        tooth_y = front_depth(tooth_cut, x + .5, target_y)
        if not np.isfinite(tooth_y):
            constraints_fit.append({'object': name, 'x': x, 'target_y': target_y, 'status': 'NO_TOOTH_AT_TARGET'})
            continue
        def difference(wrap):
            tri = gum_cut.copy()
            tri[:, :, 2] -= wrap * weight_cut * SCALE
            return front_depth(tri, x + .5, target_y) - tooth_y
        lo, hi = MIN_WRAP, MAX_WRAP
        flo, fhi = difference(lo), difference(hi)
        if flo < 0:
            value, status = MIN_WRAP, 'LOWER_BOUND_GUM_ALREADY_FRONT'
        elif fhi > 0:
            value, status = MAX_WRAP, 'UPPER_BOUND_TOOTH_STILL_FRONT'
        else:
            for _ in range(20):
                mid = (lo + hi) / 2
                if difference(mid) > 0:
                    lo = mid
                else:
                    hi = mid
            value, status = (lo + hi) / 2, 'SOLVED_LOCAL_CROSSING'
        raw_field[x] = value
        constraints_fit.append({'object': name, 'x': x, 'target_y': target_y, 'wrap_source_px': value, 'status': status})

known = np.where(np.isfinite(raw_field))[0]
xx = np.arange(963)
continuous = np.interp(xx, known, raw_field[known])
continuous = gaussian(continuous, 2.)
# Only the six-upper cervical zone is changed, with a broad smooth outside fade.
outside = smooth((xx - 223.) / 12.) * smooth((739. - xx) / 12.)
continuous *= outside


def apply_gum(values):
    wrap = np.clip(sample_c2(values, gum_px), MIN_WRAP, MAX_WRAP)
    after = gum_before.copy()
    after[:, 2] -= wrap * gum_weight * SCALE
    staged[gum_indices] = after
    return after


def ownership_errors():
    gum_tri = staged[gum_triangles]
    records = []
    for name, curve in source_curves.items():
        tooth_tri = tri_root[name]
        for x, target_y in curve:
            wx = ((x + .5) / 963 - .5) * 16
            tc = tooth_tri[(tooth_tri[:, :, 0].min(axis=1) <= wx) & (tooth_tri[:, :, 0].max(axis=1) >= wx)]
            gc = gum_tri[(gum_tri[:, :, 0].min(axis=1) <= wx) & (gum_tri[:, :, 0].max(axis=1) >= wx)]
            first = None
            crack = []
            for y in range(target_y - 3, target_y + 5):
                ty, gy = front_depth(tc, x + .5, y + .5), front_depth(gc, x + .5, y + .5)
                if not np.isfinite(ty) and not np.isfinite(gy):
                    crack.append(y)
                if ty < gy and first is None:
                    first = y
            records.append({'object': name, 'x': x, 'source_first_enamel_y': target_y,
                             'compound_first_enamel_y': first, 'error': first - target_y if first is not None else None,
                             'no_pair_surface_rows': crack})
    return records


iterations = []
for iteration in range(4):
    gum_after = apply_gum(continuous)
    records = ownership_errors()
    bad = [r for r in records if r['error'] is None or abs(r['error']) > 1]
    iterations.append({'iteration': iteration, 'columns': len(records), 'outside1px': len(bad),
                        'max_abs_error': max(abs(r['error']) for r in records if r['error'] is not None)})
    print('UPPER_ROOT_CPU_OWNERSHIP', iterations[-1], flush=True)
    if not bad:
        break
    correction = np.zeros(963)
    present = np.zeros(963)
    for r in records:
        if r['error'] is not None:
            # Later enamel means excessive tissue wrap. No global gum shift.
            correction[r['x']] = -1.2 * r['error']
            present[r['x']] = 1.
    correction = gaussian(correction, 1.2) / np.maximum(gaussian(present, 1.2), .1)
    continuous = np.clip(continuous + correction, MIN_WRAP, MAX_WRAP) * outside

gum_after = apply_gum(continuous)
records = ownership_errors()
arrays[gum_name + '__before'], arrays[gum_name + '__after'] = gum_before, gum_after
changes = []
for name in [p['object'] for p in parts] + [gum_name]:
    triangles = T[O == names.index(name)]
    before_n = np.cross(V[triangles[:, 1]] - V[triangles[:, 0]], V[triangles[:, 2]] - V[triangles[:, 0]])
    after_n = np.cross(staged[triangles[:, 1]] - staged[triangles[:, 0]], staged[triangles[:, 2]] - staged[triangles[:, 0]])
    area0, area1 = np.linalg.norm(before_n, axis=1), np.linalg.norm(after_n, axis=1)
    dot = np.sum(before_n * after_n, axis=1) / np.maximum(area0 * area1, 1e-30)
    b, a = arrays[name + '__before'], arrays[name + '__after']
    changes.append({'object': name, 'vertices': len(a), 'triangles': len(triangles),
                    'max_world_Z_change': float(np.max(np.abs(a[:, 2] - b[:, 2]))),
                    'world_XY_byte_identical': np.array_equal(a[:, :2], b[:, :2]),
                    'zero_area_triangles': int((area1 < 1e-14).sum()),
                    'negative_normal_dot_count': int((dot < 0).sum()),
                    'minimum_area_ratio': float(np.min(area1 / np.maximum(area0, 1e-30)))})
np.savez_compressed(OUT / 'upper6_root_wrap_v14_coordinates.npz', **arrays)
bad = [r for r in records if r['error'] is None or abs(r['error']) > 1]
report = {'version': 'v14', 'status': 'CPU_SIX_UPPER_FEASIBILITY_NOT_DCC_OR_ART_ACCEPTED',
          'original_basis': str(ROOT / 'Assembly/MC_Menu3D_Candidate_Combined_full32_textured_v18.blend'),
          'original_basis_sha256': '0c5c30cd6782e44e5d9e355f117c066cd0887dc06a38e739af97cf246affb46a',
          'source_v13_uv_manifest': str(uvpath), 'source_v13_uv_manifest_sha256': hashlib.sha256(uvpath.read_bytes()).hexdigest(),
          'source_geometry_npz': str(gpath), 'source_geometry_npz_sha256': hashlib.sha256(gpath.read_bytes()).hexdigest(),
          'coordinate_stage': str(OUT / 'upper6_root_wrap_v14_coordinates.npz'),
          'root_helper': 'dSourceY=-6*S((12-q)/11)*S((x-arc0+3)/2)*S((arc1+3-x)/2); q uses original GumsV03 C2 margin. C2fade support extends3px outside observed ownership span; physical X unchanged. LateralL-left andLateralR-right root amplitude blends3→6px over6px byC2smootherstep, source-owned local corner corrections.',
          'gum_helper': 'dSourceY=wrap(x)*S((12+q)/12), wrap−2..+5px fitted to source-owned first-hit crossing, sigma2px smooth + C2 interpolation. Negative wrap is a localized withdrawal where oldV09gumguide occludes currentV13observed enamel; no whole-gum shift.',
          'old_corner567': next((r for r in records if r['object'] == 'MC_DAY03_Upper_Central_R' and r['x'] == 567), None),
          'changes': changes, 'local_crossing_solver': constraints_fit, 'wrap_field_source_px': continuous.tolist(),
          'ownership_iterations': iterations, 'compound_ownership_columns': records,
          'outside1px_columns': bad, 'CPU_source_ownership_within1px_pass': not bad,
          'UV_provenance': {'arrays': 'UNCHANGED', 'worldXZ_to_UV0_affine_receipt': 'STALE',
                             'UV_SourceProjection': 'STALE_DIAGNOSTIC', 'new_mesh_map_bake': False},
          'closed_endpoint_contract': 'Use separately authored absolute closed-world roots from original closed endpoint arrays, inverse exact parent, then ClosedCorrective=newClosedLocal-newOpenBasis. Never stack open delta into closed target.',
          'limitations': ['CPU two-surface first-hit fit excludes unchanged neighboring crowns; actual full evaluated-scene BVH and PBR/gray proof remain mandatory.',
                         'Source edge as first white-mask row has pixel uncertainty; this does not prove final1:1 anatomy or materials.',
                         'Topology/UV unchanged does not authorize stale source-affine or texture-bake claims.']}
(OUT / 'upper6_root_wrap_v14_preparation.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps({'changes': changes, 'outside1px': bad, 'corner567': report['old_corner567']}, indent=2))
