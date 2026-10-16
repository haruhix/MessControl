"""CPU six-crown source-centroid comparison of actual B portrait translations."""
from pathlib import Path
import argparse
import json
import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Assembly/Review/ReflectionProbes_v21'
CASES = [('B_Z0', ROOT / 'Assembly/Review/ReflectionProbes_v20/B_front_portrait'),
         ('B_Zplus1', OUT / 'B_portrait_Zplus1'),
         ('B_Zplus2', OUT / 'B_portrait_Zplus2')]
p = argparse.ArgumentParser()
p.add_argument('--receiver-split', action='store_true')
args = p.parse_args()
if args.receiver_split:
    OUT = ROOT / 'Assembly/Review/ReflectionProbes_v22'
    CASES = [('B_Z0', ROOT / 'Assembly/Review/ReflectionProbes_v20/B_front_portrait'),
             ('B_Zplus2', ROOT / 'Assembly/Review/ReflectionProbes_v21/B_portrait_Zplus2'),
             ('ReceiverSplit', OUT / 'ReceiverSplit')]


def component(mask):
    n, labels, stats, centers = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8)
    if n < 2:
        return {'pixels': 0}
    i = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    return {'pixels': int(stats[i, cv2.CC_STAT_AREA]), 'bbox_xywh': stats[i, :4].tolist(),
            'centroid_xy': centers[i].tolist()}


source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
parts = json.loads((ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json').read_text())['parts'][:6]
ui = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/CanonicalOpen/InteriorRefined/ui_occlusion.png').convert('L')) > 0
L = cv2.cvtColor(source, cv2.COLOR_RGB2LAB)[..., 0].astype(float)
r, g, b = source.transpose(2, 0, 1)
white = (r > 235) & (g > 200) & (b > 185) & ((L - cv2.GaussianBlur(L, (0, 0), 3)) > 5)
linear = {name: np.load(folder / 'black_linear.npy')[..., :3] @ np.array([.2126, .7152, .0722]) for name, folder in CASES}
normals = {name: np.asarray(Image.open(folder / 'normal.png').convert('RGB')) for name, folder in CASES}
sheet = Image.new('RGB', (1250, 60 + 6 * 185), (18, 23, 26))
draw = ImageDraw.Draw(sheet)
heading = ('SOURCE / B Z0 / B Z+2 / RECEIVER SPLIT - identical portrait orientation and emitter radiance'
           if args.receiver_split else 'SOURCE / B Z0 / B Z+1 / B Z+2 - identical portrait orientation and emitter radiance')
draw.text((10, 8), heading, fill='white')
draw.text((10, 26), 'Unclipped black response, common normalization per crown; centroid measurements at relative50%/75% peak', fill=(180, 210, 204))
records = []
totals = {name: {str(q): [] for q in (.5, .75)} for name, folder in CASES}
for i, part in enumerate(parts):
    mask = np.asarray(Image.open(part['source_visible_front_mask']).convert('L')) > 0
    core = (cv2.erode(mask.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0) & ~ui
    ys, xs = np.where(mask)
    y0, y1, x0, x1 = int(ys.min()), int(ys.max()), int(xs.min()), int(xs.max())
    source_neck_region = core.copy()
    if 'Fang' in part['part']:
        source_neck_region[int(y0 + .5 * (y1 - y0)):, :] = False
    source_component = component(source_neck_region & white)
    target = np.array(source_component['centroid_xy'])
    row = {'part': part['part'], 'source_white_cue': source_component, 'cases': {}}
    common_peak = max(float(linear[name][core].max()) for name, folder in CASES)
    box = (max(0, x0 - 5), max(0, y0 - 5), min(963, x1 + 6), min(410, y1 + 6))
    y = 60 + 185 * i
    draw.text((8, y), part['part'], fill='white')
    draw.text((8, y + 18), 'S ' + str(np.round(target, 1).tolist()), fill=(155, 220, 210))
    crop = Image.fromarray(source).crop(box)
    crop.thumbnail((160, 145))
    sheet.paste(crop, (190, y))
    for j, (name, folder) in enumerate(CASES):
        peak = float(linear[name][core].max())
        components = {}
        for q in (.5, .75):
            c = component(core & (linear[name] >= peak * q))
            delta = np.array(c['centroid_xy']) - target
            c['delta_from_source_xy'] = delta.tolist()
            c['distance_from_source_pixels'] = float(np.linalg.norm(delta))
            totals[name][str(q)].append(c['distance_from_source_pixels'])
            components[str(q)] = c
        row['cases'][name] = {'peak_linear_luminance': peak, 'relative_peak_components': components}
        gray = np.clip(linear[name] / max(common_peak, 1e-12) * 255, 0, 255).astype(np.uint8)
        rgb = np.repeat(gray[..., None], 3, axis=2)
        rgb[~mask] = 0
        crop = Image.fromarray(rgb).crop(box)
        crop.thumbnail((160, 145))
        x = 410 + j * 270
        sheet.paste(crop, (x, y))
        c = components['0.5']
        draw.text((x, y + 149), f'centroid {np.round(c["centroid_xy"], 1).tolist()}', fill=(200, 215, 204))
        draw.text((x, y + 165), f'error {c["distance_from_source_pixels"]:.2f}px', fill=(225, 215, 175))
    records.append(row)
sheet.save(OUT / ('source_vs_receiver_split_unclipped_highlights.png' if args.receiver_split else 'source_vs_translated_unclipped_highlights.png'))
comparison = {}
for name, folder in CASES:
    comparison[name] = {q: {'mean_centroid_distance_pixels': float(np.mean(errors)),
                           'rms_centroid_distance_pixels': float(np.sqrt(np.mean(np.square(errors)))),
                           'max_centroid_distance_pixels': float(max(errors))} for q, errors in totals[name].items()}
    shape_errors = []
    for row in records:
        source_dims = np.array(row['source_white_cue']['bbox_xywh'][2:], dtype=float)
        actual_dims = np.array(row['cases'][name]['relative_peak_components']['0.5']['bbox_xywh'][2:], dtype=float)
        shape_errors.append(np.abs(actual_dims - source_dims))
    comparison[name]['bbox_shape_at_half_peak'] = {
        'mean_absolute_width_error_pixels': float(np.mean(shape_errors, axis=0)[0]),
        'mean_absolute_height_error_pixels': float(np.mean(shape_errors, axis=0)[1]),
        'note': 'Source beauty white cue and black-response half-peak are different signals; inspect actual shape, not this metric alone.'}
best = min(CASES[1:], key=lambda case: comparison[case[0]]['0.5']['mean_centroid_distance_pixels'])[0]
whole = Image.new('RGB', (963, 438 * 4), (18, 23, 26))
draw = ImageDraw.Draw(whole)
for i, (name, a) in enumerate([('SOURCE', source)] + [(n, normals[n]) for n, f in CASES]):
    draw.text((10, 438 * i + 8), name, fill='white')
    whole.paste(Image.fromarray(a), (0, 438 * i + 28))
whole.save(OUT / ('source_and_receiver_split_actuals.png' if args.receiver_split else 'source_and_translated_actuals.png'))
report = {'status': 'ACTUAL_RECEIVER_SPLIT_DIAGNOSTIC_NOT_ACCEPTANCE' if args.receiver_split else 'ACTUAL_TWO_TRANSLATION_DIAGNOSTIC_NOT_ACCEPTANCE', 'parts': records,
          'comparison': comparison, 'closer_of_two_granted_translations_at_half_peak': best,
          'limitations': ['Source beauty white cue is not an isolated physical reflection pass.',
                         'Centroid distance is one local fit metric; actual image review remains mandatory.',
                         'No geometry, diffuse rig, material constants or source files were changed.']}
if args.receiver_split:
    report['actual_visual_review'] = {
        'centroid_improves': True,
        'sliver_shape_regresses_vs_Bplus2': True,
        'evidence': ['CentralR half-peak19x10 vs source12x18.',
                     'LateralR half-peak21x10 vs source8x15.',
                     'FangL half-peak5x15 vs source11x48.'],
        'decision': 'Do not freeze the profile from centroid score alone. Further fit needs both locus and sliver shape; no mesh edit authorized by this report.'}
(OUT / 'centroid_comparison.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps({'comparison': comparison, 'closer_translation': best}, indent=2))
