"""Migrate existing rows once, preserving the former fragment sizes as explicit values."""
import json
from pathlib import Path
import unreal as u

lib = u.EditorAssetLibrary
table = lib.load_asset('/Game/Data/DT_BreakfastMenu')
if not table:
    raise RuntimeError('Breakfast menu missing')
rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
if lib.get_metadata_tag(table, 'MCFragmentScaleVersion') != '1':
    for row in rows:
        scale = row.get('Scale', dict(X=1, Y=1, Z=1))
        row['FragmentScale'] = {axis: max(.01, float(scale[axis]) * .5) for axis in 'XYZ'}
        row['AbsorbSeconds'] = 2
    if not u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)):
        raise RuntimeError('Food table migration failed')
    lib.set_metadata_tag(table, 'MCFragmentScaleVersion', '1')
    lib.save_loaded_asset(table, only_if_is_dirty=False)
plan = lib.load_asset('/Game/Data/DA_Day01')
plan.set_editor_property('ulcer_heal_seconds', 7)
lib.save_loaded_asset(plan, only_if_is_dirty=False)
report = {
    'treatment_seconds': plan.get_editor_property('ulcer_heal_seconds'),
    'rows': [{key: row.get(key) for key in ('Name', 'Scale', 'FragmentScale', 'SpoilSeconds', 'AbsorbSeconds')} for row in rows],
}
out = Path(u.Paths.project_dir()).resolve() / 'Artifacts/UlcerRework'
out.mkdir(parents=True, exist_ok=True)
(out / 'Settings.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_ULCER_ASSETS_PASS ' + json.dumps(report))
