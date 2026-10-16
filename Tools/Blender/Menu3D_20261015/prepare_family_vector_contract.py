"""CPU observed contours for Day01/02; cervical curves are OCCLUSION constraints.

No mesh/DCC authoring and no physical roots inferred from the pixel mask edge.
"""
from pathlib import Path
import hashlib
import json
import math
import os

os.environ['OMP_NUM_THREADS'] = '2'
os.environ['OPENBLAS_NUM_THREADS'] = '2'
import cv2
import numpy as np

ROOT = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
BASE = ROOT / 'Evidence/SourceAnalysis'


def smooth_vector(mask, n=192):
    # Pixel-center contour +.5 becomes a subpixel cell-boundary approximation.
    contours, _ = cv2.findContours(mask.astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    contour = max(contours, key=cv2.contourArea)
    raw = cv2.approxPolyDP(contour, .65, True).reshape(-1, 2).astype(float) + .5
    controls = []
    for a, b in zip(raw, np.roll(raw, -1, axis=0)):
        steps = max(1, math.ceil(np.linalg.norm(b-a)/6))
        controls.extend(a+(b-a)*i/steps for i in range(steps))
    controls = np.asarray(controls)
    dense = []
    for i, a in enumerate(controls):
        b, prev, after = controls[(i+1) % len(controls)], controls[i-1], controls[(i+2) % len(controls)]
        d = max(np.linalg.norm(b-a), 1e-6)
        ma = (b-prev)/max(np.linalg.norm(a-prev)+d, 1e-6)*d
        mb = (after-a)/max(d+np.linalg.norm(after-b), 1e-6)*d
        for t in np.linspace(0, 1, max(8, int(d*4)), endpoint=False):
            dense.append((2*t**3-3*t*t+1)*a+(t**3-2*t*t+t)*ma+(-2*t**3+3*t*t)*b+(t**3-t*t)*mb)
    dense = np.asarray(dense)
    closed = np.r_[dense, dense[:1]]
    lengths = np.r_[0, np.cumsum(np.linalg.norm(np.diff(closed, axis=0), axis=1))]
    distances = np.linspace(0, lengths[-1], n, endpoint=False)
    return np.column_stack([np.interp(distances, lengths, closed[:, j]) for j in range(2)])


for day in ('DAY01', 'DAY02'):
    folder = BASE / (day + '_Closed')
    inventory_path = folder / 'part_inventory.json'
    inv = json.loads(inventory_path.read_text())
    source = cv2.imread(str(folder / 'source.png'))
    H, W = source.shape[:2]
    label = cv2.imread(str(folder / 'Masks/label_occlusion.png'), 0) > 127
    observed_domain = ~(cv2.dilate(label.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0)
    observed_domain[:, :2] = observed_domain[:, -2:] = False
    out = folder / 'VectorContract_v01'
    out.mkdir(exist_ok=True)
    overlay = source.copy()
    records = []
    for part in inv['parts']:
        mask_path = folder / part['crown_mask'].replace('\\', '/')
        original = cv2.imread(str(mask_path), 0) > 127
        # A badge can split one physical tooth into disconnected observed
        # fragments. Bridge ONLY its unknown occluded area from the existing
        # manual seed; never turn the graphic's cut edge into a tooth notch.
        seed = np.zeros((H, W), np.uint8)
        cv2.fillPoly(seed, [np.asarray(part['polygon'], np.int32)], 1)
        completed = original | (label & (seed > 0))
        closed = cv2.morphologyEx(completed.astype(np.uint8), cv2.MORPH_CLOSE,
                                 cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))) > 0
        completed[label] = closed[label]
        vector = smooth_vector(completed)
        # Raster is a diagnostic of the observed contour only, not a whole-solid
        # silhouette target once a real hidden cervical root is introduced.
        high = np.zeros((H*4, W*4), np.uint8)
        cv2.fillPoly(high, [np.round(vector*4).astype(np.int32)], 255)
        raster = cv2.resize(high, (W, H), interpolation=cv2.INTER_AREA) > 127
        first = (cv2.Canny(original.astype(np.uint8)*255, 50, 100) > 0) & observed_domain
        second = (cv2.Canny(raster.astype(np.uint8)*255, 50, 100) > 0) & observed_domain
        da = cv2.distanceTransform((~first).astype(np.uint8), cv2.DIST_L2, 5)
        db = cv2.distanceTransform((~second).astype(np.uint8), cv2.DIST_L2, 5)
        distances = np.r_[da[second], db[first]]
        metrics = {'observed_mask_iou': float((original & raster & observed_domain).sum() / max(1, ((original | raster) & observed_domain).sum())),
                   'boundary_p95_px': float(np.percentile(distances, 95)),
                   'max_boundary_px': float(distances.max()),
                   'exclusion': 'Badge plus2px boundary uncertainty and2px cropped side strips are not anatomical observations.'}
        record = {
            'part': part['name'], 'row': part['row'],
            'ordinal_left_to_right': part['ordinal_left_to_right'],
            'visible_mask': str(mask_path),
            'visible_mask_sha256': hashlib.sha256(mask_path.read_bytes()).hexdigest(),
            'whole_outline_candidate_xy': vector.tolist(), 'observed_contour_fit': metrics,
            'inferred_label_bridge_pixels': int((completed & label).sum()),
            'label_bridge_policy': 'Existing manual seed + bounded hidden-area close; inferred only, not observed source anatomy. May not be scored as observed.',
            'cervical_occlusion_path_xy': part['collar'],
            'cervical_semantics': 'First-hit ownership crossing between opaque tooth and gum; NOT the physical tooth endcap.',
            'observed_apex_xy': part.get('observed_apex_xy'),
            'apex_status': part.get('apex_status'),
            'crop_or_label_policy': 'Unobserved continuation requires a volumetric hidden body; do not model a physical crop wall or label notch.',
            'hidden_root_policy': 'Continue actual tooth under opaque gingiva, with rounded gingival return and compound camera-ray ownership gate.',
            'source_depth_policy': 'No source Y/depth supplied. Depth, occlusal/back surfaces and unprovided open pose must be documented as inferred.',
        }
        records.append(record)
        cv2.polylines(overlay, [np.round(vector).astype(np.int32)], True, (60, 240, 180), 1, cv2.LINE_AA)
        cv2.polylines(overlay, [np.round(part['collar']).astype(np.int32)], False, (230, 160, 50), 1, cv2.LINE_AA)
    report = {
        'status': 'CPU_VECTOR_CONSTRAINTS_NOT_MODELED_OR_ACCEPTED',
        'source_inventory': str(inventory_path),
        'source_inventory_sha256': hashlib.sha256(inventory_path.read_bytes()).hexdigest(),
        'source_size': [W, H], 'camera': inv['world_projection'],
        'parts': records,
        'next_gate': 'Lead vector overlay review, source-cusp/crop constraints, then real volumetric modeling and compound gum ownership.',
        'forbidden_shortcut': 'No literal extrusion of pixel stairs, glints, cervical mask edge or label footprint.',
    }
    (out / 'vector_contract.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    cv2.imwrite(str(out / 'observed_contour_and_cervical_overlay.png'), overlay)
    print(day, len(records), 'parts; worst p95', max(p['observed_contour_fit']['boundary_p95_px'] for p in records))
