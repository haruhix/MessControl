import unreal as u
world=u.EditorLevelLibrary.get_pie_worlds(False)[0]
pc=u.MCPlayerController.cast(u.GameplayStatics.get_player_controller(world,0))
assert pc.is_reward_menu_open()
pc.choose_reward_perk(0)
