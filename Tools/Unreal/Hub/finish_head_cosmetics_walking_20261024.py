"""Verify observed runtime shader data and restore this walking check's state."""
import hashlib
import json
import re
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/HeadCosmetics20261024/WalkFollow'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
report = json.loads((OUT / 'WalkingValidation.json').read_text(encoding='utf8'))
assert report['status'] == 'passed', report.get('error')
log = (ROOT / 'Saved/Logs/MessControl.log').read_text(encoding='utf8', errors='replace')
assert 'Starting Live Coding compile.' not in log, 'Check must use the persisted module after editor restart'
pending = None
observed = {}
for line in log.splitlines():
    if 'WALK_CPD_SAMPLE ' in line:
        pending = json.loads(line.split('WALK_CPD_SAMPLE ', 1)[1])
    elif pending and '.CustomPrimitiveDataInternal = ' in line:
        match = re.search(r'CustomPrimitiveDataInternal = \(Data=\(([^)]*)\)\)', line)
        assert match, line
        actual = [float(v) for v in match.group(1).split(',')]
        assert len(actual) == 13
        error = max(abs(a - b) for a, b in zip(actual, pending['expected']))
        assert error < .0002, (pending, actual, error)
        observed[pending['label']] = {'actual': actual, 'max_float_error': error}
        pending = None
assert set(observed) == {s['label'] for s in report['cpd_samples']}
assert len(observed) == 28

summary = {'status': 'passed', 'items': {}, 'shader_samples': len(observed),
           'max_shader_float_error': max(r['max_float_error'] for r in observed.values()),
           'compiled_module': str(ROOT / 'Binaries/Win64/UnrealEditor-MessControl.dll'),
           'module_sha256': hashlib.sha256((ROOT / 'Binaries/Win64/UnrealEditor-MessControl.dll').read_bytes()).hexdigest(),
           'verified_after_editor_restart_without_live_coding': True}
for item in sorted({i for s in report['samples'] for i in s['items']}):
    samples = [s for s in report['samples'] if item in s['items']]
    moving = [s for s in samples if s['speed_cm_s'] > 50]
    assert len(moving) >= 8
    position_range = [max(s['head_in_actor'][axis] for s in moving) -
                      min(s['head_in_actor'][axis] for s in moving) for axis in range(3)]
    assert max(position_range) > .1, (item, position_range)
    summary['items'][item] = {
        'moving_frames': len(moving),
        'max_speed_cm_s': max(s['speed_cm_s'] for s in moving),
        'distance_from_start_cm': max(s['distance_from_start_cm'] for s in samples),
        'stretch_range': [min(s['stretch'] for s in moving), max(s['stretch'] for s in moving)],
        'head_motion_in_actor_cm': position_range,
        'max_socket_error_cm': max(e for s in samples for e in s['attachment_errors_cm'])}
assert len(summary['items']) == 7

before = json.loads((OUT / 'BeforeWalking.json').read_text())
settings = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
assert u.ToolsetLibrary.set_object_properties(settings, json.dumps(before['play']))
u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property(
    'bThrottleCPUWhenNotForeground', before['throttle'])
profiles = (ROOT / 'Saved/HubProfiles/PIE').resolve()
for path in profiles.glob('*'):
    assert path.resolve().parent == profiles
    if path.is_file() and path.suffix in ['.sav', '.tmp'] and path.name not in before['profiles']:
        path.unlink()
for name, backup in before['profiles'].items():
    path = profiles / name
    assert path.resolve().parent == profiles
    path.write_bytes(Path(backup).read_bytes())
    assert path.read_bytes() == Path(backup).read_bytes()
summary['profile_files_restored'] = len(before['profiles'])
summary['editor_map'] = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name()
summary['dirty_packages'] = [p.get_path_name() for p in
    list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
    list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())]
(OUT / 'WalkingSummary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf8')
(OUT / 'ObservedShaderData.json').write_text(json.dumps(observed, indent=2))
print('HEAD_COSMETICS_WALKING_VERIFIED', json.dumps(summary, ensure_ascii=False))
