"""CPU physical-unit microrelief resources and bounded SSS comparison drafts.

These are nonrepeating LOCAL STUDIES, not UV0 maps or mesh-map bakes. Height is
synthetic anatomy-inspired geometry, never source RGB/white reflection as normal.
No current material binding or scene is changed; final Painter transfer waits
for accepted open/closed joints and unique UV/actual metric validation.
"""
from pathlib import Path
import copy
import hashlib
import json
import math

import cv2
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Textures/Revisions/v24_PhysicalWetDetail_Prepare'
OUT.mkdir(parents=True, exist_ok=True)
cv2.setNumThreads(2)
N, WORLD_WIDTH = 1024, 2.
PIXEL_WORLD = WORLD_WIDTH / N
SOURCE_PIXELS_PER_WORLD = 963 / 16
source = ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png'
source_image = Image.open(source).convert('RGB')
binding_path = ROOT / 'Textures/Revisions/v08_InverseResponse_v18/SmoothGain_Full32_v23/material_binding_continuous_gain.json'
binding = json.loads(binding_path.read_text())
reference = json.loads((ROOT / 'Textures/Revisions/v05_SourceLocked/Gingiva/gingiva_source_inventory.json').read_text())
report = {'status': 'CPU_PHYSICAL_RESOURCES_AND_SSS_DRAFTS_NOT_APPLIED_NOT_PAINTER_OR_BAKE',
          'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'binding_basis': str(binding_path), 'binding_sha256': hashlib.sha256(binding_path.read_bytes()).hexdigest(),
          'units': {'canonical_width_world': 16, 'canonical_width_pixels': 963,
                    'world_per_source_pixel': 16/963, 'study_world_extent': [WORLD_WIDTH, WORLD_WIDTH],
                    'study_resolution': [N, N], 'study_world_per_texel': PIXEL_WORLD},
          'reference_frequency_guides': reference['frequency_patches'], 'studies': [], 'sss_candidates': [],
          'hard_guards': ['No source intensity, glint, violet pigment or brightness converted to height/normal.',
                          'Violet vessel/mottle patterns remain BaseColor pigmentation unless separately proved geometry.',
                          'Study images are not seamless tiles and must never repeat across the mouth.',
                          'Final uniqueUV0 height must use actual tangent/world metric; no uniformUV bump-distance guess.',
                          'No current scene constants or maps changed. No Painter/DCC invocation.',
                          'Actual new mesh-map bake/export waits both endpoint/joint acceptance.']}


def study(name, seed, diameter_range, height_range, count, roughness_sigma):
    rng = np.random.default_rng(seed)
    height = np.zeros((N, N), np.float64)
    for _ in range(count):
        center = rng.uniform(0, N, 2)
        diameter = rng.uniform(*diameter_range)
        axis_ratio = rng.uniform(.65, 1.55)
        sx = diameter / PIXEL_WORLD * .25 * math.sqrt(axis_ratio)
        sy = diameter / PIXEL_WORLD * .25 / math.sqrt(axis_ratio)
        theta = rng.uniform(0, 2 * math.pi)
        amplitude = rng.uniform(*height_range)
        radius = math.ceil(max(sx, sy) * 3)
        x0, x1 = max(0, int(center[0]) - radius), min(N, int(center[0]) + radius + 1)
        y0, y1 = max(0, int(center[1]) - radius), min(N, int(center[1]) + radius + 1)
        yy, xx = np.mgrid[y0:y1, x0:x1]
        dx, dy = xx - center[0], yy - center[1]
        u, v = math.cos(theta) * dx + math.sin(theta) * dy, -math.sin(theta) * dx + math.cos(theta) * dy
        exponent = (u / sx) ** 2 + (v / sy) ** 2
        bump = np.exp(-.5 * exponent)
        bump[exponent > 9] = 0
        height[y0:y1, x0:x1] += amplitude * bump
    # Remove only the low-frequency mean; this represents relief, not a shift
    # of the actual modeled body or a paint-color variation.
    height -= cv2.GaussianBlur(height, (0, 0), 18)
    dy, dx = np.gradient(height, PIXEL_WORLD)
    normal = np.dstack((-dx, -dy, np.ones_like(height)))
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    angle = np.degrees(np.arccos(np.clip(normal[..., 2], -1, 1)))
    folder = OUT / name
    folder.mkdir(exist_ok=True)
    hmax = float(np.max(np.abs(height)))
    encoded_height = np.clip(.5 + .5 * height / max(hmax, 1e-12), 0, 1)
    Image.fromarray(np.uint16(np.round(encoded_height * 65535))).save(folder / (name + '_HeightSignedWorld_STUDY_16bit.png'))
    Image.fromarray(np.uint8(np.round((normal * .5 + .5) * 255))).save(folder / (name + '_Normal_OpenGL_STUDY.png'))
    directx = normal.copy()
    directx[..., 1] *= -1
    Image.fromarray(np.uint8(np.round((directx * .5 + .5) * 255))).save(folder / (name + '_Normal_DirectX_STUDY.png'))
    density = cv2.GaussianBlur(np.abs(height), (0, 0), 1.2)
    delta = (density - density.mean()) / max(density.std(), 1e-12) * roughness_sigma
    delta = np.clip(delta, -2 * roughness_sigma, 2 * roughness_sigma)
    Image.fromarray(np.uint16(np.round((.5 + delta / (4 * roughness_sigma)) * 65535))).save(folder / (name + '_RoughnessDelta_STUDY_16bit.png'))
    np.savez_compressed(folder / (name + '_PhysicalFields_STUDY.npz'),
                        height_world=height.astype(np.float32), roughness_delta=delta.astype(np.float32))
    return {'material': name, 'seed': seed, 'independent_anatomy_events': count,
            'diameter_world': diameter_range,
            'diameter_canonical_source_pixels': [v * SOURCE_PIXELS_PER_WORLD for v in diameter_range],
            'individual_height_world': height_range,
            'individual_height_source_pixels': [v * SOURCE_PIXELS_PER_WORLD for v in height_range],
            'exported_signed_height_range_world': [-hmax, hmax],
            'height_decode': 'worldHeight=(PNG16/65535-.5)*2*hmax',
            'slope_normal_RMS_degrees': float(np.sqrt(np.mean(angle * angle))),
            'slope_normal_p95_degrees': float(np.percentile(angle, 95)),
            'roughness_delta_sigma_target': roughness_sigma, 'folder': str(folder),
            'inferred': True, 'source_RGB_to_normal': False,
            'preview_status': 'Resources only. Actual grazing/canonical render and final UV metric still required.'}


for spec in [
    ('MC_Gum', 271601, (.012, .030), (.00010, .00040), 21000, .010),
    ('MC_Interior', 271602, (.016, .036), (.00008, .00030), 15000, .012),
    ('MC_Tongue', 271603, (.010, .024), (.00015, .00060), 24000, .008),
]:
    report['studies'].append(study(*spec))

# Orthogonal SSS comparisons isolate weight from radius before any joint test.
for name, weight, scale in [
    ('S0_Baseline', [.005, .025], .012),
    ('S1_WeightOnly', [.035, .10], .012),
    ('S2_RadiusOnly', [.005, .025], .040),
    ('S3_WeightAndRadius', [.035, .10], .040),
]:
    candidate = copy.deepcopy(binding)
    gum = candidate['materials']['MC_Gum']
    gum['shader_constants']['subsurface_weight_range'] = weight
    gum['shader_constants']['subsurface_scale'] = scale
    candidate['status'] = 'CPU_SSS_COMPARISON_DRAFT_NOT_APPLIED_NOT_ACCEPTED'
    candidate['changed_materials'] = ['MC_Gum']
    candidate['note'] = 'Same existing SSS anatomy support and all native maps. Only explicit Gum weight/radius candidate differs. Compare under fixed light/camera; no geometry or color compensation.'
    path = OUT / (name + '_binding_DRAFT.json')
    path.write_text(json.dumps(candidate, indent=2), encoding='utf8')
    report['sss_candidates'].append({'name': name, 'binding': str(path), 'gum_weight_range': weight,
                                     'scale_world': scale, 'existing_radius_RGB': [1, .33, .16],
                                     'effective_radius_canonical_pixels_RGB': [scale * v * SOURCE_PIXELS_PER_WORLD for v in (1, .33, .16)],
                                     'not_applied': True})
report['final_Painter_layer_plan'] = [
    {'name': 'SOURCE Pigment - preserve observed coral/violet pattern', 'channel': 'BaseColor', 'change': 'None in this study'},
    {'name': 'ANATOMY Wetness - editable source-supported broad mask', 'channel': 'Roughness/Coat', 'change': 'Preserve existing placement; no source white dots as normals'},
    {'name': 'INFERRED Fine wet grain - physical height', 'channel': 'Height', 'change': 'UniqueUV0 anatomical field after geometry freeze; calibrate physical amplitude, no repeating study tile'},
    {'name': 'INFERRED Fine wet grain - roughness delta', 'channel': 'Roughness', 'change': 'Small signed delta, deep mucosa center gain20%, cheeks full, no periodic dots'},
    {'name': 'ANATOMY Thin-tissue diffusion', 'channel': 'Subsurface support', 'change': 'Final mask from accepted volume/thickness; bounded existing support comparison first'},
]
report['enamel_policy'] = 'No papillae/no gum grain on enamel. Preserve observed per-crown pigmentation; optional fine striation roughness only after source-specific frequency proof.'
report['validation_required'] = ['Fixed canonical963px and closeup; layer on/off; source-color and highlight masks.',
                                 'Black-base grazing response with identical reflection profile.',
                                 'No stone/orange-peel or repeated dots; detail subdued at menu distance.',
                                 'SSS frontal color/edge softness plus bounded backlit thin-flesh check; no waxy whole-gum glow.',
                                 'Do not accept/reject solely from RMS normal angle; actual renderer proof decides.',
                                 'Rebuild exported PBR from maps; OpenGL/DX distinct; no normal+same-height displacement double application.']
(OUT / 'physical_detail_and_SSS_preparation.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps({'status': report['status'], 'studies': report['studies'], 'sss': report['sss_candidates']}, indent=2))
