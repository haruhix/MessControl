"""Set only the authored jump duration; retain every existing clip and tuning value."""
import unreal as u,json
from pathlib import Path
r=Path(u.Paths.project_dir());out=r/'Artifacts/NutKnightPlayback_20261016'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
p=u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain');s=p.get_editor_property('settings');b=s.get_editor_property('boss')
before=b.export_text();clip=b.get_editor_property('tank_jump_animation');seconds=clip.get_play_length()
b.set_editor_property('tank_jump_animation_seconds',seconds)
s.set_editor_property('boss',b);p.set_editor_property('settings',s);assert u.EditorAssetLibrary.save_loaded_asset(p)
(out/'ConnectionReport.json').write_text(json.dumps({'before':before,'after':b.export_text(),'jump_seconds':seconds,'jump_asset':clip.get_path_name(),'changed_fields':['tank_jump_animation_seconds']},indent=2))
print('KNIGHT_PLAYBACK_CONNECTED',seconds)
