"""CPU-only actual v18 central tooth/gingiva YZ sections and visible-ray data."""
from pathlib import Path
import csv
import hashlib
import json

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
RESPONSE = ROOT / 'Assembly/Review/MaterialResponse_v18'
OUT = ROOT / 'Gums/Diagnostics/v18_NeckCrossSection'
OUT.mkdir(parents=True, exist_ok=True)
manifest = json.loads((RESPONSE / 'response_manifest.json').read_text())
geometry = np.load(RESPONSE / 'world_geometry_uv0.npz')
ownership = np.load(RESPONSE / 'camera_uv0_ownership.npz')
V, T, O = geometry['vertices'], geometry['triangles'], geometry['tri_object']
names = manifest['objects']
inventory_path = ROOT / 'Textures/Revisions/v05_SourceLocked/Gingiva/gingiva_source_inventory.json'
inventory = json.loads(inventory_path.read_text())
curves = {p['crown']: np.asarray(p['visible_neck_points_xy'], dtype=float) for p in inventory['upper_per_crown_margins']}


def section(object_name, world_x):
    triangles = V[T[O == names.index(object_name)]]
    keep = (triangles[:, :, 0].min(axis=1) <= world_x) & (triangles[:, :, 0].max(axis=1) >= world_x)
    triangles = triangles[keep]
    segments = []
    for tri in triangles:
        normal = np.cross(tri[1] - tri[0], tri[2] - tri[0])
        normal /= max(np.linalg.norm(normal), 1e-15)
        hits = []
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            if abs(b[0] - a[0]) < 1e-12:
                continue
            t = (world_x - a[0]) / (b[0] - a[0])
            if -1e-8 <= t <= 1 + 1e-8:
                hits.append((a + t * (b - a))[1:])
        unique = []
        for hit in hits:
            if not any(np.linalg.norm(hit - old) < 1e-8 for old in unique):
                unique.append(hit)
        if len(unique) == 2:
            segments.append({'yz': np.asarray(unique), 'geometric_face_normal_xyz': normal})
    return segments


def at_z(segments, z):
    hits = []
    for segment in segments:
        a, b = segment['yz']
        if abs(b[1] - a[1]) < 1e-12:
            continue
        t = (z - a[1]) / (b[1] - a[1])
        if -1e-7 <= t <= 1 + 1e-7:
            hits.append({'Y': float(a[0] + t * (b[0] - a[0])),
                         'normal_xyz': segment['geometric_face_normal_xyz'].tolist()})
    hits.sort(key=lambda hit: hit['Y'])
    return {'front': hits[0], 'back': hits[-1], 'thickness_Y': hits[-1]['Y'] - hits[0]['Y'],
            'intersection_count': len(hits)} if hits else None


records, csvrows = [], []
all_sections = {}
for part, source_x in [('Upper_Central_L', 428.), ('Upper_Central_R', 525.)]:
    world_x = (source_x / 963 - .5) * 16
    neck_y = float(np.interp(source_x, curves[part][:, 0], curves[part][:, 1]))
    neck_z = (205 - neck_y) / 963 * 16
    crown_name, gum_name = 'MC_DAY03_' + part, 'MC_DAY03_Upper_Gum'
    crown, gum = section(crown_name, world_x), section(gum_name, world_x)
    all_sections[part] = {'crown': crown, 'gum': gum}
    crown_points = np.concatenate([s['yz'] for s in crown])
    gum_points = np.concatenate([s['yz'] for s in gum])
    crown_rim_z = float(crown_points[:, 1].max())
    gum_rim_z = float(gum_points[:, 1].min())
    crown_rim_y = crown_points[np.abs(crown_points[:, 1] - crown_rim_z) < 1e-7, 0]
    gum_rim_y = gum_points[np.abs(gum_points[:, 1] - gum_rim_z) < 1e-7, 0]
    record = {'part': part, 'source_x': source_x, 'world_x': world_x,
              'source_cervical_y': neck_y, 'source_cervical_world_z': neck_z,
              'tooth_closed_solid_top': {'Z': crown_rim_z, 'source_y': 205 - crown_rim_z * 963 / 16,
                                          'Y_min_max': [float(crown_rim_y.min()), float(crown_rim_y.max())]},
              'gum_closed_solid_bottom': {'Z': gum_rim_z, 'source_y': 205 - gum_rim_z * 963 / 16,
                                          'Y_min_max': [float(gum_rim_y.min()), float(gum_rim_y.max())]},
              'rim_delta_gum_minus_tooth_Y': float(gum_rim_y.min() - crown_rim_y.min()),
              'rim_delta_gum_minus_tooth_Z': gum_rim_z - crown_rim_z,
              'sections_at_source_offsets': [], 'actual_camera_first_hit_pixels': []}
    for offset in (-8., -4., -2., -1., -.5, 0., .5, 1., 2., 4., 8., 12.):
        z = (205 - neck_y - offset) / 963 * 16
        c, g = at_z(crown, z), at_z(gum, z)
        row = {'source_y_offset': offset, 'source_y': neck_y + offset, 'world_Z': z,
               'tooth': c, 'gum': g}
        if c and g:
            row['front_Y_gum_minus_tooth'] = g['front']['Y'] - c['front']['Y']
        record['sections_at_source_offsets'].append(row)
        for label, hit in [('tooth', c), ('gum', g)]:
            if hit:
                csvrows.append([part, source_x, offset, z, label, hit['front']['Y'], hit['back']['Y'],
                                hit['thickness_Y'], *hit['front']['normal_xyz']])
    for py in range(round(neck_y) - 4, round(neck_y) + 6):
        ix, iy = int(source_x), int(py)
        oi = int(ownership['object'][iy, ix])
        record['actual_camera_first_hit_pixels'].append({'pixel_xy': [ix, iy],
            'world_Y': float(ownership['depth'][iy, ix]), 'object': names[oi] if oi >= 0 else None})
    near_tooth = at_z(crown, crown_rim_z - .5 * 16 / 963)
    near_gum = at_z(gum, gum_rim_z + .5 * 16 / 963)
    if near_tooth and near_gum:
        nt, ng = np.array(near_tooth['front']['normal_xyz']), np.array(near_gum['front']['normal_xyz'])
        record['geometric_front_normal_angle_across_neck_degrees'] = float(np.degrees(np.arccos(np.clip(nt @ ng, -1, 1))))
        record['normal_sample_policy'] = '0.5sourcepx inward from each actual physical closed-solid rim; not across empty source-space gap'
    records.append(record)
with (OUT / 'central_joint_sections.csv').open('w', newline='', encoding='utf8') as f:
    writer = csv.writer(f)
    writer.writerow(['part', 'source_x', 'source_y_offset', 'world_Z', 'surface', 'front_Y', 'back_Y', 'thickness_Y', 'face_Nx', 'face_Ny', 'face_Nz'])
    writer.writerows(csvrows)

sheet = Image.new('RGB', (1400, 760), (18, 23, 27))
draw = ImageDraw.Draw(sheet)
for i, record in enumerate(records):
    left, top, width, height = 50 + i * 700, 65, 600, 600
    draw.text((left, 12), record['part'] + f' at sourceX={record["source_x"]:g}', fill='white')
    draw.text((left, 29), 'Camera is left (-Y). Red=gingiva, ivory=enamel. Exact triangle-plane cuts.', fill=(190, 211, 204))
    y_bounds, z_bounds = (-.6, .65), (record['source_cervical_world_z'] - .30, record['source_cervical_world_z'] + .30)
    def screen(yz):
        return (left + (yz[0] - y_bounds[0]) / (y_bounds[1] - y_bounds[0]) * width,
                top + (z_bounds[1] - yz[1]) / (z_bounds[1] - z_bounds[0]) * height)
    for label, color in [('gum', (234, 110, 112)), ('crown', (245, 223, 190))]:
        for segment in all_sections[record['part']][label]:
            points = [screen(v) for v in segment['yz']]
            # Clip in drawing space to avoid huge lines from offscreen backs.
            a, b = segment['yz']
            if max(a[1], b[1]) >= z_bounds[0] and min(a[1], b[1]) <= z_bounds[1]:
                draw.line(points, fill=color, width=2)
    z = record['source_cervical_world_z']
    draw.line([screen((y_bounds[0], z)), screen((y_bounds[1], z))], fill=(65, 130, 130))
    draw.text((left, top + height + 20), f'Rim dY={record["rim_delta_gum_minus_tooth_Y"]:.5f}; dZ={record["rim_delta_gum_minus_tooth_Z"]:.5f}', fill='white')
    draw.text((left, top + height + 38), f'Near-neck geometric normal angle={record.get("geometric_front_normal_angle_across_neck_degrees", float("nan")):.1f}deg', fill='white')
sheet.save(OUT / 'actual_central_joint_YZ_sections.png')
report = {'status': 'CPU_ACTUAL_TRIANGLE_CROSS_SECTION_DIAGNOSIS_NO_DCC_OR_MESH_EDITS',
          'source_geometry_npz': str(RESPONSE / 'world_geometry_uv0.npz'),
          'source_geometry_npz_sha256': hashlib.sha256((RESPONSE / 'world_geometry_uv0.npz').read_bytes()).hexdigest(),
          'camera_ownership': str(RESPONSE / 'camera_uv0_ownership.npz'),
          'normal_note': 'Exact evaluated triangle geometric normals, not custom/split or rendered smooth normals. Signs follow actual triangle winding.',
          'projection': {'X': '(source_x/963-.5)*16', 'Z': '(205-source_y)/963*16', 'camera': 'negative Y toward positive Y'},
          'parts': records,
          'limitations': ['Single cross-section per central crown is a local diagnostic; full cervical loops need separate compound validation.',
                         'Source cervical curve denotes visible ownership, not proof of a physical closed-solid border.',
                         'No isolated crown matte can validate hidden-root continuation; evaluate actual gingiva+crown first-hit ownership.']}
(OUT / 'central_joint_diagnosis.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps([{k: v for k, v in r.items() if k in ('part', 'source_cervical_y', 'tooth_closed_solid_top', 'gum_closed_solid_bottom', 'rim_delta_gum_minus_tooth_Y', 'rim_delta_gum_minus_tooth_Z', 'geometric_front_normal_angle_across_neck_degrees')} for r in records], indent=2))
