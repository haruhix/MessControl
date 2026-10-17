"""Independent fire-cast posing draft grounded in the saved ARP rig report.

This module is data/math only: it never imports bpy or changes a blend file.
Run with normal Python to print explicit FK Euler/quaternion pose data.
FK arm poses are solved against the inspected local-Z-only elbow hinge.
The root author owns live Blender authoring and visual approval of this draft.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / "Saved/Codex/WizardWorkbenchRigInspection.json"
RELEASE_SOURCE = .55
TELL_SECONDS = .9
RECOVER_SECONDS = .9
SOURCE_FPS = 30
SOURCE_FRAME_FIRST = 1
SOURCE_FRAME_LAST = 101


def source_frame(source_t):
    return SOURCE_FRAME_FIRST + source_t*(SOURCE_FRAME_LAST-SOURCE_FRAME_FIRST)


def game_seconds(source_t):
    """Keep source release .55 at the exact .9-second gameplay tell boundary."""
    if source_t <= RELEASE_SOURCE:
        return source_t / RELEASE_SOURCE * TELL_SECONDS
    return TELL_SECONDS + (source_t - RELEASE_SOURCE) / (1-RELEASE_SOURCE) * RECOVER_SECONDS


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def mul(a, x): return tuple(v*x for v in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def unit(a): return mul(a, 1/math.sqrt(dot(a, a)))
def columns(x, y, z): return tuple(tuple(c[r] for c in (x, y, z)) for r in range(3))
def transpose(a): return tuple(zip(*a))
def mv(a, b): return tuple(dot(row, b) for row in a)
def mm(a, b): return tuple(tuple(dot(row, col) for col in transpose(b)) for row in a)


def euler_matrix(degrees):
    x, y, z = (math.radians(v) for v in degrees)
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    # Blender Euler XYZ: column vectors, Rz @ Ry @ Rx.
    return ((cz*cy, cz*sy*sx-sz*cx, cz*sy*cx+sz*sx),
            (sz*cy, sz*sy*sx+cz*cx, sz*sy*cx-cz*sx),
            (-sy, cy*sx, cy*cx))


def matrix_euler(a):
    y = math.asin(max(-1, min(1, -a[2][0])))
    if abs(math.cos(y)) > 1e-6:
        x, z = math.atan2(a[2][1], a[2][2]), math.atan2(a[1][0], a[0][0])
    else:
        x, z = math.atan2(-a[1][2], a[1][1]), 0
    return tuple(math.degrees(v) for v in (x, y, z))


def matrix_quaternion(a):
    # Blender quaternion order WXYZ; branch avoids unstable near-180deg trace.
    trace = sum(a[i][i] for i in range(3))
    if trace > 0:
        s = math.sqrt(1+trace)*2
        q = (.25*s, (a[2][1]-a[1][2])/s, (a[0][2]-a[2][0])/s, (a[1][0]-a[0][1])/s)
    else:
        i = max(range(3), key=lambda v: a[v][v]); j, k = (i+1)%3, (i+2)%3
        s = math.sqrt(max(0, 1+a[i][i]-a[j][j]-a[k][k]))*2
        xyz = [0., 0., 0.]; xyz[i] = .25*s
        xyz[j], xyz[k] = (a[j][i]+a[i][j])/s, (a[k][i]+a[i][k])/s
        q = ((a[k][j]-a[j][k])/s, *xyz)
    return unit(q)


def axis_angle(axis, degrees):
    x, y, z = unit(axis); a = math.radians(degrees); c, s, v = math.cos(a), math.sin(a), 1-math.cos(a)
    return ((x*x*v+c, x*y*v-z*s, x*z*v+y*s),
            (y*x*v+z*s, y*y*v+c, y*z*v-x*s),
            (z*x*v-y*s, z*y*v+x*s, z*z*v+c))


def rest_rotation(bone):
    return columns(*(bone["axes"][a] for a in "XYZ"))


def hand_rotation(normal, digit_direction):
    z = unit(normal)
    y = unit(sub(digit_direction, mul(z, dot(digit_direction, z))))
    return columns(unit(cross(y, z)), y, z)


def solve_current_fk_chain(*, arm_origin, arm_basis0, forearm_basis0, hand_basis0,
                           upper_length, lower_length, wrist_target, elbow_hint,
                           hand_normal, digit_direction, side):
    """Pure solver for CURRENT evaluated zero-FK matrices of the revised rig.

    Root calls this after posing root/pelvis, shell(head), and shoulder; reset
    c_arm_fk/c_forearm_fk/c_hand_fk basis to identity first, then evaluate.
    Pass current world/armature-space ROTATION matrices as nested rows. The
    shoulder constraint's evaluated arm head is arm_origin. All target vectors
    must use the same space. This handles reparenting arms/shoulders to head.x;
    no root/head rotation is guessed or duplicated here.

    This function returns quaternion basis values and never mutates Blender.
    Once returned rotations are applied, FK preserves hand length and hinge.
    """
    assert side in ("r", "l")
    delta = sub(wrist_target, arm_origin); distance = math.sqrt(dot(delta, delta))
    assert .025 < distance < upper_length+lower_length-.006, (side, distance, upper_length+lower_length)
    aim = unit(delta)
    pole = sub(elbow_hint, arm_origin)
    bend_dir = unit(sub(pole, mul(aim, dot(pole, aim))))
    along = (upper_length**2-lower_length**2+distance**2)/(2*distance)
    height = math.sqrt(max(0, upper_length**2-along**2))
    elbow = add(arm_origin, add(mul(aim, along), mul(bend_dir, height)))
    upper, lower = unit(sub(elbow, arm_origin)), unit(sub(wrist_target, elbow))
    sign = 1 if side == "r" else -1
    hinge_normal = mul(unit(cross(upper, lower)), sign)
    arm_world = columns(unit(cross(upper, hinge_normal)), upper, hinge_normal)
    rest_relative = mm(transpose(arm_basis0), forearm_basis0)
    assert abs(rest_relative[2][2]-1) < .0001, "Baseline forearm hinge plane changed: do not approximate an arbitrary elbow."
    fore_rest_y = mv(rest_relative, (0, 1, 0))
    rest_bend = math.degrees(math.atan2(-fore_rest_y[0], fore_rest_y[1]))
    wanted_bend = sign*math.degrees(math.acos(max(-1, min(1, dot(upper, lower)))))
    elbow_z = wanted_bend-rest_bend
    forearm_basis = euler_matrix((0, 0, elbow_z))
    fore_world = mm(mm(arm_world, rest_relative), forearm_basis)
    wrist_parent_world = mm(fore_world, mm(transpose(forearm_basis0), hand_basis0))
    wrist_world = hand_rotation(hand_normal, digit_direction)
    arm_basis = mm(transpose(arm_basis0), arm_world)
    wrist_basis = mm(transpose(wrist_parent_world), wrist_world)
    actual_lower = mv(fore_world, (0, 1, 0))
    error = sub(actual_lower, lower)
    return dict(
        arm_world_rotation=arm_world,
        forearm_world_rotation=fore_world,
        hand_world_rotation=wrist_world,
        arm_origin_world=arm_origin,
        arm_basis_quaternion=matrix_quaternion(arm_basis),
        forearm_basis_quaternion=matrix_quaternion(forearm_basis),
        hand_basis_quaternion=matrix_quaternion(wrist_basis),
        arm_basis_euler_deg=matrix_euler(arm_basis),
        forearm_basis_euler_deg=(0., 0., elbow_z),
        hand_basis_euler_deg=matrix_euler(wrist_basis),
        elbow_world=elbow, wrist_world=wrist_target,
        reach_ratio=distance/(upper_length+lower_length),
        forearm_direction_error=math.sqrt(dot(error, error)))


def transform_artistic_targets(pose, torso_delta_rotation, torso_delta_translation):
    """Optional exact shell transform of canonical artistic targets.

    torso_delta_rotation is CURRENT shell rotation @ inverse REST shell rotation;
    torso_delta_translation is CURRENT shell origin - delta_rotation @ REST shell
    origin. This is a full rigid transform about the actual shell pivot, not an
    Euler assumption. Root may instead author these world paths directly.
    """
    return {side: dict(
        wrist_target=add(mv(torso_delta_rotation,pose["wrists"][side]),torso_delta_translation),
        elbow_hint=add(mv(torso_delta_rotation,pose["poles"][side]),torso_delta_translation),
        hand_normal=mv(torso_delta_rotation,pose["normals"][side]),
        digit_direction=mv(torso_delta_rotation,pose["digits"][side])) for side in ("r","l")}


# Coordinates are in the inspected character's rest-space: +Z up, -Y target,
# right character side -X. These are artistic posing targets, not rig-axis guesses.
# Six anchor silhouettes. Extra per-part timing keys are listed below.
STORYBOARD = [
    dict(name="Ready", t=0., root=(0, 0, -.055), body=(2, 0, -5), head=(-3, 0, 4),
         shoulders={"r": (0, 0, 20), "l": (0, 0, -20)},
         wrists={"r": (-.55, -.05, .61), "l": (.52, -.10, .67)},
         poles={"r": (-.80, .07, .64), "l": (.77, .03, .60)},
         normals={"r": (.85, -.30, .40), "l": (-.85, -.30, .40)},
         digits={"r": (0, -.8, -.3), "l": (0, -.8, -.3)}, curls={"r": (22, 35, 22), "l": (26, 38, 24)}),
    dict(name="Scoop", t=.18, root=(-.02, .015, -.095), body=(5, -3, -11), head=(-3, 0, 7),
         shoulders={"r": (0, 0, 28), "l": (0, 0, -58)},
         wrists={"r": (-.46, -.24, .69), "l": (.10, -.32, .70)},
         poles={"r": (-.77, -.02, .57), "l": (.55, -.12, .50)},
         normals={"r": (.88, -.15, .45), "l": (-.78, -.12, .62)},
         digits={"r": (.1, -.7, .7), "l": (-.1, -.7, .7)}, curls={"r": (36, 48, 30), "l": (31, 43, 27)}),
    dict(name="Loaded", t=.43, root=(-.045, .035, -.135), body=(-10, -6, -24), head=(4, 0, 20),
         shoulders={"r": (-2, 0, 20), "l": (0, 0, -65)},
         wrists={"r": (-.58, -.05, .88), "l": (.04, -.38, .74)},
         poles={"r": (-.80, .08, .81), "l": (.55, -.12, .49)},
         normals={"r": (.80, -.28, .52), "l": (-.67, -.12, .73)},
         digits={"r": (.1, -.7, .7), "l": (-.1, -.8, .6)}, curls={"r": (45, 58, 37), "l": (36, 48, 30)}),
    dict(name="Release", t=.55, root=(.012, -.070, -.070), body=(17, 3, 17), head=(-7, 0, -11),
         shoulders={"r": (0, 0, 68), "l": (0, 0, -20)},
         wrists={"r": (-.37, -.55, .92), "l": (.38, -.08, .62)},
         poles={"r": (-.69, -.30, .76), "l": (.70, .00, .48)},
         normals={"r": (-.15, -.85, -.50), "l": (-.8, -.2, .55)},
         digits={"r": (-.1, -.5, .85), "l": (0, -.8, -.25)}, curls={"r": (32, 45, 28), "l": (56, 68, 43)}),
    dict(name="Spent", t=.64, root=(.018, -.095, -.090), body=(22, 4, 22), head=(-10, 0, -15),
         shoulders={"r": (0, 0, 72), "l": (0, 0, -14)},
         wrists={"r": (-.35, -.58, .90), "l": (.43, -.01, .59)},
         poles={"r": (-.67, -.33, .74), "l": (.73, .06, .45)},
         normals={"r": (-.12, -.85, -.52), "l": (-.8, -.2, .55)},
         digits={"r": (-.1, -.5, .85), "l": (0, -.8, -.25)}, curls={"r": (21, 31, 21), "l": (48, 60, 38)}),
    dict(name="Settled", t=1., root=(0, 0, -.055), body=(2, 0, -5), head=(-3, 0, 4),
         shoulders={"r": (0, 0, 20), "l": (0, 0, -20)},
         wrists={"r": (-.55, -.05, .61), "l": (.52, -.10, .67)},
         poles={"r": (-.80, .07, .64), "l": (.77, .03, .60)},
         normals={"r": (.85, -.30, .40), "l": (-.85, -.30, .40)},
         digits={"r": (0, -.8, -.3), "l": (0, -.8, -.3)}, curls={"r": (22, 35, 22), "l": (26, 38, 24)}),
]


def solve_fk_pose(pose, bones):
    """Convert an authored silhouette to exact FK basis rotations, no rig mutation.

    Shoulder controls determine arm origin through existing Copy Location tail
    constraint. Arm orientation inherits root.x, not shoulder orientation.
    Elbow rotates only local Z as the saved locks specify. The wrist orientation
    is converted through the exact forearm-to-hand rest basis.
    """
    controls, diagnostics = {}, {}
    body = euler_matrix(pose["body"])
    root_r = rest_rotation(bones["c_root_master.x"])
    controls["c_root_master.x"] = dict(
        world_translation_delta=pose["root"],
        basis_quaternion=matrix_quaternion(mm(mm(transpose(root_r), body), root_r)))
    head_r = rest_rotation(bones["c_head.x"])
    head_q = mm(mm(transpose(head_r), euler_matrix(pose["head"])), head_r)
    controls["c_head.x"] = dict(basis_quaternion=matrix_quaternion(head_q), basis_euler_deg=matrix_euler(head_q))

    for side in ("r", "l"):
        shoulder, arm, fore, hand = (bones[n+"."+side] for n in ("c_shoulder", "c_arm_fk", "c_forearm_fk", "c_hand_fk"))
        assert fore["lock_rotation"] == [True, True, False], "Inspection changed: elbow is no longer Z-only."
        sh_r, arm_r, fore_r, hand_r = map(rest_rotation, (shoulder, arm, fore, hand))
        sh_basis = euler_matrix(pose["shoulders"][side])
        shoulder_origin = shoulder["head"]
        arm_origin = add(shoulder_origin, mv(mm(sh_r, sh_basis), (0, shoulder["length"], 0)))
        target = pose["wrists"][side]
        delta = sub(target, arm_origin); distance = math.sqrt(dot(delta, delta))
        l1, l2 = arm["length"], fore["length"]
        assert .025 < distance < l1+l2-.006, (pose["name"], side, distance, l1+l2)
        aim = unit(delta)
        pole = sub(pose["poles"][side], arm_origin)
        bend_dir = unit(sub(pole, mul(aim, dot(pole, aim))))
        along = (l1*l1-l2*l2+distance*distance)/(2*distance)
        height = math.sqrt(max(0, l1*l1-along*along))
        elbow = add(arm_origin, add(mul(aim, along), mul(bend_dir, height)))
        upper, lower = unit(sub(elbow, arm_origin)), unit(sub(target, elbow))
        sign = 1 if side == "r" else -1
        hinge_normal = mul(unit(cross(upper, lower)), sign)
        arm_world = columns(unit(cross(upper, hinge_normal)), upper, hinge_normal)
        rest_relative = mm(transpose(arm_r), fore_r)
        fore_rest_y = mv(rest_relative, (0, 1, 0))
        rest_bend = math.degrees(math.atan2(-fore_rest_y[0], fore_rest_y[1]))
        wanted_bend = sign*math.degrees(math.acos(max(-1, min(1, dot(upper, lower)))))
        elbow_z = wanted_bend-rest_bend
        fore_world = mm(mm(arm_world, rest_relative), euler_matrix((0, 0, elbow_z)))
        wrist_parent_world = mm(fore_world, mm(transpose(fore_r), hand_r))
        wrist_world = hand_rotation(pose["normals"][side], pose["digits"][side])
        rotations = {
            "c_shoulder."+side: sh_basis,
            "c_arm_fk."+side: mm(transpose(arm_r), arm_world),
            "c_forearm_fk."+side: euler_matrix((0, 0, elbow_z)),
            "c_hand_fk."+side: mm(transpose(wrist_parent_world), wrist_world),
        }
        for name, rotation in rotations.items():
            controls[name] = dict(basis_quaternion=matrix_quaternion(rotation), basis_euler_deg=matrix_euler(rotation))
        actual_lower = mv(fore_world, (0, 1, 0))
        diagnostics[side] = dict(arm_origin=arm_origin, elbow=elbow, wrist=target,
                                 reach_ratio=distance/(l1+l2), elbow_local_z_deg=elbow_z,
                                 forearm_direction_error=math.sqrt(dot(sub(actual_lower, lower), sub(actual_lower, lower))))

        # Exact axis transport: curl about the inspected hand's transverse X,
        # transformed into each digit control's own rolled local coordinates.
        # Avoid setting arbitrary local X on differently rolled digit bases.
        # +curl rotates the long digits toward negative controller Z. This is a
        # control-space statement; confirm mesh anatomical palm side visually.
        hand_x = hand["axes"]["X"]
        for digit, spread in (("pinky", 9), ("ring", -9), ("thumb", 0)):
            for joint, curl in enumerate(pose["curls"][side], 1):
                name = f"c_{digit}{joint}.{side}"
                digit_r = rest_rotation(bones[name])
                curl_axis = mv(transpose(digit_r), hand_x)
                # Long digit bases reduce the original V without making them
                # identical; thumb stays independently opposed in the source.
                rotation = axis_angle(curl_axis, -curl if digit != "thumb" else -.55*curl)
                if joint == 1 and spread:
                    normal_axis = mv(transpose(digit_r), hand["axes"]["Z"])
                    rotation = mm(axis_angle(normal_axis, spread if side == "r" else -spread), rotation)
                controls[name] = dict(basis_quaternion=matrix_quaternion(rotation),
                                      basis_euler_deg=matrix_euler(rotation),
                                      axis_local=curl_axis, curl_deg=curl)
    return dict(name=pose["name"], source_t=pose["t"], game_seconds=game_seconds(pose["t"]),
                source_frame=source_frame(pose["t"]), controls=controls, diagnostics=diagnostics)


# Each entry is (source time, anchor index). A separate authored in-between is
# recommended where anchor values repeat: tension .43-.49 is a moving hold.
# These schedules intentionally do not share a master easing/progress scalar.
CONTROL_SCHEDULES = {
    "root_translation": [(0,0),(.16,1),(.42,2),(.48,2),(.535,3),(.59,4),(.78,1),(1,5)],
    "root_rotation": [(0,0),(.19,1),(.45,2),(.50,2),(.55,3),(.61,4),(.83,1),(1,5)],
    "head_rotation": [(0,0),(.14,1),(.38,2),(.535,2),(.585,3),(.655,4),(.86,1),(1,5)],
    "shoulder_r": [(0,0),(.17,1),(.44,2),(.50,2),(.545,3),(.615,4),(.80,1),(1,5)],
    "arm_r": [(0,0),(.20,1),(.455,2),(.51,2),(.55,3),(.625,4),(.82,1),(1,5)],
    "elbow_r": [(0,0),(.22,1),(.465,2),(.525,2),(.555,3),(.635,4),(.845,1),(1,5)],
    "wrist_r": [(0,0),(.24,1),(.48,2),(.53,2),(.55,3),(.645,4),(.87,1),(1,5)],
    "fingers_r": [(0,0),(.26,1),(.485,2),(.545,2),(.56,3),(.66,4),(.92,1),(1,5)],
    "shoulder_l": [(0,0),(.14,1),(.40,2),(.48,2),(.53,3),(.60,4),(.81,1),(1,5)],
    "arm_l": [(0,0),(.16,1),(.42,2),(.49,2),(.535,3),(.62,4),(.84,1),(1,5)],
    "elbow_l": [(0,0),(.19,1),(.43,2),(.50,2),(.54,3),(.63,4),(.85,1),(1,5)],
    "wrist_l": [(0,0),(.21,1),(.45,2),(.505,2),(.545,3),(.65,4),(.885,1),(1,5)],
    "fingers_l": [(0,0),(.23,1),(.46,2),(.515,2),(.55,3),(.665,4),(.92,1),(1,5)],
}

AUTHORING_NOTES = {
    "curve_cadence": "Block silhouettes first. Do not flat-tangent every key. Preserve velocity through scoop; nearly-flat moving tension .43-.50; accelerating drive .50-.55; pelvis brakes before shell; wrist/fingers finish later; recovery decelerates with one diminishing correction. Use per-channel Bezier handles and extra breakdowns.",
    "feet": "Legs stay IK, arms switch to FK (ik_fk_switch=1). Plant front left foot world(.245,.045,.156448588) and rear right world(-.255,.265,.156448588). Preserve foot matrices while root/head moves; do not restore thigh anchors from a frozen pose. Front sole stays flat; author right rear heel peel through source .51-.61 and settle .80-.90 after visually verifying foot-roll driver direction.",
    "rig_properties": "Set c_hand_ik.* ik_fk_switch=1, auto_stretch=0; c_foot_ik.* ik_fk_switch=0, auto_stretch=0. Keep c_hand_fk.* fingers_grasp=0 and per-digit base bend_all=0 when using these explicit digit rotations.",
    "head": "Revised performance rig uses head.x for whole shell+face mass; root.x retains pelvis. Root author must reinterpret body/head staging accordingly. Call solve_current_fk_chain with actual evaluated zero-FK matrices AFTER pelvis, shell, and shoulder posing; do not apply old report-only quaternion arrays blindly to the reparented rig. Keep the face outside both hand trajectories.",
    "silhouette": "Huge hands must read cupped/half-fist, with a charge-sized gap; never three straight digits pointing up. Support hand catches below charge then recoils low. Face remains visible from front and gameplay three-quarter. Do not add effects before this passes.",
    "limits": "Kinematic FK rotations and reach are checked against saved rest matrices; this cannot verify mesh intersections, palms' anatomical side, constraint-driver evaluation, or artistic appeal. Root author must verify actual Blender renders and tune the six silhouettes.",
    "new_shell_controls": "Preserve original parents for exporter compatibility. Root's new c_arm_shell_fk.side drives original c_arm_fk world transform. solve_current_fk_chain returns arm_world_rotation and arm_origin_world for that new control; write original c_forearm_fk.side local Euler (0,0,Z), evaluate, then write original c_hand_fk.side hand_world_rotation preserving current wrist translation. These world outputs do not depend on whether the acting upper-arm control is original or a helper.",
}


def build_design(report_path=REPORT):
    report = json.loads(Path(report_path).read_text(encoding="utf-8"))
    bones = {b["name"]: b for b in report["bones"]}
    return dict(report=str(report_path), source_release=.55, game_release_seconds=.9,
                game_duration_seconds=1.8, poses=[solve_fk_pose(p,bones) for p in STORYBOARD],
                schedules=CONTROL_SCHEDULES, notes=AUTHORING_NOTES)


if __name__ == "__main__":
    print(json.dumps(build_design(), indent=2))
