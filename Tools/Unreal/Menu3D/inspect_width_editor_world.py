import json
from pathlib import Path
import unreal as u
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
actors=u.GameplayStatics.get_all_actors_of_class(world,u.Actor) if world else []
report={'world':world.get_path_name() if world else None,
    'actors':[{'label':a.get_actor_label(),'path':a.get_path_name(),'tags':[str(t) for t in a.tags]} for a in actors],
    'map_packages':[a.get_path_name() for a in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/width_world_state.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_WIDTH_WORLD '+json.dumps(report))
