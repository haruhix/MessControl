"""CPU metric/light-coverage audit of the actual v29 Gum height candidate."""
from pathlib import Path
import argparse
import fnmatch
import json
import math

import cv2
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
PACKET = ROOT / 'Gums/ClosedPosePrototype/BlenderCandidate_v06_PadSource_IntermediateRelax/TransferPacket/evaluated_pose_triangle_uv_packet.npz'
parser = argparse.ArgumentParser()
parser.add_argument('--binding', default=str(ROOT / 'Textures/Revisions/v28_WholeMaterialFinish_CPU/material_binding_whole_finish_candidate.json'))
parser.add_argument('--profile', default=str(ROOT / 'Assembly/Review/RootContactV14_ReflectionProfiles_v27/A_Bplus2_UpperSix/look_profile.json'))
args = parser.parse_args()
OUT = Path(args.binding).resolve().parent
binding = json.loads(Path(args.binding).read_text())
height = cv2.imread(binding['materials']['MC_Gum']['physical_microdetail']['height_uv0'], cv2.IMREAD_UNCHANGED).astype(np.float64)/65535
height = (height-.5)*binding['materials']['MC_Gum']['physical_microdetail']['bump_distance_world']
dh_dv, dh_du = np.gradient(height, 1/2048)
dh_dv *= -1  # image rows are opposite UV V
profile = json.loads(Path(args.profile).read_text())
data = np.load(PACKET)
records = {}
for jaw in ('Upper', 'Lower'):
    name = f'MC_DAY03_{jaw}_Gum'
    prefix = 'open::' + name + '::'
    points = data[prefix+'world_vertices']
    triangles = data[prefix+'triangles']
    uv = data[prefix+'UV0_triangles'].astype(np.float64)
    p = points[triangles]
    e1, e2 = p[:, 1]-p[:, 0], p[:, 2]-p[:, 0]
    normals = np.cross(e1, e2)
    norms = np.linalg.norm(normals, axis=1)
    normals /= np.maximum(norms[:, None], 1e-12)
    center = p.mean(axis=1)
    sx = np.floor((center[:, 0]/16+.5)*963).astype(int)
    sy = np.floor(205-center[:, 2]/16*963).astype(int)
    inside = (sx >= 0) & (sx < 963) & (sy >= 0) & (sy < 410) & (normals[:, 1] < -.05)
    source_mask_path = ROOT / ('Textures/Revisions/v05_SourceLocked/Gingiva/' + ('upper_gingiva_visible.png' if jaw == 'Upper' else 'lower_gingiva_visible_strict.png'))
    mask = np.asarray(Image.open(source_mask_path).convert('L')) > 0
    inside &= mask[np.clip(sy, 0, 409), np.clip(sx, 0, 962)]
    de1, de2 = uv[:, 1]-uv[:, 0], uv[:, 2]-uv[:, 0]
    determinant = de1[:, 0]*de2[:, 1]-de1[:, 1]*de2[:, 0]
    inside &= abs(determinant) > 1e-12
    p, center, normals, e1, e2, uv, de1, de2, determinant = (value[inside] for value in (p, center, normals, e1, e2, uv, de1, de2, determinant))
    j_u = (e1*de2[:, 1, None]-e2*de1[:, 1, None])/determinant[:, None]
    j_v = (-e1*de2[:, 0, None]+e2*de1[:, 0, None])/determinant[:, None]
    uu, vv = uv.mean(axis=1).T
    xx, yy = np.clip(np.floor(uu*2048).astype(int), 0, 2047), np.clip(np.floor((1-vv)*2048).astype(int), 0, 2047)
    a = np.einsum('ij,ij->i', j_u, j_u)
    b = np.einsum('ij,ij->i', j_u, j_v)
    c = np.einsum('ij,ij->i', j_v, j_v)
    det = np.maximum(a*c-b*b, 1e-20)
    du, dv = dh_du[yy, xx], dh_dv[yy, xx]
    gradient = j_u*((c*du-b*dv)/det)[:, None] + j_v*((a*dv-b*du)/det)[:, None]
    angle = np.degrees(np.arctan(np.linalg.norm(gradient, axis=1)))
    pitch = np.sqrt(np.linalg.norm(np.cross(j_u, j_v), axis=1))/2048*963/16
    view = np.array([0., -30., 0.])-center
    view /= np.linalg.norm(view, axis=1, keepdims=True)
    reflected = 2*np.einsum('ij,ij->i', view, normals)[:, None]*normals-view
    misses = []
    light_records = []
    for light in profile['lights']:
        if light.get('specular_factor', 1) <= 0:
            continue
        obj = light.get('receiver_objects', [])
        mats = light.get('receiver_materials', [])
        if (obj or mats) and not any(fnmatch.fnmatchcase(name, pattern) for pattern in obj) and 'MC_Gum' not in mats:
            continue
        position, target = np.asarray(light['position']), np.asarray(light['target'])
        forward = (target-position)/np.linalg.norm(target-position)
        up = np.array([0., 1., 0.]) if abs(forward[2]) > 1-1e-10 else np.array([0., 0., 1.])
        right = np.cross(forward, up)
        right /= np.linalg.norm(right)
        up = np.cross(right, forward)
        roll = math.radians(light.get('roll_degrees', 0))
        right, up = right*math.cos(roll)+up*math.sin(roll), -right*math.sin(roll)+up*math.cos(roll)
        normal = -forward
        denominator = reflected @ normal
        t = ((position-center) @ normal)/np.where(abs(denominator) > 1e-9, denominator, 1e-9)
        hit = center+reflected*t[:, None]
        offset = hit-position
        x = np.clip(offset @ right, -light['size']/2, light['size']/2)
        y = np.clip(offset @ up, -light.get('size_y', light['size'])/2, light.get('size_y', light['size'])/2)
        nearest = position+x[:, None]*right+y[:, None]*up
        direction = nearest-center
        direction /= np.linalg.norm(direction, axis=1, keepdims=True)
        # A backwards ray-plane intersection is not an angular distance of180.
        # Bound the actual rectangle's angular miss using a dense local grid.
        # This remains a coverage diagnostic, not a physical reflected-energy pass.
        best_dot = np.full(len(center), -1., np.float64)
        for card_x in np.linspace(-light['size']/2, light['size']/2, 7):
            for card_y in np.linspace(-light.get('size_y', light['size'])/2, light.get('size_y', light['size'])/2, 33):
                direction = position+right*card_x+up*card_y-center
                direction /= np.linalg.norm(direction, axis=1, keepdims=True)
                best_dot = np.maximum(best_dot, np.einsum('ij,ij->i', reflected, direction))
        miss = np.degrees(np.arccos(np.clip(best_dot, -1, 1)))
        misses.append(miss)
        light_records.append({'name': light['name'], 'mirror_direction_miss_p10_p50_p90_deg': np.percentile(miss, [10, 50, 90]).tolist(), 'exact_forward_ray_plane_fraction': float(np.mean(t > 0))})
    best = np.min(misses, axis=0)
    records[jaw] = {'source_visible_front_triangles': len(center),
        'approx_face_normal_XYZ_p50': np.median(normals, axis=0).tolist(),
        'approx_face_normal_Y_p10_p50_p90': np.percentile(normals[:, 1], [10, 50, 90]).tolist(),
        'actual_UV0_2K_isotropic_texel_source_px_p10_p50_p90': np.percentile(pitch, [10, 50, 90]).tolist(),
        'height_gradient_after_2K_UV_raster_slope_deg_RMS': float(np.sqrt(np.mean(angle**2))),
        'height_gradient_after_2K_UV_raster_slope_deg_p50_p90_p99': np.percentile(angle, [50, 90, 99]).tolist(),
        'best_card_mirror_direction_miss_p10_p50_p90_deg': np.percentile(best, [10, 50, 90]).tolist(),
        'normal_tilt_half_miss_approx_p10_p50_p90_deg': (np.percentile(best, [10, 50, 90])*.5).tolist(),
        'lights': light_records}
report = {'status': 'CPU_DIAGNOSTIC_NOT_MATERIAL_ACCEPTANCE_OR_REPAIR', 'metric': records,
    'limits': ['Face normal approximation, not Cycles smooth/interpolated/SSS BSDF normal.',
               'Source semantic front mask, not a new all-scene ray visibility gate.',
               'Exact mirror rectangle test ignores rough-lobe width, energy, shadowing and indirect bounce.',
               'Needed half-angle is geometric coverage diagnostic, not an instruction to add arbitrary normal amplitude.'],
    'next_intervention': ['Keep physical nonrepeating detail ABOVE actual UV/source sampling bandwidth, not mostly subtexel.',
        'A roughness-only layer cannot create narrow glints when the correct mirror directions never see any card.',
        'Judge actual whole-view sparkle plus closeup; do not replace anatomy/pigment with brighter normals or bake glints into BaseColor.']}
(OUT / 'gum_finish_sampling_and_light_coverage_diagnosis.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report, indent=2))
