"""Explicit functional blocking only: legacy DAY01/02 to native world morphs.

Run only in a granted isolated background Blender slot. No rendering, source
authoring, radiance, character conversion, or final-art/1:1 acceptance claim.
Blender coordinate unit stays 1; native FBX metadata handles centimeters.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys
import bpy
import numpy as np

AR = Path(r'E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1')
SOURCE = AR/'Jaws/MC_Jaws_Family.blend'
EXPECTED_SHA = '17ed2fd4e9f0ed2360c5184eac0ed49ca6061bfe8394f71fb0515c67722ac00d'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', default=str(AR/'Assembly/UnrealHandoff/FunctionalBlocking_DAY01_DAY02_v01'))
opt = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
OUT = Path(opt.out).resolve()
if not bpy.app.background or not OUT.is_relative_to(AR/'Assembly/UnrealHandoff'):
    raise RuntimeError('Isolated background and task-owned native handoff output required')
if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != EXPECTED_SHA:
    raise RuntimeError('Legacy blocking source SHA changed')
if any((OUT/('SK_'+day+'_Blocking.fbx')).exists() for day in ('DAY01', 'DAY02')):
    raise RuntimeError('Use a fresh native handoff revision')
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'export_legacy_family_blocking_executed.py').write_bytes(Path(__file__).read_bytes())
MATERIALS = ['MC_Enamel', 'MC_Gum']
MORPHS = [(0., 'Pose_000'), (.25, 'Pose_025'), (.5, 'Pose_050'), (.75, 'Pose_075')]
variants = []
world_archive = {}

for day, kind, count, expected_triangles in [('DAY01', 'Human', 22, 85440), ('DAY02', 'Predator', 23, 87840)]:
    print('BLOCKING_BEGIN', day, flush=True)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene = bpy.context.scene
    scene.render.threads_mode, scene.render.threads = 'FIXED', 2
    control = bpy.data.objects['MC_JawsRoot']
    control.animation_data_clear()
    collection_name = f'MC_Jaws_{day}_{kind}'
    source_collection = bpy.data.collections[collection_name]
    objects = sorted([obj for obj in source_collection.all_objects if obj.type == 'MESH'], key=lambda obj: obj.name)
    if len(objects) != count or any(not obj.name.startswith('MC_'+day+'_') for obj in objects):
        raise RuntimeError('Legacy variant membership/count differs from saved reports')
    for obj in objects:
        obj.hide_set(False)
        obj.hide_viewport, obj.hide_render = False, False
    poses, faces, uv_loops, mat_indices, records, pose_gates = {}, [], [], [], [], []
    original_topology = {}
    for amount in (1., 0., .25, .5, .75):
        control['OpenAmount'] = amount
        control.update_tag()
        scene.frame_set(42)
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        dg.update()
        parts = []
        offset = 0
        total_triangles = 0
        for obj in objects:
            ev = obj.evaluated_get(dg)
            mesh = ev.to_mesh(preserve_all_data_layers=True, depsgraph=dg)
            mesh.calc_loop_triangles()
            coordinates = np.empty((len(mesh.vertices), 3), np.float32)
            mesh.vertices.foreach_get('co', coordinates.ravel())
            matrix = np.asarray(ev.matrix_world)
            world = coordinates@matrix[:3, :3].T+matrix[:3, 3]
            if not np.isfinite(world).all():
                raise RuntimeError('Nonfinite evaluated blocking pose')
            native = world.copy()
            native[:, 1] *= -1.
            parts.append(native.astype(np.float32))
            loop_vertices = np.empty(len(mesh.loops), np.int32)
            mesh.loops.foreach_get('vertex_index', loop_vertices)
            counts = np.empty(len(mesh.polygons), np.int32)
            mesh.polygons.foreach_get('loop_total', counts)
            signature = hashlib.sha256(loop_vertices.tobytes()+counts.tobytes()).hexdigest()
            triangles = np.asarray([tuple(t.vertices) for t in mesh.loop_triangles], np.int32)
            q = world[triangles]
            area = np.linalg.norm(np.cross(q[:, 1]-q[:, 0], q[:, 2]-q[:, 0]), axis=1)*.5
            volume = float(np.einsum('ij,ij->i', q[:, 0], np.cross(q[:, 1], q[:, 2])).sum()/6)
            if volume <= 0 or np.any(area < 1e-10):
                raise RuntimeError('Invalid actual legacy blocking solid: '+obj.name)
            total_triangles += len(triangles)
            pose_gates.append({'object': obj.name, 'OpenAmount': amount, 'vertices': len(world),
                               'triangles': len(triangles), 'positive_volume': volume, 'minimum_area': float(area.min())})
            if amount == 1.:
                original_topology[obj.name] = signature
                primary = mesh.uv_layers.get('UV0')
                if primary is None:
                    raise RuntimeError('Unique UV0 missing: '+obj.name)
                uv = np.empty((len(mesh.loops), 2), np.float32)
                primary.data.foreach_get('uv', uv.ravel())
                if np.any((uv < 0) | (uv > 1)):
                    raise RuntimeError('Out-of-bounds UV0 in legacy blocking')
                sizes = np.cumsum(counts)[:-1]
                polygons = np.split(loop_vertices+offset, sizes)
                uv_polygons = np.split(uv, sizes)
                # Y reflection has negative determinant. Reverse the geometry
                # and matching face-corner UV order together, exactly once.
                faces.extend(tuple(row[::-1]) for row in polygons)
                uv_loops.extend(np.concatenate([row[::-1] for row in uv_polygons]))
                slots = np.empty(len(mesh.polygons), np.int32)
                mesh.polygons.foreach_get('material_index', slots)
                slot_map = [MATERIALS.index(mat.name.split('.')[0]) for mat in mesh.materials]
                mat_indices.extend(np.asarray(slot_map, np.int32)[slots].tolist())
                records.append({'mesh': obj.name, 'vertex_start': offset, 'vertex_count': len(world),
                                'source_parent': obj.parent.name if obj.parent else None})
            elif original_topology[obj.name] != signature:
                raise RuntimeError('Pose changed blocking topology/order: '+obj.name)
            offset += len(world)
            ev.to_mesh_clear()
        if total_triangles != expected_triangles:
            raise RuntimeError('Evaluated legacy count differs from authored report: '+str(total_triangles))
        poses[amount] = np.concatenate(parts)
        world_archive[day+'__'+str(amount)] = poses[amount]
        print('BLOCKING_SAMPLED', day, amount, 'vertices', len(poses[amount]), flush=True)
    if any(value.shape != poses[1.].shape for value in poses.values()):
        raise RuntimeError('Blocking morph vertex correspondence changed')

    bpy.ops.wm.read_factory_settings(use_empty=True)
    mesh = bpy.data.meshes.new('MC_'+day+'_BlockingSurface')
    mesh.from_pydata(poses[1.].tolist(), [], faces)
    mesh.update()
    obj = bpy.data.objects.new('SK_'+day+'_Blocking', mesh)
    bpy.context.scene.collection.objects.link(obj)
    for name in MATERIALS:
        mesh.materials.append(bpy.data.materials.new(name))
    mesh.polygons.foreach_set('material_index', np.asarray(mat_indices, np.int32))
    mesh.polygons.foreach_set('use_smooth', np.ones(len(mesh.polygons), bool))
    uv = mesh.uv_layers.new(name='UV0')
    uv.data.foreach_set('uv', np.asarray(uv_loops, np.float32).ravel())
    uv.active_render = True
    obj.shape_key_add(name='Basis')
    for amount, name in MORPHS:
        key = obj.shape_key_add(name=name)
        key.data.foreach_set('co', poses[amount].ravel())
        key.value = 0.
    armature = bpy.data.armatures.new('MC_MenuMouth_Skeleton')
    rig = bpy.data.objects.new('MC_MenuMouth_Rig', armature)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bone = armature.edit_bones.new('root')
    bone.head, bone.tail = (0., 0., 0.), (0., 0., 100.)
    bpy.ops.object.mode_set(mode='OBJECT')
    group = obj.vertex_groups.new(name='root')
    group.add(list(range(len(mesh.vertices))), 1., 'REPLACE')
    modifier = obj.modifiers.new('RootSkin', 'ARMATURE')
    modifier.object = rig
    obj.parent = rig
    bpy.context.scene.unit_settings.system = 'NONE'
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    blend = OUT/('MC_'+day+'_BlockingMorphSource.blend')
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    path = OUT/('SK_'+day+'_Blocking.fbx')
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True,
        object_types={'MESH', 'ARMATURE'}, use_mesh_modifiers=False,
        add_leaf_bones=False, use_armature_deform_only=True,
        bake_anim=False, path_mode='STRIP', axis_forward='-Y', axis_up='Z',
        apply_unit_scale=False, apply_scale_options='FBX_SCALE_NONE',
        mesh_smooth_type='FACE', use_tspace=False)
    variants.append({'day': day, 'kind': kind, 'functional_blocking_only': True,
        'source_collection': collection_name, 'source_root': 'MC_JawsRoot[OpenAmount]',
        'source_variant_root': collection_name+'_Root', 'source_parents': ['MC_Jaw_'+day+'_Upper', 'MC_Jaw_'+day+'_Lower'],
        'source_mesh_count': count, 'vertices': len(mesh.vertices), 'triangles': expected_triangles,
        'parts': records, 'actual_pose_solid_gates': pose_gates,
        'blend': str(blend), 'blend_sha256': hashlib.sha256(blend.read_bytes()).hexdigest(),
        'fbx': str(path), 'fbx_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'materials': MATERIALS, 'UV0_first_only': True, 'root_bone': 'root',
        'morphs': [name for amount, name in MORPHS],
        'Open_basis_bounds': [poses[1.].min(0).tolist(), poses[1.].max(0).tolist()],
        'Closed_bounds': [poses[0.].min(0).tolist(), poses[0.].max(0).tolist()]})
    print('BLOCKING_EXPORTED', day, str(path), flush=True)

np.savez_compressed(OUT/'source_world_poses_BlenderUnits.npz', **world_archive)
report = {'status': 'FUNCTIONAL_BLOCKING_ONLY_NOT_FINAL_ART_NOT_SOURCE_1TO1',
    'source': str(SOURCE), 'source_sha256': EXPECTED_SHA,
    'source_unchanged': hashlib.sha256(SOURCE.read_bytes()).hexdigest() == EXPECTED_SHA,
    'source_equivalence': 'Exact archived rejected_v03 family, explicitly reused solely for functional opening/transitions.',
    'variants': variants, 'native_y_flip': True, 'polygon_and_UV_loop_winding_reversed': True,
    'source_coordinate_to_native_cm': 100., 'vertices_not_premultiplied_by100': True,
    'unit_and_FBX_flags': 'Same as current successful DAY03 exporter: scene NONE, apply_unit_scale=False, FBX_SCALE_NONE, axes -Y/Z.',
    'runtime_morph_rule': 'w_i=max(0,1-abs(OpenAmount-sample_i)/.25), samples0,.25,.5,.75; Open1 is Basis/allzero.',
    'rendering_performed': False, 'character_assets_touched': False,
    'native_import_required': 'Fresh FBX options convert_scene=False, import_morph_targets=True; root owns UE import and actual gray perspective behavior review.',
    'shared_interior': 'Existing native DAY03 Interior/Tongue may be reused as explicit blocking; no new static meshes exported.',
    'limits': ['No fidelity, physical-material, final-art or UE/runtime acceptance claim.',
               'Legacy DAY02 has21 blades, not the later23 source-locked crowns; intentionally functional blocking.']}
if not report['source_unchanged']:
    raise RuntimeError('Source was modified')
(OUT/'functional_blocking_family_handoff.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('FUNCTIONAL_BLOCKING_FAMILY_READY', str(OUT), flush=True)
