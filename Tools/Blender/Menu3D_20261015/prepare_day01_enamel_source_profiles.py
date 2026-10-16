"""Reference-derived DAY01 enamel color zoning; CPU evidence only.

Source beauty is lighting-confounded, not uniquely recovered physical albedo.
Bright reflections/UI never seed diffuse color and never become normals.
"""
from pathlib import Path
import hashlib
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
SRC = ROOT / 'Evidence/SourceAnalysis/DAY01_Closed'
OUT = ROOT / 'Textures/Revisions/DAY01_Closed_SourceProfiles_v01'
W, H = 963, 212
cv2.setNumThreads(2)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def linear(rgb):
    value = np.asarray(rgb, np.float32)/255
    return np.where(value <= .04045, value/12.92, ((value+.055)/1.055)**2.4)


def srgb(rgb):
    value = np.clip(rgb, 0, 1)
    return np.uint8(np.rint(np.where(value <= .0031308, value*12.92, 1.055*np.maximum(value, 0)**(1/2.4)-.055)*255))


def mask(path):
    return np.asarray(Image.open(path).convert('L')) > 0


def main():
    if OUT.exists() and any(OUT.iterdir()):
        raise RuntimeError('Fresh source-prep branch required')
    OUT.mkdir(parents=True, exist_ok=True)
    source = np.asarray(Image.open(SRC / 'source.png').convert('RGB'))
    assert source.shape == (H, W, 3)
    vector = load(SRC / 'VectorContract_v01/vector_contract.json')
    assert len(vector['parts']) == 10
    ui = cv2.dilate(mask(SRC / 'Masks/label_occlusion.png').astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool)
    lab = cv2.cvtColor(source, cv2.COLOR_RGB2LAB).astype(np.float32)
    high = lab[..., 0]-cv2.GaussianBlur(lab[..., 0], (0, 0), 3)
    r, g, b = source.transpose(2, 0, 1).astype(np.float32)
    reflections = ((r > 240) & (g > 218) & (b > 197)) | ((lab[..., 0] > 208) & (high > 7) & (g > 190) & (b > 173))
    reflections = cv2.dilate(reflections.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool)
    yy, xx = np.mgrid[:H, :W]
    truth = linear(source)
    known_union = np.zeros((H, W), bool)
    crown_union = known_union.copy()
    profile_preview = np.zeros_like(source)
    records = []
    for part in vector['parts']:
        name = part['part']
        observed = mask(part['visible_mask'])
        crown_union |= observed
        known = cv2.erode(observed.astype(np.uint8), np.ones((5, 5), np.uint8)).astype(bool) & ~ui & ~reflections
        if known.sum() < 20:
            raise RuntimeError('Insufficient observed enamel pigment for ' + name)
        known_union |= known
        # Explicit cervical source path is an opaque gum/tooth crossing,
        # never an assumption that the complete physical tooth ends there.
        path = np.asarray(part['cervical_occlusion_path_xy'], np.float32)
        order = np.argsort(path[:, 0])
        neck = np.interp(np.arange(W), path[order, 0], path[order, 1])
        ys, xs = np.where(observed)
        tip_y = int(ys.max())
        t = np.clip((yy-neck[None])/np.maximum(tip_y-neck[None], 1), 0, 1)
        median = np.median(truth[known], axis=0)
        nodes = np.linspace(0, 1, 21)
        values, counts = [], []
        for node in nodes:
            selected = known & (np.abs(t-node) < .075)
            counts.append(int(selected.sum()))
            values.append(np.median(truth[selected], axis=0) if selected.sum() >= 15 else median)
        values = cv2.GaussianBlur(np.array(values, np.float32), (1, 5), .65)
        field = np.stack([np.interp(t.ravel(), nodes, values[:, i]).reshape(H, W) for i in range(3)], axis=-1)
        profile_preview[observed] = srgb(field)[observed]
        folder = OUT / name
        folder.mkdir()
        Image.fromarray(np.dstack((source, np.uint8(known)*255))).save(folder / f'{name}_QualifiedSourceAppearance_RGBA.png')
        Image.fromarray(np.dstack((srgb(field), np.uint8(observed)*255))).save(folder / f'{name}_ReflectionExcluded_ZoneProfile_RGBA.png')
        zones = {}
        for label, low, high_t in [('cervical', 0, .22), ('body', .22, .75), ('incisal', .75, 1.001)]:
            selected = known & (t >= low) & (t < high_t)
            colors = source[selected]
            zones[label] = {'qualified_pixels': int(selected.sum()), 'median_sRGB8': np.median(colors, axis=0).tolist() if len(colors) else None,
                            'RGB8_p10_p90': np.percentile(colors, [10, 90], axis=0).tolist() if len(colors) else None,
                            'status': 'OBSERVED_LIT_APPEARANCE_NOT_RECOVERED_PHYSICAL_ALBEDO'}
            Image.fromarray(np.uint8(selected)*255).save(folder / f'{name}_{label}_Observed.png')
        record = {'part': name, 'material': 'MC_Enamel', 'source_visible_mask': part['visible_mask'],
                  'source_visible_mask_sha256': sha(part['visible_mask']), 'source_bbox_xyxy': [int(xs.min()), int(ys.min()), int(xs.max()+1), int(ys.max()+1)],
                  'qualified_source_pixels': int(known.sum()), 'excluded_reflection_pixels': int((observed & reflections).sum()),
                  'cervical_semantics': part['cervical_semantics'], 'zones': zones,
                  'profile': [{'t': float(tval), 'linear_rgb': values[i].tolist(), 'known_pixels': counts[i]} for i, tval in enumerate(nodes)],
                  'hidden_back_policy': 'Same-part body median/profile continuation is inferred; do not duplicate front reflections.',
                  'normal_roughness_SSS_status': 'Not recovered from brightness. No physical normal, roughness or SSS authored by this color-profile tool.'}
        (folder / f'{name}_SourceProfile.json').write_text(json.dumps(record, indent=2), encoding='utf8')
        records.append(record)
    for filename, value in [('DAY01_QualifiedPigmentKnown', known_union), ('DAY01_WhiteReflection_NoAlbedo', reflections & crown_union), ('DAY01_UI_NoTransfer', ui)]:
        Image.fromarray(np.uint8(value)*255).save(OUT / f'{filename}.png')
    Image.fromarray(np.dstack((source, np.uint8(known_union)*255))).save(OUT / 'DAY01_QualifiedSourceAppearance_RGBA.png')
    sheet = Image.new('RGB', (W, H*3+72), '#182229')
    draw = ImageDraw.Draw(sheet)
    views = [('SOURCE BEAUTY — lighting-confounded', source), ('QUALIFIED COLOR — UI / bright reflection excluded', np.where(known_union[..., None], source, 24).astype(np.uint8)),
             ('VERTICAL SOURCE ZONING — not physical albedo', profile_preview)]
    for index, (label, image) in enumerate(views):
        draw.text((8, index*(H+24)+5), label, fill='#eee8da')
        sheet.paste(Image.fromarray(image), (0, index*(H+24)+24))
    sheet.save(OUT / 'DAY01_SourceColorProfiles_Review.png')
    report = {'status': 'CPU_SOURCE_PALETTE_PROFILE_EVIDENCE_NOT_APPLIED_NOT_PBR_ACCEPTED', 'source': str(SRC / 'source.png'),
              'source_sha256': sha(SRC / 'source.png'), 'vector_contract': str(SRC / 'VectorContract_v01/vector_contract.json'),
              'vector_sha256': sha(SRC / 'VectorContract_v01/vector_contract.json'), 'parts': records,
              'observed_scope': '10 closed upper crowns only. Lower/open/back/depth are unobserved and explicitly inferred.',
              'DCC_Painter_GPU_used': False, 'geometry_atlas_or_current_materials_modified': False}
    (OUT / 'DAY01_SourceColorProfiles_Manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps({'status': report['status'], 'parts': len(records), 'out': str(OUT), 'qualified_pixels': int(known_union.sum())}, indent=2))


if __name__ == '__main__':
    main()
