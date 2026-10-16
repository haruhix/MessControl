"""Validate and release new tongue maps, linking unchanged cavity maps directly."""
from copy import deepcopy
from pathlib import Path
import hashlib
import json
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior'
SOURCE = BASE / 'UV_Organic_r05_Candidate01'
HANDOFF = BASE / 'UV_r06_Refine03_Candidate01'
OUT = HANDOFF / 'Textures'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2), encoding='utf8')


def main():
    if (OUT / 'release_status.json').exists():
        raise RuntimeError('Selected maps already released; use a new explicit revision.')
    link = json.loads((OUT / 'source_frozen_linkage.json').read_text())
    for name, value in link['source_frozen_files'].items():
        assert sha(name) == value['sha256'], f'Changed original: {name}'
    for name, value in link['new_frozen_files'].items():
        assert sha(name) == value, f'Changed Refine03 geometry/UV: {name}'
    assert sha(HANDOFF / 'release_manifest.json') == link['source_release_sha256']
    assert json.loads((OUT / 'bake_success.json').read_text())['job']['status'] == 'success'
    assert json.loads((OUT / 'painter_viewport_qa.json').read_text())['actual_visual_review_performed']
    original = json.loads(Path(link['parent_binding']).read_text())
    binding = deepcopy(original)
    qa = {'schema': 'messcontrol.r06.tongue_physical_texture_qa.v1',
          'frozen_sources_unchanged': True, 'source_uv_transfer_verified': link['uv_transfer'],
          'selected_cavity_policy': 'Five original frozen r05 maps linked directly; unused re-exports differ by at most one RGB8 level on sparse pixels.',
          'sets': {}, 'warnings': []}
    for name in ['MC_Interior', 'MC_Tongue']:
        qa['sets'][name] = {}
        coverage = np.asarray(Image.open(OUT / 'AnatomyMasks' / (name + '_Coverage.png')).convert('L')) > 0
        if name == 'MC_Tongue':
            files = {
                'BaseColorTexture': OUT / 'PainterPBR_01/MC_Tongue_Base_color_1001.png',
                'RoughnessTexture': OUT / 'PainterPBR_01/MC_Tongue_Specular_roughness_1001.png',
                'NormalTexture': OUT / 'PainterPBR_01/MC_Tongue_Normal_DirectX_1001.png',
                'AOTexture': OUT / 'PainterPBR_01/MC_Tongue_Mixed_AO_1001.png',
                'ThicknessTexture': OUT / 'PainterMeshMaps_01/MC_Tongue_thickness.1001.png'}
        else:
            files = {key: Path(value) for key, value in original['texture_sets'][name].items()}
        for role, path in files.items():
            image = Image.open(path)
            assert image.size == (4096, 4096), f'Wrong map dimensions: {path}'
            array = np.asarray(image.convert('RGB') if role in ['BaseColorTexture', 'NormalTexture'] else image.convert('L'), dtype=np.float32)/255
            surface = array[coverage]
            stats = {'minimum': surface.min(0).tolist(), 'maximum': surface.max(0).tolist(),
                     'mean': surface.mean(0, dtype=np.float64).tolist(),
                     'p05_p50_p95': np.percentile(surface, [5, 50, 95], axis=0).tolist()}
            if role == 'NormalTexture':
                vectors = surface*2-1; length = np.linalg.norm(vectors, axis=1)
                angles = np.degrees(np.arccos(np.clip(vectors[:, 2]/np.maximum(length, 1e-8), -1, 1)))
                stats['tangent_deflection_degrees_p50_p95_p99'] = np.percentile(angles, [50, 95, 99]).tolist()
                assert np.min(vectors[:, 2]) > 0, 'Inverted tangent normal found.'
                assert np.percentile(angles, 99) < (12 if name == 'MC_Interior' else 18)
            qa['sets'][name][role] = stats
            binding['texture_sets'][name][role] = str(path.resolve())
            binding['map_metadata'][name][role].update(path=str(path.resolve()), sha256=sha(path), bytes=path.stat().st_size)
    for path in (OUT / 'AnatomyMasks').glob('MC_Interior_*.png'):
        assert sha(path) == sha(SOURCE / 'Textures/AnatomyMasks' / path.name)
    for name in ['MC_Interior', 'MC_Tongue']:
        mask_root = OUT / 'AnatomyMasks' if name == 'MC_Tongue' else SOURCE / 'Textures/AnatomyMasks'
        binding['additional_authored_masks'][name] = {
            role: {'path': str(mask_root / (name + '_' + role + '.png')),
                   'sha256': sha(mask_root / (name + '_' + role + '.png')), 'srgb': False,
                   'semantics': 'Geometry-authored anatomy support, not metric physical thickness'}
            for role in ['ThinTissueMask', 'ArtistDensityMask']}
    project = OUT / 'MC_DAY03_Interior_r06_Refine03.spp'
    saved = {'path': str(project), 'sha256': sha(project), 'bytes': project.stat().st_size}
    write('painter_saved_project.json', saved)
    binding.update(status='FROZEN_PAINTER_RELEASE_NATIVE_UNREAL_VISUAL_PENDING',
                   mesh_packet_sha256=sha(HANDOFF / 'texture_mesh_packet.npz'),
                   static_obj_sha256=sha(HANDOFF / 'MC_DAY03_Interior_Painter_Open.obj'),
                   native_fbx_sha256={p.name: sha(p) for p in HANDOFF.glob('SM_DAY03_*.fbx')},
                   editable_project=saved, selected_pbr_revision='01', selected_mesh_map_revision='01',
                   baking_settings=str(OUT / 'baking_settings.json'),
                   staged_mtl_normalization=str(OUT / 'staged_mtl_identity.json'),
                   viewport_review=str(OUT / 'painter_viewport_qa.json'), texture_qa=str(OUT / 'texture_qa.json'),
                   source_release_manifest_sha256=link['source_release_sha256'],
                   anatomy_mask_receipt=str(OUT / 'AnatomyMasks/anatomy_mask_receipt.json'),
                   uv_reprojection_receipt=str(OUT / 'source_frozen_linkage.json'),
                   cavity_map_policy='MC_Interior: direct frozen r05 map links. Geometry/UV/FBX and regenerated anatomy masks verified exact.',
                   tongue_authoring_policy='Copied editable r05 layer stack, all six anatomy-image bindings regenerated on Refine03 UV; original physical palette/roughness/procedural papillae preserved; fresh 4K six-map bake. No screen-space/shadow paint.',
                   validation_scope='Exact source/UV/linkage and physical-map files; actual Painter front/oblique/grazing reviewed. Native Unreal masking, animation and final reference look remain pending.')
    write('texture_qa.json', qa)
    write('shader_binding.json', binding)
    release = {'status': 'FROZEN_RELEASED_TO_ROOT_FOR_NATIVE_UNREAL',
               'binding': str(OUT / 'shader_binding.json'), 'binding_sha256': sha(OUT / 'shader_binding.json'),
               'editable_project': saved, 'new_maps': 'MC_Tongue five physical maps, 4096x4096; fresh normals/AO/curvature/position/thickness mesh maps.',
               'reused_maps': 'MC_Interior five direct frozen r05 links.',
               'native_visual_approval': 'PENDING_ROOT_UNREAL_GATE',
               'mutations_after_release': 'New explicit revision required; do not alter selected files.'}
    write('release_status.json', release)
    print(json.dumps(release, indent=2))


if __name__ == '__main__':
    main()
