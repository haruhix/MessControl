"""Deterministic native-food hybrid collision plan; writes reports only.

Run outside Unreal with NumPy:
python Tools/Unreal/plan_food_collision.py Saved/FoodCollisionProbe/FoodCollisionBefore.json

The input must contain actual asset-local native LOD vertices/indices and their
render_geometry_sha256. Unreal authoring consumes the resulting JSON separately.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import sys

# The exact runtime stays isolated under Saved; resolve it before importing NumPy.
if '--exact-source' in sys.argv:
    dependency_parser = argparse.ArgumentParser(add_help=False)
    dependency_parser.add_argument('--dependencies-root', type=Path, default=Path('Saved/FoodCollisionProbe/ExactPythonDeps'))
    dependency_args, _ = dependency_parser.parse_known_args()
    sys.path.insert(0, str(dependency_args.dependencies_root.resolve()))
import numpy as np
from food_collision_components import analyze
from food_collision_star import decompose
from food_collision_geometry import numpy_hull
from food_collision_authoring_guard import file_sha256, json_sha256


def pkg(path):
    return path.split('.')[0]


def hull_element(points):
    p, f = numpy_hull(points)
    return {'vertex_data': p.tolist(), 'index_data': f.reshape(-1).tolist(),
            'transform': {'translation': [0, 0, 0], 'rotationQuat': [0, 0, 0, 1], 'scale': [1, 1, 1]}}


def orient_closed_faces(points, faces):
    faces = np.asarray(faces, int).copy()
    edge_uses = {}
    for index, (a, b, c) in enumerate(faces):
        for i, j in ((a, b), (b, c), (c, a)):
            edge_uses.setdefault(tuple(sorted((int(i), int(j)))), []).append((index, int(i), int(j)))
    if any(len(uses) != 2 for uses in edge_uses.values()):
        return None
    adjacent = [[] for _ in faces]
    for pair in edge_uses.values():
        (a, i, j), (b, k, l) = pair
        same = (i, j) == (k, l)
        adjacent[a].append((b, same))
        adjacent[b].append((a, same))
    reverse = {0: False}
    stack = [0]
    while stack:
        f = stack.pop()
        for other, same in adjacent[f]:
            desired = reverse[f] ^ same
            if other in reverse and reverse[other] != desired:
                return None
            if other not in reverse:
                reverse[other] = desired
                stack.append(other)
    if len(reverse) != len(faces):
        return None
    for f, flip in reverse.items():
        if flip:
            faces[f] = faces[f][[0, 2, 1]]
    triangles = points[faces]
    volume = float(np.einsum('ij,ij->i', triangles[:, 0], np.cross(triangles[:, 1], triangles[:, 2])).sum()/6)
    if volume < 0:
        faces = faces[:, [0, 2, 1]]
        volume = -volume
    return faces, volume


def egg_fragment_collision_cap(component):
    p = np.asarray(component['vertex_data'], float)
    f = np.asarray(component['index_data'], int).reshape(-1, 3)
    edges = {}
    for tri in f:
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            key = tuple(sorted((int(a), int(b))))
            edges[key] = edges.get(key, 0)+1
    boundary = [edge for edge, count in edges.items() if count == 1]
    neighbours = {}
    for a, b in boundary:
        neighbours.setdefault(a, []).append(b)
        neighbours.setdefault(b, []).append(a)
    if len(boundary) != 4 or len(neighbours) != 4 or any(len(v) != 2 for v in neighbours.values()):
        return None, {'status': 'unsupported_boundary_not_single_quad'}
    loop = [min(neighbours)]
    while len(loop) < 4:
        nxt = sorted(v for v in neighbours[loop[-1]] if v not in loop)
        if not nxt:
            return None, {'status': 'unsupported_boundary_loop'}
        loop.append(nxt[0])
    bp = p[loop]
    _, _, vh = np.linalg.svd(bp-bp.mean(axis=0))
    residual = float(abs((bp-bp.mean(axis=0)) @ vh[-1]).max())
    if residual > .0002:
        return None, {'status': 'unsupported_nonplanar_boundary', 'plane_max_residual_cm': residual}
    capped = np.concatenate((f, [[loop[0], loop[1], loop[2]], [loop[0], loop[2], loop[3]]]))
    oriented = orient_closed_faces(p, capped)
    if oriented is None:
        return None, {'status': 'unsupported_closed_orientation'}
    capped, volume = oriented
    c = copy.deepcopy(component)
    c.update({'closed': True, 'boundary_edges': 0, 'nonmanifold_edges': 0,
              'triangle_count': len(capped), 'index_data': capped.reshape(-1).tolist()})
    result = decompose(c)
    if result['status'] == 'unsupported_no_strict_centroid_kernel':
        # A cut piece can be star-shaped around a point other than its vertex
        # centroid. Project deterministically into its oriented face halfspaces.
        triangles = p[capped]
        normals = np.cross(triangles[:, 1]-triangles[:, 0], triangles[:, 2]-triangles[:, 0])
        normals /= np.linalg.norm(normals, axis=1)[:, None]
        offsets = -np.einsum('ij,ij->i', normals, triangles[:, 0])
        q = p.mean(axis=0)
        clearance = max(np.linalg.norm(np.ptp(p, axis=0))*1e-7, .0002)*2
        for iteration in range(10000):
            distances = normals @ q+offsets
            worst = int(np.argmax(distances))
            if distances[worst] <= -clearance:
                result = decompose(c, kernel=q)
                result['kernel_solver_iterations'] = iteration
                break
            q -= normals[worst]*(distances[worst]+clearance)
        result['kernel_solver_max_plane_distance_cm'] = float((normals @ q+offsets).max())
    certificate = {'status': result['status'], 'boundary_loop_vertex_ids': loop,
                   'boundary_positions': bp.tolist(), 'plane_max_residual_cm': residual,
                   'collision_only_cap_triangles': capped[-2:].tolist(),
                   'closed_oriented_volume_cm3': volume,
                   'convex_count': result.get('convex_count'),
                   'kernel': result.get('kernel'),
                   'kernel_solver_iterations': result.get('kernel_solver_iterations'),
                   'kernel_solver_max_plane_distance_cm': result.get('kernel_solver_max_plane_distance_cm'),
                   'note': 'Only the collision solid is capped; source render geometry is unchanged.'}
    return result, certificate


def plan_mesh(mesh):
    name = mesh['path'].split('.')[-1]
    parts = analyze(mesh)['components']
    result = {'path': pkg(mesh['path']), 'source_object_path': mesh['path'],
              'source_render_sha256': mesh['render_geometry_sha256'],
              'method': 'native_vhacd', 'convex_elems': [], 'voxel_components': [],
              'native_vhacd': {'maximum_convex_hulls': 128, 'maximum_convex_vertices': 256, 'voxel_resolution': 16000000}}
    if name == 'SM_Egg_01':
        result.update(method='source_convex', convex_elems=[hull_element(parts[0]['vertex_data'])])
    elif name == 'SM_Egg_03':
        source, certificate = egg_fragment_collision_cap(parts[0])
        result['collision_only_cap_certificate'] = certificate
        if source and source['status'] == 'exact_star_surface_convex_union' and source['convex_count'] <= 100:
            result.update(method='source_convex', convex_elems=source['convex_elems'])
    elif name in ('SM_Grape_A', 'SM_Grape_D'):
        result.update(method='source_convex', convex_elems=[hull_element(parts[0]['vertex_data'])])
        result['approximation_note'] = 'All original berry vertices retained; very small triangulated-quad concavities use their convex envelope.'
    elif name in ('SM_GrapesGreen', 'SM_GrapesPurple'):
        # The native stem is the largest connected component; the other 46 are
        # independent closed berries, preserving spaces between those berries.
        stem = max(parts, key=lambda c: c['triangle_count'])
        berries = [c for c in parts if c is not stem]
        if len(berries) != 46 or not all(c['closed'] for c in berries):
            raise ValueError(f'{name}: expected 46 closed berry components and one stem')
        result.update(method='hybrid_components',
                      convex_elems=[hull_element(c['vertex_data']) for c in berries],
                      voxel_components=[{'vertex_data': stem['vertex_data'], 'index_data': stem['index_data']}])
        result['approximation_note'] = 'One all-source-vertex hull per berry; only the isolated native stem uses V-HACD.'
    else:
        # Preserve exact small source fragments without unreasonable shape counts.
        is_simple = name == 'SM_Egg_02' or any(word in name for word in ('Slice', 'Quarters', 'CarrotACutA'))
        if is_simple:
            decomposed = [decompose(c) for c in parts]
            if all(d['status'] == 'exact_star_surface_convex_union' for d in decomposed):
                count = sum(d['convex_count'] for d in decomposed)
                if count <= 100:
                    result.update(method='source_convex', convex_elems=sum((d['convex_elems'] for d in decomposed), []))
                    result['source_surface_certificate'] = [{'component': i, 'kernel': d['kernel'],
                        'support_tolerance_cm': d['support_tolerance_cm'],
                        'maximum_absolute_relative_hull_volume_error': d['maximum_absolute_relative_hull_volume_error']}
                        for i, d in enumerate(decomposed)]
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--exact-source', action='store_true', help='Preserve closed native surfaces with pinned kernel/Tetgen convex unions; certify open caps and raw planar risk')
    parser.add_argument('--dependencies-root', type=Path, default=Path('Saved/FoodCollisionProbe/ExactPythonDeps'))
    parser.add_argument('--tetgen-library', type=Path, help='Explicit checked raw _tetgen.pyd, for an existing isolated dependency install')
    parser.add_argument('--actor-scale', type=float, default=20, help='Actor stress scale used for explicit external-cap world-distance guards')
    parser.add_argument('--maximum-external-cap-world-gap-cm', type=float, default=1)
    args = parser.parse_args()
    source = json.loads(args.input.read_text(encoding='utf-8-sig'))
    if args.exact_source:
        from food_collision_exact import build_exact_plan
        output = args.output or Path('Saved/FoodCollisionProbe/FoodExactSourcePlan.json')
        output.parent.mkdir(parents=True, exist_ok=True)
        def save_progress(result):
            temporary = output.with_suffix(output.suffix+'.tmp')
            temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
            temporary.replace(output)
        result = build_exact_plan(source, args.dependencies_root, args.tetgen_library,
                                  args.actor_scale, args.maximum_external_cap_world_gap_cm,
                                  source_report=args.input, progress=save_progress)
        if not result['complete']:
            raise RuntimeError(f"Exact source plan incomplete: {len(result['errors'])} errors; see {output}")
        print('Saved complete exact source plan', output, flush=True)
        return
    if source.get('complete') is not True or source.get('errors'):
        raise RuntimeError('A complete, error-free native audit is required; unfinished exports are not planning evidence.')
    if any(m.get('error') or not m.get('render_geometry_sha256') or not m.get('native_render_sections') for m in source['meshes']):
        raise RuntimeError('Native render vertices, triangles and SHA256 are required for every planned mesh.')
    result = {'source_report': str(args.input.resolve()), 'source_report_sha256': file_sha256(args.input),
              'scope': source['scope'], 'scope_sha256': json_sha256(source['scope']),
              'saved_table_rows_sha256': json_sha256(source['saved_table_rows']),
              'coordinate_space': 'native asset-local; no actor or DataTable scale applied',
              'meshes': [], 'source_render_changes': False, 'complete': False}
    for mesh in source['meshes']:
        entry = plan_mesh(mesh)
        entry['table_uses'] = mesh['table_uses']
        result['meshes'].append(entry)
        print(entry['path'], entry['method'], 'source', len(entry['convex_elems']), 'voxel_parts', len(entry['voxel_components']), flush=True)
    result['complete'] = True
    output = args.output or Path('Saved/FoodCollisionProbe/FoodHybridConvexPlan.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Saved', output, flush=True)


if __name__ == '__main__':
    main()
