"""Repair one lower-body return seam in an isolated copy of the Wizard pool."""
import hashlib
import json
import math
import re
import sys
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import author_wizard_mixamo_pool as pool

DIRECTORY = ROOT / 'ArtSource/NutAnimationStyle/Wizard/MixamoPool'
INPUT = DIRECTORY / 'NutWizard_Mixamo_AnimationPool_v2.blend'
INPUT_REPORT = DIRECTORY / 'AnimationPoolReport_v2.json'
OUTPUT = DIRECTORY / 'NutWizard_Mixamo_AnimationPool_v3.blend'
OUTPUT_REPORT = DIRECTORY / 'AnimationPoolReport_v3.json'
ACTION = 'MC_POOL_Channel_SummonSustain'
FIRST, LAST = 80, 87
LOWER_WORDS = ('foot', 'toes', 'leg', 'thigh', 'root', 'traj')


def sanitize_pool_ui(rig, report):
    """Avoid Blender's byte-truncated UTF-8 NLA strip display names."""
    track = next(track for track in rig.animation_data.nla_tracks
                 if track.name == 'MC_POOL_ALL_32_PREVIEW')
    for ordinal, strip in enumerate(track.strips, 1):
        strip.name = ('%02d %s' % (ordinal, strip.action.name))[:63]
    markers = sorted(bpy.context.scene.timeline_markers, key=lambda marker: marker.frame)
    assert len(markers) == len(report['clips'])
    for marker, row in zip(markers, report['clips']):
        marker.name = '%02d %s' % (row['ordinal'], row['labelRU'][:24])


def sanitize_existing_v3():
    assert bpy.app.background
    report = json.loads(OUTPUT_REPORT.read_text(encoding='utf8'))
    assert report['blend_sha256'] == pool.author.sha(OUTPUT)
    v2_hash, source_hash = pool.author.sha(INPUT), pool.author.sha(pool.SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(OUTPUT))
    sanitize_pool_ui(bpy.data.objects['rig'], report)
    bpy.context.scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
    assert v2_hash == pool.author.sha(INPUT) and source_hash == pool.author.sha(pool.SOURCE)
    report['blend_sha256'] = pool.author.sha(OUTPUT)
    report['ui_label_repair'] = 'ASCII NLA strip names <=63 bytes; timeline labels <=51 UTF-8 bytes.'
    OUTPUT_REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print('POOL_SEAM_REPAIR_UI_READY', str(OUTPUT), flush=True)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def bone_name(path):
    match = re.match(r'pose\.bones\["([^"]+)"\]', path)
    return match[1] if match else None


def curve_signature(action, predicate=lambda curve: True, outside=False):
    return [(curve.data_path, curve.array_index,
             [(tuple(key.co), key.interpolation) for key in curve.keyframe_points
              if not outside or not FIRST <= key.co.x <= LAST])
            for curve in pool.author.curves(action) if predicate(curve)]


def immutable_scene(rig):
    return dict(
        rest=[(bone.name, bone.parent.name if bone.parent else None,
               list(value for row in bone.matrix_local for value in row)) for bone in rig.data.bones],
        constraints=[(bone.name, bone.rotation_mode,
                      [(c.name, c.type, c.mute, c.influence,
                        getattr(getattr(c, 'target', None), 'name', None), getattr(c, 'subtarget', None))
                       for c in bone.constraints]) for bone in rig.pose.bones],
        drivers=[(fc.data_path, fc.array_index, fc.driver.type, fc.driver.expression,
                  [(v.name, v.type, [(getattr(t.id, 'name', None), t.data_path, t.bone_target,
                                      t.transform_type, t.transform_space) for t in v.targets])
                   for v in fc.driver.variables]) for fc in rig.animation_data.drivers],
        cameras=[(obj.name, list(value for row in obj.matrix_world for value in row),
                  obj.data.type, obj.data.lens, obj.data.ortho_scale, obj.data.shift_x, obj.data.shift_y)
                 for obj in bpy.data.objects if obj.type == 'CAMERA'],
        nla=[(track.name, [(index, strip.action.name if strip.action else None,
                           strip.frame_start, strip.frame_end, strip.action_frame_start,
                           strip.action_frame_end) for index, strip in enumerate(track.strips)])
             for track in rig.animation_data.nla_tracks])


def main():
    assert bpy.app.background, 'Use isolated Blender, preserving the interactive application'
    assert not OUTPUT.exists() and not OUTPUT_REPORT.exists(), 'Preserve earlier revisions'
    report = json.loads(INPUT_REPORT.read_text(encoding='utf8'))
    input_hash, source_hash = pool.author.sha(INPUT), pool.author.sha(pool.SOURCE)
    assert report['passed'] and report['blend_sha256'] == input_hash
    assert report['source_sha256'] == source_hash
    bpy.ops.wm.open_mainfile(filepath=str(INPUT))
    scene, rig = bpy.context.scene, bpy.data.objects['rig']
    stored_defaults = pool.defaults(rig)
    action = bpy.data.actions[ACTION]
    lower = [name for name in report['names'] if any(word in name for word in LOWER_WORDS)]
    lower_set = set(lower)
    lower_curves = lambda curve: bone_name(curve.data_path) in lower_set
    before_others = {a.name: digest(pool.signature(a)) for a in bpy.data.actions if a != action}
    before_upper = digest(curve_signature(action, lambda curve: not lower_curves(curve)))
    before_outside = digest(curve_signature(action, lower_curves, outside=True))
    before_scene = digest(immutable_scene(rig))
    props = []
    for curve in pool.author.curves(action):
        match = re.fullmatch(r'pose\.bones\["([^"]+)"\]\["([^"]+)"\]', curve.data_path)
        if match and match[1] in lower_set:
            item = tuple(match.groups())
            if item not in props:
                props.append(item)
    pool.author.bind(rig, action, stored_defaults)

    def sample(frame):
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        return dict(
            pose={name: rig.pose.bones[name].matrix_basis.decompose() for name in lower},
            props={(name, prop): float(rig.pose.bones[name][prop]) for name, prop in props},
            world={name: (rig.matrix_world @ rig.pose.bones[name].matrix).translation.copy() for name in lower},
            euler={name: rig.pose.bones[name].rotation_euler.copy() for name in lower},
            quaternion={name: rig.pose.bones[name].rotation_quaternion.copy() for name in lower})

    original = {frame: sample(frame) for frame in range(FIRST-1, LAST+2)}
    entry = original[FIRST-1]
    previous_euler = {name: value.copy() for name, value in entry['euler'].items()}
    previous_quat = {name: value.copy() for name, value in entry['quaternion'].items()}
    weights = {}
    for frame in range(FIRST, LAST+1):
        scene.frame_set(frame)
        weight = pool.author.smooth((frame-FIRST)/(LAST-FIRST))
        weights[frame] = weight
        pose = pool.author.blend_pose(entry['pose'], original[frame]['pose'], weight)
        for name, (location, rotation, scale) in pose.items():
            assert all(math.isfinite(value) for component in (location, rotation, scale) for value in component)
            bone = rig.pose.bones[name]
            bone.location, bone.scale = location, scale
            if bone.rotation_mode == 'QUATERNION':
                if previous_quat[name].dot(rotation) < 0:
                    rotation = -rotation
                bone.rotation_quaternion = rotation
                previous_quat[name] = rotation.copy()
                rotation_path = 'rotation_quaternion'
            else:
                assert bone.rotation_mode != 'AXIS_ANGLE', name
                bone.rotation_euler = rotation.to_euler(bone.rotation_mode, previous_euler[name])
                previous_euler[name] = bone.rotation_euler.copy()
                rotation_path = 'rotation_euler'
            for path in ('location', rotation_path, 'scale'):
                bone.keyframe_insert(data_path=path, frame=frame, group=name)
        for name, prop in props:
            a, b = entry['props'][name, prop], original[frame]['props'][name, prop]
            rig.pose.bones[name][prop] = a+(b-a)*weight
            rig.pose.bones[name].keyframe_insert(data_path='["'+prop+'"]', frame=frame, group=name)
    for curve in pool.author.curves(action):
        if lower_curves(curve):
            for key in curve.keyframe_points:
                if FIRST <= key.co.x <= LAST:
                    key.interpolation = 'LINEAR'
            curve.update()
    pool.author.bind(rig, action, stored_defaults)
    repaired = {frame: sample(frame) for frame in range(FIRST-1, LAST+2)}
    before_jump = {name: (original[FIRST]['world'][name]-entry['world'][name]).length for name in lower}
    after_jump = {name: (repaired[FIRST]['world'][name]-repaired[FIRST-1]['world'][name]).length for name in lower}
    exit_difference = {name: (repaired[LAST]['world'][name]-original[LAST]['world'][name]).length for name in lower}
    assert max(after_jump.values()) < 1e-5, after_jump
    assert max(exit_difference.values()) < 1e-5, exit_difference
    assert before_upper == digest(curve_signature(action, lambda curve: not lower_curves(curve)))
    assert before_outside == digest(curve_signature(action, lower_curves, outside=True))
    assert before_others == {a.name: digest(pool.signature(a)) for a in bpy.data.actions if a != action}
    assert before_scene == digest(immutable_scene(rig))
    pool.author.bind(rig, None, stored_defaults)
    tracks = [track for track in rig.animation_data.nla_tracks if track.name == 'MC_POOL_ALL_32_PREVIEW']
    assert len(tracks) == 1
    rig.animation_data.use_nla = True
    tracks[0].mute = False
    sanitize_pool_ui(rig, report)
    scene.frame_set(1)
    bpy.context.view_layer.update()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
    assert input_hash == pool.author.sha(INPUT) and source_hash == pool.author.sha(pool.SOURCE)
    repair = dict(action=ACTION, frames=[FIRST, LAST], entry_frame=FIRST-1, weights=weights,
        lower_bones=lower, properties=[list(item) for item in props],
        method='Smoothstep lower local location/quaternion/scale and lower properties; first frame weight 0, last weight 1.',
        before_frame80_world_delta_m=before_jump, after_frame80_world_delta_m=after_jump,
        frame87_original_pose_world_error_m=exit_difference,
        before_signature_other_actions=before_others,
        upper_action_curves_unchanged=True, action_outside_repair_interval_unchanged=True,
        all_other_actions_unchanged=True, rig_constraints_rest_cameras_nla_unchanged=True,
        source_and_v2_files_unchanged=True)
    report.update(blend=str(OUTPUT), blend_sha256=pool.author.sha(OUTPUT),
                  parent_blend=str(INPUT), parent_blend_sha256=input_hash, selective_seam_repair=repair,
                  ui_label_repair='ASCII NLA strip names <=63 bytes; timeline labels <=51 UTF-8 bytes.')
    for row in report['clips']:
        if row['action'] == ACTION:
            row['selective_seam_repair'] = dict(frames=[FIRST, LAST], lower_bones=lower)
    OUTPUT_REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print('POOL_SEAM_REPAIR_READY', str(OUTPUT), flush=True)
    print('SEAM_BEFORE_M', json.dumps(before_jump), flush=True)
    print('SEAM_AFTER_M', json.dumps(after_jump), flush=True)


if __name__ == '__main__':
    main()
