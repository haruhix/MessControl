"""Create missing editable Catch/HUD extras components; preserve existing Designer trees."""
import json
from pathlib import Path
import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
base='/Game/Gameplay/UI/V2'
lib=u.EditorAssetLibrary
theme=u.load_asset(base+'/Theme/DA_UITheme_V2');assert theme
roles={str(k):v for k,v in theme.get_editor_property('widget_classes').items()}
specs=[('UnifiedCatch','WBP_UICatch','MCUnifiedCatchWidget'),('UnifiedHUDExtras','WBP_UIHUDExtras','MCUnifiedHUDExtrasWidget'),('UnifiedLoading','WBP_UILoading','MCUnifiedLoadingWidget'),('UnifiedBoss','WBP_UIBoss','MCUnifiedBossWidget'),('UnifiedAlert','WBP_UIAlert','MCUnifiedAlertWidget')]
classes={name:u.load_class(None,'/Script/MessControl.'+name) for _,_,name in specs}
assert all(classes.values()),'Use the current native build'
for role,name,native in specs:
    expected=base+'/'+name+'.'+name+'_C'
    assert str(roles.get(role,'')) in ('','None','nullptr') or expected in str(roles[role]),('Preserve an unrelated role',role)
report=[]
for role,name,native in specs:
    path=base+'/'+name;existed=lib.does_asset_exist(path)
    bp=u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(path,classes[native]);assert bp
    cls=u.load_class(None,path+'.'+name+'_C');assert cls
    defaults=u.get_default_object(cls)
    if role=='UnifiedCatch':
        arts={u.MCHubItemRarity.COMMON:'Fish',u.MCHubItemRarity.RARE:'Octopus',u.MCHubItemRarity.EPIC:'FishPair',u.MCHubItemRarity.LEGENDARY:'Chest'}
        icons={};materials={}
        for rarity,glyph in arts.items():
            icons[rarity]=u.load_asset(base+'/Art/Catch/T_UI_PSD_Catch_'+glyph)
            materials[rarity]=u.load_asset(base+'/Art/Catch/MI_UI_PSD_Catch_'+glyph)
        assert all(icons.values()) and all(materials.values())
        current_icons=defaults.get_editor_property('catch_icons')
        current_materials=defaults.get_editor_property('catch_icon_materials')
        # Seed only missing variants; authored icon overrides remain authoritative.
        for key,value in icons.items():
            if key not in current_icons or not str(current_icons[key]) or str(current_icons[key]) in ('None','nullptr'):current_icons[key]=value
        for key,value in materials.items():
            if key not in current_materials or not str(current_materials[key]) or str(current_materials[key]) in ('None','nullptr'):current_materials[key]=value
        defaults.set_editor_property('catch_icons',current_icons)
        defaults.set_editor_property('catch_icon_materials',current_materials)
        if not existed:defaults.set_editor_property('bAllowAnalyticIconFallback',False)
        u.BlueprintEditorLibrary.compile_blueprint(bp)
        assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
    roles[role]=cls
    report.append({'asset':path,'new':not existed,'role':role,'native':native})
roles['Loading']=roles['UnifiedLoading']
theme.set_editor_property('widget_classes',roles)
assert lib.save_loaded_asset(theme,only_if_is_dirty=False)
u.MCGameplayUIFactory.invalidate_new_theme()
out=Path('E:/CodexWork/MessControl/Validation/UICatch_20261104');out.mkdir(parents=True,exist_ok=True)
(out/'ViewAuthorReport.json').write_text(json.dumps({'components':report,'theme':theme.get_path_name(),'root':'Append only through unified_catch_umg_recipe.json','runtimeState':'Existing source owners remain; no receipts, rewards or gameplay mutations authored.'},ensure_ascii=False,indent=2),encoding='utf-8')
print('CATCH_EXTRAS_COMPONENTS_AUTHORED',len(report))
