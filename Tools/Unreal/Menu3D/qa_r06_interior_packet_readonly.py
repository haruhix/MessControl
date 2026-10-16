"""Independent read-only source/FBX/OBJ validation in isolated headless Blender.

Writes a report outside the frozen packet. Does not save any Blender file.
"""
import bpy
import bmesh
import collections
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior'
PACKET = BASE / 'UV_r06_Refine03_Candidate01'
OUT = BASE / 'QA_r06_NativeLook'
NAMES = ('SM_DAY03_Interior', 'SM_DAY03_Tongue')
ANCHOR = (-5.757009, -2.887758, -2.076843)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def array(collection, field, width, dtype):
    value = np.empty(len(collection) * width, dtype=dtype)
    collection.foreach_get(field, value)
    return value.reshape((-1, width)) if width > 1 else value


def snapshot(obj):
    mesh = obj.data
    assert not obj.modifiers, 'Frozen packet unexpectedly depends on modifiers'
    mesh.calc_loop_triangles()
    co = array(mesh.vertices, 'co', 3, np.float32)
    matrix = np.asarray(obj.matrix_world, dtype=np.float64)
    world = co @ matrix[:3, :3].T + matrix[:3, 3]
    normals = array(mesh.corner_normals, 'vector', 3, np.float32).astype(float)
    normals = normals @ np.linalg.inv(matrix[:3, :3])
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-30)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    nonmanifold = sum(not edge.is_manifold for edge in bm.edges)
    bm.free()
    return dict(co=co, world=world, matrix=matrix,
                lv=array(mesh.loops, 'vertex_index', 1, np.int32),
                uv=array(mesh.uv_layers.active.data, 'uv', 2, np.float32),
                tri=array(mesh.loop_triangles, 'loops', 3, np.int32),
                tp=array(mesh.loop_triangles, 'polygon_index', 1, np.int32),
                starts=array(mesh.polygons, 'loop_start', 1, np.int32),
                counts=array(mesh.polygons, 'loop_total', 1, np.int32),
                pm=array(mesh.polygons, 'material_index', 1, np.int32),
                normals=normals, materials=[m.name for m in mesh.materials],
                nonmanifold=nonmanifold)


def read_blend(path):
    bpy.ops.wm.open_mainfile(filepath=str(path), load_ui=False)
    bpy.context.view_layer.update()
    return {name: snapshot(bpy.data.objects[name]) for name in NAMES}


def kd(points):
    tree = KDTree(len(points))
    for index, point in enumerate(points):
        tree.insert(point, index)
    tree.balance()
    return tree


def corner_keys(data, vertex_map=None):
    ids = data['lv'] if vertex_map is None else vertex_map[data['lv']]
    loop_mat = np.repeat(data['pm'], data['counts'])
    quant_uv = np.round(data['uv'].astype(float), 6)
    return [(int(v), float(uv[0]), float(uv[1]), data['materials'][int(m)])
            for v, uv, m in zip(ids, quant_uv, loop_mat)]


def compare_export(source, other):
    tree = kd(source['world'])
    nearest = [tree.find(co) for co in other['world']]
    mapping = np.array([item[1] for item in nearest], dtype=np.int32)
    max_position = max(item[2] for item in nearest)
    # Continuous tolerance avoids false half-bin mismatches from OBJ decimal rounding.
    source_materials = [source['materials'][int(m)] for m in np.repeat(source['pm'], source['counts'])]
    other_materials = [other['materials'][int(m)] for m in np.repeat(other['pm'], other['counts'])]
    buckets = collections.defaultdict(list)
    source_keys = []
    for v, uv, normal, material in zip(source['lv'], source['uv'], source['normals'], source_materials):
        key = (int(v), float(uv[0]), float(uv[1]), material)
        source_keys.append(key)
        buckets[(int(v), material)].append((np.asarray(uv), normal, key))
    other_keys, angles, uv_errors = [], [], []
    for v, uv, normal, material in zip(mapping[other['lv']], other['uv'], other['normals'], other_materials):
        candidates = buckets.get((int(v), material), [])
        if not candidates:
            continue
        chosen = min(candidates, key=lambda item: float(np.linalg.norm(item[0] - uv)))
        uv_error = float(np.linalg.norm(chosen[0] - uv))
        uv_errors.append(uv_error)
        if uv_error > 1e-7:
            continue
        other_keys.append(chosen[2])
        normal = normal / max(float(np.linalg.norm(normal)), 1e-30)
        matching = [item for item in candidates if np.linalg.norm(item[0] - uv) <= 1e-7]
        dot = max(float(np.dot(normal, item[1] / np.linalg.norm(item[1]))) for item in matching)
        angles.append(float(np.degrees(np.arccos(np.clip(dot, -1, 1)))))
    result = dict(max_position_error_cm=max_position * 100,
                  corner_UV_material_association=collections.Counter(source_keys) == collections.Counter(other_keys),
                  max_uv_coordinate_error=max(uv_errors, default=1.),
                  normal_angle_max_degrees=max(angles, default=180.),
                  normal_corners_checked=len(angles), corners=len(other['lv']),
                  vertex_count=len(other['world']), triangle_count=len(other['tri']))
    result['pass'] = (max_position < 2e-6 and result['corner_UV_material_association']
                      and result['normal_angle_max_degrees'] < .01
                      and len(angles) == len(other['lv']))
    return result


def geometry_metrics(data):
    p = data['world'][data['lv'][data['tri']]]
    area = np.linalg.norm(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]), axis=1) * .5
    tree = kd(data['world'])
    symmetry = max(tree.find((-co[0], co[1], co[2]))[2] for co in data['world'])
    bvh = BVHTree.FromPolygons([tuple(v) for v in data['world']],
                              data['lv'][data['tri']].tolist(), all_triangles=True)
    contact = bvh.find_nearest(Vector(ANCHOR))
    return dict(vertices=len(data['world']), triangles=len(p), nonmanifold_edges=data['nonmanifold'],
                degenerate_triangles=int(np.count_nonzero(area < 1e-10)),
                signed_volume=float(np.einsum('ij,ij->i', p[:, 0], np.cross(p[:, 1], p[:, 2])).sum() / 6),
                bounds_source=[data['world'].min(axis=0).tolist(), data['world'].max(axis=0).tolist()],
                reflection_error_source_max=symmetry,
                nearest_character_anchor_distance_cm=float(contact[3]) * 100)


def read_obj(path):
    vertices, uvs, normals = [], [], []
    materials, faces, mat_names = [], [], []
    active_mat = None
    for line in path.read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'v': vertices.append([float(v) for v in fields[1:4]])
        elif fields[0] == 'vt': uvs.append([float(v) for v in fields[1:3]])
        elif fields[0] == 'vn': normals.append([float(v) for v in fields[1:4]])
        elif fields[0] == 'usemtl':
            active_mat = fields[1]
            if active_mat not in mat_names: mat_names.append(active_mat)
        elif fields[0] == 'f':
            assert active_mat
            faces.append([[int(v) - 1 for v in token.split('/')] for token in fields[1:]])
            materials.append(mat_names.index(active_mat))
    flat = np.array([corner for face in faces for corner in face], dtype=np.int32)
    return dict(world=np.asarray(vertices), lv=flat[:, 0], uv=np.asarray(uvs)[flat[:, 1]],
                normals=np.asarray(normals)[flat[:, 2]], pm=np.asarray(materials),
                counts=np.asarray([len(face) for face in faces]), materials=mat_names,
                tri=np.zeros((sum(len(face)-2 for face in faces), 3), dtype=np.int32))


def main():
    assert bpy.app.background
    OUT.mkdir(parents=True, exist_ok=True)
    report = dict(agent='native_look_qa', backend='isolated factory-startup headless Blender',
                  blender_version=bpy.app.version_string, live_mcp='unavailable127.0.0.1:9876',
                  scope='Read-only source/FBX/OBJ inspection, no saved source/material/editor changes')
    manifest = json.loads((PACKET / 'release_manifest.json').read_text())
    report['manifest_sha256'] = digest(PACKET / 'release_manifest.json')
    assert report['manifest_sha256'] == '535bf9e152190f64ab2663851278278c81df5214f79d1db547e418e28bac0ccb'
    report['file_hashes'] = {name: digest(PACKET / name) == sha for name, sha in manifest['files'].items()}
    current = read_blend(PACKET / 'MC_DAY03_InteriorSource.blend')
    approved = read_blend(Path(manifest['parent_approved_shape_source']))
    prior = read_blend(BASE / 'UV_Organic_r05_Candidate01/MC_DAY03_InteriorSource.blend')
    report['source_geometry_exact_approved_refine03'] = {
        name: all(np.array_equal(current[name][key], approved[name][key])
                  for key in ('co', 'lv', 'tri', 'starts', 'counts', 'matrix')) for name in NAMES}
    report['cavity_exact_r05'] = all(np.array_equal(current[NAMES[0]][key], prior[NAMES[0]][key])
                                   for key in ('co', 'lv', 'tri', 'uv', 'matrix', 'normals'))
    report['source_geometry'] = {name: geometry_metrics(data) for name, data in current.items()}
    report['fresh_fbx'] = {}
    for name in NAMES:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=str(PACKET / (name + '.fbx')), use_custom_normals=True)
        bpy.context.view_layer.update()
        mesh_objects = [obj for obj in bpy.data.objects if obj.type == 'MESH']
        assert len(mesh_objects) == 1
        fresh = snapshot(mesh_objects[0])
        report['fresh_fbx'][name] = compare_export(current[name], fresh)
        report['fresh_fbx'][name]['geometry'] = geometry_metrics(fresh)
    obj = read_obj(PACKET / 'MC_DAY03_Interior_Painter_Open.obj')
    offset = len(current[NAMES[0]]['world'])
    a, b = current[NAMES[0]], current[NAMES[1]]
    combined = dict(world=np.concatenate([a['world'], b['world']]),
                    lv=np.concatenate([a['lv'], b['lv'] + offset]),
                    uv=np.concatenate([a['uv'], b['uv']]),
                    normals=np.concatenate([a['normals'], b['normals']]),
                    pm=np.concatenate([np.zeros(len(a['pm']), dtype=np.int32), np.ones(len(b['pm']), dtype=np.int32)]),
                    counts=np.concatenate([a['counts'], b['counts']]), materials=['MC_Interior', 'MC_Tongue'])
    report['painter_obj'] = compare_export(combined, obj)
    packet = np.load(PACKET / 'texture_mesh_packet.npz')
    report['source_packet_vertices_exact'] = np.array_equal(np.concatenate([a['co'], b['co']]), packet['vertices'])
    report['source_packet_loops_exact'] = np.array_equal(combined['lv'], packet['loop_vertex_indices'])
    report['source_packet_uv_exact'] = np.array_equal(combined['uv'], packet['loop_uv'])
    transfer = np.load(PACKET / 'dorsal_uv_transfer.npz')
    report['uv_transfer'] = dict(vertices_exact=np.array_equal(transfer['vertices'], b['co']),
                               loops_exact=np.array_equal(transfer['loop_vertex_indices'], b['lv']),
                               old_uv_exact_r05=np.array_equal(transfer['old_loop_uv'], prior[NAMES[1]]['uv']),
                               new_uv_exact_current=np.array_equal(transfer['new_loop_uv'], b['uv']),
                               ventral_exact=np.array_equal(transfer['old_loop_uv'][~transfer['dorsal_loop_mask']],
                                                            transfer['new_loop_uv'][~transfer['dorsal_loop_mask']]))
    report['geometry_integrity_pass'] = all(
        data['nonmanifold_edges'] == 0 and data['degenerate_triangles'] == 0 and data['signed_volume'] > 0
        and data['reflection_error_source_max'] < 3e-5
        for data in report['source_geometry'].values())
    report['tongue_source_anchor_near_surface_pass'] = report['source_geometry'][NAMES[1]]['nearest_character_anchor_distance_cm'] < .1
    checks = [all(report['file_hashes'].values()), all(report['source_geometry_exact_approved_refine03'].values()),
              report['cavity_exact_r05'], all(v['pass'] for v in report['fresh_fbx'].values()),
              report['painter_obj']['pass'], report['source_packet_vertices_exact'], report['source_packet_loops_exact'],
              report['source_packet_uv_exact'], all(report['uv_transfer'].values()),
              report['geometry_integrity_pass'], report['tongue_source_anchor_near_surface_pass']]
    report['status'] = 'PASS' if all(checks) else 'HOLD'
    report['limits'] = ['No Unreal/Painter mutations or new PBR bake validation',
                        'Anchor-nearest-surface agreement does not validate animated character feet or native child scale',
                        'Technical source/export pass does not establish final art/reference fidelity']
    (OUT / 'independent_source_export_contact_v02.json').write_text(json.dumps(report, indent=2))
    print('RESULT ' + json.dumps({'status': report['status'], 'report': str(OUT / 'independent_source_export_contact_v02.json')}))


if __name__ == '__main__':
    main()
