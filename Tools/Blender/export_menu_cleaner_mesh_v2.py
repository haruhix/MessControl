"""Export a menu-only player copy with matched lip/aperture correctives.

Run inside the caller-owned Blender. This utility never opens/saves a .blend,
renders, starts Blender, changes gameplay packages, or exports an animation.
MCP: runpy.run_path(path, run_name='__main__', init_globals={
    'MC_MENU_MESH_EXPORT_OPTIONS': {}})

The lip recipe is computed from canonical geometry. The reviewed aperture
companion and hand-island manifest are stable ArtSource data, never temporary
audit dependencies. Every original morph, UV, weight, slot and rest bone stays.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import traceback

import bpy
from mathutils import Matrix


PROJECT = Path(__file__).resolve().parents[2]
DEFAULTS = {
    'canonical_blend': str(PROJECT / 'ArtSource/CharacterCurrent/CharacterGameplay.blend'),
    'canonical_rig': 'root',
    'canonical_mesh': 'SK_CharacterGameplay',
    'export_mesh_name': 'SK_MenuCleanerActing',
    'output_path': str(PROJECT / 'ArtSource/UI/MenuSceneV2_20261022/Blender/Exports/SK_MenuCleanerActing.fbx'),
    'corrective_name': 'Mouth_MenuSoftClosed',
    'aperture_corrective_name': 'Mouth_MenuApertureFollow',
    'acting_spec': str(PROJECT / 'ArtSource/UI/MenuSceneV2_20261022/Blender/Menu_CharacterActingSpec_v3.json'),
    'target_gap_cm': 0.6,
    'transition_cm': 0.8,
    'expected_vertices': 4006,
    'expected_bones': 107,
    'expected_scene_unit_scale': 1.0,
}
LIP_BOUNDS = ((-15.00841522, 14.98352051), (-30.24642181, -25.73956490), (53.14626312, 65.87297058))
EXPECTED_BASIS_SHA256 = '40cdd84ed8f9f7c089675147d11b49560003745885aa3fda5f42ce220cb8c76b'
EXPECTED_TOPOLOGY_SHA256 = '0dd246fb3e0f8c972e2aab5b4f4c9b222d81f3f48e19b0fd4ba52aabaa25b4b6'


def _sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def _json(path, value):
    temporary = path.with_name(path.name + '.writing')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf8')
    os.replace(temporary, path)


def _hash_coordinates(points):
    digest = hashlib.sha256()
    for point in points:
        digest.update(struct.pack('<3f', *point.co))
    return digest.hexdigest()


def _topology_hash(mesh):
    digest = hashlib.sha256()
    for polygon in mesh.polygons:
        digest.update(struct.pack('<I', len(polygon.vertices)))
        digest.update(struct.pack('<' + 'I' * len(polygon.vertices), *polygon.vertices))
    return digest.hexdigest()


def _load_acting_spec(mesh, cfg):
    path = Path(cfg['acting_spec']).resolve()
    if path.drive.upper() != 'E:':
        raise ValueError('Reviewed acting data must be on E:.')
    spec = json.loads(path.read_text(encoding='utf8'))
    if (spec['schema_version'] != 1 or spec['expected_vertices'] != 4006 or len(mesh.vertices) != 4006
            or spec['basis_sha256'] != EXPECTED_BASIS_SHA256 or spec['topology_sha256'] != EXPECTED_TOPOLOGY_SHA256):
        raise RuntimeError('Acting recipe does not match the reviewed 4006-vertex mesh.')
    if _topology_hash(mesh) != spec['topology_sha256']:
        raise RuntimeError('Canonical topology hash differs; refusing vertex-index corrective.')
    if _hash_coordinates(mesh.shape_keys.key_blocks[0].data) != spec['basis_sha256']:
        raise RuntimeError('Canonical Basis hash differs; refusing an unreviewed shape.')
    return spec


def ensure_menu_correctives(mesh_object, options=None):
    """Add/verify only two menu shapes; caller owns the independent mesh.

    Also used by the continuous menu solver so its saved scene and exported
    canonical mesh carry identical geometry. This function never animates,
    exports, opens/saves a blend, or edits Basis/weights/rest bones.
    """
    cfg = dict(DEFAULTS)
    if options:
        cfg.update(options)
    if abs(cfg['target_gap_cm'] - .6) > 1e-9 or abs(cfg['transition_cm'] - .8) > 1e-9:
        raise ValueError('The reviewed aperture companion requires lip target .6cm and transition .8cm.')
    mesh = mesh_object.data
    if not mesh.shape_keys:
        raise RuntimeError('Menu character needs the original canonical shape keys.')
    spec = _load_acting_spec(mesh, cfg)
    keys = mesh.shape_keys.key_blocks
    basis = keys[0]
    points = [point.co.copy() for point in basis.data]
    lip, bounds = _lip_component(mesh)
    faces = [list(poly.vertices) for poly in mesh.polygons if all(i in lip for i in poly.vertices)]
    lip_offsets = {}
    for i in sorted(lip):
        point = points[i]
        section = _inner_section(points, faces, point.x)
        if section:
            dz = -max(0.0, (section['gap_cm'] - cfg['target_gap_cm']) * .5) * math.tanh((point.z - section['middle_z_cm']) / cfg['transition_cm'])
            if abs(dz) > 1e-6:
                lip_offsets[i] = (0.0, 0.0, dz)
    aperture = spec['aperture_follow']
    allowed = set(aperture['allowed_vertex_indices'])
    aperture_offsets = {row['vertex']: tuple(row['delta_cm']) for row in aperture['delta_at_weight_one_cm']}
    if (len(allowed) != 62 or len(aperture_offsets) != aperture['expected_changed_vertices']
            or len(aperture_offsets) != 42 or not set(aperture_offsets).issubset(allowed)
            or any(abs(delta[0]) > 1e-9 or abs(delta[1]) > 1e-9 or abs(delta[2]) > 2.0 for delta in aperture_offsets.values())):
        raise RuntimeError('Aperture recipe escaped the reviewed 42-vertex Z-only scope.')
    reports = {}
    for name, offsets, allowed_count in ((cfg['corrective_name'], lip_offsets, 144),
                                         (cfg['aperture_corrective_name'], aperture_offsets, 62)):
        key = keys.get(name)
        created = key is None
        if created:
            key = mesh_object.shape_key_add(name=name, from_mix=False)
            key.relative_key = basis
            key.slider_min, key.slider_max, key.value = 0.0, 1.0, 0.0
            for i, point in enumerate(points):
                key.data[i].co = point
            for i, delta in offsets.items():
                for axis in range(3):
                    key.data[i].co[axis] += delta[axis]
        # An existing menu shape may be reused only when it is the same recipe.
        # Never silently overwrite an artist's different correction.
        if key.relative_key != basis or key.mute or key.vertex_group:
            raise RuntimeError('Menu shape has an unreviewed relative key, mask or mute state: ' + name)
        for i, point in enumerate(points):
            delta = offsets.get(i, (0.0, 0.0, 0.0))
            if max(abs(key.data[i].co[axis] - point[axis] - delta[axis]) for axis in range(3)) > 8e-6:
                raise RuntimeError('Existing menu shape differs from reviewed recipe: ' + name)
        reports[name] = {'name': name, 'created': created, 'allowed_vertices': allowed_count,
                         'affected_vertices': len(offsets), 'affected_vertex_indices': sorted(offsets),
                         'max_delta_cm': max(abs(delta[2]) for delta in offsets.values()),
                         'value': key.value, 'basis_lip_bounds_cm': bounds,
                         'displacement_axis': '+/-Z only'}
    reports['source_spec'] = {'path': cfg['acting_spec'], 'sha256': _sha(cfg['acting_spec']),
                              'basis_sha256': spec['basis_sha256'], 'topology_sha256': spec['topology_sha256']}
    return reports


def _signature(mesh, rig):
    weight = hashlib.sha256()
    for vertex in mesh.data.vertices:
        weight.update(struct.pack('<I', vertex.index))
        for item in vertex.groups:
            weight.update(mesh.vertex_groups[item.group].name.encode('utf8') + b'\0')
            weight.update(struct.pack('<f', item.weight))
    rest = hashlib.sha256()
    for bone in rig.data.bones:
        rest.update((bone.name + '\0' + (bone.parent.name if bone.parent else '') + '\0').encode('utf8'))
        for row in bone.matrix_local:
            rest.update(struct.pack('<4f', *row))
    topology = hashlib.sha256()
    for polygon in mesh.data.polygons:
        topology.update(struct.pack('<I', len(polygon.vertices)))
        topology.update(struct.pack('<' + 'I' * len(polygon.vertices), *polygon.vertices))
        topology.update(struct.pack('<I', polygon.material_index))
    uv = hashlib.sha256()
    for layer in mesh.data.uv_layers:
        uv.update(layer.name.encode('utf8') + b'\0')
        for point in layer.data:
            uv.update(struct.pack('<2f', *point.uv))
    return {'vertices': len(mesh.data.vertices), 'polygons': len(mesh.data.polygons),
            'bones': len(rig.data.bones), 'weights_sha256': weight.hexdigest(),
            'rest_and_hierarchy_sha256': rest.hexdigest(), 'topology_and_slots_sha256': topology.hexdigest(),
            'uv_sha256': uv.hexdigest(), 'uv_layers': [layer.name for layer in mesh.data.uv_layers],
            'material_slots': [slot.material.name if slot.material else None for slot in mesh.material_slots],
            'morph_hashes': {key.name: _hash_coordinates(key.data) for key in mesh.data.shape_keys.key_blocks}}


def _lip_component(mesh):
    adjacent = [set() for _ in mesh.vertices]
    for edge in mesh.edges:
        a, b = edge.vertices
        adjacent[a].add(b)
        adjacent[b].add(a)
    remaining = set(range(len(adjacent)))
    found = []
    basis = mesh.shape_keys.key_blocks[0]
    while remaining:
        first = remaining.pop()
        group, queue = {first}, [first]
        while queue:
            for index in adjacent[queue.pop()]:
                if index in remaining:
                    remaining.remove(index)
                    group.add(index)
                    queue.append(index)
        if len(group) == 144:
            bounds = [(min(basis.data[i].co[a] for i in group), max(basis.data[i].co[a] for i in group)) for a in range(3)]
            if max(abs(bounds[a][b] - LIP_BOUNDS[a][b]) for a in range(3) for b in range(2)) < 0.002:
                found.append((group, bounds))
    if len(found) != 1:
        raise RuntimeError('Expected one measured 144-vertex front lip component; refusing an unreviewed mesh.')
    return found[0]


def _inner_section(points, faces, x):
    """Projected solid-lip bands at X=x; two bands delimit the open cavity."""
    intervals = []
    for face in faces:
        values = []
        for i, j in zip(face, face[1:] + face[:1]):
            a, b = points[i], points[j]
            if min(a.x, b.x) <= x <= max(a.x, b.x):
                if abs(b.x - a.x) < 1e-9:
                    values.extend((a.z, b.z))
                else:
                    values.append(a.z + (b.z - a.z) * (x - a.x) / (b.x - a.x))
        if len(values) > 1:
            intervals.append((min(values), max(values)))
    merged = []
    for low, high in sorted(intervals):
        if merged and low <= merged[-1][1] + 1e-4:
            merged[-1][1] = max(high, merged[-1][1])
        else:
            merged.append([low, high])
    if len(merged) != 2:
        return None
    bottom, top = merged[0][1], merged[1][0]
    return {'x_cm': float(x), 'gap_cm': top - bottom, 'middle_z_cm': (top + bottom) * 0.5}


def export_menu_cleaner_mesh(options=None):
    cfg = dict(DEFAULTS)
    if options:
        unknown = set(options) - set(cfg)
        if unknown:
            raise ValueError('Unknown export options: ' + ', '.join(sorted(unknown)))
        cfg.update(options)
    output = Path(cfg['output_path']).resolve()
    canonical = Path(cfg['canonical_blend']).resolve()
    if output.drive.upper() != 'E:' or output.suffix.lower() != '.fbx':
        raise ValueError('An .fbx destination on E: is required.')
    if not canonical.is_file():
        raise FileNotFoundError(canonical)
    if not 0.25 <= cfg['target_gap_cm'] <= 1.5 or cfg['transition_cm'] <= 0:
        raise ValueError('Unreviewed lip closure amount; target gap must be .25..1.5cm and transition positive.')
    scene, view = bpy.context.scene, bpy.context.view_layer
    if bpy.context.mode not in {'OBJECT', 'POSE'}:
        raise RuntimeError('Export supports Object/Pose mode only; finish other editing first.')
    if abs(scene.unit_settings.scale_length - cfg['expected_scene_unit_scale']) > 1e-6:
        raise RuntimeError('Live scene unit scale differs from canonical export convention; refusing a unit guess.')
    if bpy.data.objects.get(cfg['canonical_rig']) or bpy.data.objects.get(cfg['export_mesh_name']):
        raise RuntimeError('Canonical root or output mesh name is occupied; no unrelated objects will be renamed.')
    output.parent.mkdir(parents=True, exist_ok=True)
    report_path = output.with_name(output.stem + '_MeshExportReport.json')
    pending = output.with_name(output.stem + '.pending.fbx')
    old = {'frame': scene.frame_current, 'subframe': scene.frame_subframe,
           'selected': list(bpy.context.selected_objects), 'active': view.objects.active, 'mode': bpy.context.mode}
    # Track ownership explicitly. Cleanup never touches unrelated orphaned IDs.
    categories = ('objects', 'meshes', 'armatures', 'shape_keys', 'actions', 'materials', 'node_groups', 'textures', 'images')
    before = {name: set(getattr(bpy.data, name)) for name in categories}
    collection = rig = mesh = None
    report = {'status': 'running', 'source': str(canonical), 'source_sha256': _sha(canonical),
              'live_scene_source': bpy.data.filepath, 'settings': cfg,
              'scope': 'Menu-only skeletal mesh and morphs. No animation, gameplay package, .blend save or render.',
              'limitations': ['Offline lip projection is not visual or 3D self-intersection approval.',
                              'Inner tooth/cavity coverage, FBX fresh import and Unreal morph playback require root validation.']}
    try:
        if old['mode'] == 'POSE':
            bpy.ops.object.mode_set(mode='OBJECT')
        collection = bpy.data.collections.new('MC_MenuCleanerMesh_ExportOnly')
        scene.collection.children.link(collection)
        with bpy.data.libraries.load(str(canonical), link=False) as (available, loaded):
            wanted = [cfg['canonical_rig'], cfg['canonical_mesh']]
            if not all(name in available.objects for name in wanted):
                raise RuntimeError('Canonical rig/mesh object names not found in source .blend.')
            loaded.objects = wanted
        rig, mesh = loaded.objects
        if rig.type != 'ARMATURE' or mesh.type != 'MESH' or rig.name != cfg['canonical_rig']:
            raise RuntimeError('Appended canonical object types or root name differ.')
        # A reused datablock must never receive even a menu-only shape key.
        if mesh.data in before['meshes'] or mesh.data.users > 1:
            mesh.data = mesh.data.copy()
        if rig.data in before['armatures'] or rig.data.users > 1:
            rig.data = rig.data.copy()
        if mesh.data.shape_keys is None or mesh.data.shape_keys in before['shape_keys']:
            raise RuntimeError('Export mesh lacks independent shape-key data.')
        for obj in (rig, mesh):
            collection.objects.link(obj)
            obj.hide_viewport = obj.hide_render = False
            obj.hide_set(False)
        view.update()
        rig_world = rig.matrix_world.copy()
        if any(abs(value - .01) > 1e-6 for value in rig_world.to_scale()):
            raise RuntimeError('Canonical rig scale is not .01; centimetre rest-bone assumption is invalid.')
        if mesh.parent != rig:
            raise RuntimeError('Canonical mesh parent is not the appended root armature.')
        if not any(mod.type == 'ARMATURE' and mod.object == rig for mod in mesh.modifiers):
            raise RuntimeError('Canonical skin is not bound to the appended root.')
        for obj in (rig, mesh):
            obj.animation_data_clear()
        mesh.data.shape_keys.animation_data_clear()
        rig.parent = None
        rig.matrix_world = rig_world
        if rig.constraints or any(pb.constraints for pb in rig.pose.bones):
            raise RuntimeError('Canonical export rig unexpectedly contains constraints.')
        for pb in rig.pose.bones:
            pb.matrix_basis = Matrix.Identity(4)
        if len(mesh.data.vertices) != cfg['expected_vertices'] or len(rig.data.bones) != cfg['expected_bones']:
            raise RuntimeError('Canonical vertex/bone counts differ from reviewed 4006/107.')
        keys = mesh.data.shape_keys.key_blocks
        if any(name in keys for name in (cfg['corrective_name'], cfg['aperture_corrective_name'])):
            raise RuntimeError('Canonical source already contains a menu corrective; refusing silent overwrite.')
        report['source_preview_morph_values']={key.name:key.value for key in keys if abs(key.value)>1e-7}
        # Values are preview state on these newly appended independent copies.
        # FBX morph coordinates remain untouched and are hashed below.
        for key in keys:key.value=0.
        original = _signature(mesh, rig)
        menu_shapes = ensure_menu_correctives(mesh, cfg)
        lip, bounds = _lip_component(mesh.data)
        basis = keys[0]
        points = [point.co.copy() for point in basis.data]
        faces = [list(poly.vertices) for poly in mesh.data.polygons if all(i in lip for i in poly.vertices)]
        corrective = keys[cfg['corrective_name']]
        affected = menu_shapes[cfg['corrective_name']]['affected_vertex_indices']
        maximum_delta = max((corrective.data[i].co - points[i]).length for i in range(len(points)))
        if not affected or maximum_delta > 2.3:
            raise RuntimeError('Corrective is empty or exceeds the reviewed small lip displacement.')
        if any((corrective.data[i].co - points[i]).length > 1e-7 for i in range(len(points)) if i not in lip):
            raise RuntimeError('Corrective escaped the 144-vertex lip component.')
        after = _signature(mesh, rig)
        for key in ('vertices', 'polygons', 'bones', 'weights_sha256', 'rest_and_hierarchy_sha256',
                    'topology_and_slots_sha256', 'uv_sha256', 'material_slots'):
            if original[key] != after[key]:
                raise RuntimeError('Original canonical payload changed: ' + key)
        if any(after['morph_hashes'].get(name) != digest for name, digest in original['morph_hashes'].items()):
            raise RuntimeError('An existing morph coordinate array changed.')
        corrected_points = [point.co.copy() for point in corrective.data]
        report.update({'original': original, 'export_payload': after,
            'menu_correctives': menu_shapes,
            'aperture_companion_curve': 'Mouth_MenuApertureFollow equals Mouth_MenuSoftClosed; reviewed weights .42 to .50.',
            'corrective': {'name': corrective.name, 'value': corrective.value, 'allowed_component_vertices': 144,
                'affected_vertices': len(affected), 'affected_vertex_indices': affected,
                'allowed_vertex_indices': sorted(lip), 'max_delta_cm': maximum_delta,
                'basis_lip_bounds_cm': bounds, 'front_normal': [0, -1, 0], 'displacement_axis': '+/-Z only',
                'formula': 'dz=-max(0,(G(x)-target_gap)/2)*tanh((z-C(x))/transition); no open section =>0; dx=dy=0',
                'basis_sections': [_inner_section(points, faces, x) for x in (0, 3, 6, 8, 9, 10, 11)],
                'corrected_sections': [_inner_section(corrected_points, faces, x) for x in (0, 3, 6, 8, 9, 10, 11)]},
            'preserved': {'original_morphs_coordinate_hashes_equal': True, 'weights_equal': True,
                          'rest_hierarchy_equal': True, 'topology_material_indices_equal': True,
                          'uv_equal': True, 'material_slot_order_equal': True}})
        mesh.name = cfg['export_mesh_name']
        bpy.ops.object.select_all(action='DESELECT')
        rig.select_set(True)
        mesh.select_set(True)
        view.objects.active = rig
        view.update()
        # Established build_character_gameplay.py FBX convention. Explicitly
        # disable modifier application to guarantee the shape-key payload.
        settings = {'use_selection': True, 'object_types': {'ARMATURE', 'MESH'},
            'add_leaf_bones': False, 'use_armature_deform_only': False,
            'bake_anim': False, 'mesh_smooth_type': 'FACE', 'use_mesh_modifiers': False,
            'axis_forward': '-Z', 'axis_up': 'Y', 'global_scale': 1.0,
            'apply_unit_scale': True, 'apply_scale_options': 'FBX_SCALE_NONE', 'bake_space_transform': False}
        done = bpy.ops.export_scene.fbx(filepath=str(pending), **settings)
        if 'FINISHED' not in done or not pending.is_file():
            raise RuntimeError('FBX export did not finish.')
        os.replace(pending, output)
        report['export'] = {'file': str(output), 'bytes': output.stat().st_size, 'sha256': _sha(output),
            'settings': {key: sorted(value) if isinstance(value, set) else value for key, value in settings.items()},
            'armature_object': rig.name, 'mesh_object': mesh.name,
            'animation_exported': False, 'original_morph_count_including_basis': len(original['morph_hashes']),
            'export_morph_count_including_basis': len(after['morph_hashes']),
            'expected_ue_reference_bones': 108,
            'import_note': 'Own menu namespace only; reuse current player skeleton, import morph targets, do not update reference pose.'}
        report['status'] = 'exported_pending_fresh_import_and_visual_QA'
        return report
    except Exception as error:
        report.update(status='failed', error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        if bpy.context.object and bpy.context.object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
        for obj in list(set(bpy.data.objects) - before['objects']):
            bpy.data.objects.remove(obj, do_unlink=True)
        if collection is not None:
            bpy.data.collections.remove(collection)
        # Ownership-scoped zero-user cleanup, not a global orphan purge.
        for _ in range(3):
            for category in categories[1:]:
                owner = getattr(bpy.data, category)
                for data in list(set(owner) - before[category]):
                    if data.users == 0:
                        owner.remove(data)
        scene.frame_set(old['frame'], subframe=old['subframe'])
        bpy.ops.object.select_all(action='DESELECT')
        for obj in old['selected']:
            if view.objects.get(obj.name) == obj:
                obj.select_set(True)
        if old['active'] and view.objects.get(old['active'].name) == old['active']:
            view.objects.active = old['active']
            if old['mode'] == 'POSE':
                bpy.ops.object.mode_set(mode='POSE')
        else:
            view.objects.active = None
        if pending.exists():
            pending.unlink()
        report['source_sha256_after'] = _sha(canonical)
        report['source_file_unchanged'] = report['source_sha256_after'] == report['source_sha256']
        report['cleanup'] = {'created_objects_remaining': [obj.name for obj in set(bpy.data.objects) - before['objects']],
                             'source_saved': False, 'scene_frame_selection_restored': True}
        _json(report_path, report)
        print('MC_MENU_CLEANER_MESH_EXPORT', json.dumps({'status': report['status'], 'report': str(report_path)}), flush=True)


def main():
    options = globals().get('MC_MENU_MESH_EXPORT_OPTIONS')
    if options is None:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--output-path')
        values = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
        options = {key: value for key, value in vars(values).items() if value is not None}
    export_menu_cleaner_mesh(options)


if __name__ == '__main__':
    main()
