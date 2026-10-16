"""CPU bounded anatomy-supported wet film + retained physical mesoslope proposal.

Source bright pixels support only a BROAD wet-region prior. They never become
pigment, normal, height or exact white dots. Actual reflection creates glints.
"""
from pathlib import Path
import copy
import importlib.util
import json
import math

import cv2
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Textures/Revisions/v35_SparseWetFilm_CPU'
OUT.mkdir(parents=True, exist_ok=True)
cv2.setNumThreads(2)


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


physical = module('physical', 'prepare_whole_material_finish_v28.py')
raster = module('raster', 'transfer_closed_source_to_uv0.py')
physical.OUT = OUT
raster.H, raster.WORLD_W = 410, 16.
height_path, height, front, height_record = physical.gum_physical_height([(1.8, .40), (2.8, .40), (4.2, .20)], .00120, .00450)
N = 2048
packet_path = ROOT / 'Gums/ClosedPosePrototype/BlenderCandidate_v07_PadPhysicalY_UpperLowerRelax/TransferPacket/evaluated_pose_triangle_uv_packet.npz'
manifest_path = packet_path.with_name('packet_manifest.json')
packet, manifest = np.load(packet_path), json.loads(manifest_path.read_text())
objects = list(manifest['objects'])
vparts, tparts, uparts, owners = [], [], [], []
offset = 0
for i, name in enumerate(objects):
    assert manifest['objects'][name]['uv_layer'] == 'UV0'
    key = 'open::'+name+'::'
    vertices, triangles = packet[key+'world_vertices'], packet[key+'triangles']
    vparts.append(vertices)
    tparts.append(triangles+offset)
    uparts.append(packet[key+'UV0_triangles'])
    owners.append(np.full(len(triangles), i, np.int32))
    offset += len(vertices)
vertices, triangles = np.concatenate(vparts), np.concatenate(tparts)
uv, tri_owner = np.concatenate(uparts), np.concatenate(owners)
world = vertices[triangles]
depth, owner, _, _ = raster.first_hits(world, uv, tri_owner)
source = np.asarray(Image.open(ROOT / 'Evidence/SourceAnalysis/vampire_open_source.png').convert('RGB'))
known = ROOT / 'Textures/Revisions/v05_SourceLocked/Gingiva'
source_masks = {'Upper': physical.gray(known / 'upper_gingiva_visible.png') > .5,
                'Lower': physical.gray(known / 'lower_gingiva_visible_strict.png') > .5}
white = (source[..., 0] > 235) & (source[..., 1] > 200) & (source[..., 2] > 185)


def smoothstep(low, high, value):
    t = np.clip((value-low)/max(high-low, 1e-9), 0, 1)
    return t*t*(3-2*t)


film_fields, measurements = {}, {}
for index, jaw in enumerate(('Upper', 'Lower')):
    mask = source_masks[jaw]
    seeds = white & mask
    anatomical = cv2.GaussianBlur(seeds.astype(np.float32), (0, 0), 5.)
    threshold = max(float(np.percentile(anatomical[mask], 86)), float(anatomical.max())*.08, 1e-7)
    broad_support = smoothstep(threshold*.6, threshold*1.5, anatomical)*mask
    # Independent nonperiodic saliva-film breakup at1–3px. Not the source
    # highlight mask and not used to make normal/height amplitudes.
    rng = np.random.default_rng(350016+index)
    variation = cv2.GaussianBlur(rng.normal(size=mask.shape).astype(np.float32), (0, 0), 1.1)
    variation = (variation-variation.mean())/max(float(variation.std()), 1e-9)
    breakup = smoothstep(.45, 1.35, variation)
    film = broad_support*breakup
    film_fields[objects.index(f'MC_DAY03_{jaw}_Gum')] = film
    measurements[jaw] = {'observed_source_bright_fraction': float(np.mean(white[mask])),
        'broad_supported_region_fraction_gt_half': float(np.mean(broad_support[mask] > .5)),
        'independent_sparse_film_fraction_gt_half': float(np.mean(film[mask] > .5)),
        'independent_sparse_film_mean': float(np.mean(film[mask])),
        'source_seeds_used_for_normal_or_pigment': False}
    physical.save_scalar(OUT / f'{jaw}_SourceAnatomyWetSupport_REVIEW.png', broad_support)
    physical.save_scalar(OUT / f'{jaw}_IndependentSparseFilm_REVIEW.png', film)
film_atlas = np.zeros((N, N), np.float32)
for ids, pixels, weights in raster.fragments(uv*[N, -N]+[0, N], N, N, 2048):
    point = np.einsum('ij,ijk->ik', weights, world[ids])
    screen = raster.projected(point)
    sx, sy = screen[:, 0]-.5, screen[:, 1]-.5
    hit_owner = raster.remap(owner, sx[None], sy[None], True)[0].astype(np.int32)
    hit_depth = raster.remap(np.where(np.isfinite(depth), depth, 1e8), sx[None], sy[None], True)[0]
    in_frame = (sx >= 0) & (sx < 962) & (sy >= 0) & (sy < 409)
    visible = in_frame & (hit_owner == tri_owner[ids]) & (abs(point[:, 1]-hit_depth) < .02)
    for object_id, field in film_fields.items():
        selected = (tri_owner[ids] == object_id) & visible
        if selected.any():
            values = raster.remap(field, sx[selected][None], sy[selected][None])[0]
            film_atlas.reshape(-1)[pixels[selected]] = values
# The sparse field affects wet-film optics, never BaseColor or the independent
# height. Dry tissue keeps restrained broad roughness, not a continuous coat.
folder = OUT / 'MC_Gum'
film_path = folder / 'MC_Gum_SparseWetFilm_UV0_16bit.png'
rough_path = folder / 'MC_Gum_SparseWetRoughness_16bit.png'
coat_path = folder / 'MC_Gum_SparseCoatWeight_16bit.png'
coat_rough_path = folder / 'MC_Gum_SparseCoatRoughness_16bit.png'
physical.save_scalar(film_path, film_atlas)
physical.save_scalar(rough_path, .43-.21*film_atlas)
physical.save_scalar(coat_path, .01+.54*film_atlas)
physical.save_scalar(coat_rough_path, .29-.20*film_atlas)
basis_path = ROOT / 'Textures/Revisions/v08_InverseResponse_v18/SmoothGain_Full32_v23/material_binding_continuous_gain.json'
binding = json.loads(basis_path.read_text())
gum = binding['materials']['MC_Gum']
gum.update({'roughness': str(rough_path), 'coat_weight': str(coat_path), 'coat_roughness': str(coat_rough_path), 'wetness': str(film_path)})
gum['shader_constants'].update({'coat_ior': 1.33, 'normal_strength': 0})
gum['physical_microdetail'] = {'status': 'INFERRED_PHYSICAL_HEIGHT_NOT_BAKED_NORMAL', 'height_uv0': str(height_path),
    'height_midlevel': .5, 'bump_distance_world': .009, 'bump_strength': 1.,
    'normal_connection': 'Bump Normal to Principled Normal and Coat Normal; no normal/displacement double apply.',
    'physical_decode': 'height_world=(PNG16/65535-.5)*.009', **height_record}
binding['status'] = 'CPU_SPARSE_FILM_MESOSLOPE_COMBINED_PROPOSAL_NOT_APPLIED_NOT_ACCEPTED'
binding['changed_materials'] = ['MC_Gum']
binding['basis_binding'] = str(basis_path)
binding['limits'] = ['All pigment/enamel/SSS/geometry unchanged.',
    'Broad source glint support is an explicitly inferred wet-anatomy prior, not measured physical film.',
    'Independent sparse film and height are aperiodic; source brightness never converted to normal/height.',
    'Approx3–7degree retained mesoslope and .27sourcepx relief bound need actual renderer validation.',
    'The source bright fraction is a visual target; it is not physically equal to film surface coverage.']
profile = json.loads((ROOT / 'Textures/Revisions/v34_RetainedWetGrain_CPU/look_profile_gum_mirror_fields.json').read_text())
strips = [light for light in profile['lights'] if light['name'].endswith('Gum_WetReflectionField')]
profile['lights'] = [light for light in profile['lights'] if light not in strips]
panels = []
patterns = {'Upper': [(-6.2, 2.0, .55, .15), (-1.4, 3.2, .80, -.1), (4.3, 2.4, .65, .25)],
            'Lower': [(-4.5, 2.6, .70, -.1), (1.3, 2.2, .55, .2), (6.3, 1.7, .80, 0.)]}
for strip in strips:
    jaw = 'Upper' if 'UpperGum' in strip['name'] else 'Lower'
    position, target = np.asarray(strip['position']), np.asarray(strip['target'])
    forward = (target-position)/np.linalg.norm(target-position)
    up = np.array([0., 0., 1.])
    right = np.cross(forward, up);right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    radiance = strip['energy']/(math.pi*strip['size']*strip['size_y'])
    for i, (offset, width, height, stagger) in enumerate(patterns[jaw]):
        delta = right*offset+up*stagger
        panel = copy.deepcopy(strip)
        panel.update({'name': f'Menu_{jaw}Gum_SparseReflectionPanel_{i+1}', 'position': (position+delta).tolist(),
            'target': (target+delta).tolist(), 'size': width, 'size_y': height, 'energy': radiance*math.pi*width*height})
        profile['lights'].append(panel)
        panels.append(panel)
profile['status'] = binding['status']
profile['notes'] = 'Two continuous Gum strips replaced by six nonuniform separated physical panels, exact per-Gum receivers; A enamel reflections held. NeutralFill Interior/Tongue receiver correction retained.'
(OUT / 'material_binding_sparse_wet_proposal.json').write_text(json.dumps(binding, indent=2), encoding='utf8')
(OUT / 'look_profile_sparse_gum_panels.json').write_text(json.dumps(profile, indent=2), encoding='utf8')
report = {'status': binding['status'], 'source_wet_prior_and_sparse_film': measurements, 'height': height_record,
    'panels': panels, 'packet_sha256': physical.sha(packet_path), 'all_BaseColor_hashes': {name: physical.sha(mat['base_color']) for name, mat in binding['materials'].items()},
    'physical_height_derived_from_source_brightness': False, 'SSS_inputs_unchanged': True,
    'actual_review_gate': 'ONE whole Open with matched native crops after lead authorizes; source target small glints, no continuous white collar/rim, no stone/orange peel or repeated pores.'}
(OUT / 'sparse_wet_combined_proposal.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps({'status': report['status'], 'source_wet': measurements, 'height': height_record, 'panels': len(panels)}, indent=2), flush=True)
