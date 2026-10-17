"""Exercise the existing pause-menu request against the current solo PIE."""
import unreal as u

worlds = u.EditorLevelLibrary.get_pie_worlds(False)
assert len(worlds) == 1
pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
menus = [widget for widget in u.WidgetLibrary.get_all_widgets_of_class(worlds[0], u.MCGameplayMenuWidget, False)
         if widget.get_owning_player() == pc and widget.is_visible()]
assert len(menus) == 1
menus[0].show_pause()
menus[0].handle_action(u.MCGameplayMenuAction.EMOTES)
u.log('MC_PHASE1_REAL_MENU_EMOTES_REQUEST')
