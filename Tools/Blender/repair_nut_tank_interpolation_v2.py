"""Repair interpolation in a derived Tank v2 copy and export eight remaining slots.

Run with Blender 5.1 --background --factory-startup --disable-autoexec --python this.py.
Original blends, key poses, timings, artist actions and bind data remain unchanged.
"""
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/NutAnimationStyle/Tank/Nut_Tank_MC_Style_v2.blend'
OUT = ROOT / 'ArtSource/NutAnimationStyle/TankSmoothV2'
OUTPUT = OUT / 'Nut_Tank_Smooth_v2.blend'
CLIPS = ('Idle', 'WalkLeft', 'WalkRight', 'Jump', 'Transform',
         'ChargeTell', 'ChargeLoop', 'ChargeRecovery')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def curves(action):
    return [f for layer in action.layers for strip in layer.strips
            for bag in strip.channelbags for f in bag.fcurves]


def key_pose_signature(action):
    rows = [(f.data_path, f.array_index, [list(k.co) for k in f.keyframe_points])
            for f in curves(action)]
    return hashlib.sha256(json.dumps(rows).encode()).hexdigest()


def bind_signature(rig):
    return [(b.name, b.parent.name if b.parent else None, b.use_deform,
             [list(row) for row in b.matrix_local]) for b in rig.data.bones]


def main():
    original_hash = digest(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    rig = bpy.data.objects['rig']
    original_bind = bind_signature(rig)
    signatures = {a.name: key_pose_signature(a) for a in bpy.data.actions}
    durations = {a.name: list(a.frame_range) for a in bpy.data.actions}
    report = {'schema': 1, 'source': str(SOURCE), 'output': str(OUTPUT),
              'source_sha256_before': original_hash, 'blender_version': bpy.app.version_string,
              'policy': 'CONSTANT motion keys to BEZIER/AUTO_CLAMPED; terminal custom-property channels retain CONSTANT.',
              'actions': [], 'export_labels': list(CLIPS)}
    for action in bpy.data.actions:
        if not action.name.startswith('MC_Tank_'):
            continue
        changed, preserved, sample = 0, 0, None
        for curve in curves(action):
            if curve.data_path.endswith('"]'):
                assert all(k.interpolation == 'CONSTANT' for k in curve.keyframe_points), curve.data_path
                preserved += len(curve.keyframe_points)
                continue
            assert curve.data_path.endswith(('location', 'rotation_euler', 'rotation_quaternion', 'scale')), curve.data_path
            candidate = None
            for first, second in zip(curve.keyframe_points, list(curve.keyframe_points)[1:]):
                if abs(first.co.y - second.co.y) > .01:
                    frame = (first.co.x + second.co.x) * .5
                    candidate = {'path': curve.data_path, 'index': curve.array_index,
                                 'start': list(first.co), 'end': list(second.co),
                                 'midpoint_frame': frame, 'old_midpoint': curve.evaluate(frame)}
                    break
            for key in curve.keyframe_points:
                if key.interpolation == 'CONSTANT':
                    key.interpolation = 'BEZIER'
                    key.handle_left_type = key.handle_right_type = 'AUTO_CLAMPED'
                    changed += 1
            curve.update()
            if candidate is not None and sample is None:
                candidate['new_midpoint'] = curve.evaluate(candidate['midpoint_frame'])
                assert math.isfinite(candidate['new_midpoint'])
                assert abs(candidate['new_midpoint'] - candidate['old_midpoint']) > 1e-5, candidate
                sample = candidate
        assert key_pose_signature(action) == signatures[action.name], action.name
        report['actions'].append({'name': action.name, 'changed_motion_keys': changed,
                                  'preserved_custom_keys': preserved, 'midpoint_example': sample})
    assert all(key_pose_signature(a) == signatures[a.name] for a in bpy.data.actions)
    assert all(list(a.frame_range) == durations[a.name] for a in bpy.data.actions)
    assert original_bind == bind_signature(rig)
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT), relative_remap=True)
    report['source_sha256_after'] = digest(SOURCE)
    report['source_preserved'] = report['source_sha256_after'] == original_hash
    report['key_poses_preserved'] = report['durations_preserved'] = report['bind_preserved'] = True
    assert report['source_preserved']
    (OUT / 'InterpolationRepairReport.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print('TANK_SMOOTH_COPY_READY', str(OUTPUT), flush=True)

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import export_nut_style_v2 as exporter
    sys.argv = [str(Path(exporter.__file__)), '--', '--unit', 'Tank', '--source', str(OUTPUT),
                '--output-dir', str(OUT), '--clips', *CLIPS]
    exporter.main()
    manifest_path = OUT / 'ExportReport.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf8'))
    # Reuse the exporter's existing endpoint samples for focused loop/contact QA.
    loops = {}
    for label in ('Idle', 'WalkLeft', 'WalkRight', 'ChargeLoop'):
        samples = manifest['source_pose_samples'][label]
        first, last = samples[0]['bone_positions_meters'], samples[-1]['bone_positions_meters']
        deltas = {name: math.sqrt(sum((a-b)**2 for a,b in zip(first[name], last[name])))
                  for name in first}
        assert max(deltas.values(), default=0) < .0001, (label, deltas)
        loops[label] = {'endpoint_bone_displacement_m': deltas}
    manifest['loop_contact_checks'] = loops
    manifest['interpolation_repair'] = {'original_source': str(SOURCE),
                                       'original_source_sha256': original_hash,
                                       'original_source_preserved': digest(SOURCE) == original_hash,
                                       'key_poses_preserved': True, 'durations_preserved': True}
    assert manifest['interpolation_repair']['original_source_preserved']
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf8')
    print('TANK_SMOOTH_EXPORT_COMPLETE', len(CLIPS), flush=True)


if __name__ == '__main__':
    main()
