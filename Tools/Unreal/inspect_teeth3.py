import unreal as u
import json
from pathlib import Path
w = u.EditorLevelLibrary.get_game_world()
rows = []
for a in u.GameplayStatics.get_all_actors_of_class(w, u.MCToothCharacter):
    m = a.mesh
    p = a.get_editor_property('appearance')
    bones = p.get_editor_property('bone_map')
    rows.append(dict(actor=a.get_name(), capsule=str(a.get_actor_location()), mesh=str(m.get_world_location()),
        bones={str(k): str(m.get_socket_location(v)) for k,v in bones.items() if str(k) in ['body','hand_l','hand_r','leg_l','foot_l']},
        grip=str(a.get_editor_property('grip').get_editor_property('frame'))))
Path(u.Paths.project_saved_dir(), 'Teeth3_Live.json').write_text(json.dumps(rows,indent=2))
u.log('MC_TEETH3_LIVE ' + json.dumps(rows))
