"""CPU one-candidate wet Gum detail/card preparation. No DCC/paint/bake."""
from pathlib import Path
import copy
import importlib.util
import json
import math

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Textures/Revisions/v34_RetainedWetGrain_CPU'
OUT.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('physical', HERE / 'prepare_whole_material_finish_v28.py')
physical = importlib.util.module_from_spec(spec)
spec.loader.exec_module(physical)
physical.OUT = OUT
height_path, height, front, height_record = physical.gum_physical_height([(1.6, .45), (2.4, .40), (3.2, .15)], .00030, .0012)
clean_path = ROOT / 'Textures/Revisions/v08_InverseResponse_v18/SmoothGain_Full32_v23/material_binding_continuous_gain.json'
candidate = json.loads(clean_path.read_text())
finish = json.loads((ROOT / 'Textures/Revisions/v28_WholeMaterialFinish_CPU/material_binding_whole_finish_candidate.json').read_text())
gum = copy.deepcopy(finish['materials']['MC_Gum'])
# Native pigment and all enamel inputs stay at the clean baseline. This single
# next intervention isolates wet tissue from the rejected whole-finish tweak.
gum['base_color'] = candidate['materials']['MC_Gum']['base_color']
gum['physical_microdetail'].update(height_record)
gum['physical_microdetail'].update({'height_uv0': str(height_path), 'bump_distance_world': .0024,
    'physical_decode': 'height_world=(PNG16/65535-.5)*.0024', 'height_midlevel': .5, 'bump_strength': 1.})
wet = physical.gray(gum['wetness'])
old = physical.gray(candidate['materials']['MC_Gum']['roughness'])
variation = np.clip(height/.00030*.008, -.018, .018)
rough = np.clip(old*.90-.005+variation*(.4+.6*wet), .21, .38)
rough_path = OUT / 'MC_Gum/MC_Gum_RetainedWetRoughness_16bit.png'
physical.save_scalar(rough_path, rough)
gum['roughness'] = str(rough_path)
candidate['materials']['MC_Gum'] = gum
candidate['changed_materials'] = ['MC_Gum']
candidate['status'] = 'CPU_RETAINED_BANDWIDTH_WET_TISSUE_CANDIDATE_NOT_APPLIED_NOT_ACCEPTED'
candidate['basis_binding'] = str(clean_path)
candidate['note'] = 'Gum only. All source pigment and every enamel material input stay clean baseline. Height inferred, not source glints. Approx source FWHM .89/1.33/1.77px; peak physical height .072sourcepx. No giant normal perturbations.'

packet_path = ROOT / 'Gums/ClosedPosePrototype/BlenderCandidate_v07_PadPhysicalY_UpperLowerRelax/TransferPacket/evaluated_pose_triangle_uv_packet.npz'
packet = np.load(packet_path)
source_gloss = physical.gray(ROOT / 'Textures/Revisions/v07_GumSource_v14/Gingiva_WhiteReflectionExclusion.png') > .5
source_gloss = cv2.dilate(source_gloss.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0
profile_path = ROOT / 'Assembly/Review/RootContactV14_ReflectionProfiles_v27/A_Bplus2_UpperSix/look_profile.json'
profile = json.loads(profile_path.read_text())
original = copy.deepcopy(profile)
reference_card = next(light for light in profile['lights'] if light['name'] == 'Menu_Crown_ReflectionCard')
radiance = reference_card['energy']/(math.pi*reference_card['size']*reference_card['size_y'])*.6
for light in profile['lights']:
    if light['name'] == 'Menu_Interior_NeutralFill':
        # Actual Closed one-light test confirmed that its Y=-3 emitter plane
        # cuts crown bulges. Keep its intended cavity function only.
        light['receiver_objects'] = []
        light['receiver_materials'] = ['MC_Interior', 'MC_Tongue']
    if light['name'] in ('Menu_Crown_ReflectionCard', 'Menu_Fang_ReflectionCard'):
        # These exact enamel-only lists were already explicit in A v27. Remove
        # Gum material OR-link; leave all enamel receivers/light fields intact.
        light['receiver_materials'] = [name for name in light.get('receiver_materials', []) if name != 'MC_Gum']
cards = []
for jaw in ('Upper', 'Lower'):
    name = f'MC_DAY03_{jaw}_Gum'
    prefix = 'open::' + name + '::'
    vertices, triangles = packet[prefix+'world_vertices'], packet[prefix+'triangles']
    p = vertices[triangles]
    face = np.cross(p[:, 1]-p[:, 0], p[:, 2]-p[:, 0])
    normals = np.zeros_like(vertices)
    for corner in range(3):
        np.add.at(normals, triangles[:, corner], face)
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    normals = normals[triangles].mean(axis=1)
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    center = p.mean(axis=1)
    sx = np.floor((center[:, 0]/16+.5)*963).astype(int)
    sy = np.floor(205-center[:, 2]/16*963).astype(int)
    valid = (sx >= 0) & (sx < 963) & (sy >= 0) & (sy < 410) & (normals[:, 1] < -.05)
    selected = valid & source_gloss[np.clip(sy, 0, 409), np.clip(sx, 0, 962)]
    if selected.sum() < 8:
        raise RuntimeError('Not enough observed wet reflection support: ' + name)
    p0, n = center[selected], normals[selected]
    view = np.array([0., -30., 0.])-p0
    view /= np.linalg.norm(view, axis=1, keepdims=True)
    reflected = 2*np.einsum('ij,ij->i', n, view)[:, None]*n-view
    direction = np.median(reflected, axis=0)
    direction[0] = 0
    direction /= np.linalg.norm(direction)
    target = np.median(p0, axis=0)
    target[0] = 0
    position = target+direction*8
    card = {'name': f'Menu_{jaw}Gum_WetReflectionField', 'type': 'AREA', 'position': position.tolist(),
        'target': target.tolist(), 'size': 18., 'size_y': 1., 'energy': radiance*math.pi*18,
        'diffuse_factor': 0, 'specular_factor': 1, 'use_shadow': False, 'roll_degrees': 0,
        'receiver_objects': [name], 'receiver_materials': []}
    profile['lights'].append(card)
    cards.append({'light': card, 'observed_gloss_support_triangles': int(selected.sum()),
                  'mirror_direction_median_XYZ': direction.tolist(), 'source_glints_used_for_height': False})
profile['status'] = 'GUM_MIRROR_FIELD_AND_RETAINED_RELIEF_SINGLE_WHOLE_PROBE_NOT_ACCEPTED'
profile['notes'] = 'Gum glossy field changes; InteriorNeutralFill restricted to Interior/Tongue after actual closed-plane disk proof. A v27 enamel reflection cards and other diffuse lights unchanged. Two exact separate Gum receivers, no materialOR leak. No geometry/source pigment edits.'
(OUT / 'material_binding_gum_wet_candidate.json').write_text(json.dumps(candidate, indent=2), encoding='utf8')
(OUT / 'look_profile_gum_mirror_fields.json').write_text(json.dumps(profile, indent=2), encoding='utf8')
report = {'status': candidate['status'], 'height': height_record, 'cards': cards,
    'source_packet': str(packet_path), 'source_packet_sha256': physical.sha(packet_path),
    'radiance_relative_to_original_crown_card': .6,
    'enamel_inputs_unchanged': candidate['materials']['MC_Enamel'] == json.loads(clean_path.read_text())['materials']['MC_Enamel'],
    'all_BaseColor_hashes': {name: physical.sha(mat['base_color']) for name, mat in candidate['materials'].items()},
    'actual_review_required': ['One whole Open view and matched source native crop.', 'No whole-gum gray wash, orange peel or repeated dots.', 'Small wet glints should arise from actual microrelief and reflection-card edges, not painted normals.', 'Do not accept based only on source-frequency/slope/card centroid diagnostics.']}
(OUT / 'retained_wet_candidate_preparation.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report, indent=2), flush=True)
