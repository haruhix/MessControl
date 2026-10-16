"""Read dirty packages and the task editor map before an optional SM6 review."""
import json
from pathlib import Path
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
report = {'pie_running': levels.is_in_play_in_editor(), 'world': world.get_path_name(),
          'dirty_maps': [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
          'dirty_content': [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
(Path(u.Paths.project_dir()).resolve() / 'Tools/Unreal/Menu3D/Evidence/editor_before_sm6_review.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report))
