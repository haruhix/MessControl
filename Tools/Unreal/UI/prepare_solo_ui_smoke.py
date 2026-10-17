import json
from pathlib import Path
import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
settings=u.get_default_object(u.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
fields=('PlayNumberOfClients','NewWindowWidth','NewWindowHeight')
old={key:settings.get_editor_property(key) for key in fields}
u._mc_ui_play_settings=old
settings.set_editor_property('PlayNumberOfClients',1)
settings.set_editor_property('NewWindowWidth',1280)
settings.set_editor_property('NewWindowHeight',720)
path=Path(u.Paths.project_dir()).resolve()/'Saved/GameplayUI/solo_play_settings.json'
path.write_text(json.dumps({key:str(value) for key,value in old.items()},indent=2),encoding='utf-8')
u.log('MC_UI_SOLO_SETTINGS_READY')
