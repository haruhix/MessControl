"""CPU-only material/UV contract for user-overridden symmetric DAY03 lower16.

No existing geometry, source fields, PBR, radiance atlas, or export is mutated.
Actual paired geometry and UV correspondence are required before texture work.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
OUT = ROOT / 'Textures/Revisions/v41_SymmetricLower_MaterialHandoff_CPU'
UV_MANIFEST = ROOT / 'Jaws/ContourHero_v13/Full32/uv_manifest.json'
OPEN_RECEIPT = ROOT / 'Textures/Revisions/v39b_Open_Radiance_NOT_ALBEDO_Tight/open_radiance_transfer_receipt.json'


def load(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    if OUT.exists() and any(OUT.iterdir()):
        raise RuntimeError('Fresh handoff branch required')
    OUT.mkdir(parents=True, exist_ok=True)
    old = load(UV_MANIFEST)
    parts = {part['part']: part for part in old['parts']}
    records = load(OPEN_RECEIPT)
    families = ['Central', 'Lateral', 'Canine', 'Premolar', 'Molar', 'Posterior01', 'Posterior02', 'Posterior03']
    pairs = []
    for index, family in enumerate(families):
        suffix = family.removeprefix('Posterior') if family.startswith('Posterior') else None
        right = f'Lower_Posterior_R{suffix}' if suffix else f'Lower_{family}_R'
        left = f'Lower_Posterior_L{suffix}' if suffix else f'Lower_{family}_L'
        rp, lp = parts[right], parts[left]
        pairs.append({'ordinal_center_to_corner': index+1, 'canonical_object': rp['object'], 'paired_object': lp['object'],
            'current_canonical_observed_Open_source_pixels': records['known_radiance_pixels'][rp['object']],
            'current_left_observed_Open_source_pixels': records['known_radiance_pixels'][lp['object']],
            'old_canonical_front_uv_rect': rp['front_uv_rect'], 'old_canonical_back_uv_rect': rp['back_uv_rect'],
            'old_left_front_uv_rect': lp['front_uv_rect'], 'old_left_back_uv_rect': lp['back_uv_rect'],
            'new_uv_correspondence_status': 'PENDING_ACTUAL_NEW_SYMMETRIC_GEOMETRY_AND_JAWS_MANIFEST'})
    report = {
        'status': 'CPU_HANDOFF_ONLY_NO_TEXTURE_OR_GEOMETRY_MUTATION',
        'user_override': 'Symmetric lower16: eight paired left/right crowns with a center SEAM at X0. Original source asymmetry is deliberately overridden by the user.',
        'canonical_side_recommendation': '+X / RIGHT',
        'evidence': {'right_known_source_pixels': sum(p['current_canonical_observed_Open_source_pixels'] for p in pairs),
                     'left_known_source_pixels': sum(p['current_left_observed_Open_source_pixels'] for p in pairs),
                     'rationale': 'Right side has more observed enamel coverage and a complete outer molar; left source has weaker/cropped coverage and nearby reference-worker occlusion.',
                     'limits': 'The right outermost posterior is mostly cropped (30 observed pixels). Its hidden body is inferred, not a reference measurement.'},
        'pairs': pairs,
        'geometry_gate': ['Build actual mirrored 3D lower geometry before applying mirrored surface fields.',
                         'Center seam is a geometric boundary between a central LEFT and RIGHT crown, never a tooth centered on X0.',
                         'Require canonical-to-left actual vertex/face correspondence and winding/tangent parity; new geometry hashes and exact UV0 loop arrays.',
                         'Do not mirror or alter upper crowns, upper gum, interior/tongue or original existing character implicitly. Lower-gum contact branch is coordinated separately.'],
        'preferred_uv_contract': {
            'atlas': 'Unique UV0 front/back islands for each of the sixteen lower meshes; retain existing upper-owned UV islands byte-exact.',
            'canonical_parameter_copy': 'Copy right vertex UV parameters to corresponding mirrored-left vertices, then translate/fit into the left-owned unique cells. This naturally mirrors appearance with the actual geometry.',
            'no_double_mirror': 'If canonical UV parameter orientation is copied, texture pixels are copied without another horizontal flip. If left UV U is reoriented to increasing worldX, manifest must declare mirror_u and maps must follow that explicit transform.',
            'required_per_pair': ['canonical_object', 'paired_object', 'canonical_front_uv_rect', 'paired_front_uv_rect',
                                  'canonical_back_uv_rect', 'paired_back_uv_rect', 'local_uv_transform_2x3',
                                  'vertex_correspondence_or_stable_parameter', 'face_winding_parity', 'geometry_sha256', 'UV0_loop_sha256'],
            'texel_density': 'Mirrored pairs have equal world dimensions and UV texel density; do not stretch a donor atlas rectangle to an unrelated source silhouette.',
            'lower_gum': 'Use new actual signed-X/arch vertex correspondence per UV0 chart; never flip the entire packed gum atlas. Open and Closed endpoints must each have explicit correspondence.'},
        'channel_policy': {
            'radiance_not_albedo': 'Copy the canonical right observed/inferred baked appearance field onto its actual paired-left surface. Include highlights only in this explicit fixed-camera baked-light branch. All new lower symmetry is user-directed derived appearance, not original source-frame truth.',
            'base_color': 'Mirror canonical enamel pigment/calibrated material field. Preserve its current provenance and calibration status; do not relabel source-lit colors as uniquely recovered albedo.',
            'scalar_maps': 'roughness, coat_weight, coat_roughness, specular_ior_level alpha, wetness, subsurface/incisal/transmission supports and AO follow the explicit UV correspondence. Do not copy unrelated material atlas regions.',
            'tangent_normal': 'Use actual mirrored tangent frame. For copied UV parameter orientation with recomputed mirrored tangent handedness, normal RGB can be copied unchanged. For a declared reflected U coordinate, negate tangent X (red); a reflected V requires tangent Y (green). Preserve separate OpenGL and DirectX conventions. Validate by a world-normal reflection test; do not blindly flip red just because geometry is mirrored.',
            'height': 'Mirror scalar height through correspondence with equal physical scale. Future geometry-normal/AO bakes need the new actual paired mesh after endpoint freeze.',
            'confidence': 'Keep canonical source-derived observation/inference masks separately from new target-pose confidence. Mirrored-left appearance is DERIVED_BY_USER_SYMMETRY, not observed original-left truth.'},
        'frozen_source_branches': {
            'Open_radiance': str(ROOT / 'Textures/Revisions/v39b_Open_Radiance_NOT_ALBEDO_Tight/material_binding_open_Radiance_NOT_ALBEDO.json'),
            'Closed_radiance': str(ROOT / 'Textures/Revisions/v36_Closed_Radiance_NOT_ALBEDO/material_binding_closed_Radiance_NOT_ALBEDO.json'),
            'v40_diagnostic': 'Do not promote until new geometry; keep existing v40 and all PBR variants read-only.',
            'physical_PBR_binding': 'Root selects authoritative current binding before derived lower transfer; no guessed older export is loaded.'},
        'validation_plan': ['CPU source/UV/material hash gates and canonical-to-left correspondence.',
                            'Mirrored geometry: 16 crowns, eight named pairs, center seam at X0, paired world bounds/first-hit ownership at both endpoints.',
                            'Canonical-to-left texture sampling equality on actual paired vertices and tangent-frame normal parity.',
                            'Actual whole front/closed/open/intermediate captures plus fresh glTF/runtime review after parent grants DCC; source references are no longer symmetry targets for the overridden lower half.'],
        'source_uv_manifest': str(UV_MANIFEST), 'source_uv_manifest_sha256': sha(UV_MANIFEST),
        'source_Open_receipt': str(OPEN_RECEIPT), 'source_Open_receipt_sha256': sha(OPEN_RECEIPT),
        'DCC_Painter_GPU_used': False, 'existing_source_materials_mutated': False,
        'pending': 'Jaws symmetric lower source and style lower-gum endpoint/UV correspondence, then CPU texture branch.'}
    path = OUT / 'symmetric_lower_material_uv_handoff.json'
    path.write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps({'handoff': str(path), 'canonical': report['canonical_side_recommendation'], 'evidence': report['evidence'], 'pairs': len(pairs)}, indent=2))


if __name__ == '__main__':
    main()
