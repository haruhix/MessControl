"""Run in the idle, rebuilt MessControl editor. Creates only the V2 foundation.

Existing widgets/palette/font edits and existing localization rows are preserved.
No level, gameplay, settings, or PSD is changed. All outputs stay on E:.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
assert ROOT.drive.upper() == 'E:', ROOT
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
BASE = '/Game/Gameplay/UI/V2'
OUT = Path('E:/CodexWork/MessControl/Validation/UIFoundation_20261103')
OUT.mkdir(parents=True, exist_ok=True)
assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.EditorAssetLibrary
report = {'created': [], 'preserved': [], 'localization_rows_added': []}
table_path = '/Game/Gameplay/UI/Data/DT_UI_Localization'
targets = (BASE, table_path)
for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages():
    name = package.get_name()
    assert not any(name.startswith(target) for target in targets), 'Unsaved asset preserved: ' + name

# Keep editable font masters on E:; never use the user's D: source as output.
font_source = Path('D:/sda/LeFunFont/LeFunFont-Regular.otf')
expected = '19930ad7572f3ad6c282a2471b5cb3c1ab0a61080244b1646ef45e34f0211c4a'
assert hashlib.sha256(font_source.read_bytes()).hexdigest() == expected
font_dir = ROOT / 'ArtSource/UI/V2/Fonts'
font_dir.mkdir(parents=True, exist_ok=True)
master = font_dir / font_source.name
if master.exists():
    assert hashlib.sha256(master.read_bytes()).hexdigest() == expected, 'Edited font master preserved'
else:
    shutil.copy2(font_source, master)
license_source = font_source.parent / 'LeFunFont license.txt'
if license_source.exists() and not (font_dir / license_source.name).exists():
    shutil.copy2(license_source, font_dir / license_source.name)

def import_face(name, source):
    path = BASE + '/Fonts/' + name
    if lib.does_asset_exist(path):
        report['preserved'].append(path)
        return u.load_asset(path)
    task = u.AssetImportTask()
    task.set_editor_property('filename', str(source))
    task.set_editor_property('destination_path', BASE + '/Fonts')
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', False)
    task.set_editor_property('save', True)
    factory = u.FontFileImportFactory()
    factory.set_editor_property('batch_create_font_asset', u.BatchCreateFontAsset.NO)
    task.set_editor_property('factory', factory)
    assets.import_asset_tasks([task])
    face = u.load_asset(path)
    assert isinstance(face, u.FontFace), path
    face.set_editor_property('loading_policy', u.FontLoadingPolicy.INLINE)
    assert lib.save_loaded_asset(face, only_if_is_dirty=False)
    report['created'].append(path)
    return face

face = import_face('FF_LeFun_Regular', master)
cjk = import_face('FF_UI_CJKFallback', Path(u.Paths.engine_content_dir()) / 'Slate/Fonts/DroidSansFallback.ttf')
current = u.MCGameplayUIFactory.get_theme_for_version(u.MCUIVersion.CURRENT)
new_palette = u.MCGameplayUIFactory.get_theme_for_version(u.MCUIVersion.NEW).get_editor_property('palette')
font_path = BASE + '/Fonts/F_UI_Universal'
if lib.does_asset_exist(font_path):
    font = u.load_asset(font_path)
    report['preserved'].append(font_path)
else:
    fallback = current.get_editor_property('body_font').get_editor_property('font_object')
    if not isinstance(fallback, u.Font):
        fallback = u.load_asset('/Engine/EngineFonts/Roboto')
    font = assets.duplicate_asset('F_UI_Universal', BASE + '/Fonts', fallback)
    assert font
    assert u.MCGameplayUIEditorLibrary.configure_universal_ui_font(font, fallback, face)
    assert u.MCGameplayUIEditorLibrary.configure_boss_name_cjk_fallback(font, cjk)
    assert lib.save_loaded_asset(font, only_if_is_dirty=False)
    report['created'].append(font_path)

# Append stable keys without overwriting any existing copy/translation.
table = u.load_asset(table_path)
assert table, table_path
rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
known = {row['Name'] for row in rows}
for row in json.loads((ROOT / 'Tools/Unreal/UI/ui_v2_texts.json').read_text(encoding='utf-8')):
    if row['Name'] not in known:
        rows.append(row)
        known.add(row['Name'])
        report['localization_rows_added'].append(row['Name'])
# Fill only empty cells of the shared labels used by the first root slice.
# Existing translations and FText identities are retained verbatim.
report['localization_cells_filled'] = []
indexed = {row['Name']:row for row in rows}
for patch in json.loads((ROOT / 'Tools/Unreal/UI/ui_v2_existing_texts.json').read_text(encoding='utf-8')):
    target = indexed[patch['Name']]
    for language, value in patch.items():
        if language != 'Name' and not target.get(language):
            target[language] = value
            report['localization_cells_filled'].append(patch['Name'] + '.' + language)
if report['localization_rows_added'] or report['localization_cells_filled']:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False))
    assert lib.save_loaded_asset(table, only_if_is_dirty=False)

theme_path = BASE + '/Theme/DA_UITheme_V2'
if lib.does_asset_exist(theme_path):
    theme = u.load_asset(theme_path)
    report['preserved'].append(theme_path)
else:
    theme = lib.duplicate_asset(current.get_path_name().split('.')[0], theme_path)
    assert theme
    theme.set_editor_property('palette', new_palette)
    theme.set_editor_property('corner_radius', 16.0)
    theme.set_editor_property('outline_width', 3.0)
    for key in ('body_font', 'heading_font', 'display_font', 'compact_heading_font'):
        info = theme.get_editor_property(key)
        info.set_editor_property('font_object', font)
        info.set_editor_property('typeface_font_name', 'Default')
        theme.set_editor_property(key, info)
    report['created'].append(theme_path)

assert lib.save_loaded_asset(theme, only_if_is_dirty=False)
u.MCGameplayUIFactory.invalidate_new_theme()
mapping = {str(k):v for k,v in theme.get_editor_property('widget_classes').items()}
for role, name, native in (
    ('Universal', 'WBP_UIUniversal', 'MCUniversalUIWidget'),
    ('UniversalPreview', 'WBP_UIUniversalPreview', 'MCUniversalUIPreviewWidget'),
):
    path = BASE + '/' + name
    existed = lib.does_asset_exist(path)
    parent = u.load_class(None, '/Script/MessControl.' + native)
    assert parent, 'Rebuild/load the new C++ before authoring: ' + native
    bp = u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(path, parent)
    assert bp, path
    cls = u.load_class(None, path + '.' + name + '_C')
    assert cls
    if role not in mapping or not str(mapping[role]):
        mapping[role] = cls
    # The preview is authored after the Universal role is resident, so its
    # samples are instances of the editable shared Blueprint rather than copies.
    theme.set_editor_property('widget_classes', mapping)
    assert lib.save_loaded_asset(theme, only_if_is_dirty=False)
    u.MCGameplayUIFactory.invalidate_new_theme()
    report['preserved' if existed else 'created'].append(path)
theme.set_editor_property('widget_classes', mapping)
assert lib.save_loaded_asset(theme, only_if_is_dirty=False)
theme.notify_theme_changed()
u.MCGameplayUIFactory.invalidate_new_theme()
report['scope'] = 'Universal shapes/preview and F3 foundation; existing screen layouts are still being migrated.'
(OUT / 'authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_UI_V2_FOUNDATION ' + json.dumps(report, ensure_ascii=False))
