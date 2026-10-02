"""Read-only exact connected-component convexity plan from native render data.

Only closed components whose every nondegenerate triangle is a supporting face
are accepted. This deliberately does not fill holes or collapse concave features.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import numpy as np
from food_collision_geometry import numpy_hull, triangle_distances, sample_surface


def analyze(mesh, hull_audit=False):
    all_vertices, all_faces = [], []
    offset = 0
    for section in mesh.get('native_render_sections', []):
        v = np.asarray(section['vertices'], dtype=float).reshape(-1, 3)
        f = np.asarray(section['triangles'], dtype=int).reshape(-1, 3)
        all_vertices.append(v)
        all_faces.append(f + offset)
        offset += len(v)
    vertices = np.concatenate(all_vertices)
    faces = np.concatenate(all_faces)
    diagonal = float(np.linalg.norm(np.ptp(vertices, axis=0)))
    eps = max(diagonal, 1.) * 1e-7
    # LOD split normals/UV seams duplicate positions. The quantized position weld
    # retains an actual native vertex, rather than producing averaged geometry.
    _, first, remap = np.unique(np.rint(vertices/eps).astype(np.int64),
                               axis=0, return_index=True, return_inverse=True)
    welded = vertices[first]
    max_weld_displacement = float(np.linalg.norm(vertices-welded[remap], axis=1).max())
    faces = remap[faces]
    tri = welded[faces]
    cross = np.cross(tri[:, 1]-tri[:, 0], tri[:, 2]-tri[:, 0])
    area2 = np.linalg.norm(cross, axis=1)
    good = area2 > max(diagonal, 1.)**2*1e-12
    removed = int((~good).sum())
    faces = faces[good]
    parent = np.arange(len(welded))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return int(i)

    def union(a, b):
        a, b = find(a), find(b)
        if a != b:
            parent[b] = a

    for a, b, c in faces:
        union(int(a), int(b))
        union(int(a), int(c))
    groups = {}
    for index, face in enumerate(faces):
        groups.setdefault(find(int(face[0])), []).append(index)
    components = []
    for indices in sorted(groups.values(), key=lambda g: -len(g)):
        source_faces = faces[indices]
        used = np.unique(source_faces)
        points = welded[used]
        lookup = np.full(len(welded), -1, dtype=int)
        lookup[used] = np.arange(len(used))
        local_faces = lookup[source_faces]
        edges = np.concatenate((local_faces[:, [0, 1]], local_faces[:, [1, 2]],
                                local_faces[:, [2, 0]]))
        _, counts = np.unique(np.sort(edges, axis=1), axis=0, return_counts=True)
        boundary = int((counts == 1).sum())
        nonmanifold = int((counts > 2).sum())
        triangles = points[local_faces]
        normals = np.cross(triangles[:, 1]-triangles[:, 0], triangles[:, 2]-triangles[:, 0])
        normals /= np.linalg.norm(normals, axis=1)[:, None]
        centroid = points.mean(axis=0)
        reverse = np.einsum('ij,ij->i', normals, centroid-triangles[:, 0]) > 0
        normals[reverse] *= -1
        oriented = local_faces.copy()
        oriented[reverse] = oriented[reverse][:, [0, 2, 1]]
        offsets = -np.einsum('ij,ij->i', normals, triangles[:, 0])
        plane_peaks = np.empty(len(normals))
        for start in range(0, len(normals), 256):
            plane_peaks[start:start+256] = (points @ normals[start:start+256].T
                                          + offsets[start:start+256]).max(axis=0)
        violations = int((plane_peaks > eps).sum())
        max_violation = float(plane_peaks.max())
        singular = np.linalg.svd(points-centroid, compute_uv=False)
        full_dimension = len(singular) >= 3 and singular[2] > eps
        closed = boundary == 0 and nonmanifold == 0
        supports = violations == 0
        accepted = closed and supports and full_dimension
        signed_volume = abs(float(np.einsum('ij,ij->i', triangles[:, 0],
                                np.cross(triangles[:, 1], triangles[:, 2])).sum()/6))
        component = {
            'component': len(components), 'vertex_count': len(points),
            'triangle_count': len(local_faces), 'bounds_min': points.min(axis=0).tolist(),
            'bounds_max': points.max(axis=0).tolist(),
            'boundary_edges': boundary, 'nonmanifold_edges': nonmanifold,
            'closed': closed, 'full_dimension': bool(full_dimension),
            'all_triangles_support_all_vertices': supports,
            'non_supporting_triangles': violations,
            'maximum_support_plane_violation_cm': max_violation,
            'absolute_oriented_render_volume_cm3': signed_volume,
            'exact_convex_candidate': accepted,
            'near_exact_closed_convex_candidate_at_0_0002cm': closed and full_dimension and max_violation <= .0002,
            'vertex_data': points.tolist(), 'index_data': oriented.reshape(-1).tolist(),
            'transform': {'translation': [0, 0, 0], 'rotationQuat': [0, 0, 0, 1], 'scale': [1, 1, 1]},
        }
        if hull_audit and full_dimension:
            try:
                hull_points, hull_indices = numpy_hull(points)
                hull_tri = hull_points[hull_indices]
                rng = np.random.default_rng(9401+len(components))
                hull_samples = sample_surface(hull_tri, 1600, rng)
                render_samples = sample_surface(triangles, 1600, rng)
                vertex_to_hull = triangle_distances(points, hull_tri)
                render_to_hull = triangle_distances(render_samples, hull_tri)
                hull_to_render = triangle_distances(hull_samples, triangles)
                component['all_source_vertex_hull_audit'] = {
                    'method': 'convex hull of all native component positions; exact all-source-vertex distances plus 1600 area-weighted samples on each surface',
                    'vertex_count': len(hull_points), 'hull_triangle_count': len(hull_tri),
                    'max_all_source_vertex_distance_to_hull_cm': float(vertex_to_hull.max()),
                    'max_sampled_render_surface_distance_to_hull_cm': float(render_to_hull.max()),
                    'max_sampled_hull_contact_distance_to_render_cm': float(hull_to_render.max()),
                    'p95_sampled_hull_contact_distance_to_render_cm': float(np.quantile(hull_to_render, .95)),
                    'convex_vertex_data': hull_points.tolist(),
                    'convex_index_data': hull_indices.reshape(-1).tolist(),
                }
            except Exception as error:
                component['all_source_vertex_hull_audit'] = {'error': str(error)}
        components.append(component)
    qualified = bool(components) and all(c['exact_convex_candidate'] for c in components)
    return {'path': mesh['path'], 'table_uses': mesh.get('table_uses', []),
            'render_vertices': len(vertices), 'position_welded_vertices': len(welded),
            'position_weld_epsilon_cm': eps, 'maximum_weld_displacement_cm': max_weld_displacement,
            'degenerate_triangles_removed': removed, 'component_count': len(components),
            'fully_exact_union_of_closed_convex_components': qualified,
            'exact_component_count': sum(c['exact_convex_candidate'] for c in components),
            'components': components}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--hull-audit', action='store_true')
    parser.add_argument('--mesh', action='append', default=[])
    args = parser.parse_args()
    source = json.loads(args.input.read_text(encoding='utf-8-sig'))
    result = {'source_report': str(args.input.resolve()),
              'coordinate_space': 'native asset-local; no actor or DataTable scale applied',
              'method': 'position-welded connected triangles; watertight edge count; every triangle supporting all component vertices',
              'meshes': []}
    for mesh in source['meshes']:
        if args.mesh and not any(name.lower() in mesh['path'].lower() for name in args.mesh):
            continue
        entry = analyze(mesh, args.hull_audit)
        result['meshes'].append(entry)
        bad = [f"c{c['component']}:open={c['boundary_edges']},nonmanifold={c['nonmanifold_edges']},supportGap={c['maximum_support_plane_violation_cm']:.6g}" for c in entry['components'] if not c['exact_convex_candidate']]
        print(f"{mesh['path'].split('.')[-1]} exact={entry['fully_exact_union_of_closed_convex_components']} components={entry['component_count']} accepted={entry['exact_component_count']} " + '; '.join(bad[:6]), flush=True)
    result['complete'] = True
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f"Saved {args.output}, exact meshes={sum(m['fully_exact_union_of_closed_convex_components'] for m in result['meshes'])}/{len(result['meshes'])}", flush=True)


if __name__ == '__main__':
    main()
