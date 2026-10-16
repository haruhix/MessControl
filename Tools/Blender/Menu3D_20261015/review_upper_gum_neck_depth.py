"""CPU source-aligned collar profiles from the six actual seating captures."""
from pathlib import Path
import csv
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Gums/Diagnostics/v18_NeckDepthSweep'
gingiva = json.loads((ROOT / 'Textures/Revisions/v05_SourceLocked/Gingiva/gingiva_source_inventory.json').read_text())
historic = json.loads((ROOT / 'Gums/Diagnostics/v16_Neck/neck_source_vs_v16_profiles.json').read_text())
curves = {p['crown']: np.array(p['visible_neck_points_xy'], dtype=float) for p in gingiva['upper_per_crown_margins']}
source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
ui = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/CanonicalOpen/InteriorRefined/ui_occlusion.png').convert('L'))
images = {'source': source.astype(np.float32) / 255}
displays = {'source': source}
for variant in ('Yminus004', 'Yzero', 'Yplus004'):
    for mode in ('pbr', 'gray'):
        key = variant + '_' + mode
        displays[key] = np.asarray(Image.open(OUT / variant / (mode + '.png')).convert('RGB'))
        images[key] = displays[key].astype(np.float32) / 255
        images[key + '_linear'] = np.load(OUT / variant / (mode + '_linear.npy'))[..., :3]
offsets = np.arange(-8, 9, dtype=np.float32)
weights = np.array([.2126, .7152, .0722])
rows, csvrows = [], []
for old in historic['parts']:
    name = old['part']
    curve = curves[name]
    xs = np.linspace(*old['x_range'], 101).astype(np.float32)
    ys = np.interp(xs, curve[:, 0], curve[:, 1]).astype(np.float32)
    map_x = np.repeat(xs[None, :], len(offsets), axis=0)
    map_y = ys[None, :] + offsets[:, None]
    valid = cv2.remap(ui, map_x, map_y, cv2.INTER_NEAREST, borderMode=cv2.BORDER_CONSTANT, borderValue=255) == 0
    record = {'part': name, 'x_range': old['x_range'],
              'reference_v16_historic_ratio': old['reference_profile']['neck_min_to_gum_ratio'],
              'actual_v16_historic_ratio': old['actual_v16_profile']['neck_min_to_gum_ratio'],
              'profiles': {}}
    for key, image in images.items():
        pixels = cv2.remap(image, map_x, map_y, cv2.INTER_LINEAR)
        lum = pixels @ weights
        lum[~valid] = np.nan
        profile = np.nanmedian(lum, axis=1)
        rgb_profile = np.nanmedian(np.where(valid[..., None], pixels, np.nan), axis=1)
        # Explicit new protocol; do not silently equate to the historic v16 code.
        gum = np.nanmedian(lum[(offsets >= -6) & (offsets <= -4)], axis=0)
        neck = np.nanmin(lum[(offsets >= -2) & (offsets <= 1)], axis=0)
        ratio = float(np.nanmedian(neck / np.maximum(gum, 1e-8)))
        record['profiles'][key] = {'median_luminance_by_offset': profile.tolist(),
                                    'median_RGB_by_offset': rgb_profile.tolist(),
                                    'neck_min_to_near_gum_ratio': ratio,
                                    'valid_samples': int(valid.sum())}
        csvrows += [[name, key, int(offset), *rgb.tolist(), float(l)]
                    for offset, rgb, l in zip(offsets, rgb_profile, profile)]
    rows.append(record)
with (OUT / 'source_aligned_collar_profiles.csv').open('w', newline='', encoding='utf8') as f:
    writer = csv.writer(f)
    writer.writerow(['part', 'capture', 'source_y_offset_pixels', 'R', 'G', 'B', 'luminance'])
    writer.writerows(csvrows)

box = (300, 5, 650, 63)
sheet = Image.new('RGB', (1440, 4 * 170 + 35), (18, 23, 27))
draw = ImageDraw.Draw(sheet)
draw.text((10, 8), 'SOURCE / three actual gum-Y positions. LEFT native PBR; RIGHT constant linear.18 BaseColor, original other shader inputs.', fill='white')
for i, variant in enumerate(('source', 'Yminus004', 'Yzero', 'Yplus004')):
    y = 35 + i * 170
    for col, mode in enumerate(('pbr', 'gray')):
        key = 'source' if variant == 'source' else variant + '_' + mode
        crop = Image.fromarray(displays[key]).crop(box).resize((700, 116), Image.Resampling.NEAREST)
        x = 10 + col * 720
        draw.text((x, y), key, fill='white')
        sheet.paste(crop, (x, y + 20))
sheet.save(OUT / 'neck_depth_actual_contact_sheet.png')

graph = Image.new('RGB', (1200, 4 * 235), (20, 24, 27))
draw = ImageDraw.Draw(graph)
colors = {'source': (130, 240, 213), 'Yminus004_pbr': (234, 147, 118),
          'Yzero_pbr': (245, 224, 138), 'Yplus004_pbr': (146, 178, 249)}
for i, row in enumerate(rows):
    top = i * 235
    draw.text((10, top + 8), row['part'], fill='white')
    for col, mode in enumerate(('pbr', 'gray')):
        keys = ['source'] + [v + '_' + mode for v in ('Yminus004', 'Yzero', 'Yplus004')]
        draw.text((100 + col * 580, top + 8), 'Display luminance - ' + mode, fill='white')
        left, bottom = 100 + col * 580, top + 205
        for j, key in enumerate(keys):
            color = list(colors.values())[j]
            profile = row['profiles'][key]['median_luminance_by_offset']
            points = [(left + 27 * (offset + 8), bottom - float(value) * 175) for offset, value in zip(offsets, profile)]
            draw.line(points, fill=color, width=2)
            ratio = row['profiles'][key]['neck_min_to_near_gum_ratio']
            draw.text((left + j * 125, top + 25), f'{key.split("_")[0]} {ratio:.3f}', fill=color)
        draw.line([(left, bottom), (left + 432, bottom)], fill=(90, 95, 100))
        draw.line([(left + 216, top + 55), (left + 216, bottom)], fill=(55, 60, 65))
graph.save(OUT / 'source_and_actual_collar_profiles.png')
report = {'status': 'ACTUAL_CONTACT_DEPTH_DIAGNOSTIC_NOT_GEOMETRY_REPAIR_OR_ACCEPTANCE',
          'source_curve': str(ROOT / 'Textures/Revisions/v05_SourceLocked/Gingiva/gingiva_source_inventory.json'),
          'protocol': {'source_x_samples_per_part': 101, 'offsets_source_pixels': offsets.tolist(),
                       'neck': 'per-X minimum at offsets−2..+1', 'gum': 'per-X median at offsets−6..−4',
                       'ratio': 'median across valid X of neck/gum',
                       'color_space': 'display sRGB luma; separate linear actual profiles also recorded',
                       'exclusions': 'source UI/character occlusion mask at each sampled position; four middle upper margins do not intersect character ROI',
                       'historic_v16': 'Prior ratios copied explicitly; new protocol can differ and is not silently presented as identical.'},
          'parts': rows,
          'actual_visual_diagnosis': [
              'A dark cervical collar persists in all three native-PBR captures and all three constant-BaseColor controls.',
              'Both rigid world-Y shifts fail to remove the collar; zero is slightly better across all four gray-control ratios.',
              'BaseColor and UV-color boundary do not explain the persistent gray-control dark line.',
              'The control retains normal/roughness/SSS/specular/coat differences, so it does not uniquely separate contact shadow from geometric-normal or material-lobe response.',
              'Local tissue/crown seating and smooth-normal transition need joint review; no global depth placement or final texture is approved by this sweep.'
          ],
          'invariants': json.loads((OUT / 'neck_depth_sweep_manifest.json').read_text())}
(OUT / 'neck_depth_source_comparison.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps([{'part': row['part'],
                   **{key: round(value['neck_min_to_near_gum_ratio'], 4)
                      for key, value in row['profiles'].items() if not key.endswith('_linear')}} for row in rows], indent=2))
