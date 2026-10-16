"""CPU source-locked DAY02 gingiva from actual23 crown cervical supports.

No Blender invocation. Closed physical-X/Z volumes and independent UV0 are
prepared from observed row ownership; unprovided Open/depth/returns inferred.
"""
from pathlib import Path
import os
for name in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS'):
    os.environ[name] = '2'
import argparse
import collections
import hashlib
import importlib.util
import json
import time
import cv2
import numpy as np
from PIL import Image
cv2.setNumThreads(2)
AR = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
SOURCE = AR/'Evidence/SourceAnalysis/DAY02_Closed'
JAWS = AR/'Jaws/Family_DAY02/SourceClosed_v01'
OUT = AR/'Gums/DAY02/SourceClosed_v01'
parser = argparse.ArgumentParser()
parser.add_argument('--support', default=str(JAWS/'DAY02_actual_cervical_front_supports.json'))
opt = parser.parse_args()
support_path = Path(opt.support).resolve()
if not support_path.is_file():
    raise RuntimeError('Exact actual DAY02 row-specific cervical-ray handoff is required before volume preparation')
support = json.loads(support_path.read_text(encoding='utf8'))
spec = importlib.util.spec_from_file_location('physicalhelpers', AR/'Gums/ClosedPosePrototype/prepare_closed_depth_refit.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
inventory = json.loads((SOURCE/'part_inventory.json').read_text(encoding='utf8'))
vectors = json.loads((SOURCE/'VectorContract_v01/vector_contract.json').read_text(encoding='utf8'))
candidate = json.loads((JAWS/'actual_source_candidate_report.json').read_text(encoding='utf8'))
packet_path = Path(candidate['actual_closed_source_appearance_packet'])
packet_sha = hashlib.sha256(packet_path.read_bytes()).hexdigest()
if packet_sha != candidate['actual_closed_source_appearance_packet_sha256'] or packet_sha != support['packet_sha256']:
    raise RuntimeError('Actual DAY02 cervical and geometry packet SHA disagree')
packet = np.load(packet_path)
guides = np.load(OUT/'Evidence/source_closed_boundary_guides.npz')
CON = OUT/'Constraints'
CON.mkdir(exist_ok=True)
WIDTH, HEIGHT, CENTER = 963, 217, 108.5
SCALE, STEP = 16/963, .04
xx = np.arange(WIDTH)
RECTS = {'upper': {'front': (.025, .025, .975, .235), 'back': (.025, .500, .975, .710)},
         'lower': {'front': (.025, .260, .975, .470), 'back': (.025, .735, .975, .945)}}
xs = set(np.arange(0, WIDTH, 2).tolist()+[962., -96.3, -60., 1023., 1059.3])
for part in vectors['parts']:
    xs.update(float(point[0]) for point in part['cervical_occlusion_path_xy'])
# A 2px regular grid cannot reproduce a narrow interproximal pixel-cell
# corner. Add the measured centers around steep source changes, retaining
# smooth broad regions at the existing density rather than widening a notch.
critical_columns = set()
for row in ('upper', 'lower'):
    source_curve = guides[row+'_source_margin_y']
    slope = np.maximum(abs(np.diff(source_curve, prepend=source_curve[0])), abs(np.diff(source_curve, append=source_curve[-1])))
    for index in np.flatnonzero(slope > 1):
        critical_columns.update(range(max(0, int(index)-1), min(WIDTH, int(index)+2)))
xs.update(index+.5 for index in critical_columns)
xs = np.array(sorted(xs))
n = len(xs)
T = np.array([0, .001, .004, .012, .030, .065, .12, .20, .30, .42, .55, .68, .80, .90, .96, .99, 1.])
rows = len(T)
meshes, reports = {}, []


def smooth(value):
    value = np.clip(value, 0, 1)
    return value**3*(10+value*(-15+6*value))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def project(vertices):
    return np.column_stack(((vertices[:, 0]/16+.5)*WIDTH, CENTER-vertices[:, 2]/SCALE))


def row_samples(row):
    sample_x, sample_y = [], []
    for arc in support['arcs']:
        arc_row = arc.get('row', 'upper' if '_U' in arc['part'] else 'lower')
        if arc_row != row:
            continue
        for sample in arc['actual_camera_ray_neck_samples']:
            point = sample.get('source_cervical_enamel_xy', sample.get('source_cervical_xy', sample.get('source_first_enamel_xy', sample.get('source_last_enamel_xy'))))
            if point is None:
                raise RuntimeError('Cervical handoff needs explicit row-specific source pixel sample')
            x, y = point
            if abs(float(y)-guides[row+'_source_margin_y'][int(x)]) > 1:
                # A particular crown's steep flank may overlap its neighbor;
                # it is not the globally observed row-to-flesh boundary.
                continue
            sample_x.append(float(x)+.5)
            sample_y.append(sample['actual_front_worldXYZ'][1])
    if not sample_x:
        raise RuntimeError('No actual cervical samples for '+row)
    order = np.argsort(sample_x)
    return np.asarray(sample_x)[order], np.asarray(sample_y)[order]


def make_body(row):
    started = time.time()
    upper = row == 'upper'
    raw = guides[row+'_source_margin_y']
    # Raw masks record pixel INDICES. The top pixel begins at first_y;
    # the bottom pixel ends at last_y+1. Camera rays use x+.5,y+.5.
    # This is a half-open pixel-cell convention, not a conservative pad offset.
    pixel_boundary = raw + (0. if upper else 1.)
    margin = cv2.GaussianBlur(pixel_boundary.reshape(1, -1), (0, 0), 1.).ravel()
    margin = np.clip(margin, pixel_boundary-.75, pixel_boundary+.75) + (.10 if upper else -.10)
    sample_x, sample_y = row_samples(row)
    interpolated = np.interp(xx, sample_x, sample_y)
    smoothed_y = cv2.GaussianBlur(interpolated.reshape(1, -1), (0, 0), 8.).ravel()
    x = (xs/WIDTH-.5)*16
    z0 = (CENTER-np.interp(xs, xx+.5, margin))*SCALE
    outer = (4.7 if upper else -4.7) + (.08 if upper else -.08)*(x/9.6)**2
    front = np.stack([np.column_stack((x, np.zeros(n), z0*(1-t)+outer*t)) for t in T]).reshape(-1, 3)
    front_count = len(front)
    boundary = list(range(n))+[j*n+n-1 for j in range(1, rows)]+list(range((rows-1)*n+n-2, (rows-1)*n-1, -1))+[j*n for j in range(rows-2, 0, -1)]
    rim0 = np.interp(xs, xx, smoothed_y)
    exterior = np.maximum(-xs, xs-962)
    fade = smooth(exterior/96)
    rim0 = rim0*(1-fade)+(1.3+.15*(x/9.6)**2)*fade
    rim1 = 1.2+.25*(x/9.6)**2
    front[:, 1] = np.stack([rim0*(1-t)+rim1*t for t in T]).ravel()
    polygon, rim = front[boundary][:, [0, 2]]/STEP, front[boundary, 1]
    fields, solves = helper.fields(polygon, rim)
    points = front[:, [0, 2]]/STEP
    distance, segment, t, near = helper.samples(polygon, rim, points)
    following = (segment+1) % len(polygon)
    weight = smooth((distance-1)/3)
    u_slope = fields['u_slope'][segment]*(1-t)+fields['u_slope'][following]*t
    y_slope = fields['y_slope'][segment]*(1-t)+fields['y_slope'][following]*t
    u = np.maximum(0, distance*u_slope*(1-weight)+helper.helper.sample_c2(fields['depth'], points, fields['origin'])*weight)
    baseline = (near+distance*y_slope)*(1-weight)+helper.helper.sample_c2(fields['baseline'], points, fields['origin'])*weight
    amplitude = .75 if upper else .70
    front[:, 1] = baseline-amplitude*np.sqrt(u)
    front[boundary, 1] = rim
    vertices = front.tolist()
    front_grid = np.arange(front_count).reshape(rows, n)
    back_grid = front_grid.copy()
    for j in range(1, rows-1):
        for i in range(1, n-1):
            k = j*n+i
            back_grid[j, i] = len(vertices)
            vertex = front[k].copy()
            vertex[1] = baseline[k]+1.4*np.sqrt(u[k])
            vertices.append(vertex.tolist())
    vertices = np.asarray(vertices, float)
    faces, UV0 = [], []
    for back, grid in ((False, front_grid), (True, back_grid)):
        rect = RECTS[row]['back' if back else 'front']
        for j in range(rows-1):
            for i in range(n-1):
                face = [grid[j, i], grid[j, i+1], grid[j+1, i+1], grid[j+1, i]]
                ua, ub = (xs[i]-xs[0])/(xs[-1]-xs[0]), (xs[i+1]-xs[0])/(xs[-1]-xs[0])
                corners = [(ua, T[j]), (ub, T[j]), (ub, T[j+1]), (ua, T[j+1])]
                uv = [[rect[0]+a*(rect[2]-rect[0]), rect[1]+b*(rect[3]-rect[1])] for a, b in corners]
                if back:
                    face, uv = face[::-1], uv[::-1]
                faces.append(face)
                UV0.append(uv)
    faces, UV0 = np.asarray(faces, np.int32), np.asarray(UV0, np.float32)
    triangles = np.vstack((faces[:, [0, 1, 2]], faces[:, [0, 2, 3]]))
    a, b, c = (vertices[triangles[:, k]] for k in range(3))
    volume = float(np.einsum('ij,ij->i', a, np.cross(b, c)).sum()/6)
    if volume < 0:
        faces, UV0 = faces[:, ::-1].copy(), UV0[:, ::-1].copy()
        triangles = np.vstack((faces[:, [0, 1, 2]], faces[:, [0, 2, 3]]))
        volume = -volume
    areas = np.linalg.norm(np.cross(vertices[triangles[:, 1]]-vertices[triangles[:, 0]], vertices[triangles[:, 2]]-vertices[triangles[:, 0]]), axis=1)/2
    edges = collections.Counter(tuple(sorted((int(a), int(b)))) for face in faces for a, b in zip(face, np.roll(face, -1)))
    nonmanifold = sum(value != 2 for value in edges.values())
    if nonmanifold or np.any(areas < 1e-10) or not np.isfinite(vertices).all() or volume <= 0:
        raise RuntimeError('Prepared DAY02 physical gum solid invalid: '+row)
    basis = vertices.copy()
    basis[:, 2] += 1.65 if upper else -1.65
    projection = np.column_stack((vertices[:, 0]/16+.5, .5+vertices[:, 2]/(HEIGHT*SCALE))).astype(np.float32)
    result = {'closed_world': vertices, 'basis_open': basis, 'faces': faces, 'UV0': UV0,
              'UV_SourceProjection_Closed': projection, 'triangles': triangles}
    meshes[row] = result
    path = CON/(row+'_surface.npz')
    np.savez_compressed(path, **result)
    reports.append({'body': row, 'vertices': len(vertices), 'triangles': len(triangles),
        'positive_volume': volume, 'zero_area_triangles': int(np.count_nonzero(areas < 1e-10)),
        'nonmanifold_edges': nonmanifold, 'minimum_triangle_area': float(areas.min()),
        'closed_bounds': [vertices.min(0).tolist(), vertices.max(0).tolist()],
        'actual_cervical_depth_samples': len(sample_x), 'inferred_rim_Y_sigma_source_px': 8,
        'rimY_vs_actual_crown_depth_max': float(abs(np.interp(sample_x, xx, smoothed_y)-sample_y).max()),
        'source_margin_regularization_max_px': float(abs(margin-pixel_boundary).max()),
        'source_pixel_boundary_policy': 'Row margin sampled at x+.5; upperfirst_y, lowerlast_y+1 bound half-open foreground pixel cells; no arbitrary conservative offset',
        'source_missing_margin_columns_inferred': int(np.count_nonzero(~guides[row+'_source_margin_observed'])),
        'physical_grid_world': STEP, 'front_amplitude_world': amplitude, 'back_amplitude_world': 1.4,
        'UV0_rects': RECTS[row], 'constraint_path': str(path), 'constraints_sha256': sha(path),
        'field_solves': solves, 'elapsed_seconds': time.time()-started})
    print('DAY02_PHYSICAL_GUM_PREPARED', json.dumps(reports[-1]), flush=True)


for row in ('upper', 'lower'):
    make_body(row)

depth = np.full((HEIGHT, WIDTH), np.inf)
owners = np.full((HEIGHT, WIDTH), -1, np.int16)
names = []


def raster(vertices, triangles, name):
    index = len(names)
    names.append(name)
    xy = project(vertices)
    for ids in triangles:
        a, b, c = xy[ids]
        u, v = b-a, c-a
        det = u[0]*v[1]-u[1]*v[0]
        if abs(det) < 1e-14:
            continue
        lo = np.maximum([0, 0], np.floor(xy[ids].min(0)).astype(int))
        hi = np.minimum([WIDTH-1, HEIGHT-1], np.ceil(xy[ids].max(0)).astype(int))
        if np.any(hi < lo):
            continue
        yy, pixelx = np.mgrid[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
        dx, dy = pixelx+.5-a[0], yy+.5-a[1]
        s, t = (dx*v[1]-dy*v[0])/det, (u[0]*dy-u[1]*dx)/det
        inside = (s >= -1e-7) & (t >= -1e-7) & (s+t <= 1+1e-7)
        y = vertices[ids[0], 1]+s*(vertices[ids[1], 1]-vertices[ids[0], 1])+t*(vertices[ids[2], 1]-vertices[ids[0], 1])
        region = np.s_[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
        hit = inside & (y < depth[region])
        depth[region][hit], owners[region][hit] = y[hit], index


for part in inventory['parts']:
    name = 'MC_'+part['name']
    raster(packet[name+'__world_vertices'], packet[name+'__triangle_vertex_indices'], name)
bare_owners = owners.copy()
for row in ('upper', 'lower'):
    raster(meshes[row]['closed_world'], meshes[row]['triangles'], 'MC_DAY02_'+row.title()+'_Gum')
np.savez_compressed(OUT/'DAY02_CPU_closed_camera_firsthits.npz', object_id=owners, depth_Y=depth)
ui = np.zeros((HEIGHT, WIDTH), bool)
x0, y0, x1, y1 = inventory['label_occlusion_bbox_xyxy']
ui[y0:y1, x0:x1] = True
source_white = np.zeros_like(ui)
for part in inventory['parts']:
    source_white |= np.asarray(Image.open(SOURCE/part['crown_mask']).convert('L')) > 0
actual_white = (owners >= 0) & (owners < 23)
IoU = float((source_white & actual_white & ~ui).sum()/max(1, ((source_white | actual_white) & ~ui).sum()))
cervical = {}
for row in ('upper', 'lower'):
    indices = [names.index('MC_'+part['name']) for part in inventory['parts'] if part['row'] == row]
    row_white = np.isin(owners, indices)
    columns = []
    raw, observed = guides[row+'_source_margin_y'], guides[row+'_source_margin_observed']
    for x in range(WIDTH):
        ys = np.flatnonzero(row_white[:, x])
        actual = int(ys[0] if row == 'upper' else ys[-1]) if len(ys) else None
        bare_y = np.flatnonzero(np.isin(bare_owners[:, x], indices))
        bare = int(bare_y[0] if row == 'upper' else bare_y[-1]) if len(bare_y) else None
        columns.append({'x': x, 'source_observed': bool(observed[x]), 'source_cervical_y': float(raw[x]),
                        'bare_crown_cervical_y': bare, 'bare_error': bare-float(raw[x]) if bare is not None else None,
                        'prepared_cervical_y': actual, 'error': actual-float(raw[x]) if actual is not None else None,
                        'gum_owner_changed_row_margin': actual != bare})
    bad = [q for q in columns if q['source_observed'] and (q['error'] is None or abs(q['error']) > 1)]
    gum_bad = [q for q in bad if q['bare_error'] is not None and abs(q['bare_error']) <= 1]
    source_slope = np.maximum(abs(np.diff(raw, prepend=raw[0])), abs(np.diff(raw, append=raw[-1])))
    core = [q for q, slope in zip(columns, source_slope) if q['source_observed'] and slope <= 1]
    core_bad = [q for q in core if q['error'] is None or abs(q['error']) > 1]
    cervical[row] = {'observed_columns': int(observed.sum()), 'within1px': int(observed.sum())-len(bad),
                     'outside1px': bad, 'all_columns': columns,
                     'gum_introduced_outside1px': gum_bad,
                     'both_adjacent_source_slopes_le1_core': {'definition': 'Observed columns whose two adjacent source-margin differences are <=1px; raw outliers retained separately',
                         'columns': len(core), 'within1px': len(core)-len(core_bad), 'outside1px': core_bad},
                     'interpolated_unobserved_columns': int((~observed).sum())}
image = np.zeros((HEIGHT, WIDTH, 3), np.uint8)
image[actual_white] = [235, 220, 190]
image[owners == 23], image[owners == 24] = [70, 230, 170], [70, 155, 245]
Image.fromarray(image).save(OUT/'Evidence/DAY02_prepared_closed_semantic_CPU.png')
source_image = np.asarray(Image.open(SOURCE/'source.png').convert('RGB'))
Image.fromarray((source_image*.55+image*.45).astype(np.uint8)).save(OUT/'Evidence/DAY02_prepared_source_overlay_CPU.png')
receipt = {'status': 'CPU_DAY02_PHYSICAL_GINGIVA_PREPARED_NOT_DCC_OR_ART_ACCEPTED',
    'source': str(SOURCE/'source.png'), 'source_sha256': sha(SOURCE/'source.png'),
    'camera': {'width': 16, 'resolution': [963, 217], 'center': [0, 0, 0], 'source_center_y': CENTER},
    'actual_crown_blend': candidate['blend'], 'actual_crown_blend_sha256': candidate['blend_sha256'],
    'actual_crown_packet': str(packet_path), 'actual_crown_packet_sha256': packet_sha,
    'cervical_support': str(support_path), 'cervical_support_sha256': sha(support_path),
    'counts': {'observed_upper': 11, 'observed_lower': 12}, 'surfaces': reports,
    'source_X_sampling': {'regular_spacing_px': 2, 'columns_total': n,
        'critical_pixel_centers_added': len(critical_columns),
        'policy': 'Exact x+.5 centers at source steep interproximal margin changes and their immediate neighbors'},
    'CPU_closed_ownership': {'white_source_mask_IoU_label_excluded': IoU, 'cervical': cervical},
    'hierarchy': {'collection': 'MenuGingiva_DAY02_Geometry', 'root': 'MC_JawsRoot_DAY02[OpenAmount]',
        'wrappers': ['MC_Gingiva_DAY02_Upper', 'MC_Gingiva_DAY02_Lower'],
        'meshes': ['MC_DAY02_Upper_Gum', 'MC_DAY02_Lower_Gum'], 'material': 'MC_Gum'},
    'parent_contract': support['parents'], 'CPU_camera_object_order': names,
    'UV0': 'Independent DAY02 tile; four disjoint front/back rectangles; .025normalized gaps >51px at2K',
    'secondaryUV': 'UV_SourceProjection_Closed sourceW16/H217; outsidecrop inferred returns allowed, CLIP image never repeat',
    'limits': ['Actual DCC five-pose solids, source contour/ownership, unique UV0 and multiview review pending.',
        'All Y and backs/outer returns inferred; source gives only Closed front.',
        'Open basis is only rigid inferred separation; no exact Open source claim.',
        'Physical cervical crease appearance unaccepted; this does not block separate fixed-menu baked-radiance review.',
        'Source column ambiguities and CPU misses retained, no repainting masks to hide geometry faults.']}
(OUT/'geometry_preparation.json').write_text(json.dumps(receipt, indent=2), encoding='utf8')
build_contract = {'status': 'CPU_DAY02_FROZEN_INPUTS_FOR_BOUNDED_DCC',
    'source_image': str(SOURCE/'source.png'), 'source_image_sha256': receipt['source_sha256'],
    'camera': receipt['camera'], 'constraints': {q['body']: {'path': q['constraint_path'], 'sha256': q['constraints_sha256']} for q in reports},
    'observed_upper_crown_count': 11, 'observed_lower_crown_count': 12}
(OUT/'DCC_build_contract.json').write_text(json.dumps(build_contract, indent=2), encoding='utf8')
readiness = json.loads((OUT/'source_readiness.json').read_text(encoding='utf8'))
readiness.update({'status': 'CPU_SOURCE_AND_PHYSICAL_VOLUMES_READY_ACTUAL_DCC_PENDING',
                  'geometry_preparation': str(OUT/'geometry_preparation.json')})
(OUT/'source_readiness.json').write_text(json.dumps(readiness, indent=2), encoding='utf8')
print('DAY02_GUM_CPU_READY', json.dumps({'IoU': IoU, 'cervical': {row: {k: value for k, value in cervical[row].items() if k != 'all_columns'} for row in cervical}, 'surfaces': reports}), flush=True)
