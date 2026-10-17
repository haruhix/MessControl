import unreal as u
for item in u.ObjectIterator(u.MCPerkChoiceWidget):
    if item.get_path_name().startswith('/Engine/Transient.'):
        u.log('MC_UI_DESIGNER_TIME '+item.get_path_name()+' time='+str(item.get_editor_property('preview_time')))
