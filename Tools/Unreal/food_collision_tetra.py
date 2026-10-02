"""Source-boundary preserving greedy tetrahedral convex closure.

Pure NumPy; callers supply an independently verified constrained tetrahedral mesh.
Groups may merge only when their boundary is convex, or their complete convex
closure has the same volume as all enclosed source tetrahedra.
"""
from __future__ import annotations
import heapq
import time
import numpy as np
from food_collision_geometry import numpy_hull


def merge_tetrahedra(nodes, cells, max_vertices=256, epsilon=.0002):
    start = time.perf_counter()
    nodes, cells = np.asarray(nodes, float), np.asarray(cells, int)
    groups, face_owners = {}, {}
    for index, cell in enumerate(cells):
        p = nodes[cell]
        volume = abs(float(np.dot(p[1]-p[0], np.cross(p[2]-p[0], p[3]-p[0]))))/6
        boundary = {}
        for corner in range(4):
            face = list(cell[np.arange(4) != corner])
            tri = nodes[face]
            normal = np.cross(tri[1]-tri[0], tri[2]-tri[0])
            length = np.linalg.norm(normal)
            if length <= 1e-20:
                raise ValueError('Degenerate Tetgen face')
            normal /= length
            if np.dot(normal, p.mean(axis=0)-tri[0]) > 0:
                face[1], face[2] = face[2], face[1]
                normal *= -1
            key = tuple(sorted(map(int, face)))
            boundary[key] = (normal, -np.dot(normal, tri[0]), face)
            face_owners.setdefault(key, []).append(index)
        groups[index] = {'faces': boundary, 'vertices': set(map(int, cell)),
                         'volume': volume, 'neighbours': set(), 'version': 0}
    original_boundary = [key for key, owner in face_owners.items() if len(owner) == 1]
    if any(len(owners) > 2 for owners in face_owners.values()):
        raise ValueError('Nonmanifold tetrahedralization')
    for owners in face_owners.values():
        if len(owners) == 2:
            a, b = owners
            groups[a]['neighbours'].add(b)
            groups[b]['neighbours'].add(a)
    queue, attempted = [], set()

    def offer(a, b):
        a, b = sorted((a, b))
        ga, gb = groups[a], groups[b]
        heapq.heappush(queue, (-(ga['volume']+gb['volume']), a, b, ga['version'], gb['version']))

    for a, g in groups.items():
        for b in g['neighbours']:
            if b > a:
                offer(a, b)
    merges = tested = 0
    while queue:
        _, a, b, va, vb = heapq.heappop(queue)
        if a not in groups or b not in groups or groups[a]['version'] != va or groups[b]['version'] != vb:
            continue
        cache = (a, b, va, vb)
        if cache in attempted:
            continue
        attempted.add(cache)
        tested += 1
        ga, gb = groups[a], groups[b]
        ids = ga['vertices'] | gb['vertices']
        if len(ids) > max_vertices:
            continue
        boundary = ga['faces'].copy()
        for key, value in gb['faces'].items():
            if key in boundary:
                del boundary[key]
            else:
                boundary[key] = value
        values = list(boundary.values())
        normals = np.array([v[0] for v in values])
        offsets = np.array([v[1] for v in values])
        p = nodes[sorted(ids)]
        absorbed = {a, b}
        if (p @ normals.T+offsets).max() > epsilon:
            # A pair can be nonconvex although adding a third contained tetra
            # completes its convex envelope. Close over fully contained groups
            # and certify the envelope volume instead of requiring pair merges.
            hp, hi = numpy_hull(p)
            ht = hp[hi]
            hn = np.cross(ht[:, 1]-ht[:, 0], ht[:, 2]-ht[:, 0])
            hn /= np.linalg.norm(hn, axis=1)[:, None]
            ho = -np.einsum('ij,ij->i', hn, ht[:, 0])
            node_inside = (nodes @ hn.T+ho).max(axis=1) <= 1e-7
            absorbed = {gid for gid, g in groups.items() if node_inside[list(g['vertices'])].all()}
            if a not in absorbed or b not in absorbed:
                continue
            hull_volume = abs(float(np.einsum('ij,ij->i', ht[:, 0], np.cross(ht[:, 1], ht[:, 2])).sum()/6))
            enclosed_volume = sum(groups[g]['volume'] for g in absorbed)
            if abs(hull_volume-enclosed_volume) > max(1e-9, hull_volume*1e-8):
                continue
            ids = set().union(*(groups[g]['vertices'] for g in absorbed))
            if len(ids) > max_vertices:
                continue
            boundary = {}
            for g in absorbed:
                for key, value in groups[g]['faces'].items():
                    if key in boundary: del boundary[key]
                    else: boundary[key] = value
        volume = sum(groups[g]['volume'] for g in absorbed)
        neighbours = set().union(*(groups[g]['neighbours'] for g in absorbed))-absorbed
        groups[a] = {'faces': boundary, 'vertices': ids, 'volume': volume,
                     'neighbours': neighbours, 'version': va+1}
        for removed in absorbed-{a}: del groups[removed]
        for neighbour in neighbours:
            if neighbour in groups:
                groups[neighbour]['neighbours'].difference_update(absorbed)
                groups[neighbour]['neighbours'].add(a)
                offer(a, neighbour)
        merges += 1
    hulls = []
    for g in sorted(groups.values(), key=lambda g: -g['volume']):
        points, indices = numpy_hull(nodes[sorted(g['vertices'])])
        used = np.unique(indices)
        remap = np.full(len(points), -1, int)
        remap[used] = np.arange(len(used))
        points, indices = points[used], remap[indices]
        triangles = points[indices]
        volume = abs(float(np.einsum('ij,ij->i', triangles[:, 0], np.cross(triangles[:, 1], triangles[:, 2])).sum()/6))
        hulls.append({'vertex_data': points.tolist(), 'index_data': indices.reshape(-1).tolist(),
                      'transform': {'translation': [0, 0, 0], 'rotationQuat': [0, 0, 0, 1], 'scale': [1, 1, 1]},
                      'source_tetra_volume_cm3': g['volume'], 'hull_volume_cm3': volume,
                      'volume_excess_cm3': volume-g['volume'], 'boundary_triangle_count': len(g['faces'])})
    return {'convex_elems': hulls, 'convex_count': len(hulls), 'initial_tetrahedra': len(cells),
            'initial_boundary_faces': [list(f) for f in original_boundary],
            'merges': merges, 'candidate_tests': tested, 'support_epsilon_cm': epsilon,
            'maximum_vertices_per_hull': max(len(h['vertex_data']) for h in hulls),
            'total_source_volume_cm3': sum(h['source_tetra_volume_cm3'] for h in hulls),
            'total_hull_volume_cm3': sum(h['hull_volume_cm3'] for h in hulls),
            'maximum_absolute_hull_volume_excess_cm3': max(abs(h['volume_excess_cm3']) for h in hulls),
            'merge_seconds': time.perf_counter()-start}
