"""Blender-only cast studies made from the reviewed original performances.

Retains the original ARP rig, constraints, skin, source actions and foot motion.
Adds four editable Actions to a new .blend; never imports into Unreal.
"""
import hashlib
import importlib.util
import json
import math
import re
from pathlib import Path
from types import SimpleNamespace

import bpy
from mathutils import Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/Wizard/Original/NutWizard_Original.blend'
DEST = ROOT / 'ArtSource/NutAnimationStyle/Wizard/MixamoVariants'
BLEND = DEST / 'NutWizard_Mixamo_CastVariations.blend'
CASES = ('Original', 'Quick', 'Heavy', 'Ritual')
LABELS = ('Исходный Mixamo', 'Быстрый выброс', 'Тяжёлый заряд', 'Ритуальный каст')
FRAMES = 132


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def curves(action):
    return [fc for layer in action.layers for strip in layer.strips
            for bag in strip.channelbags for fc in bag.fcurves]


def smooth(value):
    value = max(0., min(1., value))
    return value * value * (3 - 2 * value)


def remap(frame, points):
    """Monotone cubic timing, including true holds, without overshoot."""
    if frame <= points[0][0]:
        return float(points[0][1])
    if frame >= points[-1][0]:
        return float(points[-1][1])
    widths = [b[0] - a[0] for a, b in zip(points, points[1:])]
    slopes = [(b[1] - a[1]) / width for a, b, width in zip(points, points[1:], widths)]
    tangents = [slopes[0]]
    for index in range(1, len(points) - 1):
        a, b = slopes[index - 1], slopes[index]
        if a * b <= 0:
            tangents.append(0.)
        else:
            w1, w2 = 2 * widths[index] + widths[index - 1], widths[index] + 2 * widths[index - 1]
            tangents.append((w1 + w2) / (w1 / a + w2 / b))
    tangents.append(slopes[-1])
    for index, (a, b) in enumerate(zip(points, points[1:])):
        if frame <= b[0]:
            width = widths[index]
            t = (frame - a[0]) / width
            return ((2*t**3 - 3*t**2 + 1)*a[1] + (t**3 - 2*t**2 + t)*width*tangents[index]
                    + (-2*t**3 + 3*t**2)*b[1] + (t**3 - t**2)*width*tangents[index+1])


def bind(rig, action, defaults):
    rig.animation_data.action = None
    for bone in rig.pose.bones:
        bone.matrix_basis = defaults[bone.name]['basis'].copy()
        for key, value in defaults[bone.name]['properties'].items():
            bone[key] = value
    rig.animation_data.action = action
    if action and action.slots:
        rig.animation_data.action_slot = action.slots[0]
    rig.animation_data.use_nla = False
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    rig.update_tag()
    bpy.context.view_layer.update()


def copy_pose(pose):
    return {name: (loc.copy(), rot.copy(), scale.copy()) for name, (loc, rot, scale) in pose.items()}


def blend_pose(first, second, weight, selected=None):
    result = copy_pose(first)
    for name in selected if selected is not None else first:
        a, b = first[name], second[name]
        result[name] = (a[0].lerp(b[0], weight), a[1].slerp(b[1], weight), a[2].lerp(b[2], weight))
    return result


def main():
    assert bpy.app.background, 'Author in an isolated Blender process'
    assert not BLEND.exists(), 'Keep existing variants; choose a fresh revision'
    before = sha(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene = bpy.context.scene
    scene.render.fps, scene.render.fps_base = 30, 1.
    scene.frame_start, scene.frame_end = 1, FRAMES
    rig, mesh = bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
    rig.animation_data_create()
    defaults = {bone.name: dict(basis=bone.matrix_basis.copy(), properties={key: bone[key] for key in bone.keys()
                if isinstance(bone[key], (bool, int, float))}) for bone in rig.pose.bones}
    originals = {name: bpy.data.actions['NUT_W_' + name] for name in ('Firenut', 'CastSummon', 'NutRain')}
    signatures = {name: [(fc.data_path, fc.array_index, tuple((tuple(key.co), key.interpolation) for key in fc.keyframe_points))
                         for fc in curves(action)] for name, action in originals.items()}
    rest = {bone.name: tuple(value for row in bone.matrix_local for value in row) for bone in rig.data.bones}
    paths = {fc.data_path for action in originals.values() for fc in curves(action)}
    names = sorted({re.match(r'pose\.bones\["([^"]+)"\]', path)[1] for path in paths if path.startswith('pose.bones[')}
                   | {'c_head.x', 'c_shoulder.l', 'c_shoulder.r', 'c_hand_fk.l', 'c_hand_fk.r'})
    property_paths = sorted(path for path in paths if path.endswith(']'))
    property_names = [re.match(r'pose\.bones\["([^"]+)"\]\["([^"]+)"\]', path).groups() for path in property_paths]
    upper = {name for name in names if not any(word in name for word in ('foot', 'toes', 'leg', 'thigh', 'root', 'traj'))}
    cache = {}

    def sample(action_name, frame):
        key = action_name, round(frame, 7)
        if key not in cache:
            bind(rig, originals[action_name], defaults)
            scene.frame_set(math.floor(frame), subframe=frame - math.floor(frame))
            bpy.context.view_layer.update()
            cache[key] = ({name: rig.pose.bones[name].matrix_basis.decompose() for name in names},
                          {(name, prop): float(rig.pose.bones[name][prop]) for name, prop in property_names})
        return cache[key]

    neutral, neutral_props = sample('Firenut', 1.)
    # The same cast preparation begins at .4s. Every study ends in the same
    # exact starting stance; source foot controls remain intact during action.
    quick = [(13, 1), (24, 13), (37, 26), (47, 42), (78, 70)]
    heavy = [(13, 1), (41, 18), (51, 18), (67, 36), (76, 44), (112, 70)]
    ritual = [(13, 1), (32, 25), (47, 34), (71, 50), (105, 66)]
    rain = [(39, 26), (68, 62), (84, 74), (105, 99)]
    timeline = {}
    for case in CASES:
        records = []
        end = {'Original': 82, 'Quick': 78, 'Heavy': 112, 'Ritual': 105}[case]
        return_end = min(131, end + 18)
        for frame in range(1, FRAMES + 1):
            if case == 'Original':
                pose, props = sample('Firenut', max(1., min(70., frame - 12.)))
            elif case == 'Quick':
                pose, props = sample('Firenut', remap(frame, quick))
            elif case == 'Heavy':
                pose, props = sample('Firenut', remap(frame, heavy))
            else:
                pose, props = sample('CastSummon', remap(frame, ritual))
                other, _ = sample('NutRain', remap(frame, rain))
                amount = .9 * smooth((frame - 39) / 22) * (1 - smooth((frame - 78) / 27))
                pose = blend_pose(pose, other, amount, upper)
            pose = copy_pose(pose)
            props = dict(props)
            if case in ('Quick', 'Heavy'):
                # Small local shoulder/head accents; original forearm hinge,
                # wrist path, stretch and feet are never replaced or twisted.
                amount = smooth((frame - 15) / 14) * (1 - smooth((frame - 72) / 22))
                angle = math.radians(4 if case == 'Quick' else 8)
                head = pose['c_head.x']
                pose['c_head.x'] = (head[0], head[1] @ Quaternion((1, 0, 0), angle * amount), head[2])
                if case == 'Heavy':
                    for name, sign in (('c_shoulder.l', 1), ('c_shoulder.r', -1)):
                        value = pose[name]
                        pose[name] = (value[0], value[1] @ Quaternion((0, 1, 0), math.radians(3) * amount * sign), value[2])
            # Common transitions keep differences in the source gesture/timing
            # legible, rather than comparing mismatched rest stances.
            entrance = smooth((frame - 13) / 9) if case == 'Ritual' else 1.
            if frame < 13:
                pose, props = copy_pose(neutral), dict(neutral_props)
            elif entrance < 1.:
                pose = blend_pose(neutral, pose, entrance)
                props = {key: neutral_props[key] + (value-neutral_props[key])*entrance for key, value in props.items()}
            if frame > end:
                amount = smooth((frame - end) / (return_end - end))
                pose = blend_pose(pose, neutral, amount)
                props = {key: value + (neutral_props[key]-value)*amount for key, value in props.items()}
            for name, transform in pose.items():
                assert all(math.isfinite(value) for component in transform for value in component), (case, frame, name)
            records.append((pose, props))
        timeline[case] = records

    report = dict(passed=False, source=str(SOURCE), source_sha256=before, blend=str(BLEND), source_fps=30,
                  timeline_frames=FRAMES, unchanged_rig=True, scope='Blender-only studies. No Unreal integration.', cases=[])
    actions = {}
    for case, label in zip(CASES, LABELS):
        action = bpy.data.actions.new('MC_Mixamo_' + case)
        action.use_fake_user = True
        action['source'] = 'NUT_W_Firenut' if case != 'Ritual' else 'NUT_W_CastSummon + NUT_W_NutRain upper body'
        action['variant_label'], action['frame_start'], action['duration_seconds'] = label, 1, FRAMES/30
        bind(rig, action, defaults)
        previous_euler, previous_quat = {}, {}
        for frame, (pose, props) in enumerate(timeline[case], 1):
            scene.frame_set(frame)
            for name, (loc, rotation, scale) in pose.items():
                bone = rig.pose.bones[name]
                bone.location, bone.scale = loc, scale
                if bone.rotation_mode == 'QUATERNION':
                    if name in previous_quat and previous_quat[name].dot(rotation) < 0:
                        rotation = -rotation
                    bone.rotation_quaternion = rotation
                    previous_quat[name] = rotation.copy()
                    prop = 'rotation_quaternion'
                else:
                    assert bone.rotation_mode not in ('AXIS_ANGLE',), bone.name
                    bone.rotation_euler = rotation.to_euler(bone.rotation_mode, previous_euler.get(name)) if name in previous_euler else rotation.to_euler(bone.rotation_mode)
                    previous_euler[name] = bone.rotation_euler.copy()
                    prop = 'rotation_euler'
                for path in ('location', prop, 'scale'):
                    bone.keyframe_insert(data_path=path, frame=frame, group=name)
            for (name, prop), value in props.items():
                bone = rig.pose.bones[name]
                bone[prop] = value
                bone.keyframe_insert(data_path='["' + prop + '"]', frame=frame, group=name)
        for fc in curves(action):
            for key in fc.keyframe_points:
                key.interpolation = 'LINEAR'
            fc.update()
        actions[case] = action
        first, last = timeline[case][0][0], timeline[case][-1][0]
        assert all((first[name][0] - last[name][0]).length < 1e-6 and first[name][1].rotation_difference(last[name][1]).angle < 1e-5 for name in names)
        report['cases'].append(dict(name=case, label=label, action=action.name, source=action['source'], active_frames=[13, {'Original':82, 'Quick':78, 'Heavy':112, 'Ritual':105}[case]], curves=len(curves(action))))
    for name, action in originals.items():
        assert signatures[name] == [(fc.data_path, fc.array_index, tuple((tuple(key.co), key.interpolation) for key in fc.keyframe_points)) for fc in curves(action)]
    assert rest == {bone.name: tuple(value for row in bone.matrix_local for value in row) for bone in rig.data.bones}
    spec = importlib.util.spec_from_file_location('mixamo_review_util', Path(__file__).with_name('preview_wizard_human_v8.py'))
    util = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(util)
    settings = SimpleNamespace(width=640, height=480, render_aa='5', focus=(-.01,-.13,.85), ortho_scale=3.1)
    cameras = util.setup_scene(scene, rig, mesh, settings)
    util.activate = bind
    scale = util.fit_cameras(scene, rig, mesh, cameras, [dict(action=action, frame_start=1, frame_end=FRAMES) for action in actions.values()], defaults, 3.1)
    scene.camera = cameras['ThreeQuarter']
    bind(rig, actions['Heavy'], defaults)
    scene.frame_set(48)
    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    notes = bpy.data.texts.new('CAST_VARIANTS_README')
    notes.write('Mixamo-based cast studies, Blender only.\nOriginal source and actions are preserved.\n'
                'Choose MC_Mixamo_Original / Quick / Heavy / Ritual in the Action Editor.\n'
                '30fps authoring, frames1-132; 60fps review samples subframes.\n'
                'Original: source Firenut with common intro/outro.\nQuick: shorter release.\n'
                'Heavy: longer charge hold, original release arc, small head/shoulder accents.\n'
                'Ritual: Summon foundation with Rain upper-body opening; Summon foot controls retained.\n')
    scene.timeline_markers.clear()
    for frame, name in ((1, 'Common stance'), (13, 'Cast starts'), (41, 'Heavy load'), (51, 'Heavy release'), (112, 'Heavy recovery'), (132, 'Common stance')):
        scene.timeline_markers.new(name, frame=frame)
    # Keep animation, materials and skin editable in a self-contained file.
    bpy.ops.file.pack_all()
    DEST.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(BLEND))
    assert sha(SOURCE) == before
    report.update(passed=True, blend_sha256=sha(BLEND), original_actions_preserved=True,
                  source_file_preserved=True, neutral_endpoints_match=True, ortho_scale=scale,
                  variant_changes='Monotone timing, bounded head/shoulder accents, real source pose composition; no new spring simulation')
    (DEST/'CastVariationsReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print('MIXAMO_CAST_VARIATIONS_READY', str(BLEND), flush=True)


if __name__ == '__main__':
    main()
