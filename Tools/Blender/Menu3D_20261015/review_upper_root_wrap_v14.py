"""CPU actual BVH/source crossing and six-crown gray/PBR collar review."""
from pathlib import Path
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
BASE = ROOT / 'Gums/Diagnostics/v14_UpperRootWrap'
OUT = BASE / 'Actual'
receipt = json.loads((BASE / 'upper6_root_wrap_v14_preparation.json').read_text())
actual = json.loads((OUT / 'actual_root_wrap_manifest.json').read_text())
baseline = ROOT / 'Gums/Diagnostics/v18_NeckDepthSweep/Yzero'
source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
images = {'source': source}
for mode in ('pbr', 'gray'):
    images['baseline_' + mode] = np.asarray(Image.open(baseline / (mode + '.png')).convert('RGB'))
    images['v14_' + mode] = np.asarray(Image.open(OUT / (mode + '.png')).convert('RGB'))
ray_lookup = {tuple(r['pixel_xy']): r for r in actual['actual_compound_first_hits']}
columns, cracks = [], []
for expected in receipt['compound_ownership_columns']:
    x, sy, name = expected['x'], expected['source_first_enamel_y'], expected['object']
    rays = [ray_lookup[(x, y)] for y in range(sy - 3, sy + 5)]
    hits = [r['pixel_xy'][1] for r in rays if r['object'] == name]
    first = min(hits) if hits else None
    columns.append({'object': name, 'x': x, 'source_first_enamel_y': sy,
                     'actual_first_enamel_y': first, 'error': first - sy if first is not None else None})
    cracks.extend([r for r in rays if abs(r['pixel_xy'][1] + .5 - sy) <= 1.5
                   and (r['object'] is None or r['object'].startswith('Interior_'))])
bad = [r for r in columns if r['error'] is None or abs(r['error']) > 1]
ui = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/CanonicalOpen/InteriorRefined/ui_occlusion.png').convert('L'))
offsets = np.arange(-8, 9, dtype=np.float32)
profiles = []
names = list(dict.fromkeys(r['object'] for r in columns))
for name in names:
    curve = np.array([[r['x'], r['source_first_enamel_y']] for r in columns if r['object'] == name])
    low, high = curve[:, 0].min() + 9, curve[:, 0].max() - 9
    xs = np.linspace(low, high, 101).astype(np.float32)
    ys = np.interp(xs, curve[:, 0], curve[:, 1]).astype(np.float32)
    mx = np.repeat(xs[None, :], len(offsets), axis=0)
    my = ys[None, :] + offsets[:, None]
    valid = cv2.remap(ui, mx, my, cv2.INTER_NEAREST, borderValue=255) == 0
    row = {'object': name, 'x_range': [int(low), int(high)], 'profiles': {}}
    for label, image in images.items():
        sampled = cv2.remap(image.astype(np.float32) / 255, mx, my, cv2.INTER_LINEAR)
        lum = sampled @ np.array([.2126, .7152, .0722])
        lum[~valid] = np.nan
        gum = np.nanmedian(lum[(offsets >= -6) & (offsets <= -4)], axis=0)
        neck = np.nanmin(lum[(offsets >= -2) & (offsets <= 1)], axis=0)
        row['profiles'][label] = {'median_luminance_by_offset': np.nanmedian(lum, axis=1).tolist(),
                                   'neck_min_to_near_gum_ratio': float(np.nanmedian(neck / np.maximum(gum, 1e-8)))}
    profiles.append(row)

sheet = Image.new('RGB', (1060, 900), (18, 23, 27))
draw = ImageDraw.Draw(sheet)
draw.text((10, 8), 'SIX UPPER v14: physical hidden roots+source-owned tissue wrap. Native v18 maps/light unchanged.', fill='white')
box = (225, 5, 735, 72)
for row, label in enumerate(('source', 'baseline_pbr', 'v14_pbr', 'baseline_gray', 'v14_gray')):
    y = 35 + row * 170
    draw.text((10, y), label + ' / uniform2x native pixel crop', fill='white')
    crop = Image.fromarray(images[label]).crop(box).resize((1020, 134), Image.Resampling.NEAREST)
    sheet.paste(crop, (10, y + 20))
sheet.save(OUT / 'source_baseline_upper6_contact_proof.png')

# Simple profile sheet makes the material-independent contact difference clear.
graph = Image.new('RGB', (1200, 6 * 185), (19, 24, 27))
draw = ImageDraw.Draw(graph)
colors = [(130, 240, 213), (243, 165, 128), (247, 223, 135)]
for i, row in enumerate(profiles):
    top = i * 185
    draw.text((8, top + 8), row['object'], fill='white')
    for col, mode in enumerate(('pbr', 'gray')):
        keys = ['source', 'baseline_' + mode, 'v14_' + mode]
        left, bottom = 80 + col * 600, top + 165
        for j, key in enumerate(keys):
            values = row['profiles'][key]['median_luminance_by_offset']
            points = [(left + 27 * (offset + 8), bottom - float(value) * 135) for offset, value in zip(offsets, values)]
            draw.line(points, fill=colors[j], width=2)
            ratio = row['profiles'][key]['neck_min_to_near_gum_ratio']
            draw.text((left + j * 155, top + 27), f'{key} {ratio:.3f}', fill=colors[j])
        draw.line([(left, bottom), (left + 432, bottom)], fill=(80, 90, 95))
graph.save(OUT / 'six_upper_collar_profiles.png')
report = {'version': 'v14', 'status': 'ACTUAL_SIX_UPPER_LOCAL_CONTACT_PROOF_NOT_WHOLE_FIDELITY_OR_TEXTURE_APPROVAL',
          'actual_receipt': str(OUT / 'actual_root_wrap_manifest.json'),
          'source_cervical_columns': len(columns), 'within1px_columns': len(columns) - len(bad),
          'outside1px_columns': bad, 'near_cervical_interior_or_background_hits': cracks,
          'actual_compound_source_gate_pass': not bad and not cracks,
          'corner567': next(r for r in columns if r['x'] == 567),
          'crossings': columns, 'collar_profiles_six_parts': profiles,
          'collar_protocol': {'source': 'current V13 visible cervical mask top row per sourceX',
                             'x_sample_region': 'per-part observed arc trimmed9px at each side',
                             'x_samples': 101, 'source_y_offsets': offsets.tolist(),
                             'neck': 'per-X minimum−2..+1', 'gum': 'per-X median−6..−4',
                             'ratio': 'median across X of neck/gum', 'UI_character_excluded': True},
          'UV_provenance': actual['UV_contract'],
          'limitations': ['Actual collar improvement is a local geometry subcheck, not final anatomical/material similarity.',
                         'Unchanged UV arrays are not a fresh source projection; affine/sourceprojection receipts remain explicitly stale.',
                         'Closed endpoints need separately authored physical continuation; never apply this open delta twice.',
                         'No source/candidate .blend was saved or whole-scene texture freeze requested.']}
(OUT / 'actual_upper6_source_crossing_and_collar_review.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps({'columns': len(columns), 'within1px': len(columns) - len(bad), 'bad': bad,
                  'crack_count': len(cracks), 'corner567': report['corner567'],
                  'ratios': [{'object': r['object'], **{k: round(v['neck_min_to_near_gum_ratio'], 4) for k, v in r['profiles'].items()}} for r in profiles]}, indent=2))
