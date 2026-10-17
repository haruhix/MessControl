import unreal as u
cls=u.load_class(None,'/Game/Gameplay/UI/WBP_BossHealth_Reference.WBP_BossHealth_Reference_C')
default=u.get_default_object(cls)
widget=u.new_object(cls)
for obj in (default,widget):
    theme=obj.get_resolved_theme()
    u.log('MC_BOSS_PROBE '+str(obj.get_path_name())+' theme='+str(theme.get_path_name())+' frame='+str(obj.get_editor_property('frame_material'))+' namefont='+str(obj.get_editor_property('boss_name_font').get_editor_property('font_object')))
    u.log('MC_BOSS_PALETTE '+str(theme.get_editor_property('palette').get_editor_property('boss_health')))
u.log('MC_BOSS_THEME_MAPPING '+str(u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme').get_editor_property('widget_classes')))

