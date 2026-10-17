"""Build a readable FK Wizard from the saved refined V8 source.

Importing this file does nothing. Root executes main() in the prepared source.
The ARP rig, mesh and V8 actions remain in a hidden archive collection.
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard'
OUTPUT = OUT / 'NutWizard_Readable_v9.blend'
REPORT = OUT / 'WizardReadableV9Report.json'
SURFACE_TOLERANCE = 0.00002  # Twenty micrometres in Blender world units.
REST_MATRIX_TOLERANCE = 0.00001  # EditBone roll reconstruction uses floats.
POSE_MATRIX_TOLERANCE = 0.00002


def _assign_action(rig, action):
    rig.animation_data_create()
    rig.animation_data.action = action
    rig.animation_data.use_nla = False
    if action and action.slots:
        rig.animation_data.action_slot = action.slots[0]
    for track in rig.animation_data.nla_tracks:
        track.mute = True


def _surface(mesh):
    bpy.context.view_layer.update()
    obj = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
    evaluated = obj.to_mesh()
    try:
        return [obj.matrix_world @ v.co for v in evaluated.vertices]
    finally:
        obj.to_mesh_clear()


def _weights(mesh):
    return [(v.index, g.group, float(g.weight))
            for v in mesh.data.vertices for g in v.groups]


def _names_and_parents(old):
    """Keep 49 skin bones and ten genuine shoulder/limb FK bones.

    The anatomical skin-parent bridges are intentional: ARP's stretch bones
    were roots under c_traj, so nearest-original-ancestor alone cannot give
    working FK. Root authorized these bridges; all rest matrices stay intact.
    """
    names = {'root.x': 'Pelvis', 'head.x': 'WalnutBody',
             'neck.x': 'WalnutNeck'}
    for side in ('r', 'l'):
        suffix = '.' + side.upper()
        pairs = {
            'c_shoulder': 'Shoulder', 'shoulder': 'SkinShoulder',
            'arm': 'UpperArm', 'forearm': 'Forearm', 'hand': 'Hand',
            'arm_stretch': 'UpperArmSkinA',
            'c_arm_twist_offset': 'UpperArmSkinB',
            'forearm_stretch': 'ForearmSkinA',
            'forearm_twist': 'ForearmSkinB',
            'thigh': 'Thigh', 'leg': 'Shin', 'foot': 'Foot',
            'toes_01': 'Toes', 'thigh_stretch': 'ThighSkinA',
            'thigh_twist': 'ThighSkinB', 'leg_stretch': 'ShinSkinA',
            'leg_twist': 'ShinSkinB',
            'c_ring1_base': 'DigitABase', 'ring1': 'DigitA1',
            'c_ring2': 'DigitA2', 'c_ring3': 'DigitA3',
            'c_pinky1_base': 'DigitBBase', 'pinky1': 'DigitB1',
            'c_pinky2': 'DigitB2', 'c_pinky3': 'DigitB3',
            'thumb1': 'Thumb1', 'c_thumb2': 'Thumb2',
            'c_thumb3': 'Thumb3',
        }
        names.update({n + '.' + side: semantic + suffix
                      for n, semantic in pairs.items()})
    deform = {b.name for b in old.data.bones if b.use_deform}
    assert len(deform) == 49, ('Expected 49 source deform bones', len(deform))
    assert deform <= names.keys(), ('Unmapped deform bones', deform - names.keys())
    assert set(names) <= set(old.data.bones.keys()), 'Missing anatomical source bones'
    assert len(set(names.values())) == len(names)
    parents = {}
    for name in names:
        parent = old.data.bones[name].parent
        while parent and parent.name not in names:
            parent = parent.parent
        parents[name] = parent.name if parent else None
    parents.update({'root.x': None, 'neck.x': 'root.x', 'head.x': 'root.x'})
    for s in ('r', 'l'):
        def n(prefix):
            return prefix + '.' + s
        bridges = {
            'c_shoulder': 'head.x', 'shoulder': n('c_shoulder'),
            'arm': n('c_shoulder'), 'forearm': n('arm'),
            'hand': n('forearm'), 'arm_stretch': n('arm'),
            'c_arm_twist_offset': n('arm'),
            'forearm_stretch': n('forearm'), 'forearm_twist': n('forearm'),
            'thigh': 'root.x', 'leg': n('thigh'), 'foot': n('leg'),
            'toes_01': n('foot'), 'thigh_stretch': n('thigh'),
            'thigh_twist': n('thigh'), 'leg_stretch': n('leg'),
            'leg_twist': n('leg'),
            'c_ring1_base': n('hand'), 'ring1': n('c_ring1_base'),
            'c_ring2': n('ring1'), 'c_ring3': n('c_ring2'),
            'c_pinky1_base': n('hand'), 'pinky1': n('c_pinky1_base'),
            'c_pinky2': n('pinky1'), 'c_pinky3': n('c_pinky2'),
            'thumb1': n('hand'), 'c_thumb2': n('thumb1'),
            'c_thumb3': n('c_thumb2'),
        }
        parents.update({n(child): parent for child, parent in bridges.items()})
    order = []
    pending = set(names)
    while pending:
        ready = sorted(n for n in pending if not parents[n] or parents[n] in order)
        assert ready, 'Anatomical hierarchy cycle'
        order.extend(ready)
        pending.difference_update(ready)
    return names, parents, order, deform


def _collection_for(name):
    if 'Skin' in name or name == 'WalnutNeck':
        return '90 Skin details'
    if '.' not in name:
        return '01 Body'
    side = 'Right' if name.endswith('.R') else 'Left'
    if name.startswith(('Digit', 'Thumb', 'Hand')):
        return ('04 ' if side == 'Right' else '05 ') + side + ' hand'
    if name.startswith(('Thigh', 'Shin', 'Foot', 'Toes')):
        return ('06 ' if side == 'Right' else '07 ') + side + ' leg'
    return ('02 ' if side == 'Right' else '03 ') + side + ' arm'


def _create_rig(old, names, parents, order):
    data = bpy.data.armatures.new('WizardReadableV9')
    new = bpy.data.objects.new('rig', data)
    bpy.context.scene.collection.objects.link(new)
    new.matrix_world = old.matrix_world.copy()
    new.show_in_front = True
    data.display_type = 'OCTAHEDRAL'
    bpy.ops.object.mode_set(mode='OBJECT') if bpy.context.object and bpy.context.object.mode != 'OBJECT' else None
    bpy.ops.object.select_all(action='DESELECT')
    new.select_set(True)
    bpy.context.view_layer.objects.active = new
    bpy.ops.object.mode_set(mode='EDIT')
    for source in order:
        original = old.data.bones[source]
        edit = data.edit_bones.new(names[source])
        edit.head = original.head_local
        edit.tail = original.tail_local
        edit.matrix = original.matrix_local.copy()
        edit.length = original.length
        edit.use_deform = original.use_deform
        edit.inherit_scale = 'NONE'
        edit.use_local_location = True
        edit.use_connect = False
        if parents[source]:
            edit.parent = data.edit_bones[names[parents[source]]]
    bpy.ops.object.mode_set(mode='OBJECT')
    collections = {}
    maximum_rest_matrix_error = 0.0
    for source in order:
        bone = data.bones[names[source]]
        rest_matrix_error = max(abs(bone.matrix_local[r][c] - old.data.bones[source].matrix_local[r][c])
                                for r in range(4) for c in range(4))
        maximum_rest_matrix_error = max(maximum_rest_matrix_error, rest_matrix_error)
        assert rest_matrix_error < REST_MATRIX_TOLERANCE, (source, rest_matrix_error)
        group = _collection_for(bone.name)
        if group not in collections:
            collections[group] = data.collections.new(group)
        collections[group].assign(bone)
        bone.color.palette = ('THEME04' if '.' not in bone.name else
                              'THEME03' if bone.name.endswith('.R') else 'THEME01')
        pb = new.pose.bones[bone.name]
        pb.rotation_mode = 'QUATERNION'
        pb['SourceBone'] = source
        pb['Meaning'] = ('Explicit skin segment/twist; rotate for manual twist distribution'
                         if 'Skin' in bone.name else 'Direct FK; local Y is bone-axis pronation')
    collections['90 Skin details'].is_visible = False
    new['MC_ReadableRigV9'] = True
    new['MC_MaximumRestMatrixError'] = maximum_rest_matrix_error
    new['Guide'] = 'Direct FK. Body, shoulder, upper arm, forearm, hand, digits. No ARP constraints or switches. Local Y rolls the bone; skin detail bones allow explicit twist adjustment.'
    return new


def main(save=True):
    scene = bpy.context.scene
    assert Path(bpy.data.filepath).name == 'NutWizard_Human_v8_Refined.blend', 'Open the prepared refined V8 source first'
    old, mesh = bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    assert old.type == 'ARMATURE' and not old.get('MC_ReadableRigV9')
    assert not bpy.data.objects.get('rig_ARP_Archive'), 'V9 already constructed in this scene'
    names, parents, order, deforms = _names_and_parents(old)
    actions = sorted((a for a in bpy.data.actions if a.name.startswith('MC_WizardV8_')), key=lambda a: a.name)
    assert len(actions) == 9, ('Expected nine V8 actions', [a.name for a in actions])
    modifiers = [m for m in mesh.modifiers if m.type == 'ARMATURE' and m.object == old]
    assert len(modifiers) == 1, 'Expected one original mesh armature modifier'
    original_frame = scene.frame_current
    original_action = old.animation_data.action
    original_weights = _weights(mesh)
    ranges = {a.name: (int(a.get('frame_start', math.floor(a.frame_range[0]))),
                       int(a.get('frame_end', math.ceil(a.frame_range[1])))) for a in actions}
    # Only a few surface comparisons, chosen to cover body scale, palms and legs.
    checks = []
    for label, fraction in [('Idle', 0), ('Walk', .5), ('Cast', .45), ('Cast', .60), ('Death', 1)]:
        action = next((a for a in actions if a.name == 'MC_WizardV8_' + label), None)
        if action:
            start, end = ranges[action.name]
            frame = round(start + (end-start)*fraction)
            _assign_action(old, action)
            scene.frame_set(frame)
            checks.append((action.name, frame, _surface(mesh)))
    old_pose_position = old.data.pose_position
    old.data.pose_position = 'REST'
    rest_reference = _surface(mesh)
    old.data.pose_position = old_pose_position
    old.name = 'rig_ARP_Archive'
    archive = bpy.data.collections.new('MC_Wizard_ARP_Archive')
    scene.collection.children.link(archive)
    archive_mesh = mesh.copy()
    archive_mesh.data = mesh.data.copy()
    archive_mesh.name = 'SM_NutWizard_ARP_Archive'
    archive.objects.link(archive_mesh)
    for collection in list(old.users_collection):
        collection.objects.unlink(old)
    archive.objects.link(old)
    archive_mesh.hide_render = True
    archive_mesh.hide_set(True)
    old.hide_render = True
    new = _create_rig(old, names, parents, order)
    modifiers[0].object = new
    if mesh.parent == old:
        world = mesh.matrix_world.copy()
        mesh.parent = new
        mesh.matrix_world = world
    for source in deforms:
        group = mesh.vertex_groups.get(source)
        if group:
            group.name = names[source]
    assert _weights(mesh) == original_weights, 'Skin weights changed'
    assert sum(b.use_deform for b in new.data.bones) == 49
    new.data.pose_position = 'REST'
    rest_new = _surface(mesh)
    assert len(rest_new) == len(rest_reference)
    rest_error = max((a-b).length for a, b in zip(rest_new, rest_reference))
    assert rest_error < SURFACE_TOLERANCE, ('Rest surface mismatch', rest_error)
    new.data.pose_position = 'POSE'
    report_actions, result_actions = [], {}
    for original in actions:
        _assign_action(old, original)
        result = bpy.data.actions.new(original.name.replace('MC_WizardV8_', 'MC_WizardV9_', 1))
        result.use_fake_user = True
        original.use_fake_user = True
        for key, value in original.items():
            if isinstance(value, (str, bool, int, float)):
                result[key] = value
        result['SourceAction'] = original.name
        _assign_action(new, result)
        start, end = ranges[original.name]
        result['frame_start'], result['frame_end'] = start, end
        previous = {}
        maximum_matrix_error = 0.0
        maximum_matrix_error_at = None
        for frame in range(start, end + 1):
            scene.frame_set(frame)
            bpy.context.view_layer.update()
            evaluated = old.evaluated_get(bpy.context.evaluated_depsgraph_get())
            transform = new.matrix_world.inverted() @ evaluated.matrix_world
            targets = {n: transform @ evaluated.pose.bones[n].matrix for n in order}
            actual = {}
            for source in order:
                bone, pb = new.data.bones[names[source]], new.pose.bones[names[source]]
                kwargs = {}
                if parents[source]:
                    parent = parents[source]
                    kwargs = dict(parent_matrix=actual[parent], parent_matrix_local=new.data.bones[names[parent]].matrix_local)
                basis = bone.convert_local_to_pose(targets[source], bone.matrix_local, invert=True, **kwargs)
                location, rotation, scale = basis.decompose()
                if source in previous and rotation.dot(previous[source]) < 0:
                    rotation.negate()
                previous[source] = rotation.copy()
                pb.location, pb.rotation_quaternion, pb.scale = location, rotation, scale
                actual[source] = bone.convert_local_to_pose(Matrix.LocRotScale(location, rotation, scale), bone.matrix_local, **kwargs)
                matrix_error = max(abs(actual[source][r][c]-targets[source][r][c]) for r in range(4) for c in range(4))
                if matrix_error > maximum_matrix_error:
                    maximum_matrix_error = matrix_error
                    maximum_matrix_error_at = dict(frame=frame, source_bone=source, bone=pb.name)
                for path in ('location', 'rotation_quaternion', 'scale'):
                    pb.keyframe_insert(path, frame=frame, group=pb.name)
        for layer in result.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for curve in bag.fcurves:
                        for point in curve.keyframe_points:
                            point.interpolation = 'LINEAR'
        assert maximum_matrix_error <= POSE_MATRIX_TOLERANCE, ('Pose bake lost a matrix transform/shear', original.name, maximum_matrix_error, maximum_matrix_error_at)
        result_actions[original.name] = result
        report_actions.append(dict(source=original.name, action=result.name, frame_start=start, frame_end=end, maximum_pose_matrix_error=maximum_matrix_error, maximum_pose_matrix_error_at=maximum_matrix_error_at))
    validation = []
    for original_name, frame, reference in checks:
        _assign_action(new, result_actions[original_name])
        scene.frame_set(frame)
        positions = _surface(mesh)
        assert len(positions) == len(reference), 'Evaluated topology changed'
        errors = [(a-b).length for a, b in zip(positions, reference)]
        maximum = max(errors)
        validation.append(dict(action=result_actions[original_name].name, frame=frame, maximum_world_vertex_error=maximum, rms_world_vertex_error=math.sqrt(sum(e*e for e in errors)/len(errors))))
        assert maximum < SURFACE_TOLERANCE, ('Baked surface differs', validation[-1])
    for action in actions:
        action['MC_ArchivedARPSource'] = True
    _assign_action(old, original_action)
    archive.hide_render = True
    archive.hide_viewport = True
    old.hide_set(True)
    chosen = next((a for a in result_actions.values() if a.name == 'MC_WizardV9_Cast'), next(iter(result_actions.values())))
    _assign_action(new, chosen)
    scene.frame_start, scene.frame_end = int(chosen['frame_start']), int(chosen['frame_end'])
    scene.frame_set(min(max(original_frame, scene.frame_start), scene.frame_end))
    bpy.ops.object.select_all(action='DESELECT')
    new.select_set(True)
    bpy.context.view_layer.objects.active = new
    report = dict(source='NutWizard_Human_v8_Refined.blend', output=str(OUTPUT), status='validated', rig=new.name, archived_rig=old.name, deform_bones=49, total_bones=len(order), constraints=sum(len(b.constraints) for b in new.pose.bones), drivers=len(new.animation_data.drivers), rest_surface_error=rest_error, maximum_rest_matrix_error=float(new['MC_MaximumRestMatrixError']), rest_matrix_tolerance=REST_MATRIX_TOLERANCE, pose_matrix_tolerance=POSE_MATRIX_TOLERANCE, surface_tolerance=SURFACE_TOLERANCE, unchanged_weight_entries=len(original_weights), name_map=names, parents={names[n]: names[parents[n]] if parents[n] else None for n in order}, actions=report_actions, surface_validation=validation, notes=['New anatomical FK parent bridges replace ARP ancestry.', 'Original bone rest matrices preserved within EditBone float precision; numeric skin weights unchanged.', 'Explicit forearm local-Y pronation; skin detail bones provide manual twist distribution.', 'Nine clips sampled every integer source frame with quaternion sign continuity; every sampled matrix checked.', 'Source rig, source mesh and V8 actions archived; no ARP constraints/drivers copied.'])
    if save:
        OUT.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
        REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))
    return report
