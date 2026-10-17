"""Compare saved Unreal FBX exports without opening or modifying any DCC project."""
import hashlib
import importlib.util
import json
from collections import Counter
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/ManualIntake/ShieldInspection'
spec = importlib.util.spec_from_file_location('mc_shield_fbx_reader', ROOT / 'Tools/Blender/bake_nut_shield_crack_layout.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def label(item):
    return item['props'][1].split(b'\0', 1)[0].split(b'::')[-1].decode('utf-8', errors='replace')


def inventory(path):
    roots, version = reader.read_fbx(path)
    objects = next(n['children'] for n in roots if n['name'] == b'Objects')
    links = next(n['children'] for n in roots if n['name'] == b'Connections')
    models = {n['props'][0]: n for n in objects if n['name'] == b'Model'}
    parents = {n['props'][1]: n['props'][2] for n in links if n['props'][0] == b'OO'}
    poses = {}
    for pose in (n for n in objects if n['name'] == b'Pose'):
        for node in (n for n in pose['children'] if n['name'] == b'PoseNode'):
            data = {n['name']: n['props'][0] for n in node['children']}
            poses[data[b'Node']] = np.array(data[b'Matrix']).reshape(4, 4).T
    triangles, geometries = [], []
    for geom in (n for n in objects if n['name'] == b'Geometry' and n['props'][-1] == b'Mesh'):
        data = {n['name']: n for n in geom['children']}
        vertices = data[b'Vertices']['props'][0].reshape(-1, 3)
        model_id = parents.get(geom['props'][0])
        transform = poses.get(model_id, np.eye(4))
        points = (transform @ np.column_stack((vertices, np.ones(len(vertices)))).T).T[:, :3]
        polygons, pending = [], []
        for index in data[b'PolygonVertexIndex']['props'][0]:
            pending.append(int(index if index >= 0 else -index - 1))
            if index < 0:
                polygons.extend((pending[0], pending[i], pending[i + 1]) for i in range(1, len(pending) - 1))
                pending = []
        assert not pending
        triangles.extend(points[list(poly)] for poly in polygons)
        geometries.append(dict(name=label(geom), vertices=len(vertices), triangles=len(polygons),
                               mesh_node=label(models[model_id]) if model_id in models else None,
                               bind_pose_present=model_id in poses,
                               bounds=[points.min(axis=0).tolist(), points.max(axis=0).tolist()]))
    bones = {}
    for ident, node in models.items():
        if node['props'][-1] != b'LimbNode':
            continue
        parent = models.get(parents.get(ident))
        bones[label(node)] = dict(parent=label(parent) if parent and parent['props'][-1] == b'LimbNode' else None,
                                  bind=poses[ident].flatten().tolist() if ident in poses else None)
    return dict(file=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), fbx_version=version,
                geometries=geometries, bones=bones), np.array(triangles, dtype=np.float64)


def key(triangle, decimals):
    return tuple(sorted(tuple(p) for p in np.round(triangle, decimals)))


def main():
    records, meshes = zip(*(inventory(OUT / (name + '.fbx')) for name in ('SK_NutTank', 'SK_NutTank_NoShield')))
    main_mesh, bare_mesh = meshes
    comparisons = []
    # Report both 0.001 and 0.01 FBX-unit tolerances; floating export noise must
    # not be mistaken for wholesale lost geometry.
    for decimals in (3, 2):
        a, b = (Counter(key(tri, decimals) for tri in mesh) for mesh in meshes)
        missing, added = a - b, b - a
        centers = [np.array(tri).mean(axis=0) for tri in missing]
        comparisons.append(dict(decimal_places=decimals, removed_triangles=sum(missing.values()),
                                unexpected_new_triangles=sum(added.values()),
                                removed_bounds=[np.array(centers).min(axis=0).tolist(), np.array(centers).max(axis=0).tolist()] if centers else None))
    a, b = records[0]['bones'], records[1]['bones']
    common = sorted(set(a) & set(b))
    bind_delta = max((max(abs(x - y) for x, y in zip(a[n]['bind'], b[n]['bind']))
                      for n in common if a[n]['bind'] is not None and b[n]['bind'] is not None), default=None)
    report = dict(exports=records, mesh_triangle_counts=[len(m) for m in meshes],
                  comparisons=comparisons, same_bone_names=set(a) == set(b),
                  same_bone_parents=all(a[n]['parent'] == b[n]['parent'] for n in common),
                  bind_pose_max_delta=bind_delta,
                  conclusion='Inspect counts/bounds before choosing a fix; this report does not fabricate rendered evidence.')
    (OUT / 'GeometryComparison.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'exports'}, indent=2))


if __name__ == '__main__':
    main()
