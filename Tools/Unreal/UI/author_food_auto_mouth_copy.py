"""Import only the six automatic food-intake tutorial rows into the saved table.

Run in an idle, clean Unreal Editor. This does not build or start PIE.
Evidence and a byte-exact package backup stay on E:.
"""
import copy
import hashlib
import json
import sys
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_FOOD_AUTO_COPY_OUTPUT',
    'E:/CodexWork/MessControl/Validation/FoodAutoMouth_20261108/Localization')).resolve()
assert OUT.drive.upper() == 'E:', 'Evidence must stay on E:'
assert not OUT.exists() or not any(OUT.iterdir()), 'Choose a fresh evidence folder'
OUT.mkdir(parents=True, exist_ok=True)
PACKAGE = '/Game/Gameplay/UI/Data/DT_UI_Localization'
KEYS = tuple('UI.Tutorial.Copy.' + key for key in (
    'AutoCutFairy', 'AutoCutHint', 'AutoFreshFairy', 'AutoFreshHint',
    'AutoBreakfastHint', 'AutoFreshWrongExit'))
FIELDS = ('SourceText', 'RU', 'EN', 'DE', 'FR', 'ES', 'ZH_HANS', 'ZH_HANT', 'JA', 'KO', 'PT_BR', 'Context')

dirty = lambda: list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(
    u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop Play first'
assert not dirty(), 'Preserve unsaved editor work before authoring'
sys.path.insert(0, str(ROOT / 'Tools/Unreal/UI'))
from author_localization_catalog import native_validate
from localization_catalog import index_rows, text_source

source = ROOT / 'Content/Gameplay/UI/Data/DT_UI_Localization.uasset'
backup = OUT / 'BeforePackages/DT_UI_Localization.uasset'
backup.parent.mkdir(parents=True, exist_ok=True)
backup.write_bytes(source.read_bytes())
assert hashlib.sha256(backup.read_bytes()).digest() == hashlib.sha256(source.read_bytes()).digest()
table = u.load_asset(PACKAGE)
assert table, PACKAGE
before = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
catalog = index_rows(json.loads((ROOT / 'Tools/Unreal/UI/gameplay_ui_texts.json').read_text(encoding='utf-8')), 'Canonical texts')
planned = copy.deepcopy(before)
planned_index = index_rows(planned, 'Saved texts')
for key in KEYS:
    canonical = catalog[key.casefold()]
    target = planned_index.get(key.casefold())
    if target is None:
        target = copy.deepcopy(canonical)
        planned.append(target)
        planned_index[key.casefold()] = target
    else:
        for field in FIELDS:
            target[field] = canonical[field]
(OUT / 'BeforeRows.json').write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding='utf-8')
try:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,
        json.dumps(planned, ensure_ascii=False)), 'Could not import automatic food tutorial copy'
    after = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    actual = index_rows(after, 'Updated texts')
    for row in before:
        for field, value in row.items():
            if row['Name'] in KEYS and field in FIELDS:
                continue
            assert actual[row['Name'].casefold()].get(field) == value, 'Unrelated field changed: ' + row['Name'] + '.' + field
    for key in KEYS:
        for field in FIELDS:
            assert text_source(actual[key.casefold()][field]) == catalog[key.casefold()][field], key + '.' + field
    valid, problems = native_validate(u, table, table)
    assert valid, '\n'.join(problems)
    assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), PACKAGE
except Exception:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,
        json.dumps(before, ensure_ascii=False)), 'Rollback failed; use package backup'
    assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Rollback save failed'
    raise
assert not dirty(), 'Unexpected dirty packages were preserved'
report = dict(passed=True, rows=list(KEYS), languages=list(FIELDS[1:-1]), backup=str(backup),
    validation='Targeted table import, unrelated fields preserved, native validation and save; gameplay acceptance is separate.')
(OUT / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_FOOD_AUTO_COPY_COMPLETE ' + str(OUT / 'Authoring.json'))
