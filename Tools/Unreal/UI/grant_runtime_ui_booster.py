import unreal as u
world=u.EditorLevelLibrary.get_pie_worlds(False)[0]
pc=u.MCPlayerController.cast(u.GameplayStatics.get_player_controller(world,0))
assert not pc.is_reward_menu_open()
perks=pc.get_editor_property('player_state').get_editor_property('perks')
assert perks.server_grant_perk('Fallback_Grenade')
u.log('MC_UI_BOOSTER_GRANTED_IN_TRANSIENT_PIE')
