"""CPU validation of authored open OBJ/basis and GumsV04 cache interfaces."""
from pathlib import Path
import hashlib
import json
import numpy as np

ROOT = Path(__file__).resolve().parents[3] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'


def sha(array):
    return hashlib.sha256(np.asarray(array).tobytes()).hexdigest()


def canonical_face_uv(faces, uv):
    order = np.argsort(faces, axis=1)
    vertices = np.take_along_axis(faces, order, axis=1)
    texcoords = np.take_along_axis(uv, order[..., None], axis=1).astype(np.float32)
    face_order = np.lexsort(vertices.T[::-1])
    return vertices[face_order], texcoords[face_order]


results = {}
for kind, folder, obj_name, basis_name in [
    ('jaws', ROOT / 'Jaws/ContourHero_v14/Full32', 'Exports/MC_DAY03_Full32_v14_Open_UV0.obj', 'OpenBasis_v14_world_local_uv.npz'),
    ('gums', ROOT / 'Gums/V04', 'Gums_SourceLocked.obj', 'OpenBasis_v04_world_local_uv.npz')]:
    basis = np.load(folder / basis_name)
    obj_vertices, materials = {}, set()
    current = None
    with (folder / obj_name).open(encoding='utf8') as f:
        for line in f:
            if line.startswith('o '):
                current = line.strip()[2:]
                obj_vertices[current] = []
            elif line.startswith('v '):
                obj_vertices[current].append([float(n) for n in line.split()[1:4]])
            elif line.startswith('usemtl '):
                materials.add(line.strip()[7:])
    objects = []
    for name, xyz in obj_vertices.items():
        actual = np.asarray(xyz)
        expected = basis[name + '__world_vertices']
        error = float(np.max(np.abs(actual - expected))) if actual.shape == expected.shape else None
        if error is None or error > 5.1e-7:
            raise RuntimeError('OBJ axes/positions/order mismatch: ' + name + '/' + str(error))
        objects.append({'object': name, 'vertices': len(expected), 'max_OBJ_rounding_error': error})
    results[kind] = {'OBJ_native_world_axes_and_order_pass': True, 'objects': objects, 'materials': sorted(materials)}
    if kind == 'gums':
        cache_records = []
        for side in ('upper', 'lower'):
            name = 'MC_DAY03_' + side.title() + '_Gum'
            cache = np.load(folder / 'Constraints' / (side + '_surface.npz'))
            expected = basis[name + '__local_vertices'].astype(np.float32)
            cached = cache['vertices'].astype(np.float32)
            if not np.array_equal(expected, cached):
                raise RuntimeError('Gum cache mesh32 positions differ: ' + side)
            indices = basis[name + '__loop_vertex_indices']
            starts = basis[name + '__polygon_loop_starts']
            sizes = basis[name + '__polygon_loop_sizes']
            if not np.all(sizes == 4):
                raise RuntimeError('Gum quad-cache contract changed')
            faces = np.asarray([indices[start:start+4] for start in starts])
            uv = np.asarray([basis[name + '__UV0'][start:start+4] for start in starts])
            va, ua = canonical_face_uv(faces, uv)
            vb, ub = canonical_face_uv(cache['faces'], cache['uv'])
            if not np.array_equal(va, vb) or not np.array_equal(ua, ub):
                raise RuntimeError('Gum cache canonical vertex/UV0 mapping differs: ' + side)
            cache_records.append({'object': name, 'mesh_positions_float32_sha256': sha(expected),
                                  'cache_positions_float32_identical': True,
                                  'canonical_face_indices_sha256': sha(va),
                                  'canonical_vertex_UV0_mapping_sha256': sha(ua),
                                  'cache_UV0_mapping_identical': True,
                                  'semantic_faces': len(cache['semantic'])})
        results[kind]['constraints_cache_interface'] = cache_records
    (folder / 'OpenBasis_CPU_export_audit.json').write_text(json.dumps(results[kind], indent=2), encoding='utf8')
print(json.dumps({'jaws': {'objects': len(results['jaws']['objects']), 'OBJ_pass': True},
                  'gums': results['gums']['constraints_cache_interface']}, indent=2))
