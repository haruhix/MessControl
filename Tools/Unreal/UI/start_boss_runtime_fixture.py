"""Show a noninteractive UI fixture in solo PIE; no boss actors or damage."""
import json
from pathlib import Path
import unreal as u

worlds=u.EditorLevelLibrary.get_pie_worlds(False)
assert len(worlds)==1
world=worlds[0]
pc=u.GameplayStatics.get_player_controller(world,0)
assert pc and isinstance(pc,u.MCPlayerController)
cls=u.load_class(None,'/Game/Gameplay/UI/WBP_BossHealth_Reference.WBP_BossHealth_Reference_C')
widget=pc.get_editor_property('boss_health_widget')
assert widget and widget.get_class()==cls
# Exercise the actual controller-owned HUD instance. Pause only this disposable
# solo PIE world so ordinary encounter snapshots cannot overwrite fixture data.
assert u.GameplayStatics.is_game_paused(world) or u.GameplayStatics.set_game_paused(world,True)
choice=pc.get_editor_property('perk_choice_widget')
if choice: choice.set_visibility(u.SlateVisibility.COLLAPSED)
widget.set_editor_property('preview_scenario',u.MCGameplayUIPreviewScenario.THREE_BOSSES)
widget.apply_editor_preview(0.)
u._mc_boss_runtime_fixture=widget
report={'fixture':True,'boss_actors_spawned':False,'class':widget.get_class().get_path_name(),
        'owner_matches':widget.get_owning_player()==pc,'count':widget.get_displayed_boss_count(),
        'in_viewport':widget.is_in_viewport(),'frame_material':str(widget.get_editor_property('frame_material'))}
assert report['count']==3 and report['owner_matches'] and report['in_viewport']
path=Path(u.Paths.project_dir()).resolve()/'Saved/GameplayUI/BossReference/runtime_fixture.json'
path.write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_BOSS_RUNTIME_FIXTURE_PASS '+json.dumps(report))
