"""Re-key the recovery palm flip with SLERP on the isolated V5 source only."""
import json
from pathlib import Path
import bpy

ROOT = Path(__file__).resolve().parents[2]
author = ROOT / 'Tools/Blender/author_nut_wizard_casts_v5.py'
code = author.read_text(encoding='utf8')
exec(compile(code.split('old_hashes = ')[0], str(author), 'exec'))
bpy.ops.wm.open_mainfile(filepath=str(OUTPUT))
scene, rig, mesh = bpy.context.scene, bpy.data.objects['rig'], bpy.data.objects['SM_NutWizard']
controls = [bone for bone in rig.pose.bones if bone.name.startswith('c_')]
report_path = OUT / 'WizardCastsV5Report.json'
report = json.loads(report_path.read_text(encoding='utf8'))
records = report['records']
starts = dict(Cast=84, HeavyCast=82, Summon=83, Rain=86)
for record in records:
    label = record['label']
    action = bpy.data.actions[record['action']]
    initial_end = dict(Cast=19, HeavyCast=18, Summon=39, Rain=16)[label]
    for frame in range(1, initial_end + 1):
        apply(LIB[label][0]((frame - 1) / 100))
        rig.animation_data.action = action
        key(frame)
    for frame in range(starts[label], 102):
        apply(LIB[label][0]((frame - 1) / 100))
        rig.animation_data.action = action
        key(frame)
    bind(action)
    record['continuous_angle_branch_repairs'] = continuous_rotation_branches(action)
    for curve in curves(action):
        discrete = curve.data_path.endswith('\"]')
        for point in curve.keyframe_points:
            point.interpolation = 'CONSTANT' if discrete else 'BEZIER'
            if not discrete:
                point.handle_left_type = point.handle_right_type = 'AUTO_CLAMPED'
        curve.update()
    print('WIZARD_V5_RECOVERY_REKEYED', label, flush=True)
qa_code = code.split('qa = {}', 1)[1].split('for name, original in old_hashes.items():', 1)[0]
exec(compile('qa = {}' + qa_code, str(author), 'exec'))
bind(bpy.data.actions['MC_Wizard_Cast'])
scene.frame_set(56)
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT))
report['qa'] = qa
report['records'] = records
report['palm_recovery_slerp'] = True
report['stable_magic_settle'] = True
report['game_idle_transition'] = 'Native skeleton quaternion blend into the existing Idle; no ARP wrist flip inside the cast.'
report['angular_qc'] = dict(diagnostic_only=True, threshold_pass_claimed=False,
                          runtime_validation='Solo PIE after Unreal animation import.')
report['blend_sha256'] = digest(OUTPUT)
report_path.write_text(json.dumps(report, indent=2), encoding='utf8')
print('WIZARD_V5_RECOVERY_PASS', flush=True)
