"""Isolated Tank V4: shield loss, unshielded loops and smooth Charge transitions.

Uses the proven ARP deform skeleton and centimeter export contract. Does not
modify the artist/V2/V3 sources, Unreal packages, textures or material links.
No preview images are rendered. Run background Blender with --disable-autoexec.
"""
import copy
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_nut_tank_style as A
from author_nut_tank_cartoon_v3 import bind_action, make_plans, signature
from export_nut_tank import SETTINGS, digest, enable_arp, fbx_inventory, select, sparse_pose_samples

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/TankCartoonV3/Nut_Tank_Cartoon_v3.blend'
OUT = ROOT / 'ArtSource/NutAnimationStyle/TankCartoonV4'
FPS = 25
VISIBLE = ('SM_NutTank', 'SM_Sheld', 'SM_Club')


def hash_json(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def geometry_signature(obj):
    return hash_json({'vertices': [list(v.co) for v in obj.data.vertices],
                      'polygons': [(list(p.vertices), p.material_index) for p in obj.data.polygons],
                      'materials': [m.name if m else None for m in obj.data.materials],
                      'object_matrix': [list(row) for row in obj.matrix_world]})


def sample_pose(scene, rig, action_name, frame):
    bind_action(rig, bpy.data.actions[action_name])
    scene.frame_set(frame)
    bpy.context.view_layer.update()
    return A.snapshot(rig)


def raised(pose, right, left):
    result = copy.deepcopy(pose)
    A.add(result, 'c_shoulder.r', 'location', (0, 0, right))
    A.add(result, 'c_shoulder.l', 'location', (0, 0, left))
    return result


def plans(base, launch):
    rest = copy.deepcopy(base)
    # Relax the left guard without changing the club grip or limb scales.
    rest['c_arm_fk.l']['rotation_euler'] = [-.30, -.06, -.10]
    rest['c_forearm_fk.l']['rotation_euler'] = [-.12, -.22, -.48]
    rest['c_hand_fk.l']['rotation_euler'] = [v + d for v, d in
                                           zip(base['c_hand_fk.l']['rotation_euler'], (.02, .04, .02))]
    shield = [(1, base),
        (3, raised(A.pose(base, root=(-.012, -.045, -.018), body=(-.09, -.035, -.04),
                         neck=(-.06, 0, -.02), head=(-.075, .04, .025),
                         arm_l=(.20, -.10, -.08), forearm_l=(.04, .07, .16),
                         arm_r=(-.04, 0, .025)), .045, 0)),
        (6, raised(A.pose(rest, root=(-.065, -.085, -.032), body=(-.21, -.12, -.15),
                         neck=(-.105, .04, -.05), head=(-.15, .13, .055),
                         arm_l=(.16, -.16, -.10), forearm_l=(.10, .06, .10),
                         arm_r=(-.10, -.025, .035)), .09, 0)),
        (10, raised(A.pose(rest, root=(.033, .012, .015), body=(.09, .045, .08),
                          neck=(.045, -.03, .035), head=(.075, -.055, -.035),
                          arm_l=(-.045, .08, .06), arm_r=(.045, .02, -.025)), .10, 0)),
        (14, raised(A.pose(rest, root=(-.012, -.018, -.005), body=(-.032, -.016, -.028),
                    neck=(-.025, .012, -.012), head=(-.03, .02, .012),
                    arm_l=(.025, -.025, -.025)), .04, 0)), (20, rest)]
    idle = [(1, rest),
        (18, A.pose(rest, root=(.008, .004, 0), body=(.004, 0, -.006),
                    neck=(-.008, 0, .004), head=(-.008, .008, .004), arm_l=(.01, 0, .005))),
        (41, A.pose(rest, root=(-.012, -.008, 0), body=(-.007, 0, .009),
                    neck=(.008, 0, -.006), head=(.006, -.01, -.005), arm_l=(-.008, .004, -.006))),
        (62, A.pose(rest, root=(.006, .005, 0), body=(.004, 0, -.004),
                    neck=(-.006, 0, .003), head=(-.006, .005, .004))), (81, rest)]
    walk = []
    for frame, pose in make_plans(base)['Walk']:
        pose = copy.deepcopy(pose)
        # The strong V3 weight drop lowered the club into the floor. Keep its
        # FK grip intact and raise the carrying shoulder, like the Charge rig.
        A.add(pose, 'c_shoulder.r', 'location', (0, 0, .12))
        # Keep the sole level during swing; the old pitch lowered the weighted
        # toe before the foot had sufficient vertical clearance.
        for side in ('l', 'r'):
            pose['c_foot_ik.' + side]['rotation_euler'] = copy.deepcopy(
                base['c_foot_ik.' + side]['rotation_euler'])
        if frame in (5, 29):
            A.add(pose, 'c_foot_ik.' + ('r' if frame == 5 else 'l'), 'location', (0, 0, .055))
        for side in ('l', 'r'):
            foot = 'c_foot_ik.' + side
            rear_distance = max(0., base[foot]['location'][1] - pose[foot]['location'][1])
            # The rear foot rolls onto its toe as the leg compresses. Correct
            # that small support height, retaining the same fore/aft gait.
            A.add(pose, foot, 'location', (0, 0, rear_distance * .055))
        for bone in ('c_arm_fk.l', 'c_forearm_fk.l', 'c_hand_fk.l'):
            A.add(pose, bone, 'rotation_euler', tuple(a - b for a, b in
                  zip(rest[bone]['rotation_euler'], base[bone]['rotation_euler'])))
        walk.append((frame, pose))
    early = raised(A.pose(base, root=(.005, -.025, -.015), body=(-.045, 0, .018),
                          neck=(-.015, 0, -.008), head=(.025, 0, -.008),
                          foot_l=(0, .075, .055)), .14, .07)
    left_contact = raised(A.pose(base, root=(.014, -.045, -.025), body=(.085, 0, -.025),
                                neck=(.02, 0, .008), head=(-.04, .015, .005),
                                foot_l=(0, .17, 0)), .21, .12)
    right_step = raised(A.pose(base, root=(-.005, -.055, -.035), body=(.115, 0, .025),
                              neck=(.025, 0, -.01), head=(-.035, 0, -.01),
                              foot_l=(0, .17, 0), foot_r=(0, -.10, .075)), .24, .15)
    coil = A.pose(launch, root=(.015, -.025, -.040), body=(-.13, 0, -.022),
                  neck=(-.04, 0, .015), head=(.055, .02, .02),
                  arm_l=(-.025, 0, .035), arm_r=(-.055, 0, -.015))
    push = A.pose(launch, root=(-.004, -.015, .025), body=(.035, 0, .014),
                  neck=(.025, 0, -.008), head=(-.025, 0, -.008))
    tell = [(1, base), (6, early), (12, left_contact), (17, right_step),
            (21, coil), (24, push), (28, launch)]
    brake = A.pose(launch, root=(-.008, -.040, .045), body=(.085, 0, -.025),
                   neck=(.06, 0, .012), head=(-.025, 0, -.008),
                   foot_l=(0, 0, .004), foot_r=(0, 0, .004))
    recoil = raised(A.pose(base, root=(.014, -.020, -.014), body=(-.085, 0, .04),
                           neck=(-.05, 0, -.018), head=(.04, .02, -.018),
                           foot_l=(0, .17, 0), foot_r=(0, -.17, 0)), .16, .10)
    rear_step = raised(A.pose(base, root=(.006, .005, -.006), body=(-.028, 0, .018),
                              neck=(-.015, 0, -.008), foot_l=(0, .17, 0),
                              foot_r=(0, -.08, .075)), .11, .065)
    rear_contact = raised(A.pose(base, root=(-.007, .008, 0), body=(.020, 0, -.016),
                                 foot_l=(0, .17, 0)), .065, .035)
    front_step = raised(A.pose(base, root=(-.004, .006, 0), body=(.012, 0, -.008),
                               foot_l=(0, .075, .070)), .03, .015)
    settle = A.pose(base, root=(.002, -.005, 0), body=(-.008, 0, .006), neck=(-.006, 0, 0))
    recovery = [(1, launch), (5, brake), (10, recoil), (15, rear_step),
                (21, rear_contact), (25, front_step), (30, settle), (36, base)]
    assert shield[-1][1] == idle[0][1] == idle[-1][1]
    assert walk[0][1] == walk[-1][1]
    assert tell[-1][1] == launch == recovery[0][1]
    return {'ShieldBreak': shield, 'ChargeTell': tell, 'ChargeRecovery': recovery,
            'UnshieldedIdle': idle, 'UnshieldedWalk': walk}


def sample_checks(scene, rig, action, label):
    bind_action(rig, action)
    end = int(action.frame_end)
    count = math.ceil((end - 1) / FPS * 60) + 1
    origin = None
    feet_displacement = 0.0
    previous = None
    angular = {}
    floor = {name: {'z_m': float('inf'), 'frame': None} for name in
             (('SM_NutTank', 'SM_Club') if label.startswith('Unshielded') or label == 'ShieldBreak' else VISIBLE)}
    for index in range(count):
        frame = min(end, 1 + index / 60 * FPS)
        scene.frame_set(math.floor(frame), subframe=frame - math.floor(frame))
        bpy.context.view_layer.update()
        depsgraph = bpy.context.evaluated_depsgraph_get()
        evaluated = rig.evaluated_get(depsgraph)
        feet = {name: (evaluated.matrix_world @ evaluated.pose.bones[name].matrix).translation.copy()
                for name in ('foot.l', 'foot.r')}
        if origin is None:
            origin = feet
        feet_displacement = max(feet_displacement, *((feet[name] - origin[name]).length for name in feet))
        rotations = {name: evaluated.pose.bones[name].matrix.to_quaternion() for name in
                     ('root.x', 'head.x', 'hand.l', 'hand.r')}
        if previous is not None:
            for name, q in rotations.items():
                angle = math.degrees(previous[name].rotation_difference(q).angle)
                angular[name] = max(angular.get(name, 0), min(angle, 360 - angle))
        previous = rotations
        # Modest authoring check, not a rendering or gameplay test suite.
        if index % 3 == 0 or index == count - 1:
            for name in floor:
                obj = bpy.data.objects[name].evaluated_get(depsgraph)
                mesh = obj.to_mesh()
                z = min((obj.matrix_world @ v.co).z for v in mesh.vertices)
                obj.to_mesh_clear()
                if z < floor[name]['z_m']:
                    floor[name] = {'z_m': z, 'frame': frame}
    if label in ('ShieldBreak', 'UnshieldedIdle'):
        assert feet_displacement < .0005, (label, feet_displacement)
    assert all(item['z_m'] >= -.001 for item in floor.values()), (label, floor)
    return {'fractional_pose_samples_60fps': count, 'feet_max_displacement_m': feet_displacement,
            'maximum_rotation_degrees_per_60fps_frame': angular, 'floor_minima': floor}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    original_hash = digest(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    rig = bpy.data.objects['rig']
    scene = next(s for s in bpy.data.scenes if rig.name in s.objects)
    bpy.context.window.scene = scene
    meshes = [bpy.data.objects[name] for name in VISIBLE]
    assert all(any(m.type == 'ARMATURE' and m.object == rig for m in obj.modifiers) for obj in meshes)
    rest_signature = signature(rig)
    mesh_signatures = {obj.name: geometry_signature(obj) for obj in meshes}
    original_actions = {a.name: hash_json([(f.data_path, f.array_index,
        [(list(k.co), list(k.handle_left), list(k.handle_right), k.interpolation) for k in f.keyframe_points])
        for f in A.fcurves(a)]) for a in bpy.data.actions}
    rig.animation_data.use_nla = False
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    shield = bpy.data.objects['SM_Sheld']
    group_names = {g.index: g.name for g in shield.vertex_groups}
    influences = {}
    for vertex in shield.data.vertices:
        for group in vertex.groups:
            if group.weight > 1e-6:
                row = influences.setdefault(group_names[group.group], {'weighted_vertices': 0,
                       'min_weight': 1., 'max_weight': 0.})
                row['weighted_vertices'] += 1
                row['min_weight'] = min(row['min_weight'], group.weight)
                row['max_weight'] = max(row['max_weight'], group.weight)
    shield_audit = {'object': shield.name, 'vertices': len(shield.data.vertices),
        'polygons': len(shield.data.polygons), 'weighted_bones': influences,
        'material_slots': [m.name if m else None for m in shield.data.materials],
        'material_face_counts': {str(i): sum(p.material_index == i for p in shield.data.polygons)
                                for i in range(len(shield.data.materials))},
        'shared_body_materials': sorted(set(m.name for m in shield.data.materials if m) &
                                       set(m.name for m in bpy.data.objects['SM_NutTank'].data.materials if m)),
        'native_solution': 'Swap same-skeleton NoShield mesh; do not hide hand.l or shared Walnut/hand-eye material slots.'}
    (OUT / 'ShieldBindingAudit.json').write_text(json.dumps(shield_audit, indent=2), encoding='utf8')
    print('TANK_V4_SHIELD_BINDING', json.dumps(shield_audit), flush=True)
    base = sample_pose(scene, rig, 'MC_Tank_Idle', 1)
    launch = sample_pose(scene, rig, 'MC_Tank_ChargeLoop', 1)
    properties = {bone.name: {key: bone[key] for key in bone.keys() if isinstance(bone[key], (bool, int, float))}
                  for bone in rig.pose.bones}
    scene.render.fps = FPS
    scene.render.fps_base = 1
    report = {'schema': 4, 'source': str(SOURCE), 'source_sha256_before': original_hash,
              'blender_version': bpy.app.version_string, 'fps': FPS,
              'native_contracts': {'ShieldBreak': {'seconds': .76, 'end_matches': 'UnshieldedIdle'},
                                   'ChargeTell': {'seconds': 1.1}, 'ChargeRecovery': {'seconds': 1.4}},
              'clips': [], 'authoring_checks': {}, 'shield_binding': shield_audit,
              'import_scope': 'Animation-only clips and one same-skeleton mesh variant; no material/texture import.',
              'limitations': ['Blender authoring checks exclude native overlays, world movement, gameplay collision and lighting.']}
    actions = {}
    old_names = {}
    for label, keys in plans(base, launch).items():
        existing = bpy.data.actions.get('MC_Tank_' + label)
        if existing:
            old_names[existing.name] = 'MC_Tank_V3_Reference_' + label
            existing.name = old_names[existing.name]
        rig.animation_data.action = None
        action = bpy.data.actions.new('MC_Tank_' + label)
        action.use_fake_user = True
        rig.animation_data.action = action
        for frame, pose in keys:
            A.keypose(rig, pose, frame)
        motion_count = custom_count = 0
        for curve in A.fcurves(action):
            custom = curve.data_path.endswith(']')
            for key in curve.keyframe_points:
                key.interpolation = 'CONSTANT' if custom else 'BEZIER'
                key.handle_left_type = key.handle_right_type = 'AUTO_CLAMPED'
                if custom:
                    custom_count += 1
                else:
                    motion_count += 1
        action.use_frame_range = True
        action.frame_start = 1
        action.frame_end = keys[-1][0]
        action['mc_loop'] = label.startswith('Unshielded')
        action['mc_style_version'] = 'TankCartoonV4'
        action['mc_authored_cartoon'] = True
        actions[label] = action
        report['clips'].append({'label': label, 'source_action': action.name,
            'file': 'A_NutTank_' + label + '.fbx', 'start_frame': 1, 'end_frame': keys[-1][0],
            'fps': FPS, 'duration_seconds': (keys[-1][0] - 1) / FPS,
            'key_poses': [frame for frame, _ in keys], 'loop': bool(action['mc_loop']),
            'unreal_float_curves': {'MCAuthoredCartoon': 1.0},
            'motion_bezier_keys': motion_count, 'motion_constant_keys': 0,
            'discrete_custom_property_keys': custom_count})
        report['authoring_checks'][label] = sample_checks(scene, rig, action, label)
        print('TANK_V4_AUTHORED', label, flush=True)
    preserved_actions = all(hash_json([(f.data_path, f.array_index,
        [(list(k.co), list(k.handle_left), list(k.handle_right), k.interpolation) for k in f.keyframe_points])
        for f in A.fcurves(bpy.data.actions[old_names.get(name, name)])]) == fingerprint
        for name, fingerprint in original_actions.items())
    assert preserved_actions
    assert signature(rig) == rest_signature
    assert {obj.name: geometry_signature(obj) for obj in meshes} == mesh_signatures
    (OUT / 'RigSignature.json').write_text(json.dumps({'rig': rig.name, 'bones': rest_signature,
        'sha256': hash_json(rest_signature), 'source_mesh_geometry_sha256': mesh_signatures}, indent=2), encoding='utf8')
    bind_action(rig, actions['UnshieldedIdle'])
    scene.frame_start = 1
    scene.frame_end = 81
    scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version = 0
    blend = OUT / 'Nut_Tank_Cartoon_v4.blend'
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    addon = enable_arp()
    for name, value in SETTINGS.items():
        setattr(scene, name, value)
    legacy = json.loads((ROOT / 'ArtSource/NutTank/ExportReport.json').read_text(encoding='utf8'))
    reference = next(row['fbx'] for row in legacy['files'] if row['file'] == 'SK_NutTank.fbx')
    reference_bind = {row['name']: row['matrix'] for row in reference['bind_poses']}
    bone_names = {row['name'] for row in reference['bones']}
    def validate(path, no_shield=False):
        info = fbx_inventory(path)
        current = {row['name']: row['matrix'] for row in info['bind_poses']}
        assert info['hierarchy_sha256'] == reference['hierarchy_sha256'], path
        names = bone_names | ({'root'} & reference_bind.keys()) if no_shield else reference_bind.keys()
        assert all(name in current for name in names), (path, sorted(set(names) - current.keys()))
        delta = max(abs(a - b) for name in names for a, b in zip(reference_bind[name], current[name]))
        assert delta < .0001, (path, delta)
        for key in ('UnitScaleFactor', 'OriginalUnitScaleFactor', 'UpAxis', 'UpAxisSign', 'FrontAxis', 'FrontAxisSign'):
            assert info['global_settings'].get(key) == reference['global_settings'].get(key), (path, key)
        if no_shield:
            assert len(info['geometry_bounds_fbx_units']) == 2, info['geometry_bounds_fbx_units']
            assert not any('sheld' in row['name'].lower() for row in info['geometry_bounds_fbx_units'])
        else:
            assert info['root_animation_constant'] and set(current) == set(reference_bind), path
        return {'file': path.name, 'bytes': path.stat().st_size, 'sha256': digest(path),
                'fbx': info, 'bind_max_delta': delta}
    exported = {'schema': 4, 'status': 'exporting', 'source': str(blend), 'arp': addon,
                'fps': FPS, 'clips': report['clips'], 'files': [], 'source_pose_samples': {},
                'native_contracts': report['native_contracts'], 'import_scope': report['import_scope']}
    for clip in report['clips']:
        label = clip['label']
        rig.animation_data.action = None
        for bone in rig.pose.bones:
            bone.matrix_basis = Matrix.Identity(4)
            for key, value in properties[bone.name].items():
                bone[key] = value
        bind_action(rig, actions[label])
        scene.arp_bake_anim = True
        scene.arp_frame_range_type = 'CUSTOM'
        scene.arp_export_start_frame = scene.frame_start = 1
        scene.arp_export_end_frame = scene.frame_end = clip['end_frame']
        exported['source_pose_samples'][label] = sparse_pose_samples(scene, rig, 1, clip['end_frame'])
        scene.frame_set(1)
        bpy.context.view_layer.update()
        select([rig, *meshes], rig)
        path = OUT / clip['file']
        assert 'FINISHED' in bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
        exported['files'].append(validate(path))
        print('TANK_V4_EXPORTED', label, flush=True)
    # Only omit the shield object. Never delete, reskin or transform body/club.
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
        for key, value in properties[bone.name].items():
            bone[key] = value
    scene.arp_bake_anim = False
    select([rig, bpy.data.objects['SM_NutTank'], bpy.data.objects['SM_Club']], rig)
    path = OUT / 'SK_NutTank_NoShield.fbx'
    assert 'FINISHED' in bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
    exported['files'].append(validate(path, no_shield=True))
    exported['mesh_variant'] = {'file': path.name, 'omitted_object': 'SM_Sheld',
        'included_objects': ['SM_NutTank', 'SM_Club'], 'same_skeleton': True,
        'import_materials': False, 'import_textures': False,
        'alignment': 'Reuse original tank model center/scale, not NoShield mesh bounds.'}
    assert signature(rig) == rest_signature
    assert {obj.name: geometry_signature(obj) for obj in meshes} == mesh_signatures
    assert digest(SOURCE) == original_hash
    exported['status'] = 'complete'
    exported['source_preserved'] = exported['hierarchy_consistent'] = exported['reference_unchanged'] = True
    exported['root_animation_constant'] = True  # Each of the five clip FBXs passed validate().
    exported['mesh_file'] = path.name
    exported['original_source_sha256'] = original_hash
    report.update({'blend': str(blend), 'source_sha256_after': digest(SOURCE),
                   'source_preserved': True, 'original_actions_preserved': preserved_actions,
                   'rig_bind_unchanged': True, 'mesh_geometry_unchanged': True, 'status': 'authored_exported'})
    for name, value in (('ExportReport.json', exported), ('AuthorReport.json', report),
                        ('BoneSamples.json', exported['source_pose_samples'])):
        (OUT / name).write_text(json.dumps(value, indent=2), encoding='utf8')
    print('TANK_V4_PASS', json.dumps({'clips': len(report['clips']), 'mesh_variant': path.name,
                                    'ShieldBreak_seconds': .76, 'source_preserved': True}), flush=True)


if __name__ == '__main__':
    main()
