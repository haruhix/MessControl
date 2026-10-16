"""CPU first-hit UV0 round-trip control, never presented as a PBR render."""
from pathlib import Path
import argparse
import json

import cv2
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
parser = argparse.ArgumentParser()
parser.add_argument('--revision', default=str(ROOT / 'Textures/Revisions/v31_ClosedSourceTransfer_ActualUV0'))
args = parser.parse_args()
OUT = Path(args.revision).resolve()
receipt = json.loads((OUT / 'closed_source_transfer_receipt.json').read_text())
radiance = receipt.get('radiance_not_albedo', False)
binding = json.loads((OUT / ('material_binding_closed_Radiance_NOT_ALBEDO.json' if radiance else 'material_binding_closed_SOURCE_DRAFT.json')).read_text())
contract = json.loads(Path(receipt['source_contract']).read_text())
packet = np.load(OUT / 'actual_closed_camera_first_hit_UV0.npz')
uv, owner = packet['UV0'], packet['object_id']
height, width = owner.shape
sx, sy = (uv[..., 0]*2048-.5).astype(np.float32), ((1-uv[..., 1])*2048-.5).astype(np.float32)
source = np.asarray(Image.open(contract['source']).convert('RGB'))
result = np.zeros((height, width, 3), np.float32)
for material in ('MC_Enamel', 'MC_Gum'):
    raw = cv2.imread(binding['materials'][material]['radiance_not_albedo' if radiance else 'base_color'], cv2.IMREAD_UNCHANGED)[..., ::-1]
    encoded = raw.astype(np.float32)/65535
    linear = np.where(encoded <= .04045, encoded/12.92, ((encoded+.055)/1.055)**2.4)
    sampled = cv2.remap(linear, sx, sy, cv2.INTER_LINEAR)
    ids = [i for i, name in enumerate(receipt['objects']) if name.endswith('_Gum') == (material == 'MC_Gum')]
    selected = np.isin(owner, ids)
    result[selected] = sampled[selected]
encoded = np.where(result <= .0031308, result*12.92, 1.055*np.maximum(result, 0)**(1/2.4)-.055)
rgb8 = np.uint8(np.rint(np.clip(encoded, 0, 1)*255))
Image.fromarray(rgb8).save(OUT / ('closed_first_hit_UV0_Radiance_NOT_ALBEDO_CPU.png' if radiance else 'closed_first_hit_UV0_source_roundtrip_UNLIT_CPU.png'))
records = []
for target in contract['targets']:
    object_id = receipt['objects'].index(target['object'])
    known = np.asarray(Image.open(target['known']).convert('L')) > 0
    if radiance:
        path = Path(target['original_source_mask'])
        if target['object'].endswith('_Upper_Gum'):
            path = ROOT / 'Evidence/SourceAnalysis/ClosedPoseFit/Gingiva/Masks/upper_flesh_visible.png'
        elif target['object'].endswith('_Lower_Gum'):
            path = ROOT / 'Evidence/SourceAnalysis/ClosedPoseFit/Gingiva/Masks/lower_front_pad_visible.png'
        known = np.asarray(Image.open(path).convert('L')) > 0
        known &= ~(np.asarray(Image.open(ROOT / 'Textures/Revisions/v08_InverseResponse_v18/ClosedEndpointPrep_v01/UI_NoTransfer.png').convert('L')) > 0)
    else:
        known = cv2.erode(known.astype(np.uint8), np.ones((7, 7), np.uint8)).astype(bool)
    known &= owner == object_id
    if not known.any():
        continue
    error = np.abs(rgb8[known].astype(np.float32)-source[known])
    records.append({'object': target['object'], 'shared_known_first_hit_pixels': int(known.sum()),
        'RGB8_abs_error_p50_p90': np.percentile(error, [50, 90]).tolist(),
        'source_RGB8_median': np.median(source[known], axis=0).tolist(),
        'roundtrip_RGB8_median': np.median(rgb8[known], axis=0).tolist()})
report = {'status': 'CPU_RADIANCE_NOT_ALBEDO_ROUNDTRIP_NOT_NATIVE_ACCEPTANCE' if radiance else 'CPU_UNLIT_COLOR_TRANSFER_ROUNDTRIP_NOT_PBR_APPEARANCE_ACCEPTANCE',
    'records': records, 'limits': ['Source appearance remains lighting-confounded.', 'Hidden/back colors are explicit continuation; no measured truth.', 'Actual shader/lighting/texture-state blends still require native/runtime views.']}
(OUT / 'closed_source_roundtrip_review.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report, indent=2))
