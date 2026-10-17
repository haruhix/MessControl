import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve()
api=u.MCGameplayUIEditorLibrary
for name,state in [('WBP_GameplayHUD_v10','ThreeBosses'),('WBP_PerkChoice_v10','Risky')]:
    cls=u.load_class(None,'/Game/Gameplay/UI/'+name+'.'+name+'_C')
    assert api.render_gameplay_ui_preview(cls,str(root/'Saved/GameplayUI/Previews'/('Probe_'+name+'.png')),u.IntPoint(1920,1080),state,'ru',1.0)
for item in u.ObjectIterator(u.Widget):
    if not item.get_path_name().startswith('/Engine/Transient.'): continue
    if item.get_name() in ('Boss1Health','Boss1Loss','Illustration','IconFallback','CataclysmPanel','XPBar','Boss1Portrait','GameplayAlertPanel'):
        line=item.get_path_name()+' vis='+str(item.get_visibility())+' opacity='+str(item.get_render_opacity())
        if isinstance(item,u.ProgressBar): line+=' percent='+str(item.get_editor_property('percent'))+' style='+str(item.get_editor_property('widget_style'))
        if isinstance(item,u.Image): line+=' brush='+str(item.get_editor_property('brush'))
        u.log('MC_UI_PROBE '+line)
