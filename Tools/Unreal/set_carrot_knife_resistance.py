"""Enable knife chopping for the saved carrot row, preserving all other menu edits."""
import copy
import json
from pathlib import Path
import unreal as u

assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before saving the menu'
table = u.EditorAssetLibrary.load_asset('/Game/Data/DT_BreakfastMenu')
assert table, 'Breakfast menu is missing'
before = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
rows = copy.deepcopy(before)
carrot = next(row for row in rows if row['Name'] == 'Carrot')
previous = carrot['Resistance']
carrot['Resistance'] = 'Soft'
backup = Path(u.Paths.project_saved_dir()) / 'CarrotKnifeMenuBefore.json'
backup.write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding='utf-8')
try:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)), 'Menu import failed'
    after = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    assert after == rows, 'An unrelated menu property changed during import'
    assert u.EditorAssetLibrary.save_loaded_asset(table), 'Menu save failed'
except Exception:
    u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(before))
    raise

u.log('MC_CARROT_KNIFE_SAVED ' + json.dumps({'before': previous, 'after': 'Soft'}))
