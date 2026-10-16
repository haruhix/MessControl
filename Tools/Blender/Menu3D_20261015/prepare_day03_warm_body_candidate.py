"""Build one BaseColor-only candidate from immutable Final96 anatomy and UV0."""
from pathlib import Path
import hashlib
import json
import shutil
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08b_SeamUV/Candidates/Final96_GumMinStretch_LocalFangRepair'
OUT = SOURCE / 'LookCandidates/WarmCoronalBody_01/Textures'
STAGE = Path('C:/Users/user/Documents/Adobe/Adobe Substance 3D Painter/MessControlMenu3D/DAY03_WarmCoronalBody_01')


def smooth(t):
    t = np.clip(t, 0, 1)
    return t*t*(3-2*t)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    packet = SOURCE / 'texture_mesh_packet.npz'
    assert sha(packet) == 'bb42a0c3fd91f8e2a180ec179c61188c9aaac085529806c30ec2b38876b13eea'
    data = np.load(packet)
    manifest = json.loads((SOURCE / 'unreal_mouth_handoff.json').read_text())
    vertices = data['vertices']; attrs = np.zeros((len(vertices), 3), np.float32)
    excluded = []
    for part in manifest['parts']:
        if part['material_slot'] != 0:
            continue
        st = part['vertex_start']; en = st + part['vertex_count']; coords = vertices[st:en]
        is_fang = part['mesh'] in ['MC_DAY03_Upper_Fang_R', 'MC_DAY03_Upper_Fang_L']
        lower = '_Lower_' in part['mesh']
        root, tip = (coords[:, 2].min(), coords[:, 2].max()) if lower else (coords[:, 2].max(), coords[:, 2].min())
        t = np.clip((coords[:, 2]-root)/(tip-root), 0, 1)
        attrs[st:en, 0] = 0 if is_fang else 1-smooth((t-.65)/.25)
        attrs[st:en, 1] = 0 if is_fang else 1
        attrs[st:en, 2] = 1 if is_fang else 0
        if is_fang:
            excluded.append(part['mesh'])
    n = 4096; loops = data['loop_vertex_indices']; uv = data['loop_uv']
    points = np.rint(np.column_stack([uv[:, 0]*(n-1), (1-uv[:, 1])*(n-1)])).astype(np.int32)
    atlas = np.zeros((n, n, 3), np.uint8); coverage = np.zeros((n, n), np.uint8)
    for tri in data['triangle_loop_indices'][data['triangle_material_indices'] == 0]:
        values = np.rint(attrs[loops[tri]].mean(0)*255).astype(np.uint8)
        cv2.fillConvexPoly(atlas, points[tri], tuple(int(v) for v in values))
        cv2.fillConvexPoly(coverage, points[tri], 255)
    _, labels = cv2.distanceTransformWithLabels(255-coverage, cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
    lookup = np.zeros((int(labels.max())+1, 3), np.uint8)
    lookup[labels[coverage > 0]] = atlas[coverage > 0]
    atlas = cv2.GaussianBlur(lookup[labels], (0, 0), .7)
    atlas[cv2.dilate(coverage, np.ones((33, 33), np.uint8)) == 0] = 0
    masks = OUT / 'AnatomyMasks'; masks.mkdir(parents=True, exist_ok=True)
    original = SOURCE / 'Textures/AnatomyMasks'
    incisal = cv2.imread(str(original/'MC_Enamel_Incisal.png'), cv2.IMREAD_GRAYSCALE)
    outputs = {'WarmCoronalBody': atlas[:, :, 0],
               'OrdinaryIncisal': np.rint(incisal.astype(np.float32)*atlas[:, :, 1]/255).astype(np.uint8),
               'FangIncisal': np.rint(incisal.astype(np.float32)*atlas[:, :, 2]/255).astype(np.uint8),
               'UpperFangID': atlas[:, :, 2], 'Coverage': coverage}
    for name, pixels in outputs.items():
        cv2.imwrite(str(masks/('MC_Enamel_'+name+'.png')), pixels)
    shutil.copy2(original/'MC_Enamel_Cervical.png', masks/'MC_Enamel_Cervical.png')
    for path in masks.glob('*.png'):
        shutil.copy2(path, STAGE/path.name)
    recipe = [{'type': 'group', 'name': 'WarmBody01 | Physical enamel pigment', 'children': [
        {'type': 'fill', 'name': '05 | Fang incisal ivory preserved', 'active_channels': ['BaseColor'], 'base_color': [.975, .94, .875], 'mask': {'background': 'Black'}},
        {'type': 'fill', 'name': '04 | Ordinary incisal ivory restrained', 'active_channels': ['BaseColor'], 'base_color': [.975, .94, .875], 'mask': {'background': 'Black'}},
        {'type': 'fill', 'name': '03 | Original cervical pigment', 'active_channels': ['BaseColor'], 'base_color': [.855, .735, .585], 'mask': {'background': 'Black'}},
        {'type': 'fill', 'name': '02 | Warm ordinary coronal body', 'active_channels': ['BaseColor'], 'base_color': [.91, .75, .65], 'mask': {'background': 'Black'}},
        {'type': 'fill', 'name': '01 | Original clean ivory base', 'active_channels': ['BaseColor'], 'base_color': [.95, .875, .77]}]}]
    (OUT/'warm_body_recipe.json').write_text(json.dumps(recipe, indent=2))
    receipt = {'packet_sha256': sha(packet), 'excluded_parts': excluded, 'body_mask': 'Ordinary crowns only, 1-smoothstep(.65,.90,t), t root to tip.',
               'lighting_sampled': False, 'new_channels': ['BaseColor'], 'incisal_opacity': {'ordinary': .35, 'upper_fang': .68},
               'body_opacity': .75, 'cervical_opacity': .62,
               'mask_files': {p.name: {'path': str(p), 'sha256': sha(p)} for p in masks.glob('*.png')}}
    (OUT/'warm_body_mask_receipt.json').write_text(json.dumps(receipt, indent=2))
    print(json.dumps({'out': str(OUT), 'excluded_parts': excluded}))


if __name__ == '__main__':
    main()
