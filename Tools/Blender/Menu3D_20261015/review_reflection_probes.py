"""CPU-only source/actual review of the three isolated reflection-card probes."""
from pathlib import Path
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Assembly/Review/ReflectionProbes_v20'


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def component(mask):
    n, labels, stats, centers = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8)
    if n < 2:
        return {'pixels': 0}
    i = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    y, x = np.where(labels == i)
    covariance = np.cov(np.column_stack((x, y)).T) if len(x) > 2 else np.eye(2)
    values, vectors = np.linalg.eigh(covariance)
    direction = vectors[:, -1]
    return {'pixels': len(x), 'bbox_xywh': stats[i, :4].tolist(),
            'centroid_xy': centers[i].tolist(),
            'PCA_ratio': float(np.sqrt(max(values[-1], 0) / max(values[0], .01))),
            'axis_degrees_from_vertical': float(np.degrees(np.arctan2(abs(direction[0]), abs(direction[1]))))}


source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
parts = load(ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json')['parts'][:6]
manifest = load(OUT / 'reflection_probe_manifest.json')
probes = [p['name'] for p in manifest['probes']]
normal = {p: np.asarray(Image.open(OUT / p / 'normal.png').convert('RGB')) for p in probes}
linear = {p: np.load(OUT / p / 'black_linear.npy')[..., :3] for p in probes}
luma = {p: a @ np.array([.2126, .7152, .0722]) for p, a in linear.items()}
ui = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/CanonicalOpen/InteriorRefined/ui_occlusion.png').convert('L')) > 0
source_lab_l = cv2.cvtColor(source, cv2.COLOR_RGB2LAB)[..., 0].astype(float)
source_local_l = source_lab_l - cv2.GaussianBlur(source_lab_l, (0, 0), 3)
r, g, b = source.transpose(2, 0, 1)
white = (r > 235) & (g > 200) & (b > 185) & (source_local_l > 5)

whole = Image.new('RGB', (963, 4 * 438), (18, 22, 26))
draw = ImageDraw.Draw(whole)
for row, (label, a) in enumerate([('SOURCE B / canonical crop', source)] + list(normal.items())):
    y = row * 438
    draw.text((10, y + 8), label + (' - ONE CARD, matched emitter radiance' if row else ''), fill='white')
    whole.paste(Image.fromarray(a), (0, y + 28))
whole.save(OUT / 'source_and_three_actuals.png')

sheet = Image.new('RGB', (1280, 60 + 6 * 185), (19, 23, 26))
draw = ImageDraw.Draw(sheet)
draw.text((10, 8), 'SOURCE / A OVERHEAD / B FRONT PORTRAIT / C HIGH PORTRAIT', fill='white')
draw.text((10, 25), 'Actual grayscale = unclipped black-response normalized to a COMMON per-tooth peak across A/B/C.', fill=(180, 205, 200))
records = []
for i, part in enumerate(parts):
    mask = np.asarray(Image.open(part['source_visible_front_mask']).convert('L')) > 0
    core = cv2.erode(mask.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0
    core &= ~ui
    ys, xs = np.where(mask)
    x0, x1, y0, y1 = int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max())
    source_neck_glint_region = core.copy()
    if 'Fang' in part['part']:
        source_neck_glint_region[int(y0 + .5 * (y1 - y0)):, :] = False
    src_component = component(source_neck_glint_region & white)
    global_peak = max(float(luma[p][core].max()) for p in probes)
    row = {'part': part['part'], 'source_white_sliver_candidate': src_component, 'probes': {}}
    y = 60 + i * 185
    draw.text((8, y), part['part'], fill='white')
    draw.text((8, y + 18), f'S {src_component.get("bbox_xywh")}', fill=(150, 220, 211))
    box = (max(0, x0 - 5), max(0, y0 - 5), min(963, x1 + 6), min(410, y1 + 6))
    source_crop = Image.fromarray(source).crop(box)
    source_crop.thumbnail((160, 155))
    sheet.paste(source_crop, (190, y))
    for j, p in enumerate(probes):
        peak = float(luma[p][core].max())
        relative = luma[p] / max(peak, 1e-12)
        levels = {str(q): component(core & (relative >= q)) for q in (.25, .5, .75)}
        row['probes'][p] = {'peak_unclipped_linear_luminance': peak,
                            'common_peak_fraction': peak / max(global_peak, 1e-12),
                            'components_relative_peak': levels,
                            'core_pixels_display_clip': int((luma[p][core] > 2 ** -.5).sum())}
        gray = np.clip(luma[p] / max(global_peak, 1e-12) * 255, 0, 255).astype(np.uint8)
        a = np.repeat(gray[..., None], 3, axis=2)
        a[~mask] = 0
        crop = Image.fromarray(a).crop(box)
        crop.thumbnail((160, 155))
        left = 420 + j * 270
        sheet.paste(crop, (left, y))
        draw.text((left, y + 155), f'.5 {levels["0.5"].get("bbox_xywh")}', fill=(223, 211, 178))
        draw.text((left, y + 169), f'peak {peak:.3f}', fill=(180, 205, 200))
    records.append(row)
sheet.save(OUT / 'source_vs_unclipped_highlights.png')
report = {'status': 'THREE_ACTUAL_CARD_PROBES_REVIEW_NOT_ACCEPTANCE',
          'source': str(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png'),
          'parts': records,
          'findings': [
              'B front portrait produces long vertical fang reflections without geometry edits; overhead A selects shoulder arcs.',
              'Central-tooth highlights rotate toward vertical with B but remain too short/low versus source; card direction is a cause, not the entire fit.',
              'C high portrait retains diagonal or shoulder-like reflections; B is the better orientation candidate.',
              'Do not change roughness or geometry from this comparison alone: first align reflection loci on four central teeth separately from fangs.',
              'Further isolated light-position/aim review or a bounded Y-only front-depth test requires lead approval; no source mesh was modified.'
          ],
          'limits': ['Source white-mask segmentation is a beauty-image cue, not isolated physical specular.',
                     'Matched area radiance does not imply matched incident irradiance or source-solid-angle per crown.',
                     'Largest relative-peak component can include multiple specular lobes; inspect actual images.',
                     'Single-card A is not the full original two-card rig.']}
(OUT / 'highlight_comparison.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps([{'part': r['part'], 'source': r['source_white_sliver_candidate'].get('bbox_xywh'),
                   **{p: r['probes'][p]['components_relative_peak']['0.5'].get('bbox_xywh') for p in probes}} for r in records], indent=2))
