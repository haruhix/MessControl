"""Bake an authored menu cleaning pose onto the existing 107-bone player rig.

Run INSIDE Blender with MenuScene_Work.blend open. This module never opens or
saves a .blend, renders, starts Blender, or imports anything into Unreal.

MCP example (the caller owns authoring and execution):
    runpy.run_path(script_path, run_name='__main__', init_globals={
        'MC_MENU_EXPORT_OPTIONS': {'output_dir': 'E:/CodexWork/MyExport'}})
Or import this module and call export_menu_cleaning({...}).

Source pose matrices are ALREADY in canonical centimetre bone coordinates.
There is deliberately NO artist-rig translation *= 100 conversion here.
Brush JSON contains Blender WORLD METRES, not FBX or Unreal transforms.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import traceback

import bpy
from bpy_extras.io_utils import axis_conversion
from mathutils import Matrix


PROJECT = Path(__file__).resolve().parents[2]
DEFAULTS = {
    'source_rig': 'RIG_Cleaner',
    'source_brush': 'GEO_Cleaner_ExistingBrush',
    'source_mesh': 'GEO_Cleaner_ExistingCharacter',
    'staging_root': 'ROOT_Cleaner',
    'canonical_blend': str(PROJECT / 'ArtSource/CharacterCurrent/CharacterGameplay.blend'),
    'canonical_object': 'root',
    'output_dir': str(PROJECT / 'ArtSource/UI/MenuSceneV2_20261022/Blender/Exports'),
    'clip_name': 'A_Menu_CleanTooth_Loop_V2',
    'frame_start': 1, 'frame_end': 289, 'fps': 24.0,
    'expected_duration_seconds': 12.0,
    'expected_bones': 107,
    'rest_matrix_tolerance': 1e-6,
    'max_bake_error_cm': 0.01,
    'max_bake_rotation_error_deg': 0.02,
    'require_identity_staging_root': True,
    'require_constant_rig_world': True,
    'require_arm_motion': True,
    'keep_baked_action': True,
    'keep_export_clone_hidden': False,
    'max_sole_samples_per_foot': 32,
}
FOOT_BONES = ('foot_l', 'toes_01_l', 'foot_r', 'toes_01_r')


def _matrix_rows(matrix):
    return [[float(value) for value in row] for row in matrix]


def _matrix_error(a, b):
    return max(abs(a[row][col] - b[row][col]) for row in range(4) for col in range(4))


def _delta(a, b):
    la, qa, sa = a.decompose()
    lb, qb, sb = b.decompose()
    # atan2 avoids an acos(dot) float-precision floor of about .04 degrees.
    # abs(w) treats q and -q as the same physical orientation.
    difference = qa.normalized().conjugated() @ qb.normalized()
    angle = math.degrees(2 * math.atan2(math.sqrt(difference.x*difference.x+difference.y*difference.y+difference.z*difference.z), abs(difference.w)))
    return {'translation_cm': float((la - lb).length),
            'rotation_deg': angle, 'scale_max_abs': max(abs(sa[i] - sb[i]) for i in range(3))}


def _sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def _json(path, value):
    temporary = path.with_name(path.name + '.writing')
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf8')
    os.replace(temporary, path)


def _ordered_bones(armature):
    ordered = []

    def visit(bone):
        ordered.append(bone)
        for child in bone.children:
            visit(child)

    for bone in armature.bones:
        if bone.parent is None:
            visit(bone)
    return ordered


def _linear_keys(action):
    # Blender 4.4+/5.x stores curves in action channel bags.
    curves = []
    if hasattr(action, 'layers'):
        for layer in action.layers:
            for strip in layer.strips:
                for slot in action.slots:
                    bag = strip.channelbag(slot)
                    if bag:
                        curves.extend(bag.fcurves)
    elif hasattr(action, 'fcurves'):
        curves.extend(action.fcurves)
    for curve in curves:
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'


def _sole_samples(mesh, rig, depsgraph, limit):
    """Find a low band of weighted sole vertices; no floor/collision inference."""
    if mesh is None or mesh.type != 'MESH':
        return {}, 'Source mesh unavailable; bone footprints only.'
    if not any(mod.type == 'ARMATURE' and mod.object == rig for mod in mesh.modifiers):
        return {}, 'Source mesh is not skinned to the selected rig; bone footprints only.'
    evaluated = mesh.evaluated_get(depsgraph)
    data = evaluated.to_mesh()
    try:
        if len(data.vertices) != len(mesh.data.vertices):
            return {}, 'Topology-changing modifiers prevent source vertex-index sole sampling.'
        groups = {group.index: group.name for group in mesh.vertex_groups}
        result = {}
        for side in ('l', 'r'):
            wanted = {'foot_' + side, 'toes_01_' + side}
            candidates = []
            for vertex in mesh.data.vertices:
                weight = sum(item.weight for item in vertex.groups if groups.get(item.group) in wanted)
                if weight >= 0.25:
                    world = evaluated.matrix_world @ data.vertices[vertex.index].co
                    candidates.append((vertex.index, world.z))
            if candidates:
                floor = min(z for _, z in candidates)
                ids = sorted(index for index, z in candidates if z <= floor + 0.015)
                count = min(limit, len(ids))
                result[side] = sorted({ids[round(i * (len(ids) - 1) / max(1, count - 1))]
                                       for i in range(count)})
        return result, None
    finally:
        evaluated.to_mesh_clear()


def export_menu_cleaning(options=None):
    """Export evaluated animation; restore frame/selection and remove own clone.

    The source action, rig, rest bones, constraints, mesh and controls are not
    edited. Only a temporary appended canonical rig gets baked keys. By default
    its unlinked Action is retained with fake-user for the caller to inspect.
    """
    cfg = dict(DEFAULTS)
    if options:
        unknown = set(options) - set(cfg)
        if unknown:
            raise ValueError('Unknown export options: ' + ', '.join(sorted(unknown)))
        cfg.update(options)
    out = Path(cfg['output_dir']).resolve()
    if out.drive.upper() != 'E:':
        raise ValueError('Export outputs must be on E:, got ' + str(out))
    clip = cfg['clip_name']
    if not clip or Path(clip).name != clip or any(char in clip for char in '/\\:'):
        raise ValueError('clip_name must be one plain asset name')
    out.mkdir(parents=True, exist_ok=True)
    fbx_path = out / (clip + '.fbx')
    brush_path = out / (clip + '_BrushWorld.json')
    report_path = out / (clip + '_BakeReport.json')
    pending_fbx = out / (clip + '.pending.fbx')
    scene, view = bpy.context.scene, bpy.context.view_layer
    start, end = int(cfg['frame_start']), int(cfg['frame_end'])
    fps = scene.render.fps / scene.render.fps_base
    duration = (end - start) / fps
    if end <= start or abs(fps - cfg['fps']) > 1e-6:
        raise ValueError(f'Expected {cfg["fps"]} fps and an increasing frame range, got {fps}, {start}..{end}')
    if abs(duration - cfg['expected_duration_seconds']) > 1e-6:
        raise ValueError(f'Clip duration is {duration}s, expected {cfg["expected_duration_seconds"]}s')
    if bpy.context.mode not in {'OBJECT', 'POSE'}:
        raise RuntimeError('Finish Edit/Sculpt/etc. mode before exporting. Only Object/Pose is supported.')
    source = bpy.data.objects.get(cfg['source_rig'])
    brush = bpy.data.objects.get(cfg['source_brush'])
    if source is None or source.type != 'ARMATURE' or brush is None:
        raise ValueError('The configured source armature and brush must exist in the live scene.')
    canonical = Path(cfg['canonical_blend']).resolve()
    if not canonical.is_file():
        raise FileNotFoundError(canonical)
    if bpy.data.objects.get(cfg['canonical_object']) is not None:
        raise RuntimeError('Object name "root" is occupied. Preserve it and resolve explicitly; no automatic rename.')
    staging = bpy.data.objects.get(cfg['staging_root'])
    if cfg['require_identity_staging_root']:
        if staging is None or _matrix_error(staging.matrix_world, Matrix.Identity(4)) > 1e-6:
            raise RuntimeError('Expected ROOT_Cleaner identity; bake refuses an unexpected staging transform.')
    old = {'frame': scene.frame_current, 'subframe': scene.frame_subframe,
           'start': scene.frame_start, 'end': scene.frame_end,
           'selected': list(bpy.context.selected_objects), 'active': view.objects.active,
           'mode': bpy.context.mode}
    report = {'status': 'running', 'source_blend': bpy.data.filepath,
              'source_disk_sha256': _sha(bpy.data.filepath) if bpy.data.filepath and Path(bpy.data.filepath).is_file() else None,
              'source_live_unsaved_changes': bool(bpy.data.is_dirty),
              'source_rig': source.name, 'source_action': source.animation_data.action.name
              if source.animation_data and source.animation_data.action else None,
              'canonical_blend': str(canonical), 'canonical_sha256': _sha(canonical),
              'settings': cfg, 'fps': fps, 'frames': [start, end], 'sample_count': end - start + 1,
              'duration_seconds': duration, 'bone_translation_multiplier': 1.0,
              'scene_unit_scale_length': scene.unit_settings.scale_length,
              'units': 'Pose/rest coordinates: canonical cm. Source world and BrushWorld JSON: Blender metres.',
              'warnings': [], 'limitations': [
                  'Footprint drift is measured against frame 1, not floor collision or sole compression.',
                  'No rendered QA, FBX fresh import, Unreal AnimSequence import or runtime validation is performed.',
                  'Only a bone action is exported; facial shape keys/materials are not animated by this utility.',
                  'On-disk source hash does not represent unsaved live authoring; source is never saved here.']}
    target = collection = baked = None
    target_data = None
    success = False
    try:
        if old['mode'] == 'POSE':
            bpy.ops.object.mode_set(mode='OBJECT')
        collection = bpy.data.collections.new('MC_MenuCleaning_ExportOnly')
        scene.collection.children.link(collection)
        with bpy.data.libraries.load(str(canonical), link=False) as (available, loaded):
            if cfg['canonical_object'] not in available.objects:
                raise RuntimeError('Canonical .blend has no object ' + cfg['canonical_object'])
            loaded.objects = [cfg['canonical_object']]
        target = loaded.objects[0]
        if target is None or target.type != 'ARMATURE' or target.name != cfg['canonical_object']:
            raise RuntimeError('Could not append the exact canonical root armature.')
        if target.data.users > 1:
            # Never edit a shared source rig's rest/connect flags, even if the
            # library loader reused a dependency already present in this file.
            target.data = target.data.copy()
        target_data = target.data
        collection.objects.link(target)
        view.update()  # Appended object's matrix_world is not reliable until evaluated.
        canonical_world = target.matrix_world.copy()
        target.animation_data_clear()
        target.parent = None
        target.matrix_world = canonical_world
        for constraint in list(target.constraints):
            target.constraints.remove(constraint)
        if any(abs(value - 0.01) > 1e-6 for value in canonical_world.to_scale()):
            raise RuntimeError('Canonical armature world scale differs from .01; refusing a unit guess.')
        source_names = set(source.data.bones.keys())
        target_names = set(target.data.bones.keys())
        if source_names != target_names or len(target_names) != cfg['expected_bones']:
            raise RuntimeError('Canonical/source 107-bone names differ: ' + str({
                'source_only': sorted(source_names - target_names), 'target_only': sorted(target_names - source_names)}))
        maximum_rest = 0.0
        for bone in target.data.bones:
            other = source.data.bones[bone.name]
            if (bone.parent.name if bone.parent else None) != (other.parent.name if other.parent else None):
                raise RuntimeError('Parent mismatch at ' + bone.name)
            maximum_rest = max(maximum_rest, _matrix_error(bone.matrix_local, other.matrix_local))
        if maximum_rest > cfg['rest_matrix_tolerance']:
            raise RuntimeError(f'Canonical rest matrices differ: {maximum_rest}')
        report['rest_comparison'] = {'bones': len(target_names), 'max_matrix_element_error': maximum_rest,
                                     'names_and_parent_hierarchy_equal': True}
        bpy.ops.object.select_all(action='DESELECT')
        target.hide_viewport = target.hide_render = False
        target.hide_set(False)
        target.select_set(True)
        view.objects.active = target
        # Match the established character exporter: allow exact local location
        # tracks even when a copied rest bone has Blender's connected flag.
        bpy.ops.object.mode_set(mode='EDIT')
        for bone in target.data.edit_bones:
            bone.use_connect = False
        bpy.ops.object.mode_set(mode='OBJECT')
        for pb in target.pose.bones:
            for constraint in list(pb.constraints):
                pb.constraints.remove(constraint)
            pb.rotation_mode = 'QUATERNION'
            pb.matrix_basis = Matrix.Identity(4)
        target.animation_data_create()
        baked = bpy.data.actions.new(clip + '_ExportBake')
        target.animation_data.action = baked
        order = _ordered_bones(target.data)
        scene.frame_start, scene.frame_end = start, end
        scene.frame_set(start)
        view.update()
        depsgraph = bpy.context.evaluated_depsgraph_get()
        initial_rig_world = source.evaluated_get(depsgraph).matrix_world.copy()
        if any(abs(value - 0.01) > 1e-6 for value in initial_rig_world.to_scale()):
            raise RuntimeError('Source rig world scale differs from .01; source centimetre-unit assumption is invalid.')
        report['source_world_matrix_m'] = _matrix_rows(initial_rig_world)
        report['canonical_world_matrix_m'] = _matrix_rows(canonical_world)
        report['stage_placement_policy'] = 'Constant source object/stage placement is excluded from bone tracks; canonical root object stays fixed.'
        source_mesh = bpy.data.objects.get(cfg['source_mesh'])
        sole_ids, warning = _sole_samples(source_mesh, source, depsgraph, int(cfg['max_sole_samples_per_foot']))
        if warning:
            report['warnings'].append(warning)
        footprint = {name: {'head_world_cm': [], 'tail_world_cm': []} for name in FOOT_BONES if name in source_names}
        sole_tracks = {side: [] for side in sole_ids}
        brush_frames = []
        first = last = None
        previous = {}
        motion = {name: {'translation_cm': 0.0, 'rotation_deg': 0.0, 'scale_max_abs': 0.0} for name in target_names}
        max_error = max_angle = max_matrix = max_world_drift = 0.0
        for frame in range(start, end + 1):
            scene.frame_set(frame)
            view.update()
            evaluated = source.evaluated_get(depsgraph)
            max_world_drift = max(max_world_drift, _matrix_error(evaluated.matrix_world, initial_rig_world))
            desired = {name: evaluated.pose.bones[name].matrix.copy() for name in target_names}
            if first is None:
                first = {name: matrix.copy() for name, matrix in desired.items()}
            actual = {}
            for bone in order:
                pb = target.pose.bones[bone.name]
                parent = actual[bone.parent.name] if bone.parent else Matrix.Identity(4)
                parent_rest = bone.parent.matrix_local if bone.parent else Matrix.Identity(4)
                pb.matrix_basis = bone.convert_local_to_pose(desired[bone.name], bone.matrix_local,
                    parent_matrix=parent, parent_matrix_local=parent_rest, invert=True)
                if bone.name in previous and pb.rotation_quaternion.dot(previous[bone.name]) < 0:
                    pb.rotation_quaternion.negate()
                previous[bone.name] = pb.rotation_quaternion.copy()
                actual[bone.name] = bone.convert_local_to_pose(pb.matrix_basis, bone.matrix_local,
                    parent_matrix=parent, parent_matrix_local=parent_rest)
                for prop in ('location', 'rotation_quaternion', 'scale'):
                    pb.keyframe_insert(data_path=prop, frame=frame, group=bone.name)
                delta = _delta(first[bone.name], desired[bone.name])
                for key in delta:
                    motion[bone.name][key] = max(motion[bone.name][key], delta[key])
            target.update_tag()
            view.update()
            target_eval = target.evaluated_get(depsgraph)
            for name in target_names:
                reconstructed = target_eval.pose.bones[name].matrix
                error = _delta(reconstructed, desired[name])
                max_error = max(max_error, error['translation_cm'])
                max_angle = max(max_angle, error['rotation_deg'])
                max_matrix = max(max_matrix, _matrix_error(reconstructed, desired[name]))
            for name, tracks in footprint.items():
                pb = evaluated.pose.bones[name]
                tracks['head_world_cm'].append(list((evaluated.matrix_world @ pb.head) * 100.0))
                tracks['tail_world_cm'].append(list((evaluated.matrix_world @ pb.tail) * 100.0))
            if sole_ids:
                skin = source_mesh.evaluated_get(depsgraph)
                data = skin.to_mesh()
                try:
                    for side, indices in sole_ids.items():
                        sole_tracks[side].append([list((skin.matrix_world @ data.vertices[index].co) * 100.0) for index in indices])
                finally:
                    skin.to_mesh_clear()
            brush_world = brush.evaluated_get(depsgraph).matrix_world.copy()
            loc, quat, scale = brush_world.decompose()
            hand_relative = {name: _matrix_rows((evaluated.matrix_world @ desired[name]).inverted() @ brush_world)
                             for name in ('hand_l', 'hand_r') if name in desired}
            brush_frames.append({'frame': frame, 'time_seconds': (frame - start) / fps,
                'matrix_world_m': _matrix_rows(brush_world), 'location_m': list(loc),
                'quaternion_wxyz': list(quat), 'scale': list(scale),
                'hand_to_brush_matrices': hand_relative})
            last = desired
        report['bake_validation'] = {'max_evaluated_endpoint_error_cm': max_error,
            'max_evaluated_rotation_error_deg': max_angle, 'max_matrix_element_error': max_matrix,
            'max_source_world_matrix_drift': max_world_drift, 'per_bone_motion_from_first_frame': motion}
        report['loop_seam'] = {name: _delta(first[name], last[name]) for name in sorted(target_names)}
        report['loop_seam_summary'] = {key: max(values[key] for values in report['loop_seam'].values())
                                       for key in ('translation_cm', 'rotation_deg', 'scale_max_abs')}
        report['footprints'] = {'bone_samples_world_cm': footprint, 'sole_vertex_indices': sole_ids,
            'sole_samples_world_cm': sole_tracks, 'sample_frames': [start, end],
            'interpretation': 'Tracks include every integer frame; report is not a floor-contact pass.'}
        for name, tracks in footprint.items():
            origin = tracks['head_world_cm'][0]
            tracks['max_head_drift_cm'] = max(math.dist(origin, point) for point in tracks['head_world_cm'])
        for side, tracks in sole_tracks.items():
            report['footprints'].setdefault('sole_max_vertex_drift_cm', {})[side] = max(
                (math.dist(tracks[0][index], pose[index]) for pose in tracks for index in range(len(pose))), default=0.0)
        if max_error > cfg['max_bake_error_cm'] or max_angle > cfg['max_bake_rotation_error_deg']:
            raise RuntimeError(f'Evaluated bake error exceeds tolerance: {max_error:.6f} cm, {max_angle:.6f} degrees')
        if cfg['require_constant_rig_world'] and max_world_drift > 1e-6:
            raise RuntimeError('Source rig object moves; fixed canonical object export would drop that motion.')
        if cfg['require_arm_motion'] and not any(motion[name]['translation_cm'] > 0.01 or motion[name]['rotation_deg'] > 0.05
                                               for name in ('hand_l', 'hand_r')):
            raise RuntimeError('Both hand tracks are static; refusing to label a static pose as a cleaning animation.')
        _linear_keys(baked)
        scene.frame_set(start)
        view.update()
        bpy.ops.object.select_all(action='DESELECT')
        target.select_set(True)
        view.objects.active = target
        export_options = {'use_selection': True, 'object_types': {'ARMATURE'}, 'add_leaf_bones': False,
            'use_armature_deform_only': False, 'axis_forward': '-Z', 'axis_up': 'Y',
            'global_scale': 1.0, 'apply_unit_scale': True, 'apply_scale_options': 'FBX_SCALE_NONE',
            'bake_space_transform': False, 'bake_anim': True, 'bake_anim_use_nla_strips': False,
            'bake_anim_use_all_actions': False, 'bake_anim_use_all_bones': True,
            'bake_anim_force_startend_keying': True, 'bake_anim_step': 1.0, 'bake_anim_simplify_factor': 0.0}
        result = bpy.ops.export_scene.fbx(filepath=str(pending_fbx), **export_options)
        if 'FINISHED' not in result or not pending_fbx.is_file():
            raise RuntimeError('FBX exporter did not finish.')
        axis_matrix = axis_conversion(to_forward='-Z', to_up='Y').to_4x4()
        brush_document = {'schema': 'MC_MenuBrushWorld_v1', 'clip': clip, 'fps': fps,
            'frames': [start, end], 'duration_seconds': duration, 'source_object': brush.name,
            'space': 'Blender world, right-handed Z-up, translation in metres; quaternion order WXYZ.',
            'matrix_convention': 'Row-major serialized matrices; column-vector multiplication M @ p.',
            'bone_translation_unit': 'cm (different from world-metre brush coordinates)',
            'fbx_axis_forward': '-Z', 'fbx_axis_up': 'Y', 'fbx_global_scale': 1.0,
            'blender_to_fbx_axis_matrix': _matrix_rows(axis_matrix),
            'candidate_unreal_world_basis': {
                'matrix': [[1, 0, 0, 0], [0, -1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]],
                'status': 'unverified for this new skeletal/brush import; root inferred mirror-Y from native character orientation',
                'translation_conversion': 'Only after basis is verified: world metres to UE centimetres multiplies translated positions by 100.',
                'warning': 'Not applied to samples. This reflection cannot be represented as one rotation quaternion.'},
            'conversion_notes': [
                'Samples are original Blender world transforms, not preconverted FBX/UE values.',
                'For an object matrix expressed in the same basis on both sides: M_fbx_axis = A @ M_blender @ A^-1.',
                'FBX unit metadata and Unreal importer conversion remain additional steps; match the existing skeletal import.',
                'Do not reuse the older world-baked static FBX -Y/Z convention for this canonical bone clip.',
                'Brush object scale may be .01 because its mesh local vertices use cm; a native UE static mesh may already bake that conversion.',
                'hand_to_brush_matrices map brush-local coordinates into hand-bone coordinates; their mixed local-unit scales are intentional.'],
            'samples': brush_frames}
        _json(brush_path, brush_document)
        os.replace(pending_fbx, fbx_path)
        report['export'] = {'fbx': str(fbx_path), 'sha256': _sha(fbx_path), 'bytes': fbx_path.stat().st_size,
            'brush_json': str(brush_path), 'brush_json_sha256': _sha(brush_path),
            'settings': {key: sorted(value) if isinstance(value, set) else value for key, value in export_options.items()},
            'action': baked.name, 'armature_object_name': target.name, 'armature_object_has_animation': True,
            'armature_object_transform_is_keyed': False,
            'expected_unreal_mesh': '/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay',
            'expected_unreal_bones': '107 deform/auxiliary bones plus the canonical armature object root (108 reference bones).',
            'import_instruction': 'Animation only; select skeleton from the live DA_PlayerAppearance mesh; do not update reference pose.'}
        report['status'] = 'exported_pending_fresh_import_and_visual_QA'
        success = True
        return report
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = str(error)
        report['traceback'] = traceback.format_exc()
        raise
    finally:
        # Remove only datablocks created by this invocation; never orphan-purge.
        if bpy.context.object and bpy.context.object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
        if target is not None:
            target.animation_data_clear()
            if success and cfg['keep_export_clone_hidden']:
                target.hide_render = True
                target.hide_set(True)
                report['cleanup'] = 'Export clone retained hidden, with baked action detached.'
            else:
                bpy.data.objects.remove(target, do_unlink=True)
                if target_data is not None and target_data.users == 0:
                    bpy.data.armatures.remove(target_data)
                if collection is not None:
                    bpy.data.collections.remove(collection)
                report['cleanup'] = 'Temporary export clone and collection removed.'
        elif collection is not None:
            bpy.data.collections.remove(collection)
        if baked is not None:
            if success and cfg['keep_baked_action']:
                baked.use_fake_user = True
            elif baked.users == 0:
                bpy.data.actions.remove(baked)
        scene.frame_start, scene.frame_end = old['start'], old['end']
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
        if pending_fbx.exists():
            pending_fbx.unlink()
        report['source_was_saved_by_exporter'] = False
        _json(report_path, report)
        print('MC_MENU_CLEANING_EXPORT', json.dumps({'status': report['status'], 'report': str(report_path)}, ensure_ascii=False), flush=True)


def main():
    supplied = globals().get('MC_MENU_EXPORT_OPTIONS')
    if supplied is None:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--output-dir')
        parser.add_argument('--clip-name')
        parser.add_argument('--source-rig')
        parser.add_argument('--source-brush')
        args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
        supplied = {key: value for key, value in vars(args).items() if value is not None}
    export_menu_cleaning(supplied)


if __name__ == '__main__':
    main()
