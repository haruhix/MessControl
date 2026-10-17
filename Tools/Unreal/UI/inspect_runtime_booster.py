import json
from pathlib import Path
import unreal as u
world=u.EditorLevelLibrary.get_pie_worlds(False)[0]
pc=u.MCPlayerController.cast(u.GameplayStatics.get_player_controller(world,0))
perks=pc.get_editor_property('player_state').get_editor_property('perks')
data={'queue':[str(x) for x in perks.get_editor_property('booster_queue')],'grenade_armed':perks.get_editor_property('grenade_armed'),'reward_open':pc.is_reward_menu_open()}
assert not data['queue'] and data['grenade_armed']
(Path(u.Paths.project_dir()).resolve()/'Saved/GameplayUI/runtime_booster.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
u.log('MC_UI_BOOSTER_ACTUAL '+json.dumps(data))
