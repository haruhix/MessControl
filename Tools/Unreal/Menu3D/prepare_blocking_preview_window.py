import json
from pathlib import Path
import unreal as u
settings=u.get_default_object(u.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
before={key:settings.get_editor_property(key) for key in ('new_window_width','new_window_height','center_new_window')}
settings.set_editor_property('new_window_width',1600)
settings.set_editor_property('new_window_height',900)
settings.set_editor_property('center_new_window',True)
out=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/preview_window_session_settings.json'
out.write_text(json.dumps(before,indent=2),encoding='utf8')
u.log('MC_BLOCKING_PREVIEW_WINDOW_SESSION_ONLY')
