"""Read-only geometric audit of native food render triangles and convex collision.

Requires only NumPy. Report coordinates are Unreal asset-local centimetres.
Metrics are sampled evidence, not exhaustive geometry or gameplay validation.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import numpy as np


def transform_vertices(vertices, transform):
    p = np.asarray(vertices, dtype=float).reshape(-1, 3)
    t = transform or {}
    scale = np.asarray(t.get('scale', [1, 1, 1]), dtype=float)
    translation = np.asarray(t.get('translation', [0, 0, 0]), dtype=float)
    q = np.asarray(t.get('rotationQuat', [0, 0, 0, 1]), dtype=float)
    q /= max(np.linalg.norm(q), 1e-30)
    x, y, z, w = q
    rotation = np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
    return (p * scale) @ rotation.T + translation


def numpy_hull(points):
    """Small deterministic incremental 3D hull fallback for native vertex sets."""
    p = np.unique(np.asarray(points, dtype=float), axis=0)
    eps = max(float(np.ptp(p, axis=0).max()), 1) * 1e-9
    if len(p) < 4:
        raise ValueError('Convex hull has fewer than four unique vertices')
    a, b = int(np.argmin(p[:, 0])), int(np.argmax(p[:, 0]))
    if np.linalg.norm(p[b]-p[a]) < eps:
        axis = int(np.argmax(np.ptp(p, axis=0)))
        a, b = int(np.argmin(p[:, axis])), int(np.argmax(p[:, axis]))
    c = int(np.argmax(np.linalg.norm(np.cross(p-p[a], p[b]-p[a]), axis=1)))
    plane = np.cross(p[b]-p[a], p[c]-p[a])
    if np.linalg.norm(plane) < eps*eps:
        raise ValueError('Convex hull is collinear')
    d = int(np.argmax(abs((p-p[a]) @ (plane/np.linalg.norm(plane)))))
    if abs(np.dot(p[d]-p[a], plane/np.linalg.norm(plane))) < eps:
        raise ValueError('Convex hull is coplanar')
    interior = p[[a, b, c, d]].mean(axis=0)

    def face(i, j, k):
        n = np.cross(p[j]-p[i], p[k]-p[i])
        length = np.linalg.norm(n)
        if length < eps*eps:
            return None
        if np.dot(n, interior-p[i]) > 0:
            j, k = k, j
            n = -n
        n /= length
        return ([i, j, k], n, -np.dot(n, p[i]))

    faces = [face(a, b, c), face(a, d, b), face(b, d, c), face(c, d, a)]
    for _ in range(len(p)*2):
        normals = np.array([f[1] for f in faces])
        offsets = np.array([f[2] for f in faces])
        distances = p @ normals.T + offsets
        peak = np.unravel_index(np.argmax(distances), distances.shape)
        if distances[peak] <= eps:
            return p, np.array([f[0] for f in faces], dtype=int)
        point = int(peak[0])
        visible = np.flatnonzero(distances[point] > eps)
        edges = {}
        for idx in visible:
            tri = faces[idx][0]
            for i, j in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
                key = tuple(sorted((i, j)))
                edges.setdefault(key, []).append((i, j))
        visible_set = set(visible.tolist())
        faces = [f for idx, f in enumerate(faces) if idx not in visible_set]
        for occurrences in edges.values():
            if len(occurrences) == 1:
                new = face(*occurrences[0], point)
                if new is not None:
                    faces.append(new)
    raise ValueError('Incremental convex hull did not converge')


def planes_from_triangles(points, indices):
    tri = points[indices]
    normals = np.cross(tri[:, 1]-tri[:, 0], tri[:, 2]-tri[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    good = lengths > max(float(np.ptp(points, axis=0).max()), 1)**2 * 1e-12
    tri, normals, lengths = tri[good], normals[good], lengths[good]
    normals /= lengths[:, None]
    interior = points.mean(axis=0)
    reverse = np.einsum('ij,ij->i', normals, interior-tri[:, 0]) > 0
    normals[reverse] *= -1
    offsets = -np.einsum('ij,ij->i', normals, tri[:, 0])
    if not len(tri) or (points @ normals.T + offsets).max() > 1e-5:
        raise ValueError('Native index_data does not bound its convex vertices')
    return tri, normals, offsets


def sample_surface(triangles, count, rng):
    t = np.asarray(triangles, dtype=float)
    area = np.linalg.norm(np.cross(t[:, 1]-t[:, 0], t[:, 2]-t[:, 0]), axis=1)
    good = area > 1e-14
    t, area = t[good], area[good]
    if not len(t):
        return np.empty((0, 3))
    selected = t[rng.choice(len(t), count, p=area/area.sum())]
    u = np.sqrt(rng.random(count))
    v = rng.random(count)
    return selected[:, 0]*(1-u[:, None]) + selected[:, 1]*(u*(1-v))[:, None] + selected[:, 2]*(u*v)[:, None]


def sample_hull_surface_with_normals(hull, count, rng):
    t = hull['triangles']
    area = np.linalg.norm(np.cross(t[:, 1]-t[:, 0], t[:, 2]-t[:, 0]), axis=1)
    chosen = rng.choice(len(t), count, p=area/area.sum())
    selected = t[chosen]
    u = np.sqrt(rng.random(count))
    v = rng.random(count)
    positions = selected[:, 0]*(1-u[:, None]) + selected[:, 1]*(u*(1-v))[:, None] + selected[:, 2]*(u*v)[:, None]
    return positions, hull['normals'][chosen]


def triangle_distances(points, triangles):
    """Exact Euclidean nearest-triangle distance, in bounded NumPy batches."""
    p, t = np.asarray(points), np.asarray(triangles)
    best = np.full(len(p), np.inf)
    for start in range(0, len(p), 32):
        query = p[start:start+32, None, :]
        local = np.full(query.shape[0], np.inf)
        for offset in range(0, len(t), 256):
            batch = t[offset:offset+256]
            a, b, c = batch[:, 0], batch[:, 1], batch[:, 2]
            ab, ac = b-a, c-a
            ap = query-a
            normal = np.cross(ab, ac)
            denom_normal = np.einsum('ij,ij->i', normal, normal)
            safe_normal = np.maximum(denom_normal, 1e-30)
            dot_normal = np.einsum('ptj,tj->pt', ap, normal)
            plane_sq = dot_normal**2 / safe_normal
            d00 = np.einsum('ij,ij->i', ab, ab)
            d01 = np.einsum('ij,ij->i', ab, ac)
            d11 = np.einsum('ij,ij->i', ac, ac)
            d20 = np.einsum('ptj,tj->pt', ap, ab)
            d21 = np.einsum('ptj,tj->pt', ap, ac)
            denom = np.maximum(d00*d11-d01*d01, 1e-30)
            u = (d11*d20-d01*d21)/denom
            v = (d00*d21-d01*d20)/denom
            inside = (u >= 0) & (v >= 0) & (u+v <= 1) & (denom_normal > 1e-24)
            distance = np.where(inside, plane_sq, np.inf)
            for origin, end in ((a, b), (b, c), (c, a)):
                direction = end-origin
                delta = query-origin
                length_sq = np.maximum(np.einsum('ij,ij->i', direction, direction), 1e-30)
                fraction = np.clip(np.einsum('ptj,tj->pt', delta, direction)/length_sq, 0, 1)
                residual = delta-fraction[:, :, None]*direction
                distance = np.minimum(distance, np.einsum('ptj,ptj->pt', residual, residual))
            local = np.minimum(local, distance.min(axis=1))
        best[start:start+len(local)] = np.sqrt(local)
    return best


def inside_union(points, hulls, eps=1e-6):
    p = np.asarray(points)
    inside = np.zeros(len(p), dtype=bool)
    for hull in hulls:
        for start in range(0, len(p), 512):
            q = p[start:start+512]
            inside[start:start+len(q)] |= (q @ hull['normals'].T + hull['offsets']).max(axis=1) <= eps
    return inside


def topology_status(triangles, eps):
    vertices = np.asarray(triangles).reshape(-1, 3)
    _, mapped = np.unique(np.rint(vertices/eps).astype(np.int64), axis=0, return_inverse=True)
    faces = mapped.reshape(-1, 3)
    nondegenerate = (faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 0] != faces[:, 2])
    faces = faces[nondegenerate]
    edges = np.concatenate((faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]))
    _, counts = np.unique(np.sort(edges, axis=1), axis=0, return_counts=True)
    return {'watertight_after_position_weld': bool(np.all(counts == 2)),
            'boundary_edges': int((counts == 1).sum()), 'nonmanifold_edges': int((counts > 2).sum())}


def connected_triangle_components(triangles, eps):
    _, mapped = np.unique(np.rint(np.asarray(triangles).reshape(-1, 3)/eps).astype(np.int64),
                          axis=0, return_inverse=True)
    faces = mapped.reshape(-1, 3)
    parent = np.arange(int(mapped.max())+1)

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return int(i)

    for a, b, c in faces:
        root = find(int(a))
        for other in (b, c):
            parent[find(int(other))] = root
    groups = {}
    for index, f in enumerate(faces):
        groups.setdefault(find(int(f[0])), []).append(index)
    return [triangles[indices] for indices in groups.values()]


def inside_render_component_union(points, components, eps):
    # Disconnected closed parts can overlap (e.g. grape berries). Global parity
    # would take their symmetric difference and mislabel the overlap as empty.
    inside = np.zeros(len(points), dtype=bool)
    for triangles in components:
        lo, hi = triangles.reshape(-1, 3).min(axis=0), triangles.reshape(-1, 3).max(axis=0)
        candidates = (~inside) & np.all(points >= lo-eps, axis=1) & np.all(points <= hi+eps, axis=1)
        if candidates.any():
            inside[candidates] = inside_render_ray(points[candidates], triangles, eps)
    return inside


def inside_render_ray(points, triangles, eps):
    direction = np.array([.833731, .391927, .387119])
    direction /= np.linalg.norm(direction)
    counts = np.zeros(len(points), dtype=np.int64)
    for start in range(0, len(points), 64):
        p = points[start:start+64, None, :]
        hits = np.zeros(len(p), dtype=np.int64)
        for offset in range(0, len(triangles), 256):
            t = triangles[offset:offset+256]
            a, ab, ac = t[:, 0], t[:, 1]-t[:, 0], t[:, 2]-t[:, 0]
            h = np.cross(direction, ac)
            determinant = np.einsum('ij,ij->i', ab, h)
            valid = abs(determinant) > eps*eps
            inverse = np.divide(1, determinant, out=np.zeros_like(determinant), where=valid)
            delta = p-a
            u = np.einsum('ptj,tj->pt', delta, h)*inverse
            q = np.cross(delta, ab)
            v = np.einsum('j,ptj->pt', direction, q)*inverse
            length = np.einsum('tj,ptj->pt', ac, q)*inverse
            hit = valid & (u > -1e-10) & (v > -1e-10) & (u+v < 1+1e-10) & (length > eps)
            hits += hit.sum(axis=1)
        counts[start:start+len(p)] = hits
    return counts % 2 == 1


def stats(distances, points, tolerance):
    d = np.asarray(distances)
    if not len(d):
        return {'samples': 0}
    worst = np.argsort(d)[-5:][::-1]
    return {'samples': len(d), 'mean_cm': float(d.mean()),
        'p50_cm': float(np.quantile(d, .50)), 'p95_cm': float(np.quantile(d, .95)),
        'p99_cm': float(np.quantile(d, .99)), 'max_cm': float(d.max()),
        'fraction_above_tolerance': float((d > tolerance).mean()),
        'worst_samples': [{'point': points[i].tolist(), 'distance_cm': float(d[i])} for i in worst]}


def audit_mesh(mesh, args, seed):
    rng = np.random.default_rng(seed)
    sections = mesh.get('native_render_sections', [])
    render = []
    for section in sections:
        vertices = np.asarray(section['vertices'], dtype=float).reshape(-1, 3)
        indices = np.asarray(section['triangles'], dtype=int).reshape(-1, 3)
        render.append(vertices[indices])
    triangles = np.concatenate(render) if render else np.empty((0, 3, 3))
    if not len(triangles):
        return {'path': mesh.get('path'), 'status': 'no_render_triangles'}
    all_vertices = np.unique(triangles.reshape(-1, 3), axis=0)
    diagonal = float(np.linalg.norm(np.ptp(all_vertices, axis=0)))
    eps = max(diagonal, 1)*1e-7
    result = {'path': mesh.get('path'), 'status': 'measured',
        'render_triangles': len(triangles), 'unique_render_vertices': len(all_vertices),
        'bounds_diagonal_cm': diagonal, 'coordinate_space': 'asset-local centimetres; element transforms applied'}
    hulls, errors = [], []
    for i, element in enumerate(mesh.get('native_collision', {}).get('convex_elems', [])):
        try:
            points = transform_vertices(element['vertex_data'], element.get('transform'))
            indices = np.asarray(element.get('index_data', []), dtype=int)
            if len(indices) and len(indices) % 3 == 0 and indices.min() >= 0 and indices.max() < len(points):
                try:
                    tri, normals, offsets = planes_from_triangles(points, indices.reshape(-1, 3))
                    method = 'native indices'
                except ValueError:
                    points, indices = numpy_hull(points)
                    tri, normals, offsets = planes_from_triangles(points, indices)
                    method = 'NumPy convex hull; native indices invalid'
            else:
                points, indices = numpy_hull(points)
                tri, normals, offsets = planes_from_triangles(points, indices)
                method = 'NumPy convex hull; native indices absent'
            hulls.append({'triangles': tri, 'normals': normals, 'offsets': offsets, 'method': method})
        except Exception as error:
            errors.append({'element': i, 'error': str(error)})
    result['convex_count'] = len(hulls)
    result['convex_geometry_errors'] = errors
    if not hulls:
        result['status'] = 'no_valid_convex_hulls'
        return result
    result['hull_face_sources'] = {method: sum(h['method'] == method for h in hulls) for method in sorted({h['method'] for h in hulls})}
    hull_triangles = np.concatenate([h['triangles'] for h in hulls])
    sampled = sample_surface(triangles, args.surface_samples, rng)
    if len(all_vertices) > args.vertex_samples:
        vertices = all_vertices[rng.choice(len(all_vertices), args.vertex_samples, replace=False)]
    else:
        vertices = all_vertices
    extreme = all_vertices[np.concatenate((all_vertices.argmin(axis=0), all_vertices.argmax(axis=0)))]
    surface = np.concatenate((sampled, vertices, extreme))
    covered = inside_union(surface, hulls, eps)
    outside = surface[~covered]
    distances = triangle_distances(outside, hull_triangles)
    full_distances = np.zeros(len(surface))
    full_distances[~covered] = distances
    result['render_surface_not_covered'] = {'outside_fraction': float((~covered).mean()),
        **stats(full_distances, surface, args.tolerance_cm)}
    # Only externally exposed hull-face samples represent collision contact.
    areas = np.array([np.linalg.norm(np.cross(h['triangles'][:, 1]-h['triangles'][:, 0], h['triangles'][:, 2]-h['triangles'][:, 0]), axis=1).sum() for h in hulls])
    hull_surface = []
    for i, hull in enumerate(hulls):
        count = max(12, int(args.hull_samples*areas[i]/areas.sum()))
        p, normals = sample_hull_surface_with_normals(hull, count, rng)
        others = hulls[:i]+hulls[i+1:]
        if others:
            # A touching neighbour can hide an internal face without overlap.
            # Test a tiny step outward so shared coplanar interfaces are excluded
            # but coincident external hull faces remain exposed.
            p = p[~inside_union(p+normals*(eps*4), others, eps)]
        hull_surface.append(p)
    hull_surface = np.concatenate(hull_surface)
    contact = triangle_distances(hull_surface, triangles)
    result['convex_contact_distance_to_render_surface'] = stats(contact, hull_surface, args.tolerance_cm)
    result['convex_contact_surface_method'] = 'Area-weighted hull samples; exclude points whose outward 4*epsilon step enters another hull (handles touching and overlapping union interfaces).'
    result['normalized_max_contact_error'] = float(contact.max()/max(diagonal, 1e-30)) if len(contact) else None
    row_scales = []
    for use in mesh.get('table_uses', []):
        value = use.get('scale', [1, 1, 1])
        scale = np.array([value.get(k, value.get(k.lower(), 1)) for k in ('X', 'Y', 'Z')], dtype=float) if isinstance(value, dict) else np.asarray(value, dtype=float)
        row_scales.append({'row': use.get('row'), 'fragment': use.get('fragment'),
            'item_scale': scale.tolist(), 'maximum_absolute_component': float(abs(scale).max())})
    maximum_item_scale = max((s['maximum_absolute_component'] for s in row_scales), default=1)
    multiplier = maximum_item_scale*args.actor_scale
    result['game_scale_upper_bounds'] = {'actor_scale': args.actor_scale, 'table_uses': row_scales,
        'maximum_item_scale': maximum_item_scale, 'upper_bound_multiplier': multiplier,
        'max_convex_contact_error_cm': float(contact.max()*multiplier) if len(contact) else None,
        'p95_convex_contact_error_cm': float(np.quantile(contact, .95)*multiplier) if len(contact) else None,
        'max_uncovered_render_gap_cm': float(full_distances.max()*multiplier),
        'note': 'Conservative distance upper bounds using max absolute nonuniform scale; not an exact runtime contact/cooking margin measurement.'}
    topology = topology_status(triangles, eps)
    result['render_topology'] = topology
    if not topology['watertight_after_position_weld']:
        result['cavity_audit'] = {'status': 'unsupported_open_or_nonmanifold_render',
            'note': 'Unsigned contact distance remains valid; no reliable solid/cavity classification.'}
    elif args.grid > 0:
        lower, upper = all_vertices.min(axis=0), all_vertices.max(axis=0)
        axes = [np.linspace(0, 1, args.grid, endpoint=False)+.371/args.grid for _ in range(3)]
        grid = np.stack(np.meshgrid(*axes, indexing='ij'), axis=-1).reshape(-1, 3)
        grid = lower+grid*(upper-lower)
        union = inside_union(grid, hulls, eps)
        candidates = grid[union]
        render_components = connected_triangle_components(triangles, eps)
        render_inside = inside_render_component_union(candidates, render_components, eps)
        extra = candidates[~render_inside]
        depth = triangle_distances(extra, triangles)
        meaningful = depth > args.tolerance_cm
        result['cavity_audit'] = {'status': 'sampled_watertight_ray_parity', 'grid_points': len(grid),
            'render_connected_components': len(render_components),
            'render_solid_method': 'Union of separately ray-parity-classified closed connected components; overlapping disconnected parts are solid.',
            'grid_points_in_union': len(candidates), 'grid_points_outside_render_but_in_union': len(extra),
            'filled_void_fraction_of_bbox_grid': float(meaningful.sum()/len(grid)),
            'deepest_void_samples': stats(depth, extra, args.tolerance_cm)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--surface-samples', type=int, default=1600)
    parser.add_argument('--vertex-samples', type=int, default=800)
    parser.add_argument('--hull-samples', type=int, default=1800)
    parser.add_argument('--grid', type=int, default=13)
    parser.add_argument('--tolerance-cm', type=float, default=.5)
    parser.add_argument('--actor-scale', type=float, default=20, help='Uniform actor scale for conservative game-space error bounds')
    parser.add_argument('--mesh', help='Substring filter on native asset path')
    args = parser.parse_args()
    source = json.loads(args.report.read_text(encoding='utf-8-sig'))
    meshes = source.get('meshes', []) if isinstance(source, dict) else source
    if args.mesh:
        meshes = [m for m in meshes if args.mesh.lower() in m.get('path', '').lower()]
    results = []
    for i, mesh in enumerate(meshes):
        try:
            result = audit_mesh(mesh, args, 1009+i)
        except Exception as error:
            result = {'path': mesh.get('path'), 'status': 'audit_error', 'error': str(error)}
        results.append(result)
        print(json.dumps({'path': result['path'], 'status': result['status'],
            'convex_count': result.get('convex_count'),
            'max_contact_cm': result.get('convex_contact_distance_to_render_surface', {}).get('max_cm')}, ensure_ascii=False), flush=True)
    output = args.output or args.report.with_name(args.report.stem+'_GeometryValidation.json')
    document = {'source_report': str(args.report.resolve()), 'implementation': 'NumPy only; native hull indices or deterministic incremental hull',
        'parameters': {k: v for k, v in vars(args).items() if k not in ('report', 'output')},
        'limitations': ['Area-weighted/vertex/grid samples are evidence, not exhaustive collision proof.',
            'Cavities are classified only for watertight position-welded render meshes.',
            'Game ItemScale/physics margins and runtime cooking need separate engine validation.'],
        'meshes': results}
    output.write_text(json.dumps(document, indent=2, ensure_ascii=False), encoding='utf-8')
    print(str(output.resolve()))
    return int(any(m['status'] == 'audit_error' or m.get('convex_geometry_errors') for m in results))


if __name__ == '__main__':
    raise SystemExit(main())
