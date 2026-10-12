"""Leave the owned review Editor idle on its original main menu map."""
from pathlib import Path
import json
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor(), 'Preserve any active PIE'
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve any existing play world'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
assert levels.load_level('/Game/Maps/L_MainMenu'), 'Main menu did not load'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
report = dict(editor_map=world.get_path_name(), pie_active=levels.is_in_play_in_editor(),
              dirty_maps=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
              packages_saved=False)
target = Path(u.Paths.project_saved_dir())/'Checks/ApprovedEditorRestoration.json'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(json.dumps(report, indent=2), encoding='utf8')
u.log('MC_APPROVED_EDITOR_RESTORED '+json.dumps(report))
