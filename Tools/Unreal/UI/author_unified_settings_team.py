"""Author editable Settings/TEAM component assets; keep the saved root layout intact.
Run idle after rebuilding Editor, then append the components with native UMG MCP.
"""
from pathlib import Path
import json
import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
root=Path(u.Paths.project_dir()).resolve();assert root.drive.upper()=='E:'
out=Path('E:/CodexWork/MessControl/Validation/UISettingsTeam_20261103');out.mkdir(parents=True,exist_ok=True)
base='/Game/Gameplay/UI/V2'
theme=u.load_asset(base+'/Theme/DA_UITheme_V2');assert theme
roles={str(k):v for k,v in theme.get_editor_property('widget_classes').items()}
created=[]
for role,name,native in [('UnifiedSettings','WBP_UISettings','MCUnifiedSettingsWidget'),('UnifiedTeam','WBP_UITeam','MCUnifiedTeamWidget')]:
    path=base+'/'+name
    existed=u.EditorAssetLibrary.does_asset_exist(path)
    cls=u.load_class(None,'/Script/MessControl.'+native);assert cls
    bp=u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(path,cls);assert bp
    generated=u.load_class(None,path+'.'+name+'_C');assert generated
    roles[role]=generated
    created.append({'asset':path,'new':not existed,'native':native})
theme.set_editor_property('widget_classes',roles)
assert u.EditorAssetLibrary.save_loaded_asset(theme,only_if_is_dirty=False)
u.MCGameplayUIFactory.invalidate_new_theme()
table=u.load_asset('/Game/Gameplay/UI/Data/DT_UI_Localization');assert table
rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table));keys={r['Name'] for r in rows};added=[]
for row in json.loads((root/'Tools/Unreal/UI/ui_v2_settings_texts.json').read_text(encoding='utf-8')):
    if row['Name'] not in keys:rows.append(row);added.append(row['Name'])
if added:
    assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows,ensure_ascii=False))
    assert u.EditorAssetLibrary.save_loaded_asset(table,only_if_is_dirty=False)
report={'components':created,'localizationKeysAdded':added,'root':'Preserved; append new branches with native UMG MCP',
        'resources':'Reflected New theme component classes reference the existing universal font; shapes/mic require no texture or material',
        'build':'MessControlEditor Win64 Development succeeded'}
(out/'Authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
