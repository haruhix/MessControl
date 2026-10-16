"""Stage a copied Painter project for the frozen Refine03 tongue packet."""
from pathlib import Path
import hashlib
import json
import shutil
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior'
SOURCE = BASE / 'UV_Organic_r05_Candidate01'
HANDOFF = BASE / 'UV_r06_Refine03_Candidate01'
OUT = HANDOFF / 'Textures'
STAGE = Path('C:/Users/user/Documents/Adobe/Adobe Substance 3D Painter/MessControlMenu3D/DAY03_Interior_r06_Refine03')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    assert sha(HANDOFF / 'release_manifest.json') == '535bf9e152190f64ab2663851278278c81df5214f79d1db547e418e28bac0ccb'
    release = json.loads((HANDOFF / 'release_manifest.json').read_text())
    for name, value in release['files'].items():
        assert sha(HANDOFF / name) == value, name
    parent_binding = SOURCE / 'Textures/shader_binding.json'
    assert sha(parent_binding) == '4bf82cf5d3b78147ab38466f93b59853895ba557c34a6e5cfd45fd60fb72e075'
    binding = json.loads(parent_binding.read_text())
    frozen = {str(parent_binding): {'sha256': sha(parent_binding)}}
    for name in ['MC_DAY03_InteriorSource.blend', 'SM_DAY03_Interior.fbx', 'SM_DAY03_Tongue.fbx',
                 'MC_DAY03_Interior_Painter_Open.obj', 'texture_mesh_packet.npz']:
        path = SOURCE / name; frozen[str(path)] = {'sha256': sha(path)}
    for roles in binding['map_metadata'].values():
        for meta in roles.values():
            assert sha(meta['path']) == meta['sha256']
            frozen[meta['path']] = {'sha256': meta['sha256']}
    spp = Path(binding['editable_project']['path'])
    assert sha(spp) == binding['editable_project']['sha256']
    frozen[str(spp)] = {'sha256': sha(spp)}
    if STAGE.exists() or OUT.exists():
        raise RuntimeError('Refine03 Painter stage already exists; inspect before retrying.')
    OUT.mkdir(); STAGE.mkdir()
    shutil.copy2(spp, STAGE / 'MC_DAY03_Interior_r06_Refine03.spp')
    for name in ['MC_DAY03_Interior_Painter_Open.obj', 'MC_DAY03_Interior_Painter_Open.mtl']:
        shutil.copy2(HANDOFF / name, STAGE / name)
    old = np.load(SOURCE / 'texture_mesh_packet.npz')
    new = np.load(HANDOFF / 'texture_mesh_packet.npz')
    transfer = np.load(HANDOFF / 'dorsal_uv_transfer.npz')
    # Cavity owns the first 65536 loops; tongue follows in the exact saved order.
    count = len(transfer['new_loop_uv']); first = len(new['loop_uv']) - count
    assert np.array_equal(new['loop_uv'][first:], transfer['new_loop_uv'])
    assert np.array_equal(old['loop_uv'][first:], transfer['old_loop_uv'])
    assert np.array_equal(new['loop_uv'][:first], old['loop_uv'][:first])
    assert np.array_equal(new['loop_vertex_indices'], old['loop_vertex_indices'])
    assert np.array_equal(new['triangle_loop_indices'], old['triangle_loop_indices'])
    assert np.array_equal(new['triangle_material_indices'], old['triangle_material_indices'])
    assert np.array_equal(new['loop_uv'][first:][~transfer['dorsal_loop_mask']], old['loop_uv'][first:][~transfer['dorsal_loop_mask']])
    report = {'schema': 'messcontrol.r06_tongue_painter_stage.v1',
              'source_release_sha256': sha(HANDOFF / 'release_manifest.json'),
              'source_packet_sha256': sha(HANDOFF / 'texture_mesh_packet.npz'),
              'source_obj_sha256': sha(HANDOFF / 'MC_DAY03_Interior_Painter_Open.obj'),
              'parent_binding': str(parent_binding), 'parent_binding_sha256': sha(parent_binding),
              'copied_project': str(STAGE / 'MC_DAY03_Interior_r06_Refine03.spp'),
              'source_frozen_files': frozen,
              'new_frozen_files': {str(HANDOFF / name): value for name, value in release['files'].items()},
              'uv_transfer': {'sha256': sha(HANDOFF / 'dorsal_uv_transfer.npz'),
                              'old_new_corner_correspondence_verified': True,
                              'ventral_and_cavity_uv_exact': True,
                              'method': 'Anatomical scalar fields are rebaked with barycentric interpolation on new UV coordinates. No old atlas is stretched.'},
              'scope': 'Tongue-only new masks and mesh-map/PBR bake; cavity links frozen r05 maps.'}
    (OUT / 'source_frozen_linkage.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'stage': str(STAGE), 'out': str(OUT), 'uv_transfer_verified': True}))


if __name__ == '__main__':
    main()
