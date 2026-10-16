"""CPU-only whole-material candidate; no Blender/Painter invocation or pigment edits.

The Gum UV0 height is an inferred physical relief draft on the actual V04 Open
surface. It is not a mesh-map bake, and its metric is not certified for Closed.
Reflection color/brightness is never converted into normal or displacement.
"""
from pathlib import Path
import copy
import hashlib
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Textures/Revisions/v28_WholeMaterialFinish_CPU'
OUT.mkdir(parents=True, exist_ok=True)
cv2.setNumThreads(2)
N = 2048
SOURCE = ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png'
ACTUAL = ROOT / 'Assembly/Review/RootContactV14_ReflectionProfiles_v27/A_Bplus2_UpperSix/normal.png'
BINDING = ROOT / 'Textures/Revisions/v08_InverseResponse_v18/SmoothGain_Full32_v23/material_binding_continuous_gain.json'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def gray(path):
    image = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if image.ndim == 3:
        image = image[..., 0]
    return image.astype(np.float32) / (65535 if image.dtype == np.uint16 else 255)


def save_scalar(path, field):
    Image.fromarray(np.uint16(np.rint(np.clip(field, 0, 1) * 65535))).save(path)


def evidence():
    source = np.asarray(Image.open(SOURCE).convert('RGB'))
    actual = np.asarray(Image.open(ACTUAL).convert('RGB'))
    assert source.shape == actual.shape == (410, 963, 3)
    enamel = ROOT / 'Textures/Revisions/v08_EnamelFull32_v13/SourceFields'
    interior = ROOT / 'Evidence/SourceAnalysis/CanonicalOpen/InteriorRefined'
    excluded = (gray(interior / 'ui_occlusion.png') > .5) | (gray(interior / 'character_occlusion.png') > .5)
    # Existing authored character differs from concept mascot. Exclude its full
    # actual left ROI, not just the source silhouette, from regional evidence.
    excluded[185:350, 55:245] = True
    known = gray(enamel / 'Full32_SourceConfidence.png') > .5
    visible = gray(enamel / 'Full32_ObservedVisibleMask.png') > .5
    gums = ROOT / 'Textures/Revisions/v05_SourceLocked/Gingiva'
    yy = np.arange(410)[:, None]
    masks = {'enamel_upper': visible & (yy < 180), 'enamel_lower': visible & (yy > 280),
             'gum_upper': gray(gums / 'upper_gingiva_visible.png') > .5,
             'gum_lower': gray(gums / 'lower_gingiva_visible_strict.png') > .5}
    report = {}
    for name, mask in masks.items():
        mask = cv2.erode(mask.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool) & ~excluded
        report[name] = {'pixels': int(mask.sum()), 'note': 'Shared source semantic region; boundaries/UI/both characters excluded. Beauty-source remains lighting-confounded.'}
        for label, image in [('reference', source), ('native_v27_A', actual)]:
            lab = cv2.cvtColor(image, cv2.COLOR_RGB2LAB).astype(np.float32)
            lum = lab[..., 0]
            highpass = lum - cv2.GaussianBlur(lum, (0, 0), 1.2)
            white = (image[..., 0] > 235) & (image[..., 1] > 200) & (image[..., 2] > 185)
            body = mask & ~white
            if name.startswith('enamel'):
                body &= known
            values = image[body]
            report[name][label] = {'body_pixels': len(values),
                'body_RGB8_median': np.median(values, axis=0).tolist() if len(values) else None,
                'L8_p10_p50_p90_p99': np.percentile(lum[mask], [10, 50, 90, 99]).tolist(),
                'bright_warm_white_fraction': float(np.mean(white[mask])),
                'fine_L8_highpass_abs_p50_p90': np.percentile(np.abs(highpass[mask]), [50, 90]).tolist()}
    # Exact native crops, same twofold enlargement. Never stretch a strip to fit.
    boxes = [(225, 0, 735, 110), (230, 341, 865, 410), (350, 0, 638, 22), (695, 0, 940, 40)]
    sheet = Image.new('RGB', (1300, sum((b[3]-b[1])*2+34 for b in boxes)), '#17181c')
    draw = ImageDraw.Draw(sheet)
    top = 0
    for number, box in enumerate(boxes):
        draw.text((8, top + 3), f'{number+1}: SOURCE LEFT / V27 A RIGHT | native crop enlarged 2x', fill='white')
        for offset, original in [(0, Image.fromarray(source)), (650, Image.fromarray(actual))]:
            crop = original.crop(box)
            # Width can exceed a half for the broad lower crop: preserve exact
            # same scale on both sides by using a shared, bounded scale there.
            scale = min(2., 640 / crop.width)
            crop = crop.resize((round(crop.width * scale), round(crop.height * scale)), Image.Resampling.NEAREST)
            sheet.paste(crop, (offset, top + 26))
        top += (box[3]-box[1])*2+34
    sheet.save(OUT / 'reference_vs_v27_whole_finish_regions.png')
    (OUT / 'reference_region_finish_evidence.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return report


def inferred_enamel_roughness(old, manifest):
    result = np.clip(old * .96, .19, .29)
    for index, part in enumerate(manifest['parts']):
        for side, key in enumerate(('front_uv_rect', 'back_uv_rect')):
            u0, v0, u1, v1 = part[key]
            xa, xb = int(np.floor(u0*N)), int(np.ceil(u1*N))
            ya, yb = int(np.floor((1-v1)*N)), int(np.ceil((1-v0)*N))
            rng = np.random.default_rng(28100 + index*2 + side)
            field = rng.normal(size=(yb-ya, xb-xa)).astype(np.float32)
            # Very quiet elongated irregular roughness only. This is not an
            # assertion that the source contains measured enamel prism normals.
            field = cv2.GaussianBlur(field, (0, 0), .7, sigmaY=4.)
            field /= max(float(field.std()), 1e-6)
            result[ya:yb, xa:xb] += np.clip(field * .0035, -.009, .009)
    return np.clip(result, .19, .29)


def gum_physical_height(field_bands=None, rms_world=.00010, bound_world=.00045):
    # One nonrepeating world-space field across the whole mouth, not repeated
    # study tiles. Independent scales break a periodic papilla/dot appearance.
    extent_x, extent_z, pitch = 20., 10., 1/256
    width, height = int(extent_x/pitch), int(extent_z/pitch)
    rng = np.random.default_rng(28042026)
    field = np.zeros((height, width), np.float32)
    bands = field_bands or [(.65, .55), (1.1, .30), (1.8, .15)]
    for sigma, weight in bands:
        component = rng.normal(size=(height, width)).astype(np.float32)
        component = cv2.GaussianBlur(component, (0, 0), sigma)
        component /= max(float(component.std()), 1e-6)
        field += weight * component
    field -= cv2.GaussianBlur(field, (0, 0), 8.)
    field *= rms_world / max(float(field.std()), 1e-6)
    hmax = bound_world
    field = np.clip(field, -hmax, hmax)
    atlas = np.zeros((N, N), np.float32)
    claimed = np.zeros((N, N), np.uint8)
    source_hashes = {}
    for jaw in ('upper', 'lower'):
        path = ROOT / f'Gums/V04/Constraints/{jaw}_surface.npz'
        source_hashes[jaw] = sha(path)
        data = np.load(path)
        points, faces, uvs = data['vertices'], data['faces'], data['uv']
        semantics = data['semantic']
        for face, uv, semantic in zip(faces, uvs, semantics):
            if str(semantic) != 'front_front':
                continue
            for ids in ((0, 1, 2), (0, 2, 3)):
                ids = list(ids)
                triangle_uv = uv[ids] * [N, -N] + [0, N]
                p0, p1, p2 = triangle_uv
                xmin = max(0, int(np.floor(triangle_uv[:, 0].min())))
                xmax = min(N-1, int(np.ceil(triangle_uv[:, 0].max())))
                ymin = max(0, int(np.floor(triangle_uv[:, 1].min())))
                ymax = min(N-1, int(np.ceil(triangle_uv[:, 1].max())))
                if xmax < xmin or ymax < ymin:
                    continue
                e1, e2 = p1-p0, p2-p0
                denominator = e1[0]*e2[1]-e1[1]*e2[0]
                if abs(denominator) < 1e-10:
                    continue
                yy, xx = np.mgrid[ymin:ymax+1, xmin:xmax+1]
                a = ((xx+.5-p0[0])*(p2[1]-p0[1]) - (yy+.5-p0[1])*(p2[0]-p0[0])) / denominator
                b = ((p1[0]-p0[0])*(yy+.5-p0[1]) - (p1[1]-p0[1])*(xx+.5-p0[0])) / denominator
                inside = (a >= -1e-6) & (b >= -1e-6) & (a+b <= 1+1e-6)
                world = points[face[ids]][0] + a[..., None]*(points[face[ids]][1]-points[face[ids]][0]) + b[..., None]*(points[face[ids]][2]-points[face[ids]][0])
                sample = cv2.remap(field, ((world[..., 0]+extent_x/2)/pitch).astype(np.float32), ((extent_z/2-world[..., 2])/pitch).astype(np.float32), cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
                atlas[ymin:ymax+1, xmin:xmax+1][inside] = sample[inside]
                claimed[ymin:ymax+1, xmin:xmax+1][inside] = 1
    assert claimed.sum() > 100000, 'Gum front chart contract not found'
    # Fade height smoothly into the chart edge; hidden caps remain undetailed.
    distance = cv2.distanceTransform(claimed, cv2.DIST_L2, 3)
    fade = np.clip(distance/5, 0, 1)
    atlas *= fade*fade*(3-2*fade)
    folder = OUT / 'MC_Gum'
    folder.mkdir(exist_ok=True)
    path = folder / 'MC_Gum_InferredPhysicalHeight_UV0_OpenMetric_16bit.png'
    save_scalar(path, .5 + atlas/(2*hmax))
    return path, atlas, claimed, {'constraints_sha256': source_hashes,
        'world_field_size': [width, height], 'world_field_extent': [extent_x, extent_z],
        'world_field_pitch': pitch, 'height_RMS_world': float(np.sqrt(np.mean(atlas[claimed>0]**2))),
        'source_frequency_FWHM_px_bands': [{'FWHM': sigma*pitch*963/16*2.355, 'weight': weight} for sigma, weight in bands],
        'height_bound_world': hmax, 'height_peak_source_pixels': hmax*963/16,
        'rough_frequency_source_pixels': [.6, 2.2], 'unique_front_texels': int(claimed.sum()),
        'coverage': 'Only actual front_front anatomical chart. Other charts neutral, not a final all-surface bake.',
        'metric_validity': 'V04 Open geometry only; Closed deformation changes metric and needs actual check.'}


def main():
    binding = json.loads(BINDING.read_text())
    original_colors = {name: {'path': mat['base_color'], 'sha256': sha(mat['base_color'])} for name, mat in binding['materials'].items()}
    report = {'status': 'CPU_PHYSICAL_FINISH_CANDIDATE_NOT_APPLIED_NOT_PAINTER_NOT_ACCEPTED',
        'basis_binding': str(BINDING), 'basis_sha256': sha(BINDING), 'preserved_BaseColor': original_colors,
        'lighting_hold': 'A_Bplus2_UpperSix v27, unchanged full-scene diffuse rig and lower/gum reflection receivers',
        'source': str(SOURCE), 'source_sha256': sha(SOURCE), 'native_actual': str(ACTUAL),
        'material_deltas': {}, 'reference_region_evidence': evidence()}
    manifest = json.loads((ROOT / 'Jaws/ContourHero_v14/Full32/uv_manifest.json').read_text())
    candidate = copy.deepcopy(binding)
    height_path, height, front, height_record = gum_physical_height()
    for name in ('MC_Enamel', 'MC_Gum'):
        mat = candidate['materials'][name]
        old = gray(mat['roughness'])
        wet = np.clip(gray(mat['wetness']), 0, 1)
        folder = OUT / name
        folder.mkdir(exist_ok=True)
        if name == 'MC_Enamel':
            rough = inferred_enamel_roughness(old, manifest)
            # Water IOR reduces coat F0 about twofold versus the old default
            # 1.5. Weight is correspondingly higher, not a broad energy boost:
            # .16+.08*w at IOR1.33 roughly matches .12*w at IOR1.5.
            # Existing lower bright area already exceeds source sixfold.
            coat, coat_rough = .16+.08*wet, .095+.025*(1-wet)
            overrides = {'ior': 1.52, 'coat_ior': 1.33, 'subsurface_weight_range': [.01, .13],
                         'subsurface_scale': .035, 'subsurface_radius': [1., .85, .65],
                         'transmission_weight': 0, 'normal_strength': 0}
            explanation = 'Energy-bounded water topcoat, sharper lobe rather than globally brighter coat; lower-row white area is already excessive. Subtle inferred roughness only, no enamel height/noise normal. Neutral diffusion radius avoids inherited red gum scattering; incisal support is a geometric prior, not recovered optical truth.'
        else:
            detail = height / .00010
            rough = np.clip(old*.90-.005 + np.clip(detail*.008, -.016, .016)*(.4+.6*wet), .21, .38)
            coat, coat_rough = .12+.34*wet, .105+.060*(1-wet)
            overrides = {'coat_ior': 1.33, 'subsurface_weight_range': [.035, .10],
                         'subsurface_scale': .040, 'subsurface_radius': [1., .33, .16],
                         'transmission_weight': 0, 'normal_strength': 0}
            explanation = 'Anatomical thin-margin diffusion plus nonrepeating physical wet relief; structured coral/violet pigment remains original. Inferred relief is not source brightness.'
            mat['physical_microdetail'] = {'status': 'CPU_HEIGHT_NOT_BAKED_NORMAL', 'height_uv0': str(height_path),
                'height_midlevel': .5, 'bump_distance_world': .0009, 'bump_strength': 1.,
                'normal_connection': 'Bump Normal to Principled Normal and Coat Normal; no existing Gum normal is bound.',
                'physical_decode': 'height_world=(PNG16/65535-.5)*.0009', **height_record}
        for key, values in [('roughness', rough), ('coat_weight', coat), ('coat_roughness', coat_rough)]:
            path = folder / f'{name}_{key}_FinishCandidate_16bit.png'
            save_scalar(path, values)
            mat[key] = str(path)
        old_constants = copy.deepcopy(mat['shader_constants'])
        mat['shader_constants'].update(overrides)
        # Direct portable images override the old MapRange values in native and
        # glTF. Record new actual ranges instead of implying stale constants.
        mat['shader_constants']['roughness_range'] = [float(rough.min()), float(rough.max())]
        mat['shader_constants']['coat_weight_range'] = [float(coat.min()), float(coat.max())]
        mat['shader_constants']['coat_roughness'] = float(np.median(coat_rough))
        mat['status'] = 'CPU_FINISH_CANDIDATE_GEOMETRY_MAP_BAKE_STILL_PENDING'
        report['material_deltas'][name] = {'before': old_constants, 'after': mat['shader_constants'], 'reason': explanation,
            'direct_images': {key: {'path': mat[key], 'sha256': sha(mat[key])} for key in ('roughness', 'coat_weight', 'coat_roughness')}}
    candidate['status'] = report['status']
    candidate['basis_binding'] = str(BINDING)
    candidate['changed_materials'] = ['MC_Enamel', 'MC_Gum']
    candidate['changed_shader_inputs'] = ['Roughness', 'Coat Weight', 'Coat Roughness', 'Coat IOR', 'Subsurface Weight/Scale/Radius', 'Gum inferred physical Bump Normal']
    candidate['hard_limits'] = ['No BaseColor changes; original pigment files/hash preserved.',
        'No actual bake/Painter export claimed. Whole-scene native comparison required.',
        'SSS changes must not create wax, blur source cervical edges, or hide geometric faults.',
        'No source glint/vein/intensity converted to normal; source sparkle frequency is a review guide only.',
        'Gum height is a UV0 Open-metric candidate; no repeating tile; no normal+same-height displacement double application.',
        'Generic glTF cannot reproduce Blender SSS; runtime finish requires separate actual calibration.']
    candidate['application_requires'] = ['Consume subsurface_radius and coat_ior constants explicitly.',
        'Honor direct coat/roughness images and physical_microdetail; current assembly does not apply every field automatically.',
        'Keep current A v27 profile and all four BaseColor inputs unchanged; no brightness/exposure compensation in this comparison.']
    path = OUT / 'material_binding_whole_finish_candidate.json'
    path.write_text(json.dumps(candidate, indent=2), encoding='utf8')
    assert all(sha(rec['path']) == rec['sha256'] for rec in original_colors.values())
    report['binding'] = str(path)
    report['physical_gum_height'] = height_record
    report['next_actual_gate'] = 'One current vs candidate whole 963px and same-region closeup under A v27. Judge ivory body/edge diffusion, wet fine sparkle, retained source coral/violet detail, and unchanged cervical first-hit ownership. Do not choose by centroid alone.'
    (OUT / 'whole_finish_candidate_preparation.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps({'status': report['status'], 'binding': str(path), 'BaseColor_hashes_unchanged': True, 'height': height_record}, indent=2), flush=True)


if __name__ == '__main__':
    main()
