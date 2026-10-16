"""Bounded RADIANCE exclusion: UI + reference worker + its cast shadow only.

No source wet glints or natural mouth shadows are excluded. Regions which were
conservative PBR inverse-fit guards are not appropriate radiance exclusions.
"""
from pathlib import Path
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Textures/Revisions/v39b_OpenRadiance_TightExclusion_Prep'
INTERIOR = ROOT / 'Evidence/SourceAnalysis/CanonicalOpen/InteriorRefined'
W, H = 963, 410


def read_mask(path):
    return np.asarray(Image.open(path).convert('L')) > 0


def main():
    if OUT.exists() and any(OUT.iterdir()):
        raise RuntimeError('Preserve prior mask evidence')
    OUT.mkdir(parents=True, exist_ok=True)
    source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
    old = read_mask(ROOT / 'Textures/Revisions/v37c_Open_Radiance_NOT_ALBEDO_Harmonic/Source_UI_Worker_OldShadow_NoTransfer.png')
    old_ui = read_mask(INTERIOR / 'ui_occlusion.png')
    yy, xx = np.mgrid[:H, :W]
    ui = np.zeros((H, W), np.uint8)
    # Filled controls hide their entire underlying surface, including glyphs.
    image = Image.fromarray(ui)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((24, 48, 235, 93), radius=12, fill=255)
    draw.rounded_rectangle((330, 202, 625, 264), radius=32, fill=255)
    # The Back interior is visibly darkened by the graphic treatment. Keep its
    # compact true footprint unknown rather than preserve a UI-shaped stain.
    draw.rounded_rectangle((402, 278, 551, 320), radius=22, fill=255)
    ui = np.asarray(image).copy() > 0
    # Existing source-inspected title glyphs and arrows are already tight;
    # unlike their former bounding rectangles, cavity between them is visible.
    title_roi = (xx >= 400) & (xx <= 555) & (yy >= 120) & (yy <= 192)
    arrows_roi = (xx >= 904) & (xx <= 947) & (yy >= 125) & (yy <= 262)
    glyphs = old_ui & (title_roi | arrows_roi)
    # Include the title's dark antialias/drop-shadow fringe, not only white ink.
    ui |= cv2.dilate(glyphs.astype(np.uint8), np.ones((7, 7), np.uint8)).astype(bool)
    # BACK is an outline and text, not an opaque pill. Preserve the natural
    # burgundy source surface visible through and around its outline.
    r, g, b = source.transpose(2, 0, 1).astype(np.float32)
    back_roi = (xx >= 399) & (xx <= 555) & (yy >= 276) & (yy <= 323)
    back_white = (r > 130) & (g > 100) & (b > 95) & (g > r*.65)
    ui |= back_roi & back_white
    ui = cv2.dilate(ui.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool)
    worker = cv2.dilate(read_mask(INTERIOR / 'character_occlusion.png').astype(np.uint8), np.ones((9, 9), np.uint8)).astype(bool)
    # Source-inspected dark contact pool below the two feet/brush. This bounded
    # polygon excludes the OLD character's shadow, not broad tongue shading.
    shadow_image = Image.new('L', (W, H))
    ImageDraw.Draw(shadow_image).polygon([(94, 315), (119, 320), (149, 321), (177, 316), (191, 319),
        (208, 326), (216, 333), (207, 340), (179, 344), (149, 342), (121, 339), (103, 332), (95, 324)], fill=255)
    shadow = np.asarray(shadow_image) > 0
    excluded = ui | worker | shadow
    for name, value in [('UI_TightOnly', ui), ('ReferenceWorker_TightOnly', worker), ('ReferenceWorker_ContactShadow_Uncertain', shadow),
                         ('Source_UI_ReferenceWorker_Shadow_NoTransfer', excluded), ('Recovered_VisibleSourcePixels', old & ~excluded),
                         ('Additional_TightGuardPixels', excluded & ~old)]:
        Image.fromarray(np.uint8(value)*255).save(OUT / f'{name}.png')
    overlay = source.copy().astype(np.float32)
    overlay[old & ~excluded] = overlay[old & ~excluded]*.4 + np.array([30, 245, 150])*.6
    overlay[excluded] = overlay[excluded]*.4 + np.array([255, 55, 180])*.6
    Image.fromarray(np.uint8(np.clip(overlay, 0, 255))).save(OUT / 'TightRadianceExclusion_Review.png')
    jaw = json.loads((ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json').read_text())
    posterior = {}
    for part in jaw['parts']:
        if not part['part'].startswith('Upper_Posterior_L'):
            continue
        observed = read_mask(part['source_visible_front_mask'])
        posterior[part['object']] = {'previous_known': int((observed & ~old).sum()), 'new_known': int((observed & ~excluded).sum()),
                                     'recovered_visible_pixels': int((observed & old & ~excluded).sum())}
    report = {'status': 'CPU_SOURCE_INSPECTED_TIGHT_RADIANCE_EXCLUSION_NOT_PBR_GUARD',
        'old_excluded_pixels': int(old.sum()), 'new_excluded_pixels': int(excluded.sum()),
        'recovered_natural_source_pixels': int((old & ~excluded).sum()), 'new_tight_guard_pixels': int((excluded & ~old).sum()),
        'frame_pixels': W*H, 'UI_pixels': int(ui.sum()), 'worker_pixels': int(worker.sum()), 'uncertain_worker_shadow_pixels': int(shadow.sum()),
        'upper_left_posterior_source_masks': posterior,
        'policy': 'Preserve all observed wet glints and natural mouth shadows. Exclude actual opaque UI footprints, UI glyphs, worker silhouette and bounded old-worker shadow only.',
        'limits': ['Contact-shadow polygon is source-inspected attribution, not uniquely recoverable shadow evidence.',
                   'True underlying tooth/gingiva hidden by opaque tab remains unknown; do not call its continuation 1:1.',
                   'Source material masks already contain conservative holes. Actual first-hit eroded same-material interiors can recover visible source inside obsolete broad guards.']}
    (OUT / 'tight_radiance_exclusion_receipt.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
