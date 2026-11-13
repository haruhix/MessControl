"""Author wide, edge-leading two-hand katana swings on the reviewed Blender rig.

Run against the saved Katana master with an explicit E: scratch directory.
The first candidate and its measurements are written there before integration.
"""
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector


WINDOWS = {
    "Diagonal From Right": ((7, 14, -85., 85., 10., -8.),),
    "Diagonal From Left": ((8, 15, 85., -85., 10., -8.),),
    "Horizontal Right to Left": ((7, 14, -85., 85., 0., 0.),),
    "Overhead": ((9, 17, 0., 0., 65., -20.),),
    "Charged Series": ((14, 19, -85., 85., 7., -5.),
                       (21, 26, 85., -85., 7., -5.),
                       (28, 33, 0., 0., 65., -20.)),
}
TIP = Vector((-.145, 0., 1.9756))
TIP_LENGTH = TIP.length
RECOVERY_DETOURS = {
    "Diagonal From Right": ((21., 25.5, .12),),
    "Horizontal Right to Left": ((20., 24.5, .12),),
    "Charged Series": ((26., 28., .12),),
}


def smooth(value):
    value = max(0., min(1., value))
    return value * value * (3. - 2. * value)


def trajectory(bearing, elevation):
    """Solve the actual curved tip bearing with the guard in front of the body."""
    yaw = math.radians(bearing)
    guard = Vector((.35 * math.sin(yaw), -.42 - .36 * math.cos(yaw), .64))
    minimum = math.asin((.065 - guard.z) / TIP_LENGTH)
    pitch = max(math.radians(elevation), minimum)
    direction = Vector((math.sin(yaw), -math.cos(yaw), 0.))
    horizontal_length = TIP_LENGTH * math.cos(pitch)
    projection = guard.dot(direction)
    radius = projection + math.sqrt(projection * projection + horizontal_length * horizontal_length
                                    - guard.x * guard.x - guard.y * guard.y)
    tip = direction * radius
    tip.z = guard.z + TIP_LENGTH * math.sin(pitch)
    return guard, tip


def cutting_pose(window, frame):
    low, high, start, finish, rise, fall = window
    fall = max(fall, math.degrees(math.asin((.065 - .64) / TIP_LENGTH)))

    def points(at):
        phase = smooth((at - low) / (high - low))
        return trajectory(start + (finish - start) * phase, rise + (fall - rise) * phase)

    guard, tip = points(frame)
    before = points(max(low, frame - .025))[1]
    after = points(min(high, frame + .025))[1]
    axis = (tip - guard).normalized()
    tangent = after - before
    tangent = (tangent - axis * tangent.dot(axis)).normalized()
    # The tip is curved toward local -X. Solve a rigid frame that puts the
    # real tip on its trajectory while +X leads the actual cutting movement.
    local = TIP.normalized()
    edge = axis * local.x + tangent * local.z
    blade = axis * local.z - tangent * local.x
    normal = blade.cross(edge).normalized()
    rotation = Matrix((edge, normal, blade)).transposed().to_quaternion()
    return Matrix.LocRotScale(guard, rotation, Vector((1., 1., 1.)))


def clear_floor(pose, points, grip):
    if min((pose @ point).z for point in points) >= .045:
        return pose
    pivot = pose @ grip
    axis = (pose.to_quaternion() @ Vector((0., 0., 1.))).cross(Vector((0., 0., 1.))).normalized()
    if axis.length < .5:
        axis = Vector((1., 0., 0.))

    def tilted(angle):
        rotation = Quaternion(axis, angle)
        return Matrix.LocRotScale(pivot - rotation @ (pivot - pose.translation),
                                 rotation @ pose.to_quaternion(), pose.to_scale())

    low, high = 0., math.radians(120.)
    for _ in range(14):
        middle = (low + high) * .5
        candidate = tilted(middle)
        if min((candidate @ point).z for point in points) < .045:
            low = middle
        else:
            high = middle
    return tilted(high)


def blend(first, second, amount):
    amount = smooth(amount)
    return Matrix.LocRotScale(first.translation.lerp(second.translation, amount),
                             first.to_quaternion().slerp(second.to_quaternion(), amount),
                             first.to_scale().lerp(second.to_scale(), amount))


def authored_pose(windows, frame, end, original):
    first, last = windows[0], windows[-1]
    if frame < first[0]:
        return blend(original, cutting_pose(first, first[0]), (frame - 1.) / (first[0] - 1.))
    if frame > last[1]:
        return blend(cutting_pose(last, last[1]), original, (frame - last[1]) / (end - last[1]))
    for index, window in enumerate(windows):
        if window[0] <= frame <= window[1]:
            return cutting_pose(window, frame)
        if index + 1 < len(windows) and window[1] < frame < windows[index + 1][0]:
            following = windows[index + 1]
            return blend(cutting_pose(window, window[1]), cutting_pose(following, following[0]),
                         (frame - window[1]) / (following[0] - window[1]))
    raise ValueError(frame)


def action_curves(owner):
    data = owner.animation_data
    for layer in data.action.layers:
        for strip in layer.strips:
            bag = strip.channelbag(data.action_slot)
            if bag:
                yield bag.fcurves


def linearize(owner, paths=None):
    for curves in action_curves(owner):
        for curve in curves:
            if paths is None or curve.data_path in paths:
                for key in curve.keyframe_points:
                    key.interpolation = 'LINEAR'


def main():
    assert bpy.app.version >= (5, 2)
    master = Path(bpy.data.filepath)
    assert all('Katana | ' + label in bpy.data.scenes for label in WINDOWS)
    assert not any(scene.get('wide_katana_version') for scene in bpy.data.scenes), \
        'Run against the retained pre-wide master or its previous source revision'
    output = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
    assert output.drive.upper() == 'E:'
    output.mkdir(parents=True, exist_ok=True)
    backup = output / 'KatanaCutVariants.before_wide.blend'
    if not backup.exists():
        backup.write_bytes(master.read_bytes())
    assert hashlib.sha256(backup.read_bytes()).digest() == hashlib.sha256(master.read_bytes()).digest()
    report = {'source': str(master), 'candidate': str(output / 'KatanaWideCandidate.blend'), 'clips': []}

    for label, windows in WINDOWS.items():
        scene = bpy.data.scenes['Katana | ' + label]
        bpy.context.window.scene = scene
        driver = next(o for o in scene.objects if o.name.startswith('Katana | Two Hand Driver'))
        rig = next(o for o in scene.objects if o.type == 'ARMATURE')
        root = rig.pose.bones['c_root.x']
        poles = [rig.pose.bones['c_arms_pole.' + side] for side in ('r', 'l')]
        scene.frame_set(1)
        blade = next(o for o in driver.children if o.name.startswith('GEO-Katana Blade') and 'Collar' not in o.name)
        relative = driver.matrix_world.inverted() @ blade.matrix_world
        blade_points = [relative @ vertex.co for vertex in blade.data.vertices]
        grip = next(o for o in driver.children if o.name.startswith('Grip | Katana Right'))
        grip_point = (driver.matrix_world.inverted() @ grip.matrix_world).translation
        end = scene.frame_end
        baseline = []
        baseline_arm_max = 0.
        for index in range((end - 1) * 4 + 1):
            frame = 1. + index * .25
            scene.frame_set(int(frame), subframe=frame % 1.)
            baseline.append((frame, driver.matrix_world.copy(), root.matrix.copy(),
                             [pole.matrix.copy() for pole in poles]))
            graph = bpy.context.evaluated_depsgraph_get()
            original_rig = rig.evaluated_get(graph)
            for side in ('r', 'l'):
                positions = [(original_rig.matrix_world @ original_rig.pose.bones[bone + '.' + side].matrix).translation
                             for bone in ('arm', 'forearm', 'hand')]
                baseline_arm_max = max(baseline_arm_max,
                                       sum((b - a).length for a, b in zip(positions, positions[1:])) * 100.)
        driver.animation_data_clear()
        if rig.animation_data.action.users > 1:
            rig.animation_data.action = rig.animation_data.action.copy()
        rotation_channel = 'rotation_quaternion' if root.rotation_mode == 'QUATERNION' else 'rotation_euler'
        changed_paths = {root.path_from_id(rotation_channel), *(pole.path_from_id('location') for pole in poles)}
        for curves in action_curves(rig):
            for curve in list(curves):
                if curve.data_path in changed_paths:
                    curves.remove(curve)
        previous = None
        for frame, original, body, elbow_matrices in baseline:
            scene.frame_set(int(frame), subframe=frame % 1.)
            pose = authored_pose(windows, frame, end, original)
            # Route the whole grip assembly in front of the belt on recovery.
            # The detour is zero at active boundaries and the shared guard.
            for low, high, distance in RECOVERY_DETOURS.get(label, ()):
                if low < frame < high:
                    pose.translation.y -= distance * math.sin(math.pi * (frame - low) / (high - low)) ** 2
            pose = clear_floor(pose, blade_points, grip_point)
            weight = min(smooth((frame - 1.) / (windows[0][0] - 1.)),
                         1. - smooth((frame - windows[-1][1]) / (end - windows[-1][1])))
            tip = pose @ TIP
            bearing = math.atan2(tip.x, -tip.y)
            torso_turn = Quaternion(Vector((0., 0., 1.)), bearing * .16 * weight)
            root.matrix = Matrix.LocRotScale(body.translation, torso_turn @ body.to_quaternion(), body.to_scale())
            root.keyframe_insert(rotation_channel, frame=frame)
            for index, (pole, elbow) in enumerate(zip(poles, elbow_matrices)):
                side = -1. if index == 0 else 1.
                position = elbow.translation + Vector((side * .035, -.025, .035)) * weight
                position = body.translation + torso_turn @ (position - body.translation)
                target = elbow.copy()
                target.translation = position
                pole.matrix = target
                pole.keyframe_insert('location', frame=frame)
            driver.matrix_world = pose
            if previous is not None:
                driver.rotation_quaternion.make_compatible(previous)
            previous = driver.rotation_quaternion.copy()
            for channel in ('location', 'rotation_quaternion', 'scale'):
                driver.keyframe_insert(channel, frame=frame)
        driver.animation_data.action.use_fake_user = True
        linearize(driver)
        linearize(rig, changed_paths)
        for index, window in enumerate(windows):
            for event, frame in (('start', window[0]), ('end', window[1])):
                name = f'Wide cut {index + 1} {event}'
                marker = scene.timeline_markers.get(name) or scene.timeline_markers.new(name)
                marker.frame = frame
        scene['wide_katana_version'] = 1
        measurements = []
        for index in range((end - 1) * 4 + 1):
            frame = 1. + index * .25
            scene.frame_set(int(frame), subframe=frame % 1.)
            bpy.context.view_layer.update()
            graph = bpy.context.evaluated_depsgraph_get()
            evaluated = rig.evaluated_get(graph)
            weapon = driver.evaluated_get(graph).matrix_world
            errors = {}
            arms = {}
            for side, name in (('r', 'Right'), ('l', 'Left')):
                hand = evaluated.matrix_world @ evaluated.pose.bones['hand.' + side].matrix
                guide = next(o for o in driver.children if o.name.startswith('Grip | IK ' + name))
                target = guide.evaluated_get(graph).matrix_world
                errors[side] = (hand.translation - target.translation).length * 100.
                positions = [(evaluated.matrix_world @ evaluated.pose.bones[bone + '.' + side].matrix).translation
                             for bone in ('arm', 'forearm', 'hand')]
                arms[side] = sum((b - a).length for a, b in zip(positions, positions[1:])) * 100.
            points = []
            for obj in scene.objects:
                if obj.name.startswith('GEO-Katana '):
                    shape = obj.evaluated_get(graph)
                    mesh = shape.to_mesh()
                    points.extend(shape.matrix_world @ vertex.co for vertex in mesh.vertices)
                    shape.to_mesh_clear()
            tip = weapon @ TIP
            measurements.append({'frame': frame, 'grip_error_cm': errors, 'arm_length_cm': arms,
                                 'tip': list(tip), 'tip_bearing_degrees': math.degrees(math.atan2(tip.x, -tip.y)),
                                 'floor_cm': min(point.z for point in points) * 100.})
        report['clips'].append({'name': label, 'windows': windows, 'baseline_arm_max_cm': baseline_arm_max,
                                'samples': measurements})
        scene.frame_set(1)
    bpy.context.window.scene = bpy.data.scenes['Katana | Heavy Carry']
    bpy.context.scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=report['candidate'])
    (output / 'WideAuthorReport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print('KATANA_WIDE_CANDIDATE_SAVED', flush=True)


if __name__ == '__main__':
    main()
