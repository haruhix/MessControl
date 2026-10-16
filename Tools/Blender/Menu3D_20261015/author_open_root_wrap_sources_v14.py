"""Granted-only source branching for approved local six-upper physical repair.

Load immutable V13 jaws and V03 gums separately. Apply the frozen, actually
reviewed open-world coordinate arrays; preserve UV0/topology and untouched meshes.
Write new OPEN-only source branches, OBJ and explicit basis/UV handoff. No render
or bake, no closed corrective creation, no original file overwrite.
"""
from pathlib import Path
import hashlib
import json
import shutil

import bpy
import numpy as np

ROOT = Path(r'E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
STAGE_ROOT = ROOT / 'Gums/Diagnostics/v14_UpperRootWrap'
STAGE = STAGE_ROOT / 'upper6_root_wrap_v14_coordinates.npz'
ACTUAL = STAGE_ROOT / 'Actual/actual_root_wrap_manifest.json'
JAW_SOURCE = ROOT / 'Jaws/ContourHero_v13/Full32/MC_DAY03_SourceHero_v13.blend'
GUM_SOURCE = ROOT / 'Gums/V03/Gums_SourceLocked.blend'
JAW_OUT = ROOT / 'Jaws/ContourHero_v14/Full32'
GUM_OUT = ROOT / 'Gums/V04'
SOURCE_EXPECTED = {str(JAW_SOURCE): '44abbe10e291f810c67185c277b20818f04746c7e877ebbd49c983c35d3c8906',
                   str(GUM_SOURCE): '02f1adf351a7cf1f11d2770c92ccf96bab97b1bd0f664cdfc12119b9e1133b7a'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def array_sha(array):
    return hashlib.sha256(np.asarray(array).tobytes()).hexdigest()


def vertices(obj, world=False):
    data = np.empty((len(obj.data.vertices), 3), dtype=np.float64)
    obj.data.vertices.foreach_get('co', data.ravel())
    if world:
        m = np.asarray(obj.matrix_world)
        data = data @ m[:3, :3].T + m[:3, 3]
    return data


def mesh_arrays(obj):
    mesh = obj.data
    loops = np.empty(len(mesh.loops), np.int32)
    starts, sizes = np.empty(len(mesh.polygons), np.int32), np.empty(len(mesh.polygons), np.int32)
    mesh.loops.foreach_get('vertex_index', loops)
    mesh.polygons.foreach_get('loop_start', starts)
    mesh.polygons.foreach_get('loop_total', sizes)
    result = {'local_vertices': vertices(obj), 'world_vertices': vertices(obj, True),
              'loop_vertex_indices': loops, 'polygon_loop_starts': starts, 'polygon_loop_sizes': sizes}
    for layer in mesh.uv_layers:
        uv = np.empty((len(layer.data), 2), np.float32)
        layer.data.foreach_get('uv', uv.ravel())
        result[layer.name] = uv
    return result


def fingerprints(obj):
    return {key: array_sha(value) for key, value in mesh_arrays(obj).items()}


def apply_reviewed_open(obj, coordinates):
    name = obj.name
    before, after = coordinates[name + '__before'], coordinates[name + '__after']
    if obj.data.shape_keys is not None or obj.modifiers:
        raise RuntimeError('Original frozen mesh must have no modifiers/shape keys: ' + name)
    actual = vertices(obj, True)
    if actual.shape != before.shape or np.max(np.abs(actual - before)) > 2e-6:
        raise RuntimeError('Original source world-basis/order mismatch: ' + name)
    before_fp = fingerprints(obj)
    inverse = np.linalg.inv(np.asarray(obj.matrix_world))
    local = after @ inverse[:3, :3].T + inverse[:3, 3]
    obj.data.vertices.foreach_set('co', local.astype(np.float32).ravel())
    obj.data.update()
    if np.max(np.abs(vertices(obj, True) - after)) > 2e-6:
        raise RuntimeError('Applied world coordinates mismatch: ' + name)
    # Recompute secondary projection only. UV0 remains the same unique layout.
    loops = np.empty(len(obj.data.loops), np.int32)
    obj.data.loops.foreach_get('vertex_index', loops)
    world = vertices(obj, True)[loops]
    uv = np.column_stack((world[:, 0] / 16 + .5, .5 + world[:, 2] / (410 / 963 * 16)))
    obj.data.uv_layers['UV_SourceProjection'].data.foreach_set('uv', uv.astype(np.float32).ravel())
    obj.data.uv_layers.active_index = list(obj.data.uv_layers.keys()).index('UV0')
    obj.data.uv_layers['UV0'].active_render = True
    after_fp = fingerprints(obj)
    for key in ('loop_vertex_indices', 'polygon_loop_starts', 'polygon_loop_sizes', 'UV0'):
        if before_fp[key] != after_fp[key]:
            raise RuntimeError('Topology/UV0 invariant failed: ' + name + '/' + key)
    obj['MC_GeometryVersion'] = 'v14_OPEN_ROOT_CONTINUATION'
    obj['MC_UV0SourceAffineStatus'] = 'STALE_AFTER_ROOT_CONTINUATION; use actual mesh barycentric worldXYZ transfer'
    obj['MC_SourceProjectionVersion'] = 'v14_actual_OPEN_worldXZ; secondary diagnostic, not unique bakeUV'
    obj['MC_ClosedEndpointStatus'] = 'SEPARATELY_AUTHORED_ABSOLUTE_TARGET_REQUIRED; no stacked open delta'
    return {'object': name, 'before': before_fp, 'after': after_fp,
            'world_max_apply_error': float(np.max(np.abs(vertices(obj, True) - after))),
            'parent': obj.parent.name if obj.parent else None}


def export_obj(objects, path):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.hide_set(False)
        obj.select_set(True)
        obj.data.uv_layers.active_index = list(obj.data.uv_layers.keys()).index('UV0')
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.wm.obj_export(filepath=str(path), export_selected_objects=True,
                          forward_axis='Y', up_axis='Z', apply_modifiers=False,
                          export_uv=True, export_normals=True, export_materials=True,
                          export_triangulated_mesh=False)


def save_basis(objects, path):
    arrays, metadata = {}, []
    for obj in objects:
        record = mesh_arrays(obj)
        for key, value in record.items():
            arrays[obj.name + '__' + key] = value
        metadata.append({'object': obj.name, 'parent': obj.parent.name if obj.parent else None,
                         'matrix_world': [list(row) for row in obj.matrix_world],
                         'hashes': {key: array_sha(value) for key, value in record.items()}})
    np.savez_compressed(path, **arrays)
    return metadata


if not bpy.app.background:
    raise RuntimeError('Only an isolated source-authoring background process is allowed')
for source, expected in SOURCE_EXPECTED.items():
    if sha(source) != expected:
        raise RuntimeError('Immutable original changed: ' + source)
actual_receipt = json.loads(ACTUAL.read_text())
if sha(STAGE) != actual_receipt['coordinate_stage_sha256']:
    raise RuntimeError('Stage differs from the actually rendered/reviewed frozen v14 stage')
coordinates = np.load(STAGE)
for folder in (JAW_OUT, GUM_OUT):
    folder.mkdir(parents=True, exist_ok=True)
for target in (JAW_OUT / 'MC_DAY03_SourceHero_v14.blend', GUM_OUT / 'Gums_SourceLocked.blend'):
    if target.exists():
        raise RuntimeError('Refusing to overwrite an existing source branch: ' + str(target))
report = {'status': 'NEW_OPEN_SOURCE_BRANCHES_LOCAL_CONTACT_METHOD_APPROVED_NOT_FINAL_ART',
          'original_sources_before': SOURCE_EXPECTED, 'frozen_stage': str(STAGE),
          'frozen_stage_sha256': sha(STAGE), 'actual_proof': str(ACTUAL),
          'closed_pose': 'No closed corrective/key copied or created. Legacy shutter drivers, if present, are not final closed anatomy.',
          'UV0': 'Topology and unique layout retained; previous worldXZ-to-UV0 affine receipt explicitly STALE on changed objects.',
          'secondary_projection': 'Changed7 objects updated from actualOPENworldXZ, versionv14. Intentional front/back projection overlap is diagnostic only.',
          'mesh_maps': 'NO NEW BAKE; both endpoints/joints still need acceptance', 'branches': {}}

# Jaws: independent new source from V13, not the assembled scene.
bpy.ops.wm.open_mainfile(filepath=str(JAW_SOURCE))
root = bpy.data.objects['MC_JawsRoot']
root['OpenAmount'] = 1.
bpy.context.view_layer.update()
meshes = sorted([obj for obj in bpy.data.collections['MC_Jaws_Family'].all_objects
                 if obj and obj.type == 'MESH' and any(s.material and s.material.name == 'MC_Enamel' for s in obj.material_slots)], key=lambda obj: obj.name)
if len(meshes) != 32:
    raise RuntimeError('Expected32 authoritative source crowns')
before = {obj.name: fingerprints(obj) for obj in meshes}
changed = [name for name in actual_receipt['target_names'] if name != 'MC_DAY03_Upper_Gum']
patches = [apply_reviewed_open(bpy.data.objects[name], coordinates) for name in changed]
unchanged = {obj.name: fingerprints(obj) == before[obj.name] for obj in meshes if obj.name not in changed}
if len(unchanged) != 26 or not all(unchanged.values()):
    raise RuntimeError('An untouched source crown changed')
root['MC_OpenBasisVersion'] = 'v14_PHYSICAL_ROOT_CONTINUATION_OPEN_ONLY'
root['MC_ClosedEndpointStatus'] = 'UNAPPROVED_LEGACY_DRIVER; replace with separately authored absolute closed targets'
(JAW_OUT / 'Exports').mkdir(exist_ok=True)
jaw_obj = JAW_OUT / 'Exports/MC_DAY03_Full32_v14_Open_UV0.obj'
export_obj(meshes, jaw_obj)
jaw_basis = JAW_OUT / 'OpenBasis_v14_world_local_uv.npz'
jaw_metadata = save_basis(meshes, jaw_basis)
uv_manifest = json.loads((JAW_SOURCE.parent / 'uv_manifest.json').read_text())
uv_manifest['geometry_version'] = 'v14_OPEN_ROOT_CONTINUATION'
uv_manifest['frozen'] = False
uv_manifest['open_geometry_gate'] = 'Actual local cervical contact/source480columns approved; final whole look and closed joints pending'
uv_manifest['status'] = 'UV0_LAYOUT_RETAINED; SOURCE_AFFINE_STALE_ON_CHANGED6; SECONDARY_PROJECTION_REFRESHED; NO_NEW_BAKE'
uv_manifest['basis_v13'] = str(JAW_SOURCE)
uv_manifest['open_basis_npz'] = str(jaw_basis)
for part in uv_manifest['parts']:
    if part['object'] in changed:
        for key in ('source_xy_rect', 'source_pixel_to_uv_scale'):
            if key in part:
                part['v13_legacy_' + key] = part.pop(key)
        part['source_reference_xy_rect'] = part.get('v13_legacy_source_xy_rect')
        part['source_XZ_to_UV0_affine_valid'] = False
        part['source_transfer_policy'] = 'Actual updated mesh barycentric worldXYZ→UV0; legacy affine fields archival only'
        part['secondary_projection_version'] = 'v14_actual_OPEN_worldXZ'
(JAW_OUT / 'uv_manifest.json').write_text(json.dumps(uv_manifest, indent=2), encoding='utf8')
bpy.ops.wm.save_as_mainfile(filepath=str(JAW_OUT / 'MC_DAY03_SourceHero_v14.blend'))
report['branches']['jaws'] = {'blend': str(JAW_OUT / 'MC_DAY03_SourceHero_v14.blend'),
                             'blend_sha256': sha(JAW_OUT / 'MC_DAY03_SourceHero_v14.blend'),
                             'obj': str(jaw_obj), 'obj_sha256': sha(jaw_obj),
                             'open_basis_npz': str(jaw_basis), 'open_basis_npz_sha256': sha(jaw_basis),
                             'changed6': patches, 'unchanged26': unchanged, 'objects': jaw_metadata}
(JAW_OUT / 'sourcehash_report.json').write_text(json.dumps(report['branches']['jaws'], indent=2), encoding='utf8')
print('V14_JAW_SOURCE_SAVED', flush=True)

# Gums: independent V03 original, one local upper patch only.
bpy.ops.wm.open_mainfile(filepath=str(GUM_SOURCE))
bpy.context.view_layer.update()
upper, lower = bpy.data.objects['MC_DAY03_Upper_Gum'], bpy.data.objects['MC_DAY03_Lower_Gum']
lower_before = fingerprints(lower)
gum_patch = apply_reviewed_open(upper, coordinates)
if fingerprints(lower) != lower_before:
    raise RuntimeError('Original lowergum changed')
(GUM_OUT / 'Constraints').mkdir(exist_ok=True)
for side, obj in [('upper', upper), ('lower', lower)]:
    old = np.load(GUM_SOURCE.parent / 'Constraints' / (side + '_surface.npz'))
    arrays = {key: old[key] for key in old.files}
    if side == 'upper':
        arrays['vertices'] = vertices(obj)
        np.savez_compressed(GUM_OUT / 'Constraints/upper_surface.npz', **arrays)
    else:
        shutil.copy2(GUM_SOURCE.parent / 'Constraints/lower_surface.npz', GUM_OUT / 'Constraints/lower_surface.npz')
constraints = json.loads((GUM_SOURCE.parent / 'Constraints/source_locked_gums.json').read_text())
constraints.update({'schema': 'canonical_source_locked_gums.v4_OPEN_ROOT_WRAP',
                    'status': 'OPEN_LOCAL_CONTACT_GEOMETRY_APPROVED; CLOSED_ENDPOINT_PENDING; NO_NEW_BAKE',
                    'basis_v03': str(GUM_SOURCE), 'open_root_wrap_receipt': str(STAGE_ROOT / 'upper6_root_wrap_v14_preparation.json'),
                    'upper_margin_y_by_x_role': 'Legacy visible source ownership guide, NOT the physicalV04 solid rim',
                    'geometry_policy': 'Upper finite tissue continues around hidden tooth roots; visible cervical edge is compound occlusion, not a zero-thickness shared physical rim',
                    'source_projection_status': 'Upper secondaryUV refreshed fromactualOpenworldXZ; UV0source-affine assumptions stale'})
(GUM_OUT / 'Constraints/source_locked_gums.json').write_text(json.dumps(constraints, indent=2), encoding='utf8')
gum_obj = GUM_OUT / 'Gums_SourceLocked.obj'
export_obj([upper, lower], gum_obj)
gum_basis = GUM_OUT / 'OpenBasis_v04_world_local_uv.npz'
gum_metadata = save_basis([upper, lower], gum_basis)
bpy.ops.wm.save_as_mainfile(filepath=str(GUM_OUT / 'Gums_SourceLocked.blend'))
report['branches']['gums'] = {'blend': str(GUM_OUT / 'Gums_SourceLocked.blend'),
                             'blend_sha256': sha(GUM_OUT / 'Gums_SourceLocked.blend'),
                             'obj': str(gum_obj), 'obj_sha256': sha(gum_obj),
                             'open_basis_npz': str(gum_basis), 'open_basis_npz_sha256': sha(gum_basis),
                             'changed_upper': gum_patch, 'lower_unchanged': fingerprints(lower) == lower_before,
                             'objects': gum_metadata}
(GUM_OUT / 'OpenBasis_handoff.json').write_text(json.dumps(report['branches']['gums'], indent=2), encoding='utf8')
gum_metrics = {'revision': 'V04_OPEN_ROOT_WRAP',
               'status': 'OPEN_LOCAL_CONTACT_SUBCHECK_APPROVED; CLOSED_ENDPOINT_AND_FINAL_LOOK_PENDING',
               'actual_proof': str(STAGE_ROOT / 'Actual/actual_upper6_source_crossing_and_collar_review.json'),
               'parts': [{'name': obj.name, 'vertices': len(obj.data.vertices),
                          'polygons': len(obj.data.polygons),
                          'uv0_sha256': fingerprints(obj)['UV0'],
                          'topology_loop_indices_sha256': fingerprints(obj)['loop_vertex_indices'],
                          'world_bounds_min': vertices(obj, True).min(axis=0).tolist(),
                          'world_bounds_max': vertices(obj, True).max(axis=0).tolist()} for obj in (upper, lower)],
               'upper_surface_cache': str(GUM_OUT / 'Constraints/upper_surface.npz'),
               'lower_surface_cache': str(GUM_OUT / 'Constraints/lower_surface.npz'),
               'UV0': 'Unchanged unique layout; source-affine mapping stale after physical deformation',
               'secondary_projection': 'Upper refreshed actualOPENworldXZ versionv14',
               'mesh_maps': 'No new normal/AO/curvature bake; both endpoints pending'}
(GUM_OUT / 'GumsMetrics.json').write_text(json.dumps(gum_metrics, indent=2), encoding='utf8')
report['original_sources_after'] = {path: sha(path) for path in SOURCE_EXPECTED}
if report['original_sources_after'] != SOURCE_EXPECTED:
    raise RuntimeError('Immutable source changed during branch authoring')
for folder in (JAW_OUT, GUM_OUT):
    (folder / 'OpenRootWrap_source_branch_receipt.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('OPEN_V14_V04_SOURCE_BRANCHES_READY', json.dumps({key: value['blend'] for key, value in report['branches'].items()}), flush=True)
