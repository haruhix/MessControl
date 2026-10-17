"""Author one broad, continuous Tank Melee in an isolated V5 copy; no renders."""
import copy
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
import author_nut_tank_style as A
from author_nut_tank_cartoon_v3 import bind_action, signature
from author_nut_tank_cartoon_v4 import geometry_signature, raised, sample_pose
from export_nut_tank import SETTINGS, digest, enable_arp, fbx_inventory, select, sparse_pose_samples

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/TankCartoonV4/Nut_Tank_Cartoon_v4.blend'
OUT = ROOT / 'ArtSource/NutAnimationStyle/TankCartoonV5'
FPS = 25
WINDUP_SECONDS = .65
RECOVERY_SECONDS = 1.2
VISIBLE = ('SM_NutTank', 'SM_Sheld', 'SM_Club')


def make_keys(base):
    stance = A.pose(base, foot_l=(-.02, 0, 0), foot_r=(.02, 0, 0))
    coil = raised(A.pose(stance, root=(.045, -.095, -.045), body=(-.18, -.95, -.095),
        neck=(-.10, .05, .035), head=(-.055, .55, .09),
        arm_r=(-1.18, -.60, -.32), forearm_r=(.20, .06, .28), hand_r=(-.26, .02, -.18),
        arm_l=(-.065, -.025, .12), forearm_l=(.04, 0, -.06)), .28, .11)
    return [(1, stance),
        (9, raised(A.pose(stance, root=(.006, -.030, -.01), body=(-.06, -.12, -.025),
            head=(-.015, .095, .02), arm_r=(-.20, -.11, -.05), arm_l=(.045, 0, .045)), .07, .025)),
        (21, raised(A.pose(stance, root=(.028, -.065, -.028), body=(-.14, -.60, -.065),
            neck=(-.07, .025, .025), head=(-.04, .39, .055),
            arm_r=(-.87, -.43, -.23), forearm_r=(.14, .04, .20), hand_r=(-.20, .02, -.16),
            arm_l=(-.035, -.015, .095)), .23, .09)),
        (31, coil),
        (36, raised(A.pose(stance, root=(.026, -.078, -.026), body=(-.12, -.50, -.068),
            neck=(-.075, .035, .028), head=(-.055, .50, .08),
            arm_r=(-.61, -.34, -.20), forearm_r=(.13, .05, .23), hand_r=(-.26, .02, -.17),
            arm_l=(-.035, -.02, .10)), .28, .105)),
        (43, raised(A.pose(stance, root=(-.006, -.075, .012), body=(.10, .19, .025),
            neck=(-.03, -.015, .01), head=(-.04, .24, .04),
            arm_r=(.18, .075, -.02), forearm_r=(-.065, .01, -.06), hand_r=(-.17, .02, -.15),
            arm_l=(.06, -.04, .085)), .30, .06)),
        (51, raised(A.pose(stance, root=(-.045, -.115, .050), body=(.35, .80, .085),
            neck=(.11, -.18, -.01), head=(.085, -.34, -.045),
            arm_r=(1.06, .48, .14), forearm_r=(-.25, .01, -.29), hand_r=(.34, .015, -.17),
            arm_l=(.10, -.055, .09)), .28, .06)),
        (57, raised(A.pose(stance, root=(-.055, -.123, .042), body=(.38, 1.0, .11),
            neck=(.18, -.27, -.035), head=(.13, -.64, -.065),
            arm_r=(1.32, .66, .22), forearm_r=(-.31, .025, -.36), hand_r=(.45, .035, -.20),
            arm_l=(.12, -.07, .055)), .30, .07)),
        (67, raised(A.pose(stance, root=(-.032, -.075, .025), body=(.24, .66, .065),
            neck=(.115, -.17, -.02), head=(.075, -.36, -.04),
            arm_r=(.75, .41, .12), forearm_r=(-.16, .015, -.20), hand_r=(.20, .015, -.12),
            arm_l=(.09, -.035, .025)), .18, .04)),
        (80, raised(A.pose(stance, root=(-.006, -.030, .005), body=(.085, .18, .018),
            neck=(.045, -.045, -.005), head=(.025, -.095, -.015),
            arm_r=(.30, .19, .025), forearm_r=(-.05, 0, -.065), arm_l=(.045, 0, 0)), .07, .015)),
        (89, raised(A.pose(stance, root=(.008, .008, -.004), body=(-.03, -.09, -.025),
            neck=(-.03, .02, .01), head=(-.025, .045, .015),
            arm_r=(-.085, -.06, -.018), arm_l=(-.02, 0, -.015)), .045, 0)),
        (101, stance)]


def authoring_checks(scene, rig):
    origin = None
    feet_drift = 0.
    feet_max_at = None
    previous = None
    angular = {}
    minima = {name: {'z_m': float('inf'), 'game_seconds': 0} for name in VISIBLE}
    count = round((WINDUP_SECONDS + RECOVERY_SECONDS) * 60) + 1
    for index in range(count):
        seconds = index / 60
        frame = 1 + 50 * seconds / WINDUP_SECONDS if seconds <= WINDUP_SECONDS else 51 + 50 * (seconds - WINDUP_SECONDS) / RECOVERY_SECONDS
        frame = min(frame, 101)
        scene.frame_set(math.floor(frame), subframe=frame - math.floor(frame))
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        evaluated = rig.evaluated_get(dg)
        feet = {bone: (evaluated.matrix_world @ evaluated.pose.bones[bone].matrix).translation.copy()
                for bone in ('foot.l', 'foot.r')}
        if origin is None:
            origin = feet
        for bone in feet:
            displacement = (feet[bone] - origin[bone]).length
            if displacement > feet_drift:
                feet_drift = displacement
                feet_max_at = {'bone': bone, 'source_frame': frame, 'game_seconds': seconds}
        rotations = {bone: evaluated.pose.bones[bone].matrix.to_quaternion() for bone in
                     ('root.x', 'head.x', 'hand.r')}
        if previous is not None:
            for bone, q in rotations.items():
                degrees = math.degrees(previous[bone].rotation_difference(q).angle)
                angular[bone] = max(angular.get(bone, 0), min(degrees, 360 - degrees))
        previous = rotations
        for name in VISIBLE:
            source = bpy.data.objects[name]
            obj = source.evaluated_get(dg)
            mesh = obj.to_mesh()
            vertex = min(mesh.vertices, key=lambda v: (obj.matrix_world @ v.co).z)
            z = (obj.matrix_world @ vertex.co).z
            if z < minima[name]['z_m']:
                minima[name] = {'z_m': z, 'game_seconds': seconds, 'source_frame': frame,
                    'vertex': vertex.index,
                    'groups': [(source.vertex_groups[g.group].name, g.weight)
                               for g in source.data.vertices[vertex.index].groups]}
            obj.to_mesh_clear()
    checks = {'native_fractional_samples_60fps': count, 'feet_max_displacement_m': feet_drift,
              'feet_max_at': feet_max_at,
              'maximum_rotation_degrees_per_game_frame_60fps': angular, 'floor_minima': minima}
    (OUT / 'AuthoringChecks.json').write_text(json.dumps(checks, indent=2), encoding='utf8')
    assert feet_drift < .0005, checks
    assert all(row['z_m'] >= -.001 for row in minima.values()), checks
    return checks


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source_hash = digest(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    rig = bpy.data.objects['rig']
    scene = bpy.data.scenes['Scene']
    bpy.context.window.scene = scene
    rest = signature(rig)
    meshes = [bpy.data.objects[name] for name in VISIBLE]
    mesh_hashes = {obj.name: geometry_signature(obj) for obj in meshes}
    rig.animation_data.use_nla = False
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    base = sample_pose(scene, rig, 'MC_Tank_Idle', 1)
    properties = {bone.name: {key: bone[key] for key in bone.keys() if isinstance(bone[key], (bool, int, float))}
                  for bone in rig.pose.bones}
    existing = bpy.data.actions.get('MC_Tank_Melee')
    if existing:
        existing.name = 'MC_Tank_V4_Reference_Melee'
    rig.animation_data.action = None
    action = bpy.data.actions.new('MC_Tank_Melee')
    action.use_fake_user = True
    rig.animation_data.action = action
    keys = make_keys(base)
    for frame, pose in keys:
        A.keypose(rig, pose, frame)
    counts = {'BEZIER_motion_keys': 0, 'CONSTANT_custom_keys': 0, 'CONSTANT_motion_keys': 0}
    for curve in A.fcurves(action):
        custom = curve.data_path.endswith(']')
        for key in curve.keyframe_points:
            key.interpolation = 'CONSTANT' if custom else 'BEZIER'
            key.handle_left_type = key.handle_right_type = 'AUTO_CLAMPED'
            counts['CONSTANT_custom_keys' if custom else 'BEZIER_motion_keys'] += 1
    action.use_frame_range = True
    action.frame_start, action.frame_end = 1, 101
    action['mc_loop'] = False
    action['mc_style_version'] = 'TankCartoonV5'
    action['mc_authored_cartoon'] = True
    action['mc_hit_fraction'] = .5
    scene.render.fps = FPS
    scene.render.fps_base = 1
    scene.frame_start, scene.frame_end = 1, 101
    checks = authoring_checks(scene, rig)
    assert signature(rig) == rest
    assert {obj.name: geometry_signature(obj) for obj in meshes} == mesh_hashes
    scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version = 0
    blend = OUT / 'Nut_Tank_Cartoon_v5.blend'
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    addon = enable_arp()
    for name, value in SETTINGS.items():
        setattr(scene, name, value)
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
        for key, value in properties[bone.name].items():
            bone[key] = value
    bind_action(rig, action)
    scene.arp_bake_anim = True
    scene.arp_frame_range_type = 'CUSTOM'
    scene.arp_export_start_frame = scene.frame_start = 1
    scene.arp_export_end_frame = scene.frame_end = 101
    pose_samples = sparse_pose_samples(scene, rig, 1, 101)
    scene.frame_set(1)
    bpy.context.view_layer.update()
    select([rig, *meshes], rig)
    path = OUT / 'A_NutTank_Melee.fbx'
    assert 'FINISHED' in bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
    info = fbx_inventory(path)
    legacy = json.loads((ROOT / 'ArtSource/NutTank/ExportReport.json').read_text(encoding='utf8'))
    reference = next(row['fbx'] for row in legacy['files'] if row['file'] == 'SK_NutTank.fbx')
    before = {row['name']: row['matrix'] for row in reference['bind_poses']}
    after = {row['name']: row['matrix'] for row in info['bind_poses']}
    assert info['hierarchy_sha256'] == reference['hierarchy_sha256']
    assert info['root_animation_constant'] and before.keys() == after.keys()
    delta = max(abs(a - b) for name in before for a, b in zip(before[name], after[name]))
    assert delta < .0001
    assert signature(rig) == rest
    assert {obj.name: geometry_signature(obj) for obj in meshes} == mesh_hashes
    assert digest(SOURCE) == source_hash
    clip = {'label': 'Melee', 'source_action': action.name, 'file': path.name,
            'start_frame': 1, 'end_frame': 101, 'fps': FPS, 'duration_seconds': 4., 'loop': False,
            'key_poses': [frame for frame, _ in keys], 'impact_fraction': .5,
            'unreal_float_curves': {'MCAuthoredCartoon': 1.0}}
    contracts = {'Melee': {'impact_fraction': .5, 'windup_seconds': WINDUP_SECONDS, 'recovery_seconds': RECOVERY_SECONDS}}
    exported = {'schema': 5, 'status': 'complete', 'source': str(blend), 'fps': FPS, 'arp': addon,
        'clips': [clip], 'files': [{'file': path.name, 'bytes': path.stat().st_size,
            'sha256': digest(path), 'fbx': info, 'bind_max_delta': delta}],
        'source_pose_samples': {'Melee': pose_samples}, 'native_contracts': contracts,
        'source_preserved': True, 'hierarchy_consistent': True, 'reference_unchanged': True,
        'root_animation_constant': True, 'import_scope': 'Animation only, existing Tank skeleton; no mesh/material import.'}
    report = {'status': 'authored_exported', 'source': str(SOURCE), 'blend': str(blend),
        'source_sha256_before': source_hash, 'source_sha256_after': digest(SOURCE), 'source_preserved': True,
        'rig_bind_unchanged': True, 'mesh_geometry_unchanged': True, 'curves': counts,
        'native_contracts': contracts, 'authoring_checks': checks,
        'pose_design': {'coil_to_contact_torso_turn_degrees': math.degrees(.80 - (-.95)),
                        'coil_to_contact_upper_arm_swing_degrees': math.degrees(1.06 - (-1.18)),
                        'coil_frame': 31, 'contact_frame': 51,
                        'release_seconds': 20 / 50 * WINDUP_SECONDS,
                        'release_frames_60fps': 20 / 50 * WINDUP_SECONDS * 60,
                        'stance_width_added_source_cm': 4},
        'style': 'Full-torso coil, wider planted stance, broad club arc, lagging head/wrist and elastic follow-through.',
        'scope': 'One Melee clip. No Blender renders, Unreal changes or gameplay tests.'}
    (OUT / 'ExportReport.json').write_text(json.dumps(exported, indent=2), encoding='utf8')
    (OUT / 'AuthorReport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print('TANK_V5_PASS', json.dumps({'file': str(path), 'checks': checks, 'source_preserved': True}), flush=True)


if __name__ == '__main__':
    main()
