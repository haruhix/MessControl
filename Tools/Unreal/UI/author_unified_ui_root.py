"""Create the single editable PSD interface root. Existing artist trees are preserved.

The menu logo is a separate PSD layer; panels/buttons remain editable UMG.
Gameplay is currently a Designer prototype, not an enabled runtime replacement.
"""
import json
from pathlib import Path
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
path='/Game/Gameplay/UI/V2/WBP_UIRoot'
parent=u.load_class(None,'/Script/MessControl.MCUnifiedUIWidget')
assert parent,'Load the rebuilt Editor first'
logo_path='/Game/Gameplay/UI/V2/Art/T_UI_PSD_MenuLogo'
if not u.EditorAssetLibrary.does_asset_exist(logo_path):
    source=Path(u.Paths.project_dir())/'ArtSource/UI/V2/Icons/T_UI_PSD_MenuLogo.png'
    assert source.is_file(),'Export logo_1 from visua2.psd first'
    task=u.AssetImportTask();task.filename=str(source)
    task.destination_path='/Game/Gameplay/UI/V2/Art';task.destination_name='T_UI_PSD_MenuLogo'
    task.automated=True;task.save=True
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture=u.load_asset(logo_path)
    texture.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('mip_gen_settings',u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('lod_group',u.TextureGroup.TEXTUREGROUP_UI)
    assert u.EditorAssetLibrary.save_loaded_asset(texture,only_if_is_dirty=False)
bp=u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(path,parent)
assert bp
cls=u.load_class(None,path+'.WBP_UIRoot_C')
defaults=u.get_default_object(cls)
bindings={str(k):str(v) for k,v in defaults.get_editor_property('text_bindings').items()}
# Migrate the initial generated HUD draft once. Later artist layout edits stay intact.
if bindings.get('V2NextEventLabel')!='UI.V2.HUD.NextEvent':
    bindings.update({'V2NextEventLabel':'UI.V2.HUD.NextEvent','V2NextEvent':'UI.V2.Preview.EventName',
                     'V2ToolName':'UI.HUD.Tool.Pickaxe','V2PlayerName':'UI.V2.Preview.Player'})
    for index in range(4):bindings['V2SkillLabel'+str(index+1)]='UI.Skill.Empty'
    defaults.set_editor_property('text_bindings',bindings)
    sizes={'V2NextEventLabel':16,'V2NextEvent':20,'V2ToolName':20,'V2PlayerName':20,
           **{'V2SkillLabel'+str(index+1):14 for index in range(4)}}
    styles=list(defaults.get_editor_property('style_bindings'))
    for binding in styles:
        if str(binding.widget_name) in sizes:binding.font_size=sizes[str(binding.widget_name)]
    defaults.set_editor_property('style_bindings',styles)
    prefix=bp.get_path_name()+':WidgetTree.'
    for name,pos,size in [('V2NextEventLabel',(5,0),(365,24)),('V2NextEvent',(5,28),(365,56)),
                          ('V2ToolName',(4,-38),(110,30)),('V2PlayerName',(126,-38),(220,30))]:
        label=u.find_object(None,prefix+name);assert label,name
        label.slot.set_position(u.Vector2D(*pos));label.slot.set_size(u.Vector2D(*size))
        label.set_auto_wrap_text(False);label.set_text_overflow_policy(u.TextOverflowPolicy.ELLIPSIS)
    u.find_object(None,prefix+'V2ToolIcon').set_editor_property('tool_slot',1)
    u.find_object(None,prefix+'V2StaminaBar').set_editor_property('progress_fill',u.MCUIColorToken.STAMINA)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
theme=u.load_asset('/Game/Gameplay/UI/V2/Theme/DA_UITheme_V2')
assert theme
mapping={str(k):v for k,v in theme.get_editor_property('widget_classes').items()}
mapping['InterfaceRoot']=u.load_class(None,path+'.WBP_UIRoot_C')
theme.set_editor_property('widget_classes',mapping)
assert u.EditorAssetLibrary.save_loaded_asset(theme,only_if_is_dirty=False)
u.MCGameplayUIFactory.invalidate_new_theme()
out=Path('E:/CodexWork/MessControl/Validation/UIRootPSD_20261103')
out.mkdir(parents=True,exist_ok=True)
cls=mapping['InterfaceRoot']
(out/'authoring.json').write_text(json.dumps({'root':path,'native':'MCUnifiedUIWidget','screen_selector':'Gameplay UI Preview / Interface screen','runtime_screens':['Menu'],'preview_prototypes':['Gameplay'],'remaining_layouts':['Hub','Wardrobe','Boosters','Tools','Perks'],'reference':'D:/sda/visua2.psd','runtime':'one existing action/data owner; distinct V2 presentation root'},indent=2),encoding='utf-8')
print('PSD_UI_ROOT_CREATED',path)
