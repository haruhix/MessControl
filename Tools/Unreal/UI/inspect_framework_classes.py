import unreal as u
for cls in (u.load_class(None,'/Script/MessControl.MCGameMode'),u.load_class(None,'/Game/Blueprints/BP_MouthGameMode.BP_MouthGameMode_C')):
    defaults=u.get_default_object(cls)
    u.log('MC_UI_FRAMEWORK '+cls.get_path_name()+' '+str({key:str(defaults.get_editor_property(key)) for key in ('player_controller_class','game_state_class','player_state_class','default_pawn_class')}))
for world in u.EditorLevelLibrary.get_pie_worlds(False):
    for actor in u.GameplayStatics.get_all_actors_of_class(world,u.PlayerController):
        u.log('MC_UI_PC_ACTOR '+actor.get_path_name()+' '+actor.get_class().get_path_name()+' pawn='+str(actor.get_controlled_pawn()))
    gm=u.GameplayStatics.get_game_mode(world)
    u.log('MC_UI_FRAMEWORK_RUNTIME '+str({key:str(gm.get_editor_property(key)) for key in ('player_controller_class','game_state_class','player_state_class','default_pawn_class')}))
