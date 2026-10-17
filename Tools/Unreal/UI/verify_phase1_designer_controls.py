"""Read the actual open Phase1 Designer after Stop twice, then after Reset."""
import json
from pathlib import Path
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
times = {widget.get_path_name(): widget.get_editor_property('preview_time')
         for widget in u.ObjectIterator(u.MCPerkChoiceWidget)
         if widget.get_class().get_name() == 'WBP_PerkChoice_Phase1_C'
         and widget.get_path_name().startswith('/Engine/Transient.World_')}
assert times, 'No actual Phase1 Designer fixture found'
output = Path(u.Paths.project_dir()).resolve() / 'Saved/GameplayUI/Phase1/Designer/control_report.json'
output.parent.mkdir(parents=True, exist_ok=True)
if not hasattr(u, '_mc_phase1_designer_stopped_times'):
    assert max(times.values()) > .1, 'Play did not advance the actual Designer'
    u._mc_phase1_designer_stopped_times = times
    report = {'play_advanced': True, 'first_stopped_times': times, 'status': 'awaiting_stop_repeat'}
else:
    report = json.loads(output.read_text(encoding='utf-8'))
    if report['status'] == 'awaiting_stop_repeat':
        assert times == u._mc_phase1_designer_stopped_times, 'Stop did not freeze fixture time'
        report.update(stop_froze_time=True, status='awaiting_reset')
    else:
        assert report['status'] == 'awaiting_reset'
        assert all(abs(value) < .0001 for value in times.values()), 'Reset did not clear fixture time'
        report.update(reset_zeroed_time=True, reset_times=times, status='passed')
        del u._mc_phase1_designer_stopped_times
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_PHASE1_DESIGNER_CONTROL_CHECK ' + json.dumps(report))
