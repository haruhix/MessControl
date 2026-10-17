import unreal as u
u.log('Object helpers: ' + str([x for x in dir(u) if x in ('new_object', 'get_transient_package', 'get_objects_with_outer', 'ObjectIterator') or 'duplicate' in x.lower()]))
bp=u.load_asset('/Game/Gameplay/UI/WBP_GameplayHUD_v10')
def inspect(w):
    if w.get_name() in ('ReserveValue','ReserveLabel','TimerValue','TimerLabel','TaskValue','TaskLabel','CleanValue','Key1'):
        u.log(w.get_name()+' text='+str(w.get_editor_property('text'))+' font='+str(w.get_editor_property('font')))
for obj in u.ObjectIterator(u.TextBlock):
    if '/Game/Gameplay/UI/WBP_GameplayHUD_v10.' in obj.get_path_name(): inspect(obj)
theme=u.load_asset('/Game/Gameplay/UI/Theme/DA_GameplayUITheme')
u.log('Marine='+str(theme.get_editor_property('palette').get_editor_property('marine')))
u.log('bindings='+str(u.get_default_object(u.load_class(None,'/Game/Gameplay/UI/WBP_GameplayHUD_v10.WBP_GameplayHUD_v10_C')).get_editor_property('style_bindings')))
