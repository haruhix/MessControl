"""Create four editable V2 presenters and their reflected shared-theme class roots.

Run in the idle, clean MessControl Editor after the current native build. Existing
component trees are preserved by CreateEditableGameplayWidget. Root branches are
appended separately with unified_hub_reward_umg_recipe.json; never rebuild WBP_UIRoot.
"""
import json
from pathlib import Path
import unreal as u

assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Preserve the current Play session'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved content'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
project_root = Path(u.Paths.project_dir()).resolve()
assert str(project_root).replace('\\', '/').lower().rstrip('/') == 'e:/devgame/messcontrol'
output_dir = Path('E:/CodexWork/MessControl/Validation/UIHubReward_20261104')
base = '/Game/Gameplay/UI/V2'
lib = u.EditorAssetLibrary
specs = [
    ('UnifiedHub', 'WBP_UIHub', 'MCUnifiedHubWidget'),
    ('UnifiedReward', 'WBP_UIReward', 'MCUnifiedRewardWidget'),
    ('UnifiedFrontend', 'WBP_UIFrontend', 'MCUnifiedFrontendWidget'),
    ('UnifiedGameplayMenu', 'WBP_UIGameplayMenu', 'MCUnifiedGameplayMenuWidget'),
]
# Validate all inputs before creating any asset or changing the shared theme.
theme = u.load_asset(base + '/Theme/DA_UITheme_V2')
table = u.load_asset('/Game/Gameplay/UI/Data/DT_UI_Localization')
assert theme and table
roles = {str(key): value for key, value in theme.get_editor_property('widget_classes').items()}
classes = {}
for role, name, native in specs:
    native_path = '/Script/MessControl.' + native
    classes[native] = u.load_class(None, native_path)
    assert classes[native], 'Load the current native Editor build: ' + native_path
    expected_class = base + '/' + name + '.' + name + '_C'
    previous = str(roles.get(role, ''))
    assert previous in ('', 'None', 'nullptr') or expected_class in previous or native_path in previous, (
        'Preserve an unrelated authored role override', role, previous)
texts_path = project_root / 'Tools/Unreal/UI/ui_v2_reward_texts.json'
new_rows = json.loads(texts_path.read_text(encoding='utf-8'))
languages = ('RU', 'EN', 'DE', 'FR', 'ES', 'PT_BR', 'ZH_HANS', 'ZH_HANT', 'JA', 'KO')
expected_keys = {'UI.Reward.Title', 'UI.Reward.Type.Passive', 'UI.Reward.Type.Active',
                 'UI.Reward.Type.Consumable', 'UI.Reward.Type.Tool',
                 'UI.Reward.AutoPickIn', 'UI.Reward.AutoPicking'}
assert len(new_rows) == len(expected_keys)
assert {row['Name'] for row in new_rows} == expected_keys
for row in new_rows:
    assert all(isinstance(row.get(language), str) and row[language] for language in languages), row['Name']
    assert row['SourceText'] == row['RU'] and isinstance(row['Context'], str)
rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
existing_keys = {row['Name'] for row in rows}
assert len(existing_keys) == len(rows), 'Preserve a localization table with duplicate keys for review'
components = []
for role, name, native in specs:
    asset_path = base + '/' + name
    existed = lib.does_asset_exist(asset_path)
    blueprint = u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(asset_path, classes[native])
    assert blueprint, ('Native authoring guard rejected asset', asset_path)
    class_path = asset_path + '.' + name + '_C'
    generated = u.load_class(None, class_path)
    assert generated and isinstance(u.get_default_object(generated), getattr(u, native)), ('Unexpected parent class', asset_path)
    roles[role] = generated
    components.append({'asset': asset_path, 'role': role, 'native': native, 'class': class_path,
                       'new': not existed, 'existingTreePreserved': existed})
# Reflected soft-class entries retain these presentation resources in the New theme/prewarm graph.
theme.set_editor_property('widget_classes', roles)
assert lib.save_loaded_asset(theme, only_if_is_dirty=False)
u.MCGameplayUIFactory.invalidate_new_theme()
added = []
preserved = []
for row in new_rows:
    if row['Name'] in existing_keys:
        preserved.append(row['Name'])
        continue  # Existing translations and user edits are never replaced.
    rows.append(row)
    existing_keys.add(row['Name'])
    added.append(row['Name'])
if added:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False))
    assert lib.save_loaded_asset(table, only_if_is_dirty=False)
report = {
    'components': components,
    'localizationKeysAdded': added,
    'existingLocalizationRowsPreserved': preserved,
    'theme': base + '/Theme/DA_UITheme_V2',
    'reflectedClassRoots': [role for role, _, _ in specs],
    'root': 'Preserved; append only with Tools/Unreal/UI/unified_hub_reward_umg_recipe.json',
    'rootPages': {'Hub': 2, 'Reward': 9, 'Frontend': 10, 'GameplayMenu': 11},
    'resources': 'Shared New theme class roots; existing fonts, shapes and source icon references. No DT_Perks changes.',
    'treePolicy': 'Create missing component seeds only. Never rebuild an existing component or root tree.',
    'validation': 'Asset authoring only; runtime, saved-root append and visual verification are separate.',
    'dirtyContentAfter': [package.get_path_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    'dirtyMapsAfter': [package.get_path_name() for package in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
}
output_dir.mkdir(parents=True, exist_ok=True)
(output_dir / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))
