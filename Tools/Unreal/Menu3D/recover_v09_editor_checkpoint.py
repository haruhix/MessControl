"""Clear this task's stale review callback and save the loaded editor widget.

Run only with PIE stopped. Does not alter graph, widget tree or other assets.
"""
import json
from pathlib import Path
import unreal as u

levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
handle = getattr(u, '_mc_pbr_compare_handle', None)
if handle:
    u.unregister_slate_post_tick_callback(handle)
    u._mc_pbr_compare_handle = None
path = '/Game/UI/MainMenu3D/WBP_MainMenu3D'
widget = u.load_asset(path)
assert widget and widget.get_class().get_name() == 'WidgetBlueprint'
u.BlueprintEditorLibrary.compile_blueprint(widget)
assert u.EditorAssetLibrary.save_loaded_asset(widget, only_if_is_dirty=False)
report = {'status': 'EDITOR_ONLY_WIDGET_COMPILED_SAVED_FRESH_PIE_PENDING',
          'widget': widget.get_path_name(), 'cleared_stale_comparison_callback': bool(handle),
          'dirty_map_packages': [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
out = Path(u.Paths.project_dir()) / 'Tools/Unreal/Menu3D/Evidence/v09_editor_recovery.json'
out.write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report))
