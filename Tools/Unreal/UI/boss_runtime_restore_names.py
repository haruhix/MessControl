import json
from pathlib import Path
import unreal as u
widget=u._mc_boss_runtime_fixture
widget.apply_editor_preview(0.)
height=widget.get_displayed_stack_height()
assert abs(height-u._mc_boss_runtime_short_height)<.1,(height,u._mc_boss_runtime_short_height)
path=Path(u.Paths.project_dir()).resolve()/'Saved/GameplayUI/BossReference/runtime_name_transition.json'
report=json.loads(path.read_text(encoding='utf-8'))
report.update({'same_widget_long_to_short':True,'restored_height':height})
path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_BOSS_RUNTIME_NAME_SHRINK_PASS '+json.dumps(report,ensure_ascii=False))
