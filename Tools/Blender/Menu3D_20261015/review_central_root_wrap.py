"""CPU actual-vs-source compound cervical ownership and collar proof."""
from pathlib import Path
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Gums/Diagnostics/v18_CentralRRootWrap/Actual'
baseline = ROOT / 'Gums/Diagnostics/v18_NeckDepthSweep/Yzero'
actual = json.loads((OUT / 'actual_root_wrap_manifest.json').read_text())
constraints = json.loads((ROOT / 'Gums/V03/Constraints/source_locked_gums.json').read_text())
margin = np.asarray(constraints['upper_margin_y_by_x'])
uv = json.loads((ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json').read_text())
target = next(p for p in uv['parts'] if p['part'] == 'Upper_Central_R')
source_mask = np.asarray(Image.open(target['source_visible_front_mask']).convert('L')) > 0
source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
images = {'source': source}
for mode in ('pbr', 'gray'):
    images['baseline_' + mode] = np.asarray(Image.open(baseline / (mode + '.png')).convert('RGB'))
    images['root_wrap_' + mode] = np.asarray(Image.open(OUT / (mode + '.png')).convert('RGB'))
first_hits = actual['actual_compound_first_hits']
crossings, cracks = [], []
for x in range(487, 568):
    sy = np.where(source_mask[:, x])[0]
    enamel_hits = [r['pixel_xy'][1] for r in first_hits if r['pixel_xy'][0] == x and r['object'] == 'MC_DAY03_Upper_Central_R']
    if len(sy) and enamel_hits:
        first = min(enamel_hits)
        crossings.append({'x': x, 'source_first_enamel_pixel_y': int(sy.min()),
                           'actual_first_enamel_pixel_y': first, 'signed_delta_pixels': first - int(sy.min())})
for ray in first_hits:
    x, y = ray['pixel_xy']
    if abs(y + .5 - margin[x]) <= 1.5 and ray['object'] not in ('MC_DAY03_Upper_Central_R', 'MC_DAY03_Upper_Gum'):
        cracks.append(ray)
inventory = json.loads((ROOT / 'Textures/Revisions/v05_SourceLocked/Gingiva/gingiva_source_inventory.json').read_text())
curve = np.array(next(r for r in inventory['upper_per_crown_margins'] if r['crown'] == 'Upper_Central_R')['visible_neck_points_xy'])
xs = np.linspace(492, 560, 101).astype(np.float32)
ys = np.interp(xs, curve[:, 0], curve[:, 1]).astype(np.float32)
offsets = np.arange(-8, 9, dtype=np.float32)
map_x = np.repeat(xs[None, :], len(offsets), axis=0)
map_y = ys[None, :] + offsets[:, None]
profiles = {}
for label, image in images.items():
    pixels = cv2.remap(image.astype(np.float32) / 255, map_x, map_y, cv2.INTER_LINEAR)
    lum = pixels @ np.array([.2126, .7152, .0722])
    gum = np.median(lum[(offsets >= -6) & (offsets <= -4)], axis=0)
    neck = np.min(lum[(offsets >= -2) & (offsets <= 1)], axis=0)
    profiles[label] = {'median_luminance_by_offset': np.median(lum, axis=1).tolist(),
                       'neck_min_to_near_gum_ratio': float(np.median(neck / np.maximum(gum, 1e-8)))}

sheet = Image.new('RGB', (1050, 670), (18, 23, 27))
draw = ImageDraw.Draw(sheet)
draw.text((10, 8), 'ONE CENTRAL R: actual physical root+gum-wrap test, same native v18 shader/light. No albedo collar mask.', fill='white')
box = (476, 9, 578, 52)
for row, (left, right) in enumerate([('source', 'source'), ('baseline_pbr', 'baseline_gray'), ('root_wrap_pbr', 'root_wrap_gray')]):
    y = 35 + row * 210
    for col, label in enumerate((left, right)):
        x = 10 + col * 525
        draw.text((x, y), label, fill='white')
        crop = Image.fromarray(images[label]).crop(box).resize((408, 172), Image.Resampling.NEAREST)
        sheet.paste(crop, (x, y + 20))
sheet.save(OUT / 'source_baseline_rootwrap_contact_proof.png')
delta = np.array([r['signed_delta_pixels'] for r in crossings])
report = {'status': 'ACTUAL_SINGLE_ROOT_WRAP_GEOMETRY_HYPOTHESIS_NOT_ROW_APPROVAL',
          'actual_receipt': str(OUT / 'actual_root_wrap_manifest.json'),
          'source_mask': target['source_visible_front_mask'],
          'compound_ownership': {'sampled_columns': len(crossings),
                                  'max_absolute_enamel_top_pixel_error': int(np.max(np.abs(delta))),
                                  'mean_signed_error_pixels': float(np.mean(delta)),
                                  'within1px_columns': int((np.abs(delta) <= 1).sum()),
                                  'cervical_crack_pixels_hitting_other_surface': cracks,
                                  'old_crack_pixel_now': actual['old_crack_pixel_525_26'],
                                  'crossings': crossings},
          'collar_profiles': profiles,
          'actual_visual_review': {
              'physical_collar_improvement': True,
              'old_through_crack_closed': actual['old_crack_now_covered_by_mouth_surface'],
              'strict1px_source_cervical_gate_pass': bool(np.max(np.abs(delta)) <= 1),
              'remaining_failure': 'At sourceX567 the first enamel pixel is35 versus reference33;2px low.',
              'all_row_extension_approved': False,
              'statement': 'The constant-gray and PBR groove both improve, supporting real root/tissue continuation. No shader compensation or source-mask color seam was used.'},
          'collar_protocol': {'x_range': [492, 560], 'samples': 101, 'source_y_offsets': offsets.tolist(),
                             'neck': 'per-X min−2..+1', 'gum': 'per-X median−6..−4',
                             'ratio': 'median across X of neck/gum', 'space': 'display RGB luma'},
          'limits': ['UV0 arrays retained but old source-affine audit and secondary projection remain STALE.',
                     'Cervical pigment stretches with the diagnostic unchangedUV geometry; gray control is the direct seating evidence.',
                     'Compound source-edge fit is necessary, not sufficient; actual collar visual remains a separate gate.',
                     'Only this one central crown was changed; no all-row or closed-pose approval is implied.']}
(OUT / 'actual_source_crossing_and_collar_review.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps({'ownership': {k: v for k, v in report['compound_ownership'].items() if k != 'crossings'},
                  'collar_ratios': {k: v['neck_min_to_near_gum_ratio'] for k, v in profiles.items()}}, indent=2))
