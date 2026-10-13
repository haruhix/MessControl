"""Read the saved warrior FBX and bake a small bone-local shield crack layout.

No Blender session, source FBX, Unreal package or artist material is modified.
The generated native points survive cooked meshes with stripped CPU vertices.
"""
import hashlib
import json
import struct
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutTank/SK_NutTank.fbx'
HEADER = ROOT / 'Source/MessControl/Private/MCNutShieldCrackLayout.h'
REPORT = ROOT / 'Saved/Checks/NutFightRedesign_20261013/ShieldCrackLayout.json'


def read_fbx(path):
    data = path.read_bytes()
    assert data.startswith(b'Kaydara FBX Binary  \0\x1a\0')
    version = struct.unpack_from('<I', data, 23)[0]
    wide = version >= 7500
    fmt, header_size = ('<QQQB', 25) if wide else ('<IIIB', 13)

    def node(offset):
        end, count, length, name_len = struct.unpack_from(fmt, data, offset)
        if end == 0:
            return None, offset + header_size
        offset += header_size
        name, offset = data[offset:offset + name_len], offset + name_len
        props = []
        scalar = {'Y': 'h', 'C': '?', 'I': 'i', 'F': 'f', 'D': 'd', 'L': 'q'}
        arrays = {'f': '<f4', 'd': '<f8', 'l': '<i8', 'i': '<i4', 'b': 'u1', 'c': 'u1'}
        for _ in range(count):
            kind, offset = chr(data[offset]), offset + 1
            if kind in scalar:
                prop_fmt = '<' + scalar[kind]
                props.append(struct.unpack_from(prop_fmt, data, offset)[0])
                offset += struct.calcsize(prop_fmt)
            elif kind in ('S', 'R'):
                size = struct.unpack_from('<I', data, offset)[0]
                offset += 4
                props.append(data[offset:offset + size])
                offset += size
            elif kind in arrays:
                size, compressed, stored = struct.unpack_from('<III', data, offset)
                offset += 12
                payload = data[offset:offset + stored]
                offset += stored
                if compressed:
                    payload = zlib.decompress(payload)
                props.append(np.frombuffer(payload, dtype=arrays[kind], count=size))
            else:
                raise ValueError('Unsupported FBX property ' + kind)
        children = []
        while offset < end:
            child, offset = node(offset)
            if child is not None:
                children.append(child)
        assert offset == end
        return {'name': name, 'props': props, 'children': children}, end

    roots, offset = [], 27
    while offset + header_size < len(data):
        item, offset = node(offset)
        if item is None:
            break
        roots.append(item)
    return roots, version


def main():
    source_sha = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    roots, version = read_fbx(SOURCE)
    objects = next(item['children'] for item in roots if item['name'] == b'Objects')
    connections = next(item['children'] for item in roots if item['name'] == b'Connections')
    models = {item['props'][0]: item for item in objects if item['name'] == b'Model'}

    def name(item):
        return item['props'][1].split(b'\0', 1)[0].split(b'::')[-1].decode()

    shield = next(key for key, item in models.items() if name(item) == 'SM_Sheld')
    hand = next(key for key, item in models.items() if name(item) == 'hand.l')
    geometry_id = next(item['props'][1] for item in connections
                       if item['props'][0] == b'OO' and item['props'][2] == shield)
    geometry = next(item for item in objects if item['props'] and item['props'][0] == geometry_id)
    bind = {}
    for pose in (item for item in objects if item['name'] == b'Pose'):
        for entry in (item for item in pose['children'] if item['name'] == b'PoseNode'):
            children = {item['name']: item['props'][0] for item in entry['children']}
            bind[children[b'Node']] = np.array(children[b'Matrix']).reshape((4, 4)).T
    vertices = next(item['props'][0] for item in geometry['children'] if item['name'] == b'Vertices').reshape((-1, 3))
    indices = next(item['props'][0] for item in geometry['children'] if item['name'] == b'PolygonVertexIndex')
    polygons, pending = [], []
    for value in indices:
        pending.append(int(value if value >= 0 else -value - 1))
        if value < 0:
            assert len(pending) == 3
            polygons.append(pending)
            pending = []
    points = (np.linalg.inv(bind[hand]) @ bind[shield]
              @ np.column_stack((vertices, np.ones(len(vertices)))).T).T[:, :3]
    # FFbxDataConverter::ConvertPos mirrors Y in the imported bone-local basis.
    points[:, 1] *= -1
    center = points.mean(axis=0)
    _, axes = np.linalg.eigh(np.cov(points.T))
    normal = axes[:, 0]
    if np.dot(center, normal) < 0:
        normal *= -1
    side = axes[:, 2]
    up = np.cross(normal, side)
    projected = np.column_stack(((points - center) @ side, (points - center) @ up))
    extent = np.ptp(projected, axis=0) * .38

    # Precompute the saved triangles in the projection plane. Ray intersection
    # then uses the same barycentric solve for authoring and clearance queries.
    triangles = points[np.array(polygons)]
    triangle_xy = np.stack(((triangles - center) @ side, (triangles - center) @ up), axis=-1)
    triangle_depth = (triangles - center) @ normal
    basis = np.stack((triangle_xy[:, 1] - triangle_xy[:, 0], triangle_xy[:, 2] - triangle_xy[:, 0]), axis=-1)
    valid = np.abs(np.linalg.det(basis)) > 1e-10
    triangles, triangle_xy, triangle_depth, basis = (array[valid] for array in (triangles, triangle_xy, triangle_depth, basis))
    inverse = np.linalg.inv(basis)
    slopes = np.einsum('ti,tij->tj', triangle_depth[:, 1:] - triangle_depth[:, :1], inverse)
    planes = np.column_stack((slopes, triangle_depth[:, 0] - np.sum(slopes * triangle_xy[:, 0], axis=1)))
    face_normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    face_normals /= np.linalg.norm(face_normals, axis=1)[:, None]
    face_normals[(face_normals @ normal) < 0] *= -1

    def surface_batch(query):
        query = np.asarray(query).reshape((-1, 2))
        delta = query[:, None, :] - triangle_xy[None, :, 0, :]
        uv = np.einsum('tij,qtj->qti', inverse, delta)
        inside = np.all(uv >= -1e-6, axis=-1) & (np.sum(uv, axis=-1) <= 1 + 1e-6)
        depths = np.einsum('qti,ti->qt', uv, triangle_depth[:, 1:] - triangle_depth[:, :1]) + triangle_depth[None, :, 0]
        depths[~inside] = -np.inf
        selected = np.argmax(depths, axis=1)
        depth = depths[np.arange(len(query)), selected]
        if not np.isfinite(depth).all():
            raise RuntimeError('Crack sample misses the saved shield surface')
        hits = center + query[:, :1] * side + query[:, 1:] * up + depth[:, None] * normal
        return hits, face_normals[selected]

    def surface(x, y, offset=.40):
        hits, normals = surface_batch([(x, y)])
        # Every ribbon column is projected AFTER curve/noise construction. A
        # constant-normal chord through this convex shell can sink into it.
        return hits[0] + normal * offset, normals[0]

    def cross2(a, b):
        return a[0] * b[1] - a[1] * b[0]

    def convex_hull(values):
        ordered = sorted(set(tuple(item) for item in values))
        def half(sequence):
            output = []
            for item in sequence:
                while len(output) >= 2 and cross2(np.subtract(output[-1], output[-2]), np.subtract(item, output[-1])) <= 0:
                    output.pop()
                output.append(item)
            return output
        return np.array(half(ordered)[:-1] + half(ordered[::-1])[:-1])

    hull = convex_hull(projected)
    def rim_point(angle):
        direction = np.array((np.cos(angle), np.sin(angle)))
        exits = []
        for a, b in zip(hull, np.roll(hull, -1, axis=0)):
            edge = b - a
            determinant = cross2(direction, -edge)
            if abs(determinant) < 1e-10:
                continue
            radius = cross2(a, -edge) / determinant
            fraction = cross2(direction, a) / determinant
            if radius > 0 and -1e-8 <= fraction <= 1 + 1e-8:
                exits.append(radius)
        if not exits:
            raise RuntimeError('Shield projection center is outside the hull')
        # Follow the actual authored silhouette, just inside its edge. Uneven
        # radius is smooth around the loop, not a random per-row buzz.
        irregularity = 1 + .012 * np.sin(angle * 3 + .43) + .008 * np.sin(angle * 7 + 1.17)
        return direction * min(exits) * .90 * irregularity

    def curve(controls, seed):
        controls = np.array(controls, dtype=float) * extent
        rows = []
        rng = np.random.default_rng(seed)
        for index in range(len(controls) - 1):
            a = controls[max(0, index - 1)]
            b, c = controls[index:index + 2]
            d = controls[min(len(controls) - 1, index + 2)]
            count = max(3, int(np.ceil(np.linalg.norm(c - b) / .85)))
            for step in range(count):
                t = step / count
                point = .5 * ((2 * b) + (-a + c) * t
                              + (2 * a - 5 * b + 4 * c - d) * t * t
                              + (-a + 3 * b - 3 * c + d) * t * t * t)
                tangent = c - b
                across = np.array((-tangent[1], tangent[0])) / np.linalg.norm(tangent)
                # Small uneven kinks, with control junctions kept exactly joined.
                point += across * rng.uniform(-.32, .32) * np.sin(np.pi * t)
                rows.append(point)
        rows.append(controls[-1])
        return np.array(rows)

    paths = []
    def add_path(controls, reveal, full, width, seed):
        path = dict(xy=curve(controls, seed), reveal=reveal, full=full,
                    width=width, seed=seed, rim=False)
        paths.append(path)
        return path

    # Unequal splits propagate from three offset impact sites. Later fine forks
    # attach to existing splits, rather than radiating from one uniform hub.
    upper = add_path([(-.17, .14), (-.32, .33), (-.18, .49), (-.24, .67), (-.15, .84)], .025, .68, 1.0, 117)
    lower = add_path([(-.17, .14), (-.08, -.04), (-.21, -.20), (-.10, -.39), (.08, -.49), (.16, -.72)], .11, .90, .95, 431)
    right = add_path([(.33, .23), (.21, .33), (.07, .31), (-.05, .20), (-.17, .14)], .30, .76, .78, 733)
    left = add_path([(-.39, -.24), (-.29, -.16), (-.21, -.20)], .48, .88, .65, 953)

    def fork(parent, fraction, controls, reveal, full, width, seed):
        parent_row = round(fraction * (len(parent['xy']) - 1))
        origin = parent['xy'][parent_row] / extent
        child = add_path([origin.tolist()] + controls, reveal, full, width, seed)
        child['attachment'] = (next(index for index, item in enumerate(paths) if item is parent), parent_row)

    fork(upper, .30, [(-.44, .35), (-.62, .29)], .28, .66, .37, 1207)
    fork(upper, .70, [(-.02, .58), (.12, .71)], .49, .84, .29, 1291)
    fork(upper, .48, [(-.31, .50), (-.43, .65)], .38, .77, .26, 1427)
    fork(lower, .23, [(-.30, -.06), (-.54, -.10)], .31, .73, .40, 1511)
    fork(lower, .61, [(.16, -.35), (.40, -.31), (.51, -.48)], .60, .94, .43, 1607)
    fork(lower, .80, [(-.20, -.56), (-.42, -.67)], .75, .98, .30, 1747)
    fork(right, .25, [(.46, .42), (.64, .53)], .43, .86, .37, 1879)
    fork(right, .54, [(.33, .17), (.52, .03)], .57, .94, .31, 1997)
    fork(right, .78, [(.04, .08), (.12, -.05)], .67, .96, .22, 2131)
    fork(left, .12, [(-.57, -.31), (-.73, -.43)], .58, .94, .30, 2293)
    # Faint target contour follows the saved shield, rather than an ideal ellipse.
    angles = np.linspace(0, 2 * np.pi, 257)
    paths.append(dict(xy=np.array([rim_point(angle) for angle in angles]),
                      reveal=-1, full=-1, width=.15, seed=2423, rim=True))

    samples, path_descriptors, path_report = [], [], []
    for path in paths:
        xy = path['xy']
        first = len(samples)
        rng = np.random.default_rng(path['seed'])
        widths, lip_columns = [], []
        for row, point in enumerate(xy):
            tangent = (xy[(row + 1) % (len(xy) - 1)] - xy[(row - 1) % (len(xy) - 1)] if path['rim']
                       else xy[min(row + 1, len(xy) - 1)] - xy[max(row - 1, 0)])
            tangent /= np.linalg.norm(tangent)
            across = np.array((-tangent[1], tangent[0]))
            fraction = row / (len(xy) - 1)
            taper = 1 if path['rim'] else max(.025, (1 - fraction) ** .62)
            phase = path['seed'] * .013
            variation = (1 + .085 * np.sin(fraction * 2 * np.pi * 5 + phase)
                         + .045 * np.sin(fraction * 2 * np.pi * 9 - phase)) if path['rim'] else rng.uniform(.76, 1.18)
            width = path['width'] * taper * variation
            widths.append(float(width))
            # Independently uneven lips keep a narrow dark seam, while breaking
            # the regular parallel-band appearance. Center/fork anchors stay put.
            columns = (-1, -.72, 0, .67, 1) if path['rim'] else (
                -1 - .06 * np.sin(row * 1.81 + phase) - .025 * np.sin(row * .41 - phase),
                -.72 + .075 * np.sin(row * 2.13 + phase), 0,
                .67 + .080 * np.sin(row * 1.67 - phase),
                1 + .055 * np.sin(row * 1.39 - phase) + .025 * np.sin(row * .53 + phase))
            lip_columns.append([float(value) for value in columns])
            positions, normals = [], []
            for column in columns:
                hit, face_normal = surface(*(point + across * width * column), offset=.65 if path['rim'] else .40)
                positions.append(hit.tolist())
                normals.append(face_normal.tolist())
            samples.append(dict(points=positions, normals=normals))
        path_descriptors.append((first, len(xy), path['reveal'], path['full'], path['rim']))
        path_report.append(dict(first=first, count=len(xy), reveal=path['reveal'], full=path['full'],
                                rim=path['rim'], seed=path['seed'], widths=widths,
                                lip_columns=lip_columns,
                                attachment=path.get('attachment'),
                                xy=(xy / extent).tolist()))

    def vector(values):
        return 'FVector(' + ','.join(f'{value:.6f}' for value in values) + ')'

    if len(samples) > 1000:
        raise RuntimeError('Shield crack sample budget exceeded')

    # Verify the geometry that the header actually stores, including rounded
    # coordinates. Clipping every ribbon triangle against the projected artist
    # triangles checks all piecewise-planar surface crossings, not just row ends.
    baked = np.round(np.array([sample['points'] for sample in samples]), 6)
    attachments = []
    for index, path in enumerate(paths):
        if 'attachment' not in path:
            continue
        parent_index, parent_row = path['attachment']
        parent = paths[parent_index]
        first = path_descriptors[index][0]
        parent_first = path_descriptors[parent_index][0]
        error = float(np.linalg.norm(baked[first, 2] - baked[parent_first + parent_row, 2]))
        reached_at = parent['reveal'] + (parent['full'] - parent['reveal']) * parent_row / (len(parent['xy']) - 1)
        margin = float(path['reveal'] - reached_at)
        if error > 1e-5 or margin < -1e-6:
            raise RuntimeError('Shield fork is detached or reveals before its parent split')
        attachments.append(dict(path=index, parent_path=parent_index, parent_row=parent_row,
                                center_error_cm=error, reveal_margin=margin))
    shell_min, shell_max = triangle_xy.min(axis=1), triangle_xy.max(axis=1)
    clip_triangles = []
    for triangle in triangle_xy:
        ordered = triangle if cross2(triangle[1] - triangle[0], triangle[2] - triangle[0]) > 0 else triangle[[0, 2, 1]]
        clip_triangles.append([tuple(point) for point in ordered])

    def clip_polygon(subject, boundary):
        output = subject
        for a, b in zip(boundary, boundary[1:] + boundary[:1]):
            if not output:
                break
            edge = (b[0] - a[0], b[1] - a[1])
            previous, clipped = output[-1], []
            before = cross2(edge, (previous[0] - a[0], previous[1] - a[1]))
            for current in output:
                after = cross2(edge, (current[0] - a[0], current[1] - a[1]))
                if (before >= -1e-9) != (after >= -1e-9):
                    fraction = before / (before - after)
                    clipped.append((previous[0] + (current[0] - previous[0]) * fraction,
                                    previous[1] + (current[1] - previous[1]) * fraction))
                if after >= -1e-9:
                    clipped.append(current)
                previous, before = current, after
            output = clipped
        return output

    def validate(stage, ribbons):
        query_points, ribbon_triangles = [], []
        for ribbon in ribbons:
            query_points.extend(ribbon.reshape((-1, 3)))
            for row in range(len(ribbon) - 1):
                for column in range(4):
                    a, b = ribbon[row, column:column + 2]
                    c, d = ribbon[row + 1, column:column + 2]
                    ribbon_triangles.extend((np.array((a, b, d)), np.array((a, d, c))))
        query_points = np.array(query_points)
        query_xy = np.column_stack(((query_points - center) @ side, (query_points - center) @ up))
        hits = np.concatenate([surface_batch(query_xy[start:start + 256])[0]
                               for start in range(0, len(query_xy), 256)])
        sample_clearance = float(np.min((query_points - hits) @ normal))
        minimum, intersections, degenerate, worst = sample_clearance, 0, 0, None
        for index, triangle in enumerate(ribbon_triangles):
            xy = np.column_stack(((triangle - center) @ side, (triangle - center) @ up))
            depth = (triangle - center) @ normal
            edges = np.column_stack((xy[1] - xy[0], xy[2] - xy[0]))
            if abs(np.linalg.det(edges)) < 1e-9:
                degenerate += 1  # Deliberately tapered native tips.
                continue
            slope = (depth[1:] - depth[0]) @ np.linalg.inv(edges)
            constant = depth[0] - slope @ xy[0]
            candidates = np.flatnonzero(np.all(shell_max >= xy.min(axis=0) - 1e-8, axis=1)
                                       & np.all(shell_min <= xy.max(axis=0) + 1e-8, axis=1))
            subject = [tuple(point) for point in xy]
            for candidate in candidates:
                overlap = clip_polygon(subject, clip_triangles[candidate])
                if not overlap:
                    continue
                intersections += 1
                difference = slope - planes[candidate, :2]
                bias = constant - planes[candidate, 2]
                clearance = min(difference[0] * point[0] + difference[1] * point[1] + bias for point in overlap)
                if clearance < minimum:
                    minimum, worst = float(clearance), dict(ribbon_triangle=index, shell_triangle=int(candidate))
        result = dict(stage=stage, vertices=len(query_points), triangles=len(ribbon_triangles),
                      tip_degenerate_triangles=degenerate, projected_intersections=intersections,
                      min_vertex_clearance_cm=sample_clearance, min_entire_triangle_clearance_cm=minimum,
                      worst=worst, passed=minimum >= -1e-5)
        if not result['passed']:
            raise RuntimeError('Shield ribbon enters saved shell: ' + json.dumps(result))
        return result

    checks = [validate('all_baked_rows', [baked[first:first + count] for first, count, *_ in path_descriptors])]
    for damage in (0, .25, .50, .75, .99):
        ribbons = []
        for first, count, reveal, full, rim in path_descriptors:
            growth = 1 if rim else np.clip((damage - reveal) / (full - reveal), 0, 1)
            last = growth * (count - 1)
            row_count = min(count, int(np.ceil(last)) + 1)
            if growth <= 0 or row_count < 2:
                continue
            rows = []
            for row in range(row_count):
                at = min(float(row), last)
                a, b = min(int(np.floor(at)), count - 1), min(int(np.floor(at)) + 1, count - 1)
                points_at = baked[first + a] * (1 - (at - a)) + baked[first + b] * (at - a)
                tip = 1 if rim else np.clip((last - at) / 2.5, 0, 1)
                width = 1 if rim else (.40 + .60 * damage) * tip * tip * (3 - 2 * tip)
                rows.append(points_at[2] + (points_at - points_at[2]) * width)
            ribbons.append(np.array(rows))
        checks.append(validate(f'native_damage_{damage:.2f}', ribbons))

    rows = ['#pragma once', '', '// Generated from the saved shield surface; centimeters in hand.l local space.',
            '// Reproduce with Tools/Blender/bake_nut_shield_crack_layout.py; artist FBX remains read-only.',
            'namespace MCNutShieldCrackLayout', '{',
            'inline const FVector Normal(' + ','.join(f'{v:.6f}' for v in normal) + ');',
            'inline const FVector Center(' + ','.join(f'{v:.6f}' for v in surface(0, 0)[0]) + ');',
            'struct FSample { FVector Points[5]; FVector Normals[5]; };',
            'struct FPath { int32 First,Count; float RevealAt,FullAt; bool Rim; };',
            'inline const FSample Samples[]={']
    for sample in samples:
        rows.append('    {{' + ','.join(vector(item) for item in sample['points']) + '},{'
                    + ','.join(vector(item) for item in sample['normals']) + '}},')
    rows.extend(['};', 'inline const FPath Paths[]={'])
    for first, count, reveal, full, rim in path_descriptors:
        rows.append(f'    {{{first},{count},{reveal:.3f}f,{full:.3f}f,{str(rim).lower()}}},')
    rows.extend(['};', '}'])
    HEADER.write_text('\n'.join(rows) + '\n', encoding='utf8')
    assert source_sha == hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    report = dict(source=str(SOURCE), source_sha256=source_sha, fbx_version=version,
                  vertices=len(vertices), triangles=len(polygons), bone='hand.l',
                  normal=normal.tolist(), center=surface(0, 0)[0].tolist(),
                  sample_count=len(samples), ribbon_vertices=len(samples) * 5,
                  surface_offset_cm=.40, rim_surface_offset_cm=.65,
                  rim_contour=dict(method='projected_saved_mesh_convex_hull', hull_vertex_count=len(hull),
                                   inward_scale=.90, smooth_radius_variation_max=.020,
                                   hull=hull.tolist()),
                  header_sha256=hashlib.sha256(HEADER.read_bytes()).hexdigest(),
                  geometry_validation=dict(method='exact_projected_triangle_intersections', checks=checks,
                                           fork_attachments=attachments, passed=True),
                  paths=path_report, samples=samples, source_preserved=True,
                  visual_validation='Deferred until MessControl editor is running')
    REPORT.write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps(dict(header_sha256=report['header_sha256'], source_sha256=source_sha,
                          samples=len(samples), vertices=len(samples) * 5, triangles=checks[0]['triangles'],
                          min_clearance_cm=min(check['min_entire_triangle_clearance_cm'] for check in checks),
                          forks_checked=len(attachments), source_preserved=True, geometry_passed=True), indent=2))


if __name__ == '__main__':
    main()
