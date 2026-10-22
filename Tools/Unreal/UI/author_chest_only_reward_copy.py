"""Save only the eight revised chest-only reward UI copy rows after an approved build.

Run in an idle, clean Editor. This does not build, start PIE, rebuild a WBP tree,
change the theme, or touch gameplay/perk definitions. Existing non-RU/EN
translations and every unrelated DataTable row are retained.
"""
import copy
import hashlib
import json
import sys
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_CHEST_ONLY_COPY_OUTPUT',
    'E:/CodexWork/MessControlChestOnly_20261021/CopyAuthoring')).resolve()
assert OUT.drive.upper() == 'E:', 'All authoring evidence must stay on E:'
assert not OUT.exists() or not any(OUT.iterdir()), 'Choose a fresh evidence folder'
OUT.mkdir(parents=True, exist_ok=True)
PACKAGE = '/Game/Gameplay/UI/Data/DT_UI_Localization'
KEYS = (
    'UI.HUD.Timer.SurviveAndLevel', 'UI.HUD.Endure',
    'UI.Tutorial.ShortComplete', 'UI.Tutorial.Copy.ShortCompleteFairy',
    'UI.HUD.Hint.Pickaxe', 'UI.HUD.Hint.Knife',
    'UI.HUD.Hint.NutsNoXP', 'UI.HUD.Hint.Nuts',
)
FIELDS = ('SourceText', 'RU', 'EN', 'Context')
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
report = dict(scope=__doc__, backup=str(backup), changed=[], preserved_translation_rows=[])
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
    report['changed'].append(key)
    if any(target.get(field) for field in ('DE', 'FR', 'ES', 'ZH_HANS', 'ZH_HANT', 'JA', 'KO', 'PT_BR', 'AdditionalLanguages', 'Localizations')):
        report['preserved_translation_rows'].append(key)

(OUT / 'BeforeRows.json').write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding='utf-8')
try:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,
        json.dumps(planned, ensure_ascii=False)), 'Could not import revised copy'
    after = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    after_index = index_rows(after, 'Updated texts')
    for row in before:
        actual = after_index[row['Name'].casefold()]
        for field, value in row.items():
            if row['Name'] in KEYS and field in FIELDS:
                continue
            assert actual.get(field) == value, 'Unrelated field changed: ' + row['Name'] + '.' + field
    for key in KEYS:
        for field in FIELDS:
            assert text_source(after_index[key.casefold()][field]) == catalog[key.casefold()][field], key + '.' + field
    valid, problems = native_validate(u, table, table)
    assert valid, '\n'.join(problems)
    assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), PACKAGE
except Exception:
    # The source table was clean on entry; restore its exact row payload if an
    # import/verification failed rather than saving a partially authored table.
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,
        json.dumps(before, ensure_ascii=False)), 'Rollback failed; use the preserved package backup'
    assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'Rollback could not be saved'
    raise
assert not dirty(), 'Unexpected dirty packages were preserved'
report['passed'] = True
report['validation'] = 'Targeted DataTable copy import, native content validation and save only; gameplay validation is separate.'
(OUT / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_CHEST_ONLY_COPY_AUTHOR_COMPLETE ' + str(OUT / 'Authoring.json'))
