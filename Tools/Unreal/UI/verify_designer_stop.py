import json
from pathlib import Path
import unreal as u
times={item.get_path_name():item.get_editor_property('preview_time') for item in u.ObjectIterator(u.MCPerkChoiceWidget) if item.get_path_name().startswith('/Engine/Transient.World_')}
path=Path(u.Paths.project_dir()).resolve()/'Saved/GameplayUI/designer_validation.json'
if not hasattr(u,'_mc_ui_stopped_times'):
    assert max(times.values())>0.1,'Play did not advance the Designer fixture'
    u._mc_ui_stopped_times=times
    result={'play_advanced':True,'stopped_times':times}
else:
    assert times==u._mc_ui_stopped_times,'Stop did not freeze Designer time'
    result={'play_advanced':True,'stop_froze_time':True,'stopped_times':times}
path.write_text(json.dumps(result,indent=2),encoding='utf-8')
u.log('MC_UI_DESIGNER_CHECK '+json.dumps(result))
