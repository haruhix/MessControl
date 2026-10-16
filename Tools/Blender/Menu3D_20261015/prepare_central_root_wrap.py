"""CPU-only CentralR hidden-root / gingival wrap coordinate prototype.

No Blender is launched. Source-evaluated topology and UV ownership remain fixed;
new coordinates are a diagnostic stage, never written into original assets.
"""
from pathlib import Path
import argparse
import hashlib
import json

import numpy as np

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
RESPONSE = ROOT / 'Assembly/Review/MaterialResponse_v18'
OUT = ROOT / 'Gums/Diagnostics/v18_CentralRRootWrap'


def smooth(t):
    t = np.clip(t, 0., 1.)
    return t * t * t * (10 + t * (-15 + 6 * t))


def intersections(vertices, triangles, world_x, world_z=None):
    tri = vertices[triangles]
    keep = (tri[:, :, 0].min(axis=1) <= world_x) & (tri[:, :, 0].max(axis=1) >= world_x)
    tri = tri[keep]
    segments = []
    for points in tri:
        hits = []
        for a, b in ((points[0], points[1]), (points[1], points[2]), (points[2], points[0])):
            if abs(b[0] - a[0]) < 1e-12:
                continue
            t = (world_x - a[0]) / (b[0] - a[0])
            if -1e-8 <= t <= 1 + 1e-8:
                hit = (a + t * (b - a))[1:]
                if not any(np.linalg.norm(hit - old) < 1e-8 for old in hits):
                    hits.append(hit)
        if len(hits) != 2:
            continue
        normal = np.cross(points[1] - points[0], points[2] - points[0])
        normal /= max(np.linalg.norm(normal), 1e-15)
        segments.append((hits, normal))
    if world_z is None:
        return segments
    found = []
    for (a, b), normal in segments:
        if abs(b[1] - a[1]) < 1e-12:
            continue
        t = (world_z - a[1]) / (b[1] - a[1])
        if -1e-7 <= t <= 1 + 1e-7:
            found.append({'Y': float(a[0] + t * (b[0] - a[0])), 'normal_xyz': normal.tolist()})
    return sorted(found, key=lambda hit: hit['Y'])


parser = argparse.ArgumentParser()
parser.add_argument('--root-px', type=float, default=6.)
parser.add_argument('--wrap-px', type=float, default=3.)
args = parser.parse_args()
if not (0 < args.root_px <= 8 and 0 < args.wrap_px <= 5):
    raise RuntimeError('Diagnostic bounds: root0..8sourcepx, wrap0..5sourcepx')
OUT.mkdir(parents=True, exist_ok=True)
geometry_path = RESPONSE / 'world_geometry_uv0.npz'
geometry = np.load(geometry_path)
V, T, O = geometry['vertices'], geometry['triangles'], geometry['tri_object']
names = json.loads((RESPONSE / 'response_manifest.json').read_text())['objects']
manifest_path = ROOT / 'Gums/V03/Constraints/source_locked_gums.json'
constraints = json.loads(manifest_path.read_text())
source_margin = np.asarray(constraints['upper_margin_y_by_x'])
cervical = next(arc for arc in constraints['upper_neck_ownership_intervals'] if arc['crown'] == 'Upper_Central_R')
xmin, xmax = cervical['x0'], cervical['x1']
staged = V.copy()
arrays, changes, selected = {}, [], {}
for name in ('MC_DAY03_Upper_Central_R', 'MC_DAY03_Upper_Gum'):
    triangles = T[O == names.index(name)]
    indices = np.unique(triangles)
    before = V[indices].copy()
    px = (before[:, 0] / 16 + .5) * 963
    py = 205 - before[:, 2] * 963 / 16
    edge = np.interp(px, np.arange(963), source_margin)
    q = py - edge
    # C2 X window: full near center, zero outside the selected cervical arc.
    wx = smooth((px - xmin) / 8.) * smooth((xmax - px) / 8.)
    if name.endswith('Central_R'):
        # The old zero-thickness rim moves hiddenUP. Interior coronal rings
        # become the actual visible cervical surface instead of a solid cap.
        dpy = -args.root_px * smooth((12. - q) / 11.) * wx
    else:
        # Physical tissue rim moves below the visible ownership curve. Its
        # new termination must be hidden by the continued crown volume.
        dpy = args.wrap_px * smooth((12. + q) / 12.) * wx
    after = before.copy()
    after[:, 2] -= dpy * 16 / 963
    staged[indices] = after
    arrays[name + '__before'] = before
    arrays[name + '__after'] = after
    selected[name] = triangles
    normals0 = np.cross(V[triangles[:, 1]] - V[triangles[:, 0]], V[triangles[:, 2]] - V[triangles[:, 0]])
    normals1 = np.cross(staged[triangles[:, 1]] - staged[triangles[:, 0]], staged[triangles[:, 2]] - staged[triangles[:, 0]])
    area0, area1 = np.linalg.norm(normals0, axis=1), np.linalg.norm(normals1, axis=1)
    dot = np.sum(normals0 * normals1, axis=1) / np.maximum(area0 * area1, 1e-30)
    changes.append({'object': name, 'vertices': len(indices), 'triangles': len(triangles),
                    'changed_vertices': int((np.linalg.norm(after - before, axis=1) > 1e-10).sum()),
                    'max_abs_world_Z_change': float(np.max(np.abs(after[:, 2] - before[:, 2]))),
                    'world_XY_byte_identical': np.array_equal(before[:, :2], after[:, :2]),
                    'min_area_ratio': float(np.min(area1 / np.maximum(area0, 1e-30))),
                    'normal_dot_p01': float(np.percentile(dot, 1)),
                    'negative_normal_dot_count': int((dot < 0).sum()),
                    'zero_area_triangles': int((area1 < 1e-14).sum()),
                    'vertex_order': 'sorted global indices from actual response triangles; verified by world-coordinate exact match before DCC application'})
np.savez_compressed(OUT / 'centralR_root_wrap_coordinates.npz', **arrays)
sections = []
for px in (500., 510., 525., 540., 550.):
    world_x = (px / 963 - .5) * 16
    neck_py = float(np.interp(px, np.arange(963), source_margin))
    row = {'source_x': px, 'source_cervical_y': neck_py, 'samples': []}
    for offset in (-2., -1., -.5, 0., .5, 1., 2., 3., 4.):
        z = (205 - neck_py - offset) * 16 / 963
        hits = {}
        for label, name in [('tooth', 'MC_DAY03_Upper_Central_R'), ('gum', 'MC_DAY03_Upper_Gum')]:
            result = intersections(staged, selected[name], world_x, z)
            hits[label] = result[0] if result else None
        visible = min(((key, hit) for key, hit in hits.items() if hit), key=lambda item: item[1]['Y'], default=None)
        row['samples'].append({'offset_source_y': offset, **hits,
                               'frontmost': visible[0] if visible else None,
                               'front_Y_gap_gum_minus_tooth': hits['gum']['Y'] - hits['tooth']['Y'] if all(hits.values()) else None})
    sections.append(row)
report = {'status': 'CPU_COORDINATE_PROTOTYPE_NOT_DCC_EXECUTED_OR_ACCEPTED',
          'basis_candidate': str(ROOT / 'Assembly/MC_Menu3D_Candidate_Combined_full32_textured_v18.blend'),
          'basis_candidate_sha256': '0c5c30cd6782e44e5d9e355f117c066cd0887dc06a38e739af97cf246affb46a',
          'geometry_npz': str(geometry_path), 'geometry_npz_sha256': hashlib.sha256(geometry_path.read_bytes()).hexdigest(),
          'source_margin_constraints': str(manifest_path), 'source_margin_constraints_sha256': hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
          'parameters': {'hidden_root_up_source_px': args.root_px, 'physical_gum_wrap_down_source_px': args.wrap_px,
                         'cervical_falloff_source_px': 12, 'C2_x_fade_source_px': 8, 'cervical_x_interval': [xmin, xmax]},
          'changes': changes, 'cpu_joint_sections': sections,
          'preserved': ['X and Y coordinates', 'Topology and closed-solid connectivity', 'All UV arrays',
                        'Materials/textures', 'Existing upper-jaw object parents and original source files'],
          'gates_required': ['Actual compound source-camera ownership rather than isolated crown silhouette.',
                             'Pixel525,26 must hit enamel or gingiva, never interior.',
                             'Source cervical visible enamel boundary within1px; no neighboring-crown reveal.',
                             'Actual native PBR and constant.18 BC contact/normal proof.',
                             'No dark collar compensation by albedo/SSS/emission.',
                             'Closed prototype must author equivalent hidden-root continuation in its own fresh domain; do not copy an open pose delta blindly.'],
          'limitations': ['This bounded topology-preserving coordinate prototype may fail actual occlusion or collar shading; it is a hypothesis, not a repair claim.',
                         'UV connectivity is unchanged but coordinates move; native PBR pigment can stretch in the small diagnostic cervical band. Gray control separates this confound.',
                         'A final accepted hidden root needs explicit inferred back/root UV coverage and a new mesh-map bake.']}
(OUT / 'cpu_root_wrap_preparation.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps({'status': report['status'], 'changes': changes,
                  'center_samples': next(r for r in sections if r['source_x'] == 525)}, indent=2))
