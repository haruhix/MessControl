"""Explicitly replace the prototype DT_Perks rows with empty placeholder definitions."""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
table = u.EditorAssetLibrary.load_asset('/Game/Gameplay/Roguelike/DT_Perks')
assert isinstance(table, u.DataTable), 'Create the roguelike foundation first'
rows = json.loads((root / 'Tools/Unreal/roguelike_perks.json').read_text(encoding='utf-8-sig'))
assert len(rows) == 12 and all(not row.get('Description') and row.get('EffectClass') == 'None' for row in rows)
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows))
assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False)
for path in ['/Game/Gameplay/Roguelike/BP_RewardChest', '/Game/Gameplay/Boss/BP_ZombieBoss']:
    blueprint = u.EditorAssetLibrary.load_asset(path)
    assert blueprint
    u.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert u.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
u.log('MC_PERK_PLACEHOLDERS_PASS 12 empty rows saved; both foundation Blueprints compiled')
u.SystemLibrary.quit_editor()
