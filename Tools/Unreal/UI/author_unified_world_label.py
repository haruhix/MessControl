"""Author one shared editable world-label form and its reflected prewarm keys."""
import json
from pathlib import Path
import unreal as u
assert '-MCUIFoundationOwned' in u.SystemLibrary.get_command_line()
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
path='/Game/Gameplay/UI/V2/Widgets/WBP_WorldLabel_V2'
theme=u.load_asset('/Game/Gameplay/UI/V2/Theme/DA_UITheme_V2');assert theme
native=u.load_class(None,'/Script/MessControl.MCWorldLabelWidget');assert native
roles={str(k):v for k,v in theme.get_editor_property('widget_classes').items()}
expected=path+'.WBP_WorldLabel_V2_C'
assert str(roles.get('WorldLabel','')) in ('','None','nullptr') or expected in str(roles['WorldLabel'])
keys=['UI.World.ReadyTitle','UI.World.ReadyCount','UI.Tutorial.Copy.SharedTarget','UI.Tutorial.Copy.YourTarget','UI.World.CursedContractLabel','UI.Reward.RiskyTitle','UI.World.Care.Ulcer','UI.World.Care.BrushRemaining','UI.World.ArenaToothCare','UI.World.HealthValue','UI.World.GrenadeArmedLabel']
existed=u.EditorAssetLibrary.does_asset_exist(path)
bp=u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(path,native);assert bp
cls=u.load_class(None,expected);defaults=u.get_default_object(cls)
existing=[str(x) for x in defaults.get_editor_property('preparation_keys')]
assert not existing or existing==keys,'Preserve an artist-edited key list'
if not existing:
 defaults.set_editor_property('preparation_keys',[u.Name(key) for key in keys])
 u.BlueprintEditorLibrary.compile_blueprint(bp)
 cls=u.load_class(None,expected);defaults=u.get_default_object(cls)
 assert [str(x) for x in defaults.get_editor_property('preparation_keys')]==keys
 assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
roles['WorldLabel']=cls;theme.set_editor_property('widget_classes',roles)
assert u.EditorAssetLibrary.save_loaded_asset(theme,only_if_is_dirty=False)
u.MCGameplayUIFactory.invalidate_new_theme()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
out=Path('E:/CodexWork/MessControl/Validation/UIOverlayWorld_20261104');out.mkdir(parents=True,exist_ok=True)
(out/'WorldLabelAuthorReport.json').write_text(json.dumps({'asset':path,'new':not existed,'native':native.get_path_name(),'role':'WorldLabel','preparationKeys':keys,'editableTreePreserved':existed,'runtimeState':'Shared presentation only; original owners keep their label text/state.'},ensure_ascii=False,indent=2),encoding='utf-8')
print('WORLD_LABEL_EDITABLE_ROLE_AND_KEYS_AUTHORED',len(keys))
