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

    def surface(x, y, offset=.40):
        origin = center + side * x + up * y
        intersections = []
        for ids in polygons:
            a, b, c = points[ids]
            matrix = np.column_stack((b - a, c - a, -normal))
            if abs(np.linalg.det(matrix)) < 1e-8:
                continue
            u, v, distance = np.linalg.solve(matrix, origin - a)
            if u >= -1e-6 and v >= -1e-6 and u + v <= 1 + 1e-6:
                face_normal = np.cross(b - a, c - a)
                face_normal /= np.linalg.norm(face_normal)
                if np.dot(face_normal, normal) < 0:
                    face_normal *= -1
                intersections.append((distance, a + u * (b - a) + v * (c - a), face_normal))
        if not intersections:
            raise RuntimeError('Crack sample misses the saved shield surface')
        _, hit, face_normal = max(intersections, key=lambda item: item[0])
        # Every ribbon column is projected AFTER curve/noise construction. A
        # constant-normal chord through this convex shell can sink into it.
        return hit + normal * offset, face_normal

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
        origin = parent['xy'][round(fraction * (len(parent['xy']) - 1))] / extent
        add_path([origin.tolist()] + controls, reveal, full, width, seed)

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
    # Curved, faint target rim replaces the old bright hexagonal outline.
    angles = np.linspace(0, 2 * np.pi, 257)
    paths.append(dict(xy=np.column_stack((np.cos(angles), np.sin(angles))) * extent * .94,
                      reveal=-1, full=-1, width=.15, seed=2423, rim=True))

    samples, path_descriptors, path_report = [], [], []
    for path in paths:
        xy = path['xy']
        first = len(samples)
        rng = np.random.default_rng(path['seed'])
        widths = []
        for row, point in enumerate(xy):
            tangent = xy[min(row + 1, len(xy) - 1)] - xy[max(row - 1, 0)]
            tangent /= np.linalg.norm(tangent)
            across = np.array((-tangent[1], tangent[0]))
            fraction = row / (len(xy) - 1)
            taper = 1 if path['rim'] else max(.025, (1 - fraction) ** .62)
            width = path['width'] * taper * rng.uniform(.76, 1.18)
            widths.append(float(width))
            positions, normals = [], []
            for column in (-1, -.72, 0, .67, 1):
                hit, face_normal = surface(*(point + across * width * column), offset=.65 if path['rim'] else .40)
                positions.append(hit.tolist())
                normals.append(face_normal.tolist())
            samples.append(dict(points=positions, normals=normals))
        path_descriptors.append((first, len(xy), path['reveal'], path['full'], path['rim']))
        path_report.append(dict(first=first, count=len(xy), reveal=path['reveal'], full=path['full'],
                                rim=path['rim'], seed=path['seed'], widths=widths,
                                xy=(xy / extent).tolist()))

    def vector(values):
        return 'FVector(' + ','.join(f'{value:.6f}' for value in values) + ')'

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
                  surface_offset_cm=.40, rim_surface_offset_cm=.65, paths=path_report, samples=samples, source_preserved=True,
                  visual_validation='Deferred until MessControl editor is running')
    REPORT.write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps({key: value for key, value in report.items() if key not in ('paths', 'samples')}, indent=2))


if __name__ == '__main__':
    main()
