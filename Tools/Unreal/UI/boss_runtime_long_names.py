"""Exercise short > long > short on the same solo runtime widget."""
import json
from pathlib import Path
import unreal as u
widget=u._mc_boss_runtime_fixture
assert widget and widget.is_in_viewport()
theme=widget.get_resolved_theme()
portraits={str(key):value for key,value in theme.get_editor_property('boss_portraits').items()}
names=('Страж','Сверхъестественный хранитель разрушенной эмалевой крепости',
       'HaselnussfestungsbelagerungskommandantderunbezwingbarenNordgrenze')
entries=[]
for index,identity in enumerate(('RottenGuardian','NutKnight','NutWizard')):
    row=u.MCBossHealthPresentation()
    row.set_editor_property('boss_id',identity)
    row.set_editor_property('name',names[index])
    row.set_editor_property('portrait',portraits[identity])
    row.set_editor_property('health',(68.,45.,40.)[index])
    row.set_editor_property('max_health',100.)
    row.set_editor_property('has_shield',index==1)
    row.set_editor_property('shield',60.)
    row.set_editor_property('max_shield',100.)
    entries.append(row)
before=widget.get_displayed_stack_height()
widget.show_bosses(entries,0.)
after=widget.get_displayed_stack_height()
assert after>before+10., (before,after)
u._mc_boss_runtime_short_height=before
u._mc_boss_runtime_long_height=after
report={'same_widget_short_to_long':True,'short_height':before,'long_height':after,'names':names}
(Path(u.Paths.project_dir()).resolve()/'Saved/GameplayUI/BossReference/runtime_name_transition.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_BOSS_RUNTIME_LONG_NAMES_PASS '+json.dumps(report,ensure_ascii=False))
