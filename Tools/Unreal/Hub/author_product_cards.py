"""Upgrade the approved Hub cards only. Run after a current Editor build, with PIE stopped."""
from pathlib import Path
import hashlib
import json
import shutil
import sys
import time
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path('E:/CodexWork/HubWardrobePolish_20261023/ProductCardAuthoring') / time.strftime('%Y%m%d_%H%M%S')
OUT.mkdir(parents=True, exist_ok=False)
PATHS = ['/Game/Gameplay/UI/WBP_WardrobeCosmeticTile', '/Game/Gameplay/UI/WBP_HubWardrobe',
         '/Game/Gameplay/UI/WBP_HubShop', '/Game/Gameplay/UI/Data/DT_UI_Localization']
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE first.'
dirty = {p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
assert not set(PATHS).intersection(dirty), 'Preserve unsaved target packages before authoring.'
report = {'backups': [], 'preserved': 'Existing panel shells, color palette, skin slots, graphs and fixed footers.',
          'layout': 'Compact 180px cards. Accessories precede skin placeholders. Clothing description remains in tooltip.',
          'thumbnail_fit': 'ScaleToFit with full nonzero-alpha source bounds plus 8px margin; no item geometry cropped.',
          'thumbnail_source_bounds': {'Face_SportGlasses': [35,211,484,359], 'Hat_WorkCap': [45,181,467,331], 'Hat_WorkCapRed': [45,181,467,331]}}
for path in PATHS:
    source = (ROOT / 'Content' / path[len('/Game/'):]).with_suffix('.uasset')
    assert source.is_file(), 'Missing existing package: ' + path
    backup = OUT / 'BeforePackages' / source.relative_to(ROOT)
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, backup)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    assert digest == hashlib.sha256(backup.read_bytes()).hexdigest()
    report['backups'].append({'path': path, 'backup': str(backup), 'sha256': digest})
lib = u.EditorAssetLibrary
card, wardrobe, shop, translations = [lib.load_asset(path) for path in PATHS]
assert all((card, wardrobe, shop, translations))
assert u.MCCosmeticsEditorLibrary.refresh_hub_product_cards(card, wardrobe, shop), 'Scoped product-card migration rejected.'

sys.path.insert(0, str(ROOT / 'Tools/Unreal/UI'))
from localization_catalog import migrate_localization_row
before = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(translations))
known = {row['Name'].casefold() for row in before}
copy = [
    ('UI.Product.Coins', '{0} монет', '{0} coins'),
    ('UI.Product.Rare', 'Редкое', 'Rare'),
    ('UI.Product.Epic', 'Эпическое', 'Epic'),
    ('UI.Product.Legendary', 'Легендарное', 'Legendary'),
    ('UI.Product.Common', 'Обычное', 'Common'),
    ('UI.Product.Trying', 'На примерке', 'Trying on'),
    ('UI.Product.Try', 'Нажми, чтобы примерить', 'Select to try on'),
    ('UI.Product.Buy', 'Купить', 'Buy'),
    ('UI.Product.Insufficient', 'Мало монет', 'Not enough coins'),
    ('UI.Product.DefaultLook', 'Исходный внешний вид персонажа.', 'The character\'s original look.'),
    ('UI.Product.Selected', 'Выбрано', 'Selected'),
    ('UI.Product.Details', 'Подробнее', 'View details'),
]
additions = [migrate_localization_row(dict(Name=key, RU=ru, EN=en, SourceText=ru, Context='Hub product cards'))
             for key, ru, en in copy if key.casefold() not in known]
if additions:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(translations, json.dumps(before + additions, ensure_ascii=False))
    after = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(translations))
    indexed = {row['Name']: row for row in after}
    assert all(indexed[row['Name']] == row for row in before), 'Existing translations changed.'
    assert lib.save_loaded_asset(translations, only_if_is_dirty=False)
report['new_localization_keys'] = [row['Name'] for row in additions]
report['status'] = 'saved; runtime/visual validation pending'
(OUT / 'ProductCardsReport.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_HUB_PRODUCT_CARDS_AUTHORING_PASS ' + str(OUT / 'ProductCardsReport.json'))
