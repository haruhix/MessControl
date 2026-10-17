"""Exercise real inherited colors, including procedural material alpha; restore the DA."""
import json
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir()).resolve()
out=root/'Saved/GameplayUI/Previews'
theme=u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
palette=theme.get_editor_property('palette')
fields=('stamina','button','boss_health')
original={key:tuple(getattr(palette.get_editor_property(key), c) for c in ('r','g','b','a')) for key in fields}
report={'renders':[]}
def render(name,state,suffix):
    cls=u.load_class(None,'/Game/Gameplay/UI/'+name+'.'+name+'_C')
    path=out/(suffix+'.png')
    assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(path),u.IntPoint(1920,1080),state,'ru',1.0)
    report['renders'].append(str(path))
try:
    palette.set_editor_property('stamina',u.LinearColor(1.0,0.03,0.6,1.0))
    palette.set_editor_property('button',u.LinearColor(0.8,0.3,0.03,1.0))
    theme.set_editor_property('palette',palette)
    theme.notify_theme_changed()
    render('WBP_GameplayHUD_v10','ThreeBosses','Theme_InheritedHUD')
    render('WBP_PauseMenu_v10','Default','Theme_SharedButtons')
    boss=original['boss_health']
    palette.set_editor_property('boss_health',u.LinearColor(boss[0],boss[1],boss[2],0.35))
    theme.set_editor_property('palette',palette)
    theme.notify_theme_changed()
    render('WBP_GameplayHUD_v10','ThreeBosses','Theme_BossAlpha035')
finally:
    for key,value in original.items(): palette.set_editor_property(key,u.LinearColor(*value))
    theme.set_editor_property('palette',palette)
    theme.notify_theme_changed()
    report['restored']=all(tuple(getattr(palette.get_editor_property(key), c) for c in ('r','g','b','a'))==value for key,value in original.items())
    assert report['restored']
(root/'Saved/GameplayUI/theme_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_UI_THEME_VALIDATION '+json.dumps(report))
