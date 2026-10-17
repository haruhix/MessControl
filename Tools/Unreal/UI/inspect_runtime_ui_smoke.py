"""Read only inspection of the current solo PIE UI; append evidence on E:."""
import json
from pathlib import Path
import unreal as u
worlds=u.EditorLevelLibrary.get_pie_worlds(False)
assert len(worlds)==1, 'Expected one solo PIE world'
world=worlds[0]
pc=u.GameplayStatics.get_player_controller(world,0)
assert pc
u.log('MC_UI_PC_CLASS '+pc.get_class().get_path_name())
pc=u.MCPlayerController.cast(pc)
gs=u.GameplayStatics.get_game_state(world)
report={'world':world.get_path_name(),'controller':pc.get_class().get_path_name(),'pawn':str(pc.get_controlled_pawn()),'cursor':pc.get_editor_property('show_mouse_cursor'),'move_ignored':pc.is_move_input_ignored(),'look_ignored':pc.is_look_input_ignored(),'reward_open':pc.is_reward_menu_open(),'widgets':[]}
teeth=gs.get_editor_property('arena_teeth')
report['reserve_teeth']=[{'id':tooth.get_editor_property('state').get_editor_property('tooth_id'),'health':tooth.get_editor_property('state').get_editor_property('health'),'lost':tooth.get_editor_property('state').get_editor_property('lost'),'consumed':tooth.get_editor_property('state').get_editor_property('consumed')} for tooth in teeth if tooth]
for native in (u.MCGameplayHUD,u.MCGameplayMenuWidget,u.MCScoreboardWidget,u.MCPerkChoiceWidget,u.MCBoosterWidget,u.MCBossHealthWidget):
    for widget in u.WidgetLibrary.get_all_widgets_of_class(world,native,False):
        item={'class':widget.get_class().get_path_name(),'name':widget.get_name(),'owner_matches':widget.get_owning_player()==pc,'visible':widget.is_visible(),'in_viewport':widget.is_in_viewport()}
        report['widgets'].append(item)
path=Path(u.Paths.project_dir()).resolve()/'Saved/GameplayUI/runtime_smoke.json'
previous=json.loads(path.read_text(encoding='utf-8')) if path.exists() else []
previous.append(report)
path.write_text(json.dumps(previous,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_UI_RUNTIME_SMOKE '+json.dumps(report,ensure_ascii=False))
