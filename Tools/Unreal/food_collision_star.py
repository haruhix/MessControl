"""Exact source-surface convex decomposition for verified star-shaped components.

An interior kernel point and each render face form an exact tetrahedral fan.
Adjacent tetrahedra are merged only when every exposed boundary face supports
all merged vertices. No voxel sampling, moving vertices or filling concavities.
Self-intersections of source meshes are not checked by this diagnostic.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import numpy as np
from food_collision_geometry import numpy_hull


def decompose(component, max_vertices=256, kernel=None):
    points = np.asarray(component['vertex_data'], float)
    faces = np.asarray(component['index_data'], int).reshape(-1, 3)
    diagonal = float(np.linalg.norm(np.ptp(points, axis=0)))
    # Native single-precision positions and triangulated nominally planar quads
    # carry sub-micron noise. This floor is 0.024cm even at maximum DT*actor120.
    eps = max(max(diagonal, 1.)*1e-7, .0002)
    if not component['closed'] or not component['full_dimension']:
        edges = {}
        for f in faces:
            for a, b in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
                key = tuple(sorted((int(a), int(b))))
                edges[key] = edges.get(key, 0)+1
        boundary = [e for e, count in edges.items() if count == 1]
        boundary_ids = sorted({v for e in boundary for v in e})
        bp = points[boundary_ids]
        residual = None
        if len(bp) >= 3:
            _, _, vh = np.linalg.svd(bp-bp.mean(axis=0))
            residual = float(abs((bp-bp.mean(axis=0)) @ vh[-1]).max())
        return {'status': 'unsupported_open_or_lower_dimensional',
                'boundary_edges': [list(e) for e in boundary],
                'boundary_vertex_ids': boundary_ids, 'boundary_vertex_positions': bp.tolist(),
                'boundary_best_fit_plane_max_residual_cm': residual,
                'note': 'No cap authored; boundary geometry is provided for separate review.'}
    # The earlier plan oriented each face relative to this kernel candidate.
    # Verify edge orientation consistency before using that orientation as solid.
    oriented_edges = {}
    for a, b, c in faces:
        for i, j in ((a, b), (b, c), (c, a)):
            oriented_edges.setdefault(tuple(sorted((int(i), int(j)))), []).append((int(i), int(j)))
    bad_edges = sum(len(v) != 2 or v[0] != v[1][::-1] for v in oriented_edges.values())
    if bad_edges:
        return {'status': 'unsupported_kernel_candidate_or_inconsistent_orientation', 'inconsistent_edges': bad_edges}
    anchor = points.mean(axis=0) if kernel is None else np.asarray(kernel, dtype=float)
    vertices = np.concatenate((points, anchor[None]))
    anchor_index = len(points)
    original_triangles = points[faces]
    cross = np.cross(original_triangles[:, 1]-original_triangles[:, 0], original_triangles[:, 2]-original_triangles[:, 0])
    normals = cross/np.linalg.norm(cross, axis=1)[:, None]
    anchor_distances = np.einsum('ij,ij->i', normals, anchor-original_triangles[:, 0])
    if anchor_distances.max() > eps or np.any(anchor_distances >= -eps):
        return {'status': 'unsupported_no_strict_centroid_kernel', 'max_kernel_plane_distance_cm': float(anchor_distances.max())}
    offsets = -np.einsum('ij,ij->i', normals, original_triangles[:, 0])
    tetra_volumes = abs(np.einsum('ij,ij->i', original_triangles[:, 0]-anchor,
                         np.cross(original_triangles[:, 1]-anchor, original_triangles[:, 2]-anchor)))/6
    face_neighbours = [set() for _ in faces]
    edge_faces = {}
    for f, tri in enumerate(faces):
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            edge_faces.setdefault(tuple(sorted((int(a), int(b)))), []).append(f)
    for f, g in edge_faces.values():
        face_neighbours[f].add(g)
        face_neighbours[g].add(f)
    groups = {f: {'faces': {f}, 'vertices': set(map(int, faces[f]))|{anchor_index},
                  'neighbours': set(face_neighbours[f])} for f in range(len(faces))}
    # Do not let skinny radial fan faces amplify tiny numeric quad deviations
    # for an already convex complete component (notably the whole egg).
    full_component_support = float((points @ normals.T+offsets).max())
    if len(points) <= max_vertices and full_component_support <= eps:
        groups = {0: {'faces': set(range(len(faces))), 'vertices': set(range(len(vertices))),
                      'neighbours': set()}}
    face_owner = np.arange(len(faces))
    rejected = set()
    tested = merged = 0

    def convex_union(a, b):
        nonlocal tested
        tested += 1
        fs = groups[a]['faces'] | groups[b]['faces']
        ids = groups[a]['vertices'] | groups[b]['vertices']
        if len(ids) > max_vertices:
            return None
        p = vertices[sorted(ids)]
        fi = sorted(fs)
        # All original faces remain on the merged external surface.
        if (p @ normals[fi].T + offsets[fi]).max() > eps:
            return None
        edge_counts = {}
        for f in fi:
            tri = faces[f]
            for i, j in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
                key = tuple(sorted((int(i), int(j))))
                edge_counts[key] = edge_counts.get(key, 0)+1
        perimeter = [edge for edge, n in edge_counts.items() if n == 1]
        if perimeter:
            rt = vertices[np.array([[anchor_index, *edge] for edge in perimeter], int)]
            rn = np.cross(rt[:, 1]-rt[:, 0], rt[:, 2]-rt[:, 0])
            length = np.linalg.norm(rn, axis=1)
            valid = length > max(diagonal, 1.)**2*1e-12
            rt, rn = rt[valid], rn[valid]/length[valid, None]
            reverse = np.einsum('ij,ij->i', rn, p.mean(axis=0)-rt[:, 0]) > 0
            rn[reverse] *= -1
            ro = -np.einsum('ij,ij->i', rn, rt[:, 0])
            if (p @ rn.T + ro).max() > eps:
                return None
        return {'faces': fs, 'vertices': ids,
                'neighbours': (groups[a]['neighbours']|groups[b]['neighbours'])-{a, b}}

    while True:
        candidates = []
        for a, g in groups.items():
            for b in g['neighbours']:
                if b > a and b in groups and (a, b) not in rejected:
                    # Grow large surface patches first; deterministic tie order.
                    candidates.append((-(len(g['faces'])+len(groups[b]['faces'])), a, b))
        if not candidates:
            break
        candidates.sort()
        made_merge = False
        for _, a, b in candidates:
            result = convex_union(a, b)
            if result is None:
                rejected.add((a, b))
                continue
            groups[a] = result
            del groups[b]
            for n in result['neighbours']:
                groups[n]['neighbours'].discard(b)
                groups[n]['neighbours'].discard(a)
                groups[n]['neighbours'].add(a)
            face_owner[list(result['faces'])] = a
            rejected = {pair for pair in rejected if a not in pair and b not in pair}
            merged += 1
            made_merge = True
            break
        if not made_merge:
            break
    hulls = []
    for group in sorted(groups.values(), key=lambda g: -len(g['faces'])):
        source = vertices[sorted(group['vertices'])]
        hp, hi = numpy_hull(source)
        ht = hp[hi]
        hull_volume = abs(float(np.einsum('ij,ij->i', ht[:, 0], np.cross(ht[:, 1], ht[:, 2])).sum()/6))
        source_volume = float(tetra_volumes[list(group['faces'])].sum())
        volume_error = hull_volume-source_volume
        hulls.append({'vertex_data': hp.tolist(), 'index_data': hi.reshape(-1).tolist(),
                      'transform': {'translation': [0, 0, 0], 'rotationQuat': [0, 0, 0, 1], 'scale': [1, 1, 1]},
                      'source_render_faces': sorted(group['faces']),
                      'hull_volume_cm3': hull_volume, 'source_tetra_volume_cm3': source_volume,
                      'hull_minus_source_volume_cm3': volume_error,
                      'relative_volume_error': volume_error/max(source_volume, 1e-30)})
    return {'status': 'exact_star_surface_convex_union', 'kernel': anchor.tolist(),
            'source_triangle_count': len(faces), 'initial_tetrahedra': len(faces),
            'convex_count': len(hulls), 'merge_count': merged, 'candidate_tests': tested,
            'support_tolerance_cm': eps, 'convex_elems': hulls,
            'whole_component_support_plane_violation_cm': full_component_support,
            'maximum_absolute_hull_volume_error_cm3': max(abs(h['hull_minus_source_volume_cm3']) for h in hulls),
            'maximum_absolute_relative_hull_volume_error': max(abs(h['relative_volume_error']) for h in hulls),
            'note': 'Exact for a simple closed consistently oriented star-shaped source surface, within stated floating-point tolerance; source self-intersection is not checked.'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mesh', action='append', default=[])
    parser.add_argument('--component-limit', type=int, default=0)
    args = parser.parse_args()
    source = json.loads(args.input.read_text(encoding='utf-8-sig'))
    result = {'source_report': str(args.input.resolve()), 'meshes': []}
    for mesh in source['meshes']:
        if args.mesh and not any(x.lower() in mesh['path'].lower() for x in args.mesh):
            continue
        entry = {'path': mesh['path'], 'table_uses': mesh.get('table_uses', []), 'components': []}
        for c in mesh['components']:
            if args.component_limit and c['component'] >= args.component_limit:
                break
            d = decompose(c)
            d['component'] = c['component']
            entry['components'].append(d)
            print(mesh['path'].split('.')[-1], c['component'], d['status'], 'hulls', d.get('convex_count'), 'tests', d.get('candidate_tests'), flush=True)
        result['meshes'].append(entry)
    result['complete'] = True
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Saved', args.output, flush=True)


if __name__ == '__main__':
    main()
