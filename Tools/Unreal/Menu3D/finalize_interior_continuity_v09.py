"""Read-only final editor state after restoring temporary PIE preferences."""
import json
from pathlib import Path
import unreal as u
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not getattr(u,'_mc_lower_review_handle',None)
settings=u.get_default_object(u.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
result={'pie_running':False,'active_lower_review_callback':False,
    'editor_map':u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name(),
    'dirty_maps':[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    'dirty_content':[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    'cvars':{name:u.SystemLibrary.get_console_variable_int_value(name) for name in (
        'r.MorphTarget.ForceUpdate','r.MorphTarget.Mode','r.AntiAliasingMethod','r.OneFrameThreadLag','r.Shadow.Virtual.Enable')},
    'play_window':{name:settings.get_editor_property(name) for name in ('NewWindowWidth','NewWindowHeight','CenterNewWindow')},
    'validation':'Native isolated-map solo PIE only; physical wheel not exercised after Computer Use was stopped by Escape. No build/cook/network test.'}
assert result['cvars']=={'r.MorphTarget.ForceUpdate':0,'r.MorphTarget.Mode':1,'r.AntiAliasingMethod':2,'r.OneFrameThreadLag':1,'r.Shadow.Virtual.Enable':0}
assert result['play_window']=={'NewWindowWidth':2560,'NewWindowHeight':1358,'CenterNewWindow':False}
target=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/v09_interior_continuity_saved_editor_state.json'
assert not result['dirty_maps'] and not result['dirty_content'],result
assert not target.exists()
target.write_text(json.dumps(result,indent=2),encoding='utf8')
u.log('MC_INTERIOR_CONTINUITY_FINAL '+json.dumps(result))
