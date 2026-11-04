"""Create missing editable source-backed Reticle component; preserve existing Designer trees."""
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
specs=[('UnifiedReticle','WBP_UIReticle','MCUnifiedReticleWidget')]
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
    roles[role]=cls
    report.append({'asset':path,'new':not existed,'role':role,'native':native})
assert 'UnifiedLoading' in roles
# Native gameplay HUD remains the original reticle state owner.
theme.set_editor_property('widget_classes',roles)
assert lib.save_loaded_asset(theme,only_if_is_dirty=False)
u.MCGameplayUIFactory.invalidate_new_theme()
out=Path('E:/CodexWork/MessControl/Validation/UIOverlayWorld_20261104');out.mkdir(parents=True,exist_ok=True)
(out/'ReticleViewAuthorReport.json').write_text(json.dumps({'components':report,'theme':theme.get_path_name(),'root':'Append only through unified_reticle_umg_patch.json','runtimeState':'Existing source owners remain; no receipts, rewards or gameplay mutations authored.'},ensure_ascii=False,indent=2),encoding='utf-8')
print('RETICLE_COMPONENT_AUTHORED',len(report))
