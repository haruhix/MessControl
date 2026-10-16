"""CPU joint centroid/area/aspect review of two actual v26 light profiles."""
from pathlib import Path
import json
import math

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Assembly/Review/RootContactV14_ReflectionProfiles_v27'
CASES = ('A_Bplus2_UpperSix', 'B_SplitPortrait')


def component(mask):
    n, labels, stats, centers = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8)
    if n <= 1:
        return None
    i = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    y, x = np.where(labels == i)
    covariance = np.cov(np.column_stack((x, y)).T) if len(x) > 2 else np.eye(2)
    values, vectors = np.linalg.eigh(covariance)
    direction = vectors[:, -1]
    return {'area_pixels': int(stats[i, cv2.CC_STAT_AREA]), 'bbox_xywh': stats[i, :4].tolist(),
            'centroid_xy': centers[i].tolist(),
            'bbox_H_over_W': float(stats[i, cv2.CC_STAT_HEIGHT] / max(stats[i, cv2.CC_STAT_WIDTH], 1)),
            'PCA_major_minor': float(np.sqrt(max(values[-1], 0) / max(values[0], .01))),
            'PCA_angle_from_vertical_degrees': float(np.degrees(np.arctan2(abs(direction[0]), abs(direction[1]))))}


source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
parts = json.loads((ROOT / 'Jaws/ContourHero_v14/Full32/uv_manifest.json').read_text())['parts'][:6]
ui = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/CanonicalOpen/InteriorRefined/ui_occlusion.png').convert('L')) > 0
L = cv2.cvtColor(source, cv2.COLOR_RGB2LAB)[..., 0].astype(float)
r, g, b = source.transpose(2, 0, 1)
white = (r > 235) & (g > 200) & (b > 185) & (L - cv2.GaussianBlur(L, (0, 0), 3) > 5)
luma = {name: np.load(OUT / name / 'black_linear.npy')[..., :3] @ np.array([.2126, .7152, .0722]) for name in CASES}
normal = {name: np.asarray(Image.open(OUT / name / 'normal.png').convert('RGB')) for name in CASES}
sheet = Image.new('RGB', (1180, 55 + 6 * 185), (18, 23, 27))
draw = ImageDraw.Draw(sheet)
draw.text((10, 8), 'SOURCE / A B+2 / B SPLIT. Actual black response uses common per-crown scale; no display clipping.', fill='white')
draw.text((10, 26), 'Joint diagnostic: centroid/10px + 0.5*|ln area ratio| + |ln H/W ratio| + angle difference/45deg.', fill=(180, 210, 201))
records = []
for i, part in enumerate(parts):
    mask = np.asarray(Image.open(part['source_visible_front_mask']).convert('L')) > 0
    core = (cv2.erode(mask.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0) & ~ui
    yy, xx = np.where(mask)
    x0, x1, y0, y1 = int(xx.min()), int(xx.max()), int(yy.min()), int(yy.max())
    reference_region = core.copy()
    if 'Fang' in part['part']:
        reference_region[int(y0 + .5 * (y1 - y0)):] = False
    reference = component(reference_region & white)
    if not reference:
        raise RuntimeError('Missing source white cue: ' + part['part'])
    record = {'part': part['part'], 'source_white_sliver_candidate': reference, 'cases': {}}
    common_peak = max(float(luma[name][core].max()) for name in CASES)
    box = (max(0, x0 - 5), max(0, y0 - 5), min(963, x1 + 6), min(410, y1 + 6))
    y = 55 + 185 * i
    draw.text((10, y), part['part'], fill='white')
    crop = Image.fromarray(source).crop(box)
    crop.thumbnail((180, 140))
    sheet.paste(crop, (200, y))
    for j, name in enumerate(CASES):
        peak = float(luma[name][core].max())
        levels = {}
        for threshold in (.25, .5, .75):
            actual = component(core & (luma[name] >= peak * threshold))
            if not actual:
                continue
            distance = float(np.linalg.norm(np.array(actual['centroid_xy']) - reference['centroid_xy']))
            area_error = abs(math.log(actual['area_pixels'] / reference['area_pixels']))
            aspect_error = abs(math.log(actual['bbox_H_over_W'] / reference['bbox_H_over_W']))
            angle_error = abs(actual['PCA_angle_from_vertical_degrees'] - reference['PCA_angle_from_vertical_degrees'])
            actual.update({'centroid_error_pixels': distance, 'abs_log_area_error': area_error,
                            'abs_log_aspect_error': aspect_error, 'angle_error_degrees': angle_error,
                            'joint_diagnostic_score': distance / 10 + .5 * area_error + aspect_error + angle_error / 45})
            levels[str(threshold)] = actual
        record['cases'][name] = {'peak_unclipped_linear_luminance': peak, 'components_relative_peak': levels}
        gray = np.clip(luma[name] / max(common_peak, 1e-10) * 255, 0, 255).astype(np.uint8)
        a = np.repeat(gray[..., None], 3, axis=2)
        a[~mask] = 0
        crop = Image.fromarray(a).crop(box)
        crop.thumbnail((180, 140))
        x = 490 + j * 330
        sheet.paste(crop, (x, y))
        level = levels['0.5']
        draw.text((x, y + 143), f'centroid {level["centroid_error_pixels"]:.1f}px; area {level["area_pixels"]}', fill=(200, 220, 210))
        draw.text((x, y + 159), f'H/W {level["bbox_H_over_W"]:.2f}; joint {level["joint_diagnostic_score"]:.2f}', fill=(230, 215, 180))
    records.append(record)
sheet.save(OUT / 'source_vs_actual_joint_highlight_sheet.png')
whole = Image.new('RGB', (963, 438 * 3), (18, 23, 27))
draw = ImageDraw.Draw(whole)
for i, (label, array) in enumerate([('SOURCE', source)] + [(name, normal[name]) for name in CASES]):
    draw.text((10, 438 * i + 8), label, fill='white')
    whole.paste(Image.fromarray(array), (0, 438 * i + 28))
whole.save(OUT / 'source_and_two_actual_profiles.png')
comparison = {}
for name in CASES:
    comparison[name] = {}
    for level in ('.25', '.5', '.75'):
        key = str(float(level))
        values = [row['cases'][name]['components_relative_peak'][key] for row in records]
        comparison[name][key] = {metric: float(np.mean([value[metric] for value in values]))
                                 for metric in ('centroid_error_pixels', 'abs_log_area_error', 'abs_log_aspect_error', 'angle_error_degrees', 'joint_diagnostic_score')}
report = {'status': 'ACTUAL_JOINT_HIGHLIGHT_REVIEW_NOT_ACCEPTED_PROFILE_OR_FINAL_LOOK',
          'comparison': comparison, 'parts': records,
          'joint_score': 'centroid_px/10 +0.5*abs(ln actualArea/sourceArea)+abs(ln actualH/W/sourceH/W)+angleDifference/45deg',
          'limits': ['Source white beauty cue and actual black-response fractions are different signals; score is diagnostic, not physical reconstruction truth.',
                     'Source/UI/character exclusion and eroded tooth cores applied. Fang tip glint is excluded from source-neck reference cue.',
                     'All three relative peak thresholds are reported to show threshold sensitivity; actual image review is mandatory.',
                     'No material constant, geometry, normal-map or SSS change is authorized by this report.']}
(OUT / 'joint_source_highlight_comparison.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(comparison, indent=2))
