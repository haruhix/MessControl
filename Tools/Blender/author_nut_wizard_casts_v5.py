"""Four broad, continuous caster performances on the unchanged production rig.

Writes only an isolated V5 .blend and report; export with export_nut_style_v2.py.
Existing draft copies can be updated with repair_nut_wizard_v5_recovery.py.
The native release55%, Rain55--85%/1.1s contract remains unchanged.
"""
import ast
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard'
SOURCE = OUT / 'NutWizard_Casts_v4.blend'
OUTPUT = OUT / 'NutWizard_Casts_v5.blend'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
source_hash = digest(SOURCE)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene = bpy.context.scene
rig = bpy.data.objects['rig']
rig.animation_data.use_nla = False
for track in rig.animation_data.nla_tracks:
    track.mute = True
rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
original_props = {b.name: {k: b[k] for k in b.keys() if isinstance(b[k], (float, int, bool))}
                  for b in rig.pose.bones}
controls = [b for b in rig.pose.bones if b.name.startswith('c_')]
rest_signature = [(b.name, b.parent.name if b.parent else None, list(map(list, b.matrix_local)), b.use_deform)
                  for b in rig.data.bones]
mesh = bpy.data.objects['SM_NutWizard']
mesh_signature = hashlib.sha256(json.dumps({
    'verts': [list(v.co) for v in mesh.data.vertices],
    'faces': [(list(p.vertices), p.material_index) for p in mesh.data.polygons],
    'weights': [[(g.group, g.weight) for g in v.groups] for v in mesh.data.vertices],
    'materials': [m.name if m else None for m in mesh.data.materials]}, sort_keys=True).encode()).hexdigest()

tree = ast.parse((ROOT / 'Tools/Blender/author_nut_wizard_style.py').read_text(encoding='utf8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, ast.FunctionDef)
                             and n.name in {'bind', 'reset', 'set_matrix', 'key'}], type_ignores=[]), 'wizard_helpers', 'exec'))
tree = ast.parse((ROOT / 'Tools/Blender/author_nut_wizard_casts_v4.py').read_text(encoding='utf8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, ast.FunctionDef)
                             and n.name in {'smooth', 'lerp', 'blend', 'between', 'pose', 'orient_hand', 'fingers', 'apply', 'curves', 'action_hash'}],
                       type_ignores=[]), 'wizard_v4_rig_helpers', 'exec'))

# Same ready pose as V4; keep idle and the other gameplay slots unchanged.
IDLE = dict(root=(0, 0, -.07), body=(0, 0, 4), head=(-4, 0, -7),
            r=(-.30, -.43, .68), l=(.30, -.41, .97),
            rn=(.2, 0, 1), ln=(-.5, -.1, -.8), rf=(.15, -1, .05), lf=(-.15, -1, .05),
            rdigits=(.22, .32, .22), ldigits=(.12, .24, .18),
            fr=(-.235, .19, .156448588), fl=(.235, .13, .156448588))
reset()
set_matrix('c_root_master.x', rest['c_root_master.x'].translation + Vector(IDLE['root']), IDLE['body'])
bpy.context.view_layer.update()
hips = {n: rig.pose.bones[n].matrix.copy() for n in ('c_thigh_b.l', 'c_thigh_b.r')}


def palm_rotation(normal, finger_direction):
    z, y = Vector(normal).normalized(), Vector(finger_direction)
    y = (y - z * y.dot(z)).normalized()
    x = y.cross(z).normalized()
    z = x.cross(y).normalized()
    return Matrix(((x.x, y.x, z.x), (x.y, y.y, z.y), (x.z, y.z, z.z))).to_quaternion()


def blend(first, second, t):
    result = {k: lerp(first[k], second[k], t) if isinstance(first[k], tuple)
              else first[k] + (second[k] - first[k]) * t for k in first}
    # A normal must never be linearly blended through zero when a palm flips
    # from upward to downward. SLERP preserves a continuous anatomical wrist.
    for side in ('r', 'l'):
        start = palm_rotation(first[side + 'n'], first[side + 'f'])
        end = palm_rotation(second[side + 'n'], second[side + 'f'])
        if start.dot(end) < 0:
            end.negate()
        rotation = start.slerp(end, t)
        result[side + 'n'] = tuple(rotation @ Vector((0, 0, 1)))
        result[side + 'f'] = tuple(rotation @ Vector((0, 1, 0)))
    return result


def performance(knots, t):
    for (start, first), (end, second) in zip(knots, knots[1:]):
        if t <= end:
            return between(first, second, t, start, end)
    return dict(knots[-1][1])


MAGIC_SETTLE = pose(root=(0, 0, -.07), body=(0, 0, 4), head=(-4, 0, -7),
                    r=(-.42, -.50, .90), l=(.42, -.50, .95),
                    rn=(.65, -.4, .65), ln=(-.65, -.4, .65),
                    rf=(-.2, -.4, 1), lf=(.2, -.4, 1),
                    rdigits=(.16, .08, .20), ldigits=(.08, .16, .20))


# Hands retain their anatomical side throughout. The release is an open palm
# flowing through an arc, rather than a straight fist jab.
FIRE_GATHER = pose(root=(-.03, .015, -.13), body=(-12, 0, -24), head=(5, 0, 18),
                   r=(-.56, -.34, 1.12), l=(.34, -.48, .68),
                   rn=(.5, -.2, .7), ln=(-.4, 0, 1), rf=(-.15, -.3, 1), lf=(.2, -1, .1),
                   rdigits=(.28, .18, .32), ldigits=(.15, .3, .18))
FIRE_ORB = pose(root=(-.025, -.015, -.15), body=(-8, 0, -14), head=(8, 0, 8),
                r=(-.34, -.64, .76), l=(.30, -.47, .65),
                rn=(.9, 0, .3), ln=(-.7, -.2, .8), rf=(0, -.2, 1), lf=(.2, -.8, .5),
                rdigits=(.65, .42, .58), ldigits=(.38, .55, .4))
FIRE_RELEASE = pose(root=(.005, -.11, -.10), body=(20, 0, 18), head=(-10, 0, -12),
                    r=(-.39, -.80, 1.01), l=(.29, -.52, .77),
                    rn=(.1, -1, .10), ln=(-.35, -.2, .9), rf=(-.3, -.1, 1), lf=(.3, -.7, .5),
                    rdigits=(.06, .02, .08), ldigits=(.12, .16, .14))
FIRE_FOLLOW = pose(**{**FIRE_RELEASE, 'root': (.02, -.12, -.115), 'body': (25, 0, 25),
                     'head': (-18, 0, -20), 'r': (-.47, -.76, 1.13), 'l': (.38, -.44, .84)})
FIRE_RECOVER = pose(root=(-.015, .005, -.12), body=(-5, 0, -12), head=(5, 0, 8),
                    r=(-.49, -.43, .91), l=(.35, -.48, .74),
                    rn=(.4, -.1, .9), ln=(-.5, -.1, .8), rf=(0, -.5, 1), lf=(.2, -1, .3))


def fire(t):
    return performance([(0, MAGIC_SETTLE), (.18, FIRE_GATHER), (.40, FIRE_ORB),
                        (.55, FIRE_RELEASE), (.66, FIRE_FOLLOW), (.83, FIRE_RECOVER), (1, MAGIC_SETTLE)], t)


TELE_WIDE = pose(root=(0, .01, -.10), body=(-7, 0, -16), head=(-4, 0, 10),
                 r=(-.57, -.37, 1.12), l=(.57, -.37, 1.12),
                 rn=(.7, -.3, .6), ln=(-.7, -.3, .6), rf=(-.1, -.3, 1), lf=(.1, -.3, 1),
                 rdigits=(.2, .1, .25), ldigits=(.1, .2, .25))
TELE_CROWN = pose(root=(0, .015, -.12), body=(4, 0, -20), head=(-16, 0, 16),
                  r=(-.32, -.44, 1.39), l=(.32, -.44, 1.39),
                  rn=(.8, -.15, -.3), ln=(-.8, -.15, -.3), rf=(-.2, 0, 1), lf=(.2, 0, 1),
                  rdigits=(.4, .18, .32), ldigits=(.18, .4, .32))
TELE_COMPRESS = pose(root=(0, .035, -.20), body=(20, 0, -24), head=(-20, 0, 20),
                     r=(-.20, -.58, .82), l=(.20, -.58, .82),
                     rn=(1, 0, .12), ln=(-1, 0, .12), rf=(0, -.2, 1), lf=(0, -.2, 1),
                     rdigits=(.72, .52, .64), ldigits=(.52, .72, .64))
TELE_OPEN = pose(root=(0, -.035, -.04), body=(-12, 0, 18), head=(-8, 0, -16),
                 r=(-.60, -.33, 1.18), l=(.60, -.33, 1.18),
                 rn=(.55, -.7, .35), ln=(-.55, -.7, .35), rf=(-.2, -.15, 1), lf=(.2, -.15, 1),
                 rdigits=(.03, .07, .04), ldigits=(.07, .03, .04))


def teleport(t):
    return performance([(0, MAGIC_SETTLE), (.17, TELE_WIDE), (.34, TELE_CROWN),
                        (.55, TELE_COMPRESS), (.70, TELE_OPEN), (.81, TELE_WIDE), (1, MAGIC_SETTLE)], t)


SUMMON_LOW = pose(root=(0, -.01, -.16), body=(19, 0, -8), head=(-22, 0, 5),
                  r=(-.49, -.52, .48), l=(.49, -.52, .48),
                  rn=(.45, -.8, .30), ln=(-.45, -.8, .30), rf=(-.2, -1, .1), lf=(.2, -1, .1),
                  rdigits=(.08, .18, .14), ldigits=(.18, .08, .14))
SUMMON_GRIP = pose(root=(0, -.015, -.18), body=(20, 0, 5), head=(-25, 0, -5),
                   r=(-.34, -.62, .61), l=(.34, -.62, .61),
                   rn=(.8, -.2, -.2), ln=(-.8, -.2, -.2), rf=(0, -.5, .8), lf=(0, -.5, .8),
                   rdigits=(.7, .35, .62), ldigits=(.35, .7, .62))
SUMMON_PULL = pose(root=(0, -.05, -.04), body=(-12, 0, 10), head=(-14, 0, -8),
                   r=(-.58, -.39, 1.19), l=(.58, -.39, 1.19),
                   rn=(.55, -.25, .8), ln=(-.55, -.25, .8), rf=(-.35, -.4, 1), lf=(.35, -.4, 1),
                   rdigits=(.1, .04, .12), ldigits=(.04, .1, .12))
SUMMON_OVER = pose(**{**SUMMON_PULL, 'root': (0, -.035, -.05), 'body': (-16, 0, 15),
                     'head': (-22, 0, -14), 'r': (-.60, -.35, 1.26), 'l': (.60, -.35, 1.26)})
SUMMON_SEAL = pose(root=(0, 0, -.10), body=(6, 0, -7), head=(-9, 0, 5),
                   r=(-.41, -.50, .94), l=(.41, -.50, .94),
                   rn=(0, -1, -.2), ln=(0, -1, -.2), rf=(0, -.2, 1), lf=(0, -.2, 1))


def summon(t):
    return performance([(0, MAGIC_SETTLE), (.19, SUMMON_LOW), (.38, SUMMON_GRIP),
                        (.55, SUMMON_PULL), (.65, SUMMON_OVER), (.82, SUMMON_SEAL), (1, MAGIC_SETTLE)], t)


RAIN_PREP = pose(root=(0, -.01, -.12), body=(10, 0, -10), head=(-8, 0, 8),
                 r=(-.52, -.42, .84), l=(.52, -.42, .84),
                 rn=(.4, -.4, 1), ln=(-.4, -.4, 1), rf=(-.2, -.3, 1), lf=(.2, -.3, 1))
RAIN_CROWN = pose(root=(0, -.015, -.06), body=(-10, 0, 0), head=(-22, 0, 0),
                  r=(-.47, -.39, 1.38), l=(.47, -.39, 1.38),
                  rn=(.6, -.4, .7), ln=(-.6, -.4, .7), rf=(-.2, -.4, 1), lf=(.2, -.4, 1),
                  rdigits=(.18, .06, .18), ldigits=(.06, .18, .18))


def rain_loop(u):
    angle = 2 * math.pi * u
    # Side-preserving circular conductors: large oval paths around the crown,
    # staggered depth and fingers, continuous velocity at the loop boundary.
    return pose(root=(.014 * math.sin(angle), -.015, -.055 + .012 * math.sin(angle)),
                body=(-10 + 4 * math.sin(angle), 0, 11 * math.sin(angle)),
                head=(-22 + 5 * math.sin(angle + .6), 0, -12 * math.sin(angle)),
                r=(-.45 - .16 * math.sin(angle), -.43 - .09 * math.cos(angle), 1.32 + .12 * math.sin(angle + .4)),
                l=(.45 + .16 * math.sin(angle), -.43 + .09 * math.cos(angle), 1.32 - .12 * math.sin(angle + .4)),
                rn=(.65, -.42 - .18 * math.sin(angle), .65 + .18 * math.cos(angle)),
                ln=(-.65, -.42 + .18 * math.sin(angle), .65 - .18 * math.cos(angle)),
                rf=(-.2 - .15 * math.cos(angle), -.4, 1), lf=(.2 + .15 * math.cos(angle), -.4, 1),
                rdigits=(.18 + .12 * math.sin(angle), .06, .22),
                ldigits=(.06, .18 - .12 * math.sin(angle), .22))


def rain(t):
    if t < .55:
        return performance([(0, MAGIC_SETTLE), (.15, RAIN_PREP), (.35, RAIN_CROWN), (.55, rain_loop(0))], t)
    if t <= .85:
        return rain_loop((t - .55) / .30)
    return between(rain_loop(1), MAGIC_SETTLE, t, .85, 1)


LIB = {
    'Cast': (fire, 'Broad diagonal gather, cupped compression and open-palm release with a continuing arc.'),
    'HeavyCast': (teleport, 'Opposing overhead rune arches compress between separate palms, followed by a wide arrival opening.'),
    'Summon': (summon, 'Grounded sigil grip, broad portal pull and delayed head/body recovery.'),
    'Rain': (rain, 'Large overhead crown and a continuous pair of offset circular conductors; native channel55--85%/1.1s.'),
}


def continuous_rotation_branches(action):
    """Keep Euler and quaternion keys on one branch without changing poses."""
    changed = []
    for curve in curves(action):
        if not curve.data_path.endswith('rotation_euler'):
            continue
        previous, count = None, 0
        for point in curve.keyframe_points:
            value = float(point.co.y)
            if previous is not None:
                value += 2 * math.pi * round((previous - value) / (2 * math.pi))
            if abs(value - point.co.y) > .001:
                delta = value - point.co.y
                point.co.y = value
                point.handle_left.y += delta
                point.handle_right.y += delta
                count += 1
            previous = value
        if count:
            curve.update()
            changed.append(dict(path=curve.data_path, index=curve.array_index, keys=count))
    groups = {}
    for curve in curves(action):
        if curve.data_path.endswith('rotation_quaternion'):
            groups.setdefault(curve.data_path, {})[curve.array_index] = curve
    for path, group in groups.items():
        assert set(group) == {0, 1, 2, 3}, path
        previous, count = None, 0
        for index in range(len(group[0].keyframe_points)):
            points = [group[c].keyframe_points[index] for c in range(4)]
            values = [float(point.co.y) for point in points]
            if previous and sum(a * b for a, b in zip(previous, values)) < 0:
                for point in points:
                    point.co.y = -point.co.y
                    point.handle_left.y = -point.handle_left.y
                    point.handle_right.y = -point.handle_right.y
                values = [-value for value in values]
                count += 1
            previous = values
        if count:
            for curve in group.values():
                curve.update()
            changed.append(dict(path=path, quaternion_flips=count))
    return changed


old_hashes = {a.name: action_hash(a) for a in bpy.data.actions}
backups = {}
records = []
for label, (function, note) in LIB.items():
    name = 'MC_Wizard_' + label
    backup = 'MC_V4_Wizard_' + label
    assert not bpy.data.actions.get(backup), backup
    bpy.data.actions[name].name = backup
    backups[name] = backup
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    for frame in range(1, 102):
        apply(function((frame - 1) / 100))
        rig.animation_data.action = action
        key(frame)
    bind(action)
    angle_repairs = continuous_rotation_branches(action)
    motion_keys = discrete_keys = 0
    for curve in curves(action):
        discrete = curve.data_path.endswith('\"]')
        assert discrete or curve.data_path.endswith(('location', 'rotation_euler', 'rotation_quaternion', 'scale')), curve.data_path
        for point in curve.keyframe_points:
            point.interpolation = 'CONSTANT' if discrete else 'BEZIER'
            if not discrete:
                point.handle_left_type = point.handle_right_type = 'AUTO_CLAMPED'
                motion_keys += 1
            else:
                discrete_keys += 1
        curve.update()
    action.asset_mark()
    action.asset_data.description = note
    action['game_slot'] = label
    action['loop'] = False
    action['release_fraction'] = .55
    action['gestures_version'] = 5
    action['MCSpellGesture'] = 1.0
    if label == 'Rain':
        action['channel_loop_start'] = .55
        action['channel_loop_end'] = .85
        action['channel_loop_period_seconds'] = 1.1
    records.append(dict(label=label, action=name, start=1, end=101, fps=30,
                        duration_seconds=100 / 30, release_fraction=.55,
                        channel_loop=[.55, .85] if label == 'Rain' else None,
                        channel_period_seconds=1.1 if label == 'Rain' else None,
                        smooth_motion_keys=motion_keys, discrete_custom_keys=discrete_keys, description=note))
    records[-1]['continuous_angle_branch_repairs'] = angle_repairs
    print('WIZARD_V5_BAKED', label, flush=True)

# Keep an isolated editable checkpoint before authoring diagnostics. A failed
# diagnostic never forces a second full bake or overwrites the V4 source.
scene.render.fps = 30
scene.frame_start, scene.frame_end = 1, 101
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))

qa = {}
legacy = json.loads((ROOT / 'ArtSource/NutWizard/ExportReport.json').read_text(encoding='utf8'))
exported_bones = {bone['name'] for entry in legacy['files'] if entry['file'] == legacy['mesh_file']
                  for bone in entry['fbx']['bones']}
limbs = ('arm_stretch.l', 'arm_stretch.r', 'forearm_stretch.l', 'forearm_stretch.r',
         'thigh_stretch.l', 'thigh_stretch.r', 'leg_stretch.l', 'leg_stretch.r')
for record in records:
    action = bpy.data.actions[record['action']]
    bind(action)
    minimum_ratio, maximum_ratio = 1., 1.
    feet = {n: [] for n in ('foot.r', 'foot.l')}
    hands = {n: [] for n in ('hand.r', 'hand.l')}
    minimum_floor = 100.
    minimum_separation = 100.
    minimum_body_clearance = 100.
    peak_rotation = 0.
    peak_rotation_bone = None
    peak_rotation_frame = None
    prior_rotations = None
    for sample in range(201):
        frame = 1 + sample * .5
        scene.frame_set(int(frame), subframe=frame % 1)
        bpy.context.view_layer.update()
        current_rotations = {}
        for bone in rig.pose.bones:
            assert all(math.isfinite(v) for row in bone.matrix for v in row), (record['label'], frame, bone.name)
            if bone.name in exported_bones:
                current_rotations[bone.name] = bone.matrix.to_quaternion()
                if prior_rotations:
                    dot = min(1., abs(prior_rotations[bone.name].dot(current_rotations[bone.name])))
                    angle = math.degrees(2 * math.acos(dot))
                    if angle > peak_rotation:
                        peak_rotation, peak_rotation_bone, peak_rotation_frame = angle, bone.name, frame
        prior_rotations = current_rotations
        for name in limbs:
            bone = rig.pose.bones[name]
            ratio = (bone.tail - bone.head).length / rig.data.bones[name].length
            minimum_ratio = min(minimum_ratio, ratio)
            maximum_ratio = max(maximum_ratio, ratio)
            assert .945 < ratio < 1.055, (record['label'], frame, name, ratio)
        for name in feet:
            feet[name].append(rig.pose.bones[name].head.copy())
        right, left = (rig.pose.bones[n].head.copy() for n in ('hand.r', 'hand.l'))
        hands['hand.r'].append(right)
        hands['hand.l'].append(left)
        minimum_separation = min(minimum_separation, (left - right).length)
        assert right.x < -.10 and left.x > .10, (record['label'], frame, 'hand side crossing', list(right), list(left))
        body = rig.pose.bones['root.x'].head
        minimum_body_clearance = min(minimum_body_clearance, (right - body).length, (left - body).length)
        if sample % 10 == 0 or sample == 110:
            evaluated = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
            data = evaluated.to_mesh()
            try:
                minimum_floor = min(minimum_floor, min((evaluated.matrix_world @ vertex.co).z for vertex in data.vertices))
            finally:
                evaluated.to_mesh_clear()
    assert minimum_floor > -.005, (record['label'], 'floor', minimum_floor)
    drift = {n: max((p - values[0]).length for p in values) for n, values in feet.items()}
    assert max(drift.values()) < .004, (record['label'], 'foot drift', drift)
    assert minimum_separation > .32, (record['label'], 'hand overlap', minimum_separation)
    # Includes inherited finger-roll conventions. A large single-frame jump
    # indicates IK pole flips/Quaternion discontinuity, not a cartoon accent.
    seam = None
    if record['label'] == 'Rain':
        scene.frame_set(56)
        bpy.context.view_layer.update()
        first = {b.name: b.matrix.copy() for b in rig.pose.bones if b.bone.use_deform}
        scene.frame_set(86)
        bpy.context.view_layer.update()
        seam = max(abs(v - rig.pose.bones[name].matrix[i][j]) for name, matrix in first.items()
                   for i, row in enumerate(matrix) for j, v in enumerate(row))
        assert seam < .0001, seam
    assert not any(point.interpolation == 'CONSTANT' for curve in curves(action)
                   if not curve.data_path.endswith('\"]') for point in curve.keyframe_points)
    qa[record['label']] = dict(finite=True, half_frame_samples=201, floor_m=minimum_floor,
                              limb_ratio_range=[minimum_ratio, maximum_ratio], foot_drift_m=drift,
                              hand_min_separation_m=minimum_separation, hands_anatomical_sides=True,
                              hand_body_center_min_distance_m=minimum_body_clearance,
                              hand_arc_lengths_m={n: sum((b - a).length for a, b in zip(values, values[1:])) for n, values in hands.items()},
                              release_wrists_m={n: list(values[110]) for n, values in hands.items()},
                              peak_deform_rotation_per_half_frame_degrees=peak_rotation, rain_loop_seam=seam,
                              peak_rotation_bone=peak_rotation_bone, peak_rotation_frame=peak_rotation_frame,
                              constant_transform_keys=0)
    print('WIZARD_V5_QA', record['label'], json.dumps(qa[record['label']]), flush=True)

for name, original in old_hashes.items():
    assert action_hash(bpy.data.actions[backups.get(name, name)]) == original, name
assert rest_signature == [(b.name, b.parent.name if b.parent else None, list(map(list, b.matrix_local)), b.use_deform)
                           for b in rig.data.bones]
assert digest(SOURCE) == source_hash
scene.render.fps = 30
scene.frame_start, scene.frame_end = 1, 101
bind(bpy.data.actions['MC_Wizard_Cast'])
scene.frame_set(56)
scene['MC_WizardStyleRevision'] = 'v5 broad continuous mage gestures, no crossing wrists'
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
report = dict(passed=True, blend=str(OUTPUT), source_sha256=source_hash, blend_sha256=digest(OUTPUT),
              source_unchanged=True, previous_actions_preserved=True, rest_preserved=True,
              meshes_weights_preserved=True, original_mesh_signature=mesh_signature,
              records=records, qa=qa, emitter_constraint=False, game_imported=False, draft_cast_only=False)
report['angular_qc'] = dict(diagnostic_only=True, threshold_pass_claimed=False,
                          runtime_validation='Solo PIE after Unreal animation import.')
report['stable_magic_settle'] = True
(OUT / 'WizardCastsV5Report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print('WIZARD_V5_COMPLETE', flush=True)
