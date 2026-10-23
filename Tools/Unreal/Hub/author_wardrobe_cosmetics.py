"""Narrow backed-up Face/Hat wardrobe append; run in the current Editor with PIE stopped."""
from pathlib import Path
import hashlib,json,shutil,time,sys
import unreal as u
ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path('E:/CodexWork/HubSystems_20261023/Cosmetics/WardrobeAuthoring')/time.strftime('%Y%m%d_%H%M%S')
OUT.mkdir(parents=True,exist_ok=False)
lib=u.EditorAssetLibrary
paths=['/Game/Gameplay/UI/WBP_HubWardrobe','/Game/Gameplay/UI/WBP_WardrobeCosmeticTile','/Game/Gameplay/UI/Data/DT_UI_Localization']
report={'backups':[],'created':[]}
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Stop PIE first.'
dirty={p.get_path_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
assert not (set(paths)&dirty),'Preserve unsaved wardrobe/tile changes before authoring.'
for path in paths:
    package=(ROOT/'Content'/path[len('/Game/'):]).with_suffix('.uasset')
    if not package.is_file():report['created'].append(path);continue
    backup=OUT/'BeforePackages'/package.relative_to(ROOT);backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(package,backup)
    digest=hashlib.sha256(package.read_bytes()).hexdigest();assert digest==hashlib.sha256(backup.read_bytes()).hexdigest()
    report['backups'].append({'path':path,'backup':str(backup),'sha256':digest})
tile=lib.load_asset(paths[1]) if lib.does_asset_exist(paths[1]) else u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(paths[1],u.MCCosmeticItemWidget)
assert tile,'Could not create editable tile.'
wardrobe=lib.load_asset(paths[0]);assert wardrobe,'Existing wardrobe is required.'
tile_class=lib.load_blueprint_class(paths[1]);assert tile_class
assert u.MCCosmeticsEditorLibrary.add_wardrobe_cosmetics_presentation(wardrobe,tile_class),'Wardrobe migration rejected; no replacement tree accepted.'
sys.path.insert(0,str(ROOT/'Tools/Unreal/UI'))
from localization_catalog import migrate_localization_row
table=lib.load_asset(paths[2]);assert table,'Existing UI localization table is required.'
before=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table));known={r['Name'].casefold() for r in before}
copy=[('UI.Wardrobe.Face','На лицо','Face'),('UI.Wardrobe.Hat','Головной убор','Headwear'),('UI.Wardrobe.Coins','Монеты: {0}','Coins: {0}'),
    ('UI.Wardrobe.NoAccessory','Без аксессуара','No accessory'),('UI.Wardrobe.Selected','Выбрано','Selected'),('UI.Wardrobe.Owned','В наличии','Owned'),('UI.Wardrobe.BuyPrice','Купить · {0} монет','Buy · {0} coins')]
additions=[migrate_localization_row(dict(Name=key,RU=ru,EN=en,SourceText=ru,Context='Hub wardrobe cosmetic slots')) for key,ru,en in copy if key.casefold() not in known]
if additions:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(before+additions,ensure_ascii=False))
    after=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table));after_by_id={r['Name']:r for r in after}
    assert all(after_by_id[r['Name']]==r for r in before),'Existing translations changed.'
    assert lib.save_loaded_asset(table,only_if_is_dirty=False)
report['added_localization_keys']=[r['Name'] for r in additions]
report['status']='saved';report['preserved']='Existing palette, six skin placeholders, theme/materials, graph and Apply/Back controls retained.'
(OUT/'WardrobeCosmeticsReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
u.log('MC_WARDROBE_COSMETICS_AUTHORING_PASS '+str(OUT/'WardrobeCosmeticsReport.json'))
