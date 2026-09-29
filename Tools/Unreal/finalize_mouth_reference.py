import unreal as u
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
for a in actors.get_all_level_actors():
    if a.get_actor_label()=='PREVIEW | Reference camera':
        a.set_actor_label('LOOK | Reference camera'); a.set_folder_path('Look/ReferenceLighting')
    if a.get_actor_label()=='PREVIEW | Throat sculpt':actors.destroy_actor(a)
    if isinstance(a,u.MCArenaToothSocket): a.get_editor_property('preview').set_hidden_in_game(True)
    if isinstance(a,u.MCThroat):
        a.get_editor_property('label').set_visibility(True)
        a.get_editor_property('sculpted_tissue').set_morph_target('SwallowOpen',0)
        a.call_method('RebuildAppearance')
u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
u.log('MC_MOUTH_REFERENCE_FINALIZED')
