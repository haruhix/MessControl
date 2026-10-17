import json
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve()
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
report={'renders':[]}
for name,state in [('WBP_Scoreboard_v10','LongText'),('WBP_GameplayHUD_v10','ThreeBosses'),('WBP_PerkChoice_v10','Risky')]:
    cls=u.load_class(None,'/Game/Gameplay/UI/'+name+'.'+name+'_C')
    path=root/'Saved/GameplayUI/Previews'/(name+'_'+state+'_1920.png')
    assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(path),u.IntPoint(1920,1080),state,'ru',1.0)
    report['renders'].append(str(path))
master=u.MCGameplayUIFactory.get_master_theme()
palette=master.get_editor_property('palette')
original=tuple(getattr(palette.get_editor_property('stamina'),c) for c in ('r','g','b','a'))
try:
    palette.set_editor_property('stamina',u.LinearColor(0.91,0.12,0.64,0.73))
    master.set_editor_property('palette',palette)
    master.notify_theme_changed()
    master=None
    u.SystemLibrary.collect_garbage()
    restored_master=u.MCGameplayUIFactory.get_master_theme()
    live=restored_master.get_editor_property('palette').get_editor_property('stamina')
    assert abs(live.r-0.91)<0.0001 and abs(live.a-0.73)<0.0001,'GC lost the in-memory shared palette'
    report['theme_survives_gc']=True
finally:
    restored_master=u.MCGameplayUIFactory.get_master_theme()
    restored_palette=restored_master.get_editor_property('palette')
    restored_palette.set_editor_property('stamina',u.LinearColor(*original))
    restored_master.set_editor_property('palette',restored_palette)
    restored_master.notify_theme_changed()
    report['theme_restored']=True
(root/'Saved/GameplayUI/final_regressions.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_UI_FINAL_REGRESSIONS '+json.dumps(report))
