import unreal as u
for name in ('MCGameplayHUD','MCGameplayMenuWidget','MCBossHealthWidget'):
    cls=u.load_class(None,'/Script/MessControl.'+name)
    assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,'E:/DEVGAME/MessControl/Saved/GameplayUI/Previews/Native_'+name+'.png',u.IntPoint(1920,1080),'ThreeBosses' if name!='MCGameplayMenuWidget' else 'Connection','ru',1.0)
theme=u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
u.log('Marine='+str(theme.get_editor_property('palette').get_editor_property('marine')))
u.log('Object helpers: ' + str([x for x in dir(u) if x in ('new_object', 'get_transient_package', 'get_objects_with_outer', 'ObjectIterator') or 'duplicate' in x.lower()]))
