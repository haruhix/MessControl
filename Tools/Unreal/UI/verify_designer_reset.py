import json
from pathlib import Path
import unreal as u
times=[item.get_editor_property('preview_time') for item in u.ObjectIterator(u.MCPerkChoiceWidget) if item.get_path_name().startswith('/Engine/Transient.World_')]
assert times and all(value==0 for value in times),'Reset did not restore zero time'
path=Path(u.Paths.project_dir()).resolve()/'Saved/GameplayUI/designer_validation.json'
result=json.loads(path.read_text(encoding='utf-8'));result['reset_zero']=True
path.write_text(json.dumps(result,indent=2),encoding='utf-8')
del u._mc_ui_stopped_times
u.log('MC_UI_DESIGNER_CHECK '+json.dumps(result))
