"""Release the reviewed one-channel DAY03 pigment candidate; no Unreal mutation."""
from copy import deepcopy
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair'
OUT = SOURCE / 'LookCandidates/WarmCoronalBody_01/Textures'
STAGE = Path('C:/Users/user/Documents/Adobe/Adobe Substance 3D Painter/MessControlMenu3D/DAY03_WarmCoronalBody_01')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf8')


def main():
    global OUT, STAGE
    parser = argparse.ArgumentParser()
    parser.add_argument('--candidate', choices=['WarmCoronalBody_01', 'WarmCoronalBody_02'], default='WarmCoronalBody_01')
    parser.add_argument('--revision', default='02')
    parser.add_argument('--project', default='MC_DAY03_WarmCoronalBody_01_PBR02.spp')
    args = parser.parse_args()
    OUT = SOURCE / 'LookCandidates' / args.candidate / 'Textures'
    STAGE = Path('C:/Users/user/Documents/Adobe/Adobe Substance 3D Painter/MessControlMenu3D') / ('DAY03_' + args.candidate)
    if (OUT / 'release_status.json').exists():
        raise RuntimeError('Already released: create a new explicit revision before changing selected files.')
    linkage = json.loads((OUT / 'source_frozen_linkage.json').read_text())
    for path, expected in linkage['source_frozen_files'].items():
        assert sha(path) == expected['sha256'], f'Frozen source changed: {path}'
    if linkage.get('parent_candidate_binding'):
        assert sha(linkage['parent_candidate_binding']) == linkage['parent_candidate_binding_sha256']
    original = json.loads((SOURCE / 'Textures/shader_binding.json').read_text())
    candidate = OUT / ('PainterPBR_' + args.revision)
    qa = {'schema': 'messcontrol.warm_coronal_body.qa.v1', 'frozen_sources_unchanged': True,
          'candidate': args.candidate, 'selected_export': candidate.name, 'map_equality': {},
          'retained_roughness_uniforms': {'113': {'Roughness': .235, 'Metallic': 0, 'Height': 0},
                                        '131': {'Roughness': .27}, '146': {'Roughness': .205}},
          'new_channels': ['MC_Enamel.BaseColorTexture'], 'lighting_sampled_or_painted': False}
    if args.candidate == 'WarmCoronalBody_01':
        qa['first_export'] = 'PainterPBR_01 rejected: replacing active channels reset original enamel roughness uniforms to .30.'
    for name, roles in original['texture_sets'].items():
        for role, path in roles.items():
            old = Path(path)
            if role == 'ThicknessTexture':
                qa['map_equality'][name + '/' + role] = {'linked_frozen_path': str(old), 'sha256': sha(old)}
                continue
            new = candidate / old.name
            assert Image.open(new).size == (4096, 4096)
            same = sha(old) == sha(new)
            if (name, role) != ('MC_Enamel', 'BaseColorTexture'):
                assert same, f'Unexpected data-channel mutation: {name}/{role}'
            qa['map_equality'][name + '/' + role] = {'byte_identical_to_original': same, 'sha256': sha(new)}
    old_bc = np.asarray(Image.open(original['texture_sets']['MC_Enamel']['BaseColorTexture']).convert('RGB'))
    new_bc = np.asarray(Image.open(candidate / 'MC_Enamel_Base_color_1001.png').convert('RGB'))
    coverage = np.asarray(Image.open(OUT / 'AnatomyMasks/MC_Enamel_Coverage.png').convert('L')) > 0
    fang = np.asarray(Image.open(OUT / 'AnatomyMasks/MC_Enamel_UpperFangID.png').convert('L'))
    # Compare the fully assigned fang charts, excluding only fractional dilation-border texels.
    fang_surface = coverage & (fang > 250)
    ordinary = coverage & (fang == 0)
    delta = np.abs(new_bc.astype(np.int16) - old_bc.astype(np.int16))
    assert np.count_nonzero(delta[fang_surface]) == 0, 'Upper fang pigment changed.'
    qa['upper_fang_pixels'] = {'count': int(fang_surface.sum()), 'changed_values': 0,
                              'old_and_new_mean_rgb8': old_bc[fang_surface].mean(0).tolist()}
    qa['ordinary_surface_rgb8'] = {'old_mean': old_bc[ordinary].mean(0).tolist(),
                                   'new_mean': new_bc[ordinary].mean(0).tolist(),
                                   'maximum_channel_delta': int(delta[ordinary].max())}
    review = json.loads((OUT / 'painter_viewport_qa.json').read_text())
    assert review['actual_visual_review_performed'] is True
    project = OUT / args.project
    shutil.copy2(STAGE / project.name, project)
    saved = {'path': str(project), 'sha256': sha(project), 'bytes': project.stat().st_size}
    write('painter_saved_project_PBR' + args.revision + '.json', saved)
    binding = deepcopy(original)
    changed = str((candidate / 'MC_Enamel_Base_color_1001.png').resolve())
    binding['texture_sets']['MC_Enamel']['BaseColorTexture'] = changed
    binding['map_metadata']['MC_Enamel']['BaseColorTexture'].update(
        path=changed, sha256=sha(changed), bytes=Path(changed).stat().st_size)
    binding.update(status='FROZEN_PAINTER_LOOK_CANDIDATE_NATIVE_AB_PENDING',
                   candidate=args.candidate, selected_pbr_revision=args.revision,
                   source_binding=str(SOURCE / 'Textures/shader_binding.json'),
                   source_binding_sha256=linkage['source_binding_sha256'],
                   editable_project=saved,
                   viewport_review=str(OUT / 'painter_viewport_qa.json'),
                   texture_qa=str(OUT / 'texture_qa.json'),
                   mutation_scope='Ordinary enamel pigment only. Upper fangs unchanged; nine other maps link directly to frozen Final96 originals.',
                   changed_roles=['MC_Enamel.BaseColorTexture'],
                   unmodified_role_policy='Use original texture_sets paths and hashes from frozen Final96. Only the candidate enamel BaseColor is selected.',
                   physical_mask_semantics='Anatomy UV masks from source root-to-tip coordinate; no reference lighting, ambient occlusion or screen projection in pigment.')
    if linkage.get('parent_candidate_binding'):
        binding['parent_candidate_binding'] = linkage['parent_candidate_binding']
        binding['parent_candidate_binding_sha256'] = linkage['parent_candidate_binding_sha256']
    binding['pigment_authoring_receipt'] = str(OUT / 'warm_body_mask_receipt.json')
    write('texture_qa.json', qa)
    write('shader_binding.json', binding)
    release = {'status': 'FROZEN_RELEASED_TO_ROOT_FOR_NATIVE_AB',
               'binding': str(OUT / 'shader_binding.json'), 'binding_sha256': sha(OUT / 'shader_binding.json'),
               'changed_texture_sha256': sha(changed), 'editable_project': saved,
               'native_visual_approval': 'PENDING_ROOT_UNREAL_GATE',
               'mutations_after_release': 'Use an explicit new revision; preserve this release.'}
    write('release_status.json', release)
    print(json.dumps(release, indent=2))


if __name__ == '__main__':
    main()
