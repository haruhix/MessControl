"""Set only the saved pepper row's fuse to ten seconds in the connected editor."""
import json
import unreal as u

table = u.EditorAssetLibrary.load_asset('/Game/Data/DT_BreakfastMenu')
assert table, 'DT_BreakfastMenu is missing'
rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
pepper = next(row for row in rows if row['Name'] == 'SpicyPepper')
pepper['FuseSeconds'] = 10.0
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False))
assert u.EditorAssetLibrary.save_loaded_asset(table)
saved = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
assert next(row for row in saved if row['Name'] == 'SpicyPepper')['FuseSeconds'] == 10.0
u.log('MC_SPICY_FUSE_TUNED seconds=10')
