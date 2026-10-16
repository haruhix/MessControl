"""One stronger physical enamel pigment test, preserving the released 01 packet."""
from pathlib import Path
import hashlib
import json
import shutil
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair'
PARENT = SOURCE / 'LookCandidates/WarmCoronalBody_01/Textures'
OUT = SOURCE / 'LookCandidates/WarmCoronalBody_02/Textures'
STAGE = Path('C:/Users/user/Documents/Adobe/Adobe Substance 3D Painter/MessControlMenu3D/DAY03_WarmCoronalBody_02')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def smooth(t):
    t = np.clip(t, 0, 1)
    return t*t*(3-2*t)


def main():
    assert sha(PARENT / 'shader_binding.json') == '3ccbba9ec4c90bf391f6946b34b24d2fb6648650161e09666683e36c04376cc3'
    if OUT.exists() or STAGE.exists():
        raise RuntimeError('Candidate 02 already exists. Inspect it rather than overwrite a revision.')
    linkage = json.loads((PARENT / 'source_frozen_linkage.json').read_text())
    for path, expected in linkage['source_frozen_files'].items():
        assert sha(path) == expected['sha256']
    OUT.mkdir(parents=True); STAGE.mkdir(parents=True)
    shutil.copytree(PARENT / 'AnatomyMasks', OUT / 'AnatomyMasks')
    project = PARENT / 'MC_DAY03_WarmCoronalBody_01_PBR02.spp'
    assert sha(project) == '661851757afcee26ede2daf40467a6a578a0db814df0616a6fa8ca2b399256db'
    shutil.copy2(project, STAGE / 'MC_DAY03_WarmCoronalBody_02.spp')
    data = np.load(SOURCE / 'texture_mesh_packet.npz')
    manifest = json.loads((SOURCE / 'unreal_mouth_handoff.json').read_text())
    vertices = data['vertices']; attrs = np.zeros(len(vertices), np.float32)
    excluded = []
    for part in manifest['parts']:
        if part['material_slot'] != 0:
            continue
        start = part['vertex_start']; end = start + part['vertex_count']; xyz = vertices[start:end]
        is_fang = part['mesh'] in ['MC_DAY03_Upper_Fang_R', 'MC_DAY03_Upper_Fang_L']
        lower = '_Lower_' in part['mesh']
        root, tip = (xyz[:, 2].min(), xyz[:, 2].max()) if lower else (xyz[:, 2].max(), xyz[:, 2].min())
        t = np.clip((xyz[:, 2]-root)/(tip-root), 0, 1)
        attrs[start:end] = 0 if is_fang else 1-smooth((t-.80)/.18)
        if is_fang:
            excluded.append(part['mesh'])
    n = 4096; loops = data['loop_vertex_indices']; uv = data['loop_uv']
    points = np.rint(np.column_stack([uv[:, 0]*(n-1), (1-uv[:, 1])*(n-1)])).astype(np.int32)
    atlas = np.zeros((n, n), np.uint8); coverage = np.zeros((n, n), np.uint8)
    for tri in data['triangle_loop_indices'][data['triangle_material_indices'] == 0]:
        cv2.fillConvexPoly(atlas, points[tri], int(np.rint(attrs[loops[tri]].mean()*255)))
        cv2.fillConvexPoly(coverage, points[tri], 255)
    _, labels = cv2.distanceTransformWithLabels(255-coverage, cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
    lookup = np.zeros(int(labels.max())+1, np.uint8)
    lookup[labels[coverage > 0]] = atlas[coverage > 0]
    atlas = cv2.GaussianBlur(lookup[labels], (0, 0), .7)
    atlas[cv2.dilate(coverage, np.ones((33, 33), np.uint8)) == 0] = 0
    path = OUT / 'AnatomyMasks/MC_Enamel_WarmCoronalBody02.png'
    cv2.imwrite(str(path), atlas)
    shutil.copy2(path, STAGE / path.name)
    linkage.update(parent_candidate_binding=str(PARENT / 'shader_binding.json'),
                   parent_candidate_binding_sha256=sha(PARENT / 'shader_binding.json'),
                   copied_parent_project_sha256=sha(project),
                   candidate_project=str(STAGE / 'MC_DAY03_WarmCoronalBody_02.spp'))
    (OUT / 'source_frozen_linkage.json').write_text(json.dumps(linkage, indent=2))
    receipt = {'packet_sha256': sha(SOURCE / 'texture_mesh_packet.npz'), 'excluded_parts': excluded,
               'body_mask': 'Ordinary crowns only, 1-smoothstep(.80,.98,t), t root to tip. Same formula for upper/lower; no lighting fields.',
               'body_basecolor_srgb': [.86, .64, .56], 'body_opacity': .95,
               'other_pigment_layers': 'Exact released 01 copied SPP; cervical and incisal layers retained.',
               'lighting_sampled': False, 'new_channels': ['BaseColor'],
               'mask_path': str(path), 'mask_sha256': sha(path)}
    (OUT / 'warm_body_mask_receipt.json').write_text(json.dumps(receipt, indent=2))
    print(json.dumps({'out': str(OUT), 'stage': str(STAGE), 'mask_sha256': sha(path)}))


if __name__ == '__main__':
    main()
