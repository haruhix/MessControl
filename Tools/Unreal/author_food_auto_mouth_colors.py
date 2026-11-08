"""Set only the new CrumbColor field in saved breakfast food rows after rebuild.

Run through remote_python.py in an idle clean Editor with the new FMCFoodRow
schema loaded. The JSON palette is an initial authoring recipe; gameplay reads
the editable saved row rather than a hardcoded native ingredient palette.
"""
import copy,json,math
from pathlib import Path
import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
OUT=Path(globals().get('MC_FOOD_COLORS_OUTPUT','E:/CodexWork/MessControl/Validation/FoodAutoMouth_20261108/FoodColors'))
assert OUT.drive.upper()=='E:'
assert not OUT.exists() or not any(OUT.iterdir()),'Choose fresh evidence folder'
assert not u.EditorLevelLibrary.get_pie_worlds(True),'Preserve running Play'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Preserve unsaved content'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved map'
recipe=json.loads((ROOT/'Tools/Unreal/food_auto_mouth_colors.json').read_text(encoding='utf8'))
table=u.load_asset(recipe['table']);assert table
before=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
assert before and all('CrumbColor' in row for row in before),'Rebuild/restart with the new FMCFoodRow schema before authoring'
planned=copy.deepcopy(before);indexed={row['Name']:row for row in planned}
assert set(recipe['rows']).issubset(indexed),'Saved row names changed; review the palette first'
OUT.mkdir(parents=True,exist_ok=True)
package=ROOT/'Content/Data/DT_BreakfastMenu.uasset'
(OUT/'BeforePackage.uasset').write_bytes(package.read_bytes())
(OUT/'BeforeRows.json').write_text(json.dumps(before,ensure_ascii=False,indent=2),encoding='utf8')
for name,rgba in recipe['rows'].items():
 assert len(rgba)==4 and all(math.isfinite(x) and 0<=x<=1 for x in rgba)
 indexed[name]['CrumbColor']=dict(zip(('R','G','B','A'),rgba))
try:
 assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(planned,ensure_ascii=False))
 after=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
 actual={row['Name']:row for row in after};assert len(after)==len(before)
 for old in before:
  for field,value in old.items():
   if field=='CrumbColor' and old['Name'] in recipe['rows']:continue
   assert actual[old['Name']][field]==value,'Unrelated field changed: '+old['Name']+'.'+field
 for name,rgba in recipe['rows'].items():
  assert all(abs(actual[name]['CrumbColor'][ch]-x)<.00001 for ch,x in zip(('R','G','B','A'),rgba)),name
 assert u.EditorAssetLibrary.save_loaded_asset(table,only_if_is_dirty=False)
except Exception:
 assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(before,ensure_ascii=False))
 assert u.EditorAssetLibrary.save_loaded_asset(table,only_if_is_dirty=False),'Rollback save failed; byte backup retained'
 raise
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
report=dict(saved=True,table=recipe['table'],field='CrumbColor',rows=recipe['rows'],unrelated_fields_preserved=True,
 source='Authorable saved FMCFoodRow; JSON is initial source recipe, not a runtime hardcoded palette',
 validation='Schema, table import/readback/save only; gameplay motion and first-use readiness are separate')
(OUT/'Authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
u.log('FOOD_AUTOMOUTH_COLORS '+json.dumps(report))
