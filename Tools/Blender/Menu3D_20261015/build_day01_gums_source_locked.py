"""Bounded isolated DAY01 gum authoring from frozen CPU physical-X/Z arrays.

SOURCE PREPARED ONLY until root grants the shared Blender/GPU execution slot.
No UI/live-session use, original source writes, generic-family substitution,
or claim that the unprovided Open view is source observed.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import os
import sys

for variable in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS'):
    os.environ[variable] = '2'

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

AR = Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
PREP = AR / 'Gums/DAY01/SourceClosed_v01'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prepared', default=str(PREP))
parser.add_argument('--crowns-blend', required=True)
parser.add_argument('--crowns-sha256', required=True)
parser.add_argument('--out', default=str(AR / 'Gums/DAY01/DCC_v01'))
parser.add_argument('--preview', action='store_true')
parser.add_argument('--device', choices=('cpu', 'optix'), default='cpu')
parser.add_argument('--samples', type=int, default=24)
opt = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
prepared = Path(opt.prepared).resolve()
source = Path(opt.crowns_blend).resolve()
out = Path(opt.out).resolve()
if not out.is_relative_to((AR / 'Gums/DAY01').resolve()) or out == prepared:
    raise RuntimeError('New candidate output must be a separate task-owned DAY01 gum directory')
if not 1 <= opt.samples <= 128:
    raise RuntimeError('Bounded preview samples must be between 1 and 128')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def mesh_digest(obj):
    """Authored basis, topology, all named UV layers and shape-key coordinates."""
    digest = hashlib.sha256()
    mesh = obj.data
    for items, prop, size, dtype in ((mesh.vertices, 'co', 3, np.float32),
                                    (mesh.loops, 'vertex_index', 1, np.int32),
                                    (mesh.polygons, 'loop_total', 1, np.int32)):
        values = np.empty(len(items) * size, dtype)
        items.foreach_get(prop, values)
        digest.update(values.tobytes())
    for layer in mesh.uv_layers:
        values = np.empty(len(mesh.loops) * 2, np.float32)
        layer.data.foreach_get('uv', values)
        digest.update(layer.name.encode('utf8'))
        digest.update(values.tobytes())
    if mesh.shape_keys:
        for key in mesh.shape_keys.key_blocks:
            values = np.empty(len(mesh.vertices) * 3, np.float32)
            key.data.foreach_get('co', values)
            digest.update(key.name.encode('utf8'))
            digest.update(values.tobytes())
    return digest.hexdigest()


def set_pose(value):
    control['OpenAmount'] = float(value)
    control.update_tag()
    scene.frame_set(42)
    bpy.context.view_layer.update()
    bpy.context.evaluated_depsgraph_get().update()


def evaluate(obj):
    graph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(graph)
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=graph)
    mesh.calc_loop_triangles()
    vertices = np.empty((len(mesh.vertices), 3), np.float32)
    mesh.vertices.foreach_get('co', vertices.ravel())
    matrix = np.asarray(evaluated.matrix_world, np.float64)
    world = vertices @ matrix[:3, :3].T + matrix[:3, 3]
    triangles = np.asarray([tuple(t.vertices) for t in mesh.loop_triangles], np.int32)
    loops = np.asarray([tuple(t.loops) for t in mesh.loop_triangles], np.int32)
    primary = mesh.uv_layers.get('UV0')
    if primary is None:
        raise RuntimeError('Required explicit unique UV0 missing: ' + obj.name)
    uv = np.empty((len(mesh.loops), 2), np.float32)
    primary.data.foreach_get('uv', uv.ravel())
    uv_triangles = uv[loops]
    a, b, c = (world[triangles[:, k]] for k in range(3))
    areas = np.linalg.norm(np.cross(b - a, c - a), axis=1) * .5
    guard = bmesh.new()
    guard.from_mesh(mesh)
    nonmanifold = sum(not e.is_manifold for e in guard.edges)
    loose = sum(not v.link_edges for v in guard.verts)
    local_volume = float(guard.calc_volume(signed=True))
    guard.free()
    facts = {'object': obj.name, 'vertices': len(world), 'triangles': len(triangles),
             'nonmanifold_edges': nonmanifold, 'loose_vertices': loose,
             'signed_volume_world': local_volume * float(np.linalg.det(matrix[:3, :3])),
             'minimum_triangle_area_world': float(areas.min()),
             'zero_area_triangles': int(np.count_nonzero(areas < 1e-10)),
             'finite': bool(np.isfinite(world).all()),
             'bounds_world': [world.min(0).tolist(), world.max(0).tolist()],
             'UV0_out_of_bounds_corners': int(np.count_nonzero(np.any((uv < 0) | (uv > 1), axis=1))),
             'uv_layer': primary.name,
             'uv_layers': [{'name': layer.name, 'active_render': layer.active_render}
                           for layer in mesh.uv_layers]}
    material_index = np.asarray([t.material_index for t in mesh.loop_triangles], np.int16)
    evaluated.to_mesh_clear()
    if nonmanifold or loose or facts['zero_area_triangles'] or not facts['finite'] \
            or facts['signed_volume_world'] <= 0 or facts['UV0_out_of_bounds_corners']:
        raise RuntimeError('Actual evaluated solid/UV gate failed: ' + json.dumps(facts))
    return {'world_vertices': world, 'triangles': triangles, 'UV0_triangles': uv_triangles,
            'tri_material_index': material_index, 'matrix_world': matrix}, facts


def principled(name, color, roughness):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.diffuse_color = (*color, 1.)
    shader = material.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = material.diffuse_color
    shader.inputs['Roughness'].default_value = roughness
    shader.inputs['IOR'].default_value = 1.43
    shader.inputs['Coat Weight'].default_value = 0.
    shader.inputs['Subsurface Weight'].default_value = 0.
    return material


def emission(name, color):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.node_tree.nodes.clear()
    node = material.node_tree.nodes.new('ShaderNodeEmission')
    node.inputs['Color'].default_value = (*color, 1.)
    output = material.node_tree.nodes.new('ShaderNodeOutputMaterial')
    material.node_tree.links.new(node.outputs[0], output.inputs['Surface'])
    return material


def camera(name, location, target, width, pixels):
    data = bpy.data.cameras.new(name)
    obj = bpy.data.objects.new(name, data)
    review.objects.link(obj)
    data.type = 'ORTHO'
    data.ortho_scale = width
    obj.location = location
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()
    scene.camera = obj
    scene.render.resolution_x, scene.render.resolution_y = pixels
    return obj


def area(name, position, target, energy, size):
    data = bpy.data.lights.new(name, 'AREA')
    obj = bpy.data.objects.new(name, data)
    review.objects.link(obj)
    data.energy, data.shape, data.size = energy, 'DISK', size
    obj.location = position
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()


print('BLENDER_DAY01_GUM_BUILD', bpy.app.version_string, flush=True)
if bpy.app.version < (5, 1, 0):
    raise RuntimeError('This isolated candidate is authored and reviewed for Blender 5.1+')
if sha(source) != opt.crowns_sha256.lower():
    raise RuntimeError('Saved crown source SHA differs from explicitly scheduled source')
contract_path = prepared / 'DCC_build_contract.json'
contract = json.loads(contract_path.read_text(encoding='utf8'))
prep_report = json.loads((prepared / 'geometry_preparation.json').read_text(encoding='utf8'))
if sha(Path(prep_report['source_image'])) != prep_report['source_sha256']:
    raise RuntimeError('Canonical source image changed after CPU constraints')
for side in ('upper', 'lower'):
    path = prepared / 'Constraints' / (side + '_surface.npz')
    if sha(path) != contract['constraints'][side]['sha256']:
        raise RuntimeError('Frozen CPU gum constraint hash changed: ' + side)
out.mkdir(parents=True, exist_ok=True)
(out / 'build_day01_gums_source_locked_executed.py').write_bytes(Path(__file__).read_bytes())
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
with bpy.data.libraries.load(str(source), link=False) as (library, selected):
    if 'MC_Jaws_DAY01' not in library.collections:
        raise RuntimeError('Scheduled source lacks MC_Jaws_DAY01 collection')
    selected.collections = ['MC_Jaws_DAY01']
crown_collection = selected.collections[0]
scene.collection.children.link(crown_collection)
control = bpy.data.objects.get('MC_JawsRoot_DAY01')
parents = {side: bpy.data.objects.get('MC_Jaw_DAY01_' + side.title())
           for side in ('upper', 'lower')}
if control is None or any(obj is None for obj in parents.values()) or 'OpenAmount' not in control:
    raise RuntimeError('Scheduled DAY01 source lacks its semantic control/parents')
control.animation_data_clear()
crowns = sorted([obj for obj in crown_collection.all_objects if obj.type == 'MESH'],
                key=lambda obj: obj.name)
if len(crowns) < 10:
    raise RuntimeError('Need all ten observed DAY01 upper crowns')
original_crown_hashes = {obj.name: mesh_digest(obj) for obj in crowns}
parent_checks = []
for amount in (0., 1.):
    set_pose(amount)
    for side, obj in parents.items():
        expected = np.eye(4)
        expected[2, 3] = (-1.65 if side == 'upper' else 1.65) * (1 - amount)
        error = float(np.abs(np.asarray(obj.matrix_world) - expected).max())
        if error > 3e-6 or np.abs(np.asarray(obj.matrix_parent_inverse) - np.eye(4)).max() > 1e-7:
            raise RuntimeError('Actual parent differs from DAY01 source contract: ' + side)
        parent_checks.append({'side': side, 'OpenAmount': amount, 'matrix_max_error': error})
set_pose(1.)
geometry = bpy.data.collections.new('MenuGingiva_DAY01_Geometry')
scene.collection.children.link(geometry)
review = bpy.data.collections.new('DAY01_Gum_ReviewOnly')
scene.collection.children.link(review)
gum_material = principled('MC_Gum', (.44, .023, .035), .38)
gum_material['Status'] = 'CONSTANT_PLACEHOLDER_NOT_REFERENCE_CALIBRATED'
gum_material['SourceDay'] = 'DAY01'
gum_objects = []
gum_arrays = {}
gum_authoring = []
for side in ('upper', 'lower'):
    data = np.load(prepared / 'Constraints' / (side + '_surface.npz'))
    basis, faces = data['basis_open'], data['faces']
    name = 'MC_DAY01_' + side.title() + '_Gum'
    mesh = bpy.data.meshes.new(name + '_Mesh')
    mesh.from_pydata(basis.tolist(), [], faces.tolist())
    mesh.update()
    if mesh.validate(clean_customdata=False):
        raise RuntimeError('Source topology required repair during authoring: ' + side)
    loop_vertices = np.empty(len(mesh.loops), np.int32)
    mesh.loops.foreach_get('vertex_index', loop_vertices)
    if not np.array_equal(loop_vertices, faces.ravel()):
        raise RuntimeError('Authored face corner order differs from CPU UV0 cache')
    uv0 = mesh.uv_layers.new(name='UV0')
    uv0.data.foreach_set('uv', data['UV0'].astype(np.float32).ravel())
    projection = mesh.uv_layers.new(name='UV_SourceProjection_Closed')
    projection.data.foreach_set('uv', data['UV_SourceProjection_Closed'][loop_vertices].ravel())
    mesh.uv_layers.active_index = 0
    uv0.active_render = True
    mesh.polygons.foreach_set('use_smooth', np.ones(len(mesh.polygons), bool))
    mesh.materials.append(gum_material)
    obj = bpy.data.objects.new(name, mesh)
    geometry.objects.link(obj)
    wrapper = bpy.data.objects.new('MC_Gingiva_DAY01_' + side.title(), None)
    geometry.objects.link(wrapper)
    wrapper.parent = parents[side]
    wrapper.matrix_parent_inverse = Matrix.Identity(4)
    wrapper.matrix_basis = Matrix.Identity(4)
    obj.parent = wrapper
    obj.matrix_parent_inverse = Matrix.Identity(4)
    obj.matrix_basis = Matrix.Identity(4)
    obj['SourceDay'], obj['SourceRow'] = 'DAY01', side
    obj['SourceConstraint'] = 'Closed source X/Z opaque ownership; physical root under tissue'
    obj['SourceDepth'] = 'INFERRED smooth physical X/Z harmonic/Poisson lens'
    obj['SourceOpenPolicy'] = 'UNPROVIDED; rigid inferred separation only'
    obj['UVPolicy'] = 'Unique DAY01 MC_Gum UV0 across upper/lower front/back; secondary sourceUV CLIP'
    gum_objects.append(obj)
    gum_arrays[side] = {key: data[key].copy() for key in data.files}
    gum_authoring.append({'object': obj.name, 'wrapper': wrapper.name, 'parent': parents[side].name,
                          'wrapper_parent_inverse_identity': True, 'wrapper_basis_identity': True,
                          'source_constraints_sha256': contract['constraints'][side]['sha256'],
                          'basis_topology_uv_hash': mesh_digest(obj)})

all_objects = crowns + gum_objects
packet, pose_facts, gum_endpoint_errors = {}, [], []
for amount in (0., .25, .5, .75, 1.):
    label = 'closed' if amount == 0 else 'open' if amount == 1 else 'sample' + f'{int(amount*100):03d}'
    set_pose(amount)
    for obj in all_objects:
        arrays, facts = evaluate(obj)
        facts['OpenAmount'] = amount
        pose_facts.append(facts)
        for key, value in arrays.items():
            packet[label + '::' + obj.name + '::' + key] = value
    for side, obj in zip(('upper', 'lower'), gum_objects):
        actual = packet[label + '::' + obj.name + '::world_vertices']
        expected = gum_arrays[side]['basis_open'].copy()
        expected[:, 2] += (-1.65 if side == 'upper' else 1.65) * (1 - amount)
        error = float(np.max(np.abs(actual - expected)))
        if error > 3e-6:
            raise RuntimeError('Actual DAY01 gum double transform/mismatched basis: ' + side)
        gum_endpoint_errors.append({'body': side, 'OpenAmount': amount, 'max_world_error': error})
np.savez_compressed(out / 'evaluated_pose_triangle_uv_packet.npz', **packet)
for obj in crowns:
    if mesh_digest(obj) != original_crown_hashes[obj.name]:
        raise RuntimeError('Gum authoring changed source crown mesh/UV/key: ' + obj.name)

scene.world = bpy.data.worlds.new('DAY01_Gum_ReviewWorld')
scene.world.use_nodes = True
background = scene.world.node_tree.nodes.get('Background')
background.inputs[0].default_value = (.022, .018, .022, 1.)
background.inputs[1].default_value = .22
area('DAY01_ClayKey', (-5, -7, 6), (0, 0, 0), 950, 7)
area('DAY01_ClayFill', (6, -5, 1), (0, 0, 0), 450, 6)
scene.render.engine = 'CYCLES'
scene.render.threads_mode, scene.render.threads = 'FIXED', 2
scene.cycles.device = 'CPU'
if opt.device == 'optix':
    preferences = bpy.context.preferences.addons['cycles'].preferences
    preferences.compute_device_type = 'OPTIX'
    preferences.get_devices()
    for device in preferences.devices:
        device.use = device.type == 'OPTIX'
    if not any(device.use for device in preferences.devices):
        raise RuntimeError('Granted OptiX preview requested but no enabled OptiX device')
    scene.cycles.device = 'GPU'
scene.cycles.samples, scene.cycles.use_denoising = opt.samples, True
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGB'
scene.render.film_transparent = False
scene.view_settings.view_transform = 'AgX'
gray = principled('DAY01_Clay_Linear018', (.18, .18, .18), .46)
rendered = []


def render(filename):
    scene.render.filepath = str(out / filename)
    bpy.ops.render.render(write_still=True)
    rendered.append(filename)


if opt.preview:
    scene.view_layers[0].material_override = gray
    for amount in (0., .25, .5, .75, 1.):
        set_pose(amount)
        pixels = (963, 212) if amount == 0 else (963, 410)
        camera('DAY01_ActualFront_' + f'{int(amount*100):03d}', (0, -35, 0), (0, 0, 0), 16, pixels)
        render('Open' + f'{int(amount*100):03d}' + '_ClayFront.png')
    set_pose(0.)
    camera('DAY01_ClosedThreeQuarter', (-12, -26, 8), (0, .4, 0), 20, (1000, 650))
    render('Closed_ClayThreeQuarter.png')
    camera('DAY01_ClosedNeckClose', (0, -35, .95), (0, 0, .95), 9.3, (1200, 430))
    render('Closed_UpperNeck_ClayClose.png')
    scene.view_layers[0].material_override = None
    old_slots = {obj.name: list(obj.data.materials) for obj in all_objects}
    black = emission('DAY01_SemanticBlack', (0, 0, 0))
    white = emission('DAY01_SemanticWhite', (1, 1, 1))
    upper_matte = emission('DAY01_UpperGumSemantic', (.02, 1, .45))
    lower_matte = emission('DAY01_LowerGumSemantic', (.02, .25, 1))
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0.
    scene.cycles.samples, scene.cycles.use_denoising = 1, False
    background.inputs[0].default_value = (0, 0, 0, 1)
    camera('DAY01_ClosedSemanticCamera', (0, -35, 0), (0, 0, 0), 16, (963, 212))
    for obj in crowns:
        obj.data.materials.clear()
        obj.data.materials.append(white)
    gum_objects[0].data.materials.clear()
    gum_objects[0].data.materials.append(upper_matte)
    gum_objects[1].data.materials.clear()
    gum_objects[1].data.materials.append(lower_matte)
    render('Closed_CompoundSemantic.png')
    for obj in all_objects:
        obj.data.materials.clear()
        row = str(obj.get('SourceRow', '')).lower()
        is_lower_crown = obj in crowns and (row == 'lower' or obj.name.startswith('MC_DAY01_L'))
        obj.data.materials.append(white if is_lower_crown else black)
    render('Closed_LowerEnamelMask.png')
    for obj in all_objects:
        obj.data.materials.clear()
        for material in old_slots[obj.name]:
            obj.data.materials.append(material)
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.exposure = 0.
    scene.cycles.samples, scene.cycles.use_denoising = opt.samples, True
    background.inputs[0].default_value = (.022, .018, .022, 1.)

set_pose(0.)
bpy.ops.object.select_all(action='DESELECT')
for obj in gum_objects:
    obj.select_set(True)
bpy.context.view_layer.objects.active = gum_objects[0]
bpy.ops.wm.obj_export(filepath=str(out / 'DAY01_Gums_Closed_UV0_REVIEW.obj'),
                      export_selected_objects=True, forward_axis='Y', up_axis='Z',
                      apply_modifiers=True, export_materials=True)
set_pose(1.)
camera('DAY01_InferredOpen_DefaultCamera', (0, -35, 0), (0, 0, 0), 16, (963, 410))
blend_path = out / 'DAY01_Gums_SourceLocked_Candidate.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
report = {'status': 'ACTUAL_LOCAL_GEOMETRY_CANDIDATE_AWAITING_RENDER_AND_SOURCE_REVIEW',
          'blender': bpy.app.version_string, 'blend': str(blend_path), 'blend_sha256': sha(blend_path),
          'source_crowns': str(source), 'source_crowns_sha256': opt.crowns_sha256.lower(),
          'source_crowns_unchanged': sha(source) == opt.crowns_sha256.lower(),
          'source_gum_constraints': str(prepared), 'constraints_contract_sha256': sha(contract_path),
          'gum_authoring': gum_authoring, 'parent_endpoint_checks': parent_checks,
          'actual_pose_solid_UV_gates': pose_facts, 'gum_actual_pose_basis_errors': gum_endpoint_errors,
          'all_crown_basis_topology_UV_shape_keys_unchanged': original_crown_hashes,
          'transfer_packet': str(out / 'evaluated_pose_triangle_uv_packet.npz'),
          'transfer_packet_sha256': sha(out / 'evaluated_pose_triangle_uv_packet.npz'),
          'transfer_policy': 'Actual evaluated world XYZ/loop triangles, explicit named UV0; five poses',
          'actual_packet_objects': [obj.name for obj in all_objects],
          'camera': {'closed': {'width': 16, 'pixels': [963, 212]},
                     'open_inferred': {'width': 16, 'pixels': [963, 410]}, 'center': [0, 0, 0]},
          'rendered': rendered, 'render_device': scene.cycles.device,
          'cpu_threads': 2, 'samples': opt.samples,
          'limits': ['Only Closed DAY01 source supplied; Open shape, backs and all Y are inferred.',
                     'Placeholder material and isolated light are not a calibrated PBR appearance.',
                     'Compound source ownership, actual lower-white mask and UV raster audit still need review.',
                     'No runtime/export-driver equivalence or final 1:1 appearance acceptance is claimed.']}
(out / 'actual_day01_gum_candidate_report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
if not report['source_crowns_unchanged']:
    raise RuntimeError('Original crown source unexpectedly changed')
print('ACTUAL_DAY01_GUM_CANDIDATE_COMPLETE', str(blend_path),
      'solid_pose_gates', len(pose_facts), 'preview_frames', len(rendered), flush=True)
