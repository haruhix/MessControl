"""Save exact PSD tool art and live HUD controls; preserve perk artwork in DT_Perks."""
from pathlib import Path
import json
import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
project=Path(u.Paths.project_dir()).resolve();assert project.drive.upper()=='E:'
bp=u.load_asset('/Game/Gameplay/UI/V2/WBP_UIRoot');assert bp
prefix=bp.get_path_name()+':WidgetTree.'
cls=u.load_class(None,'/Game/Gameplay/UI/V2/WBP_UIRoot.WBP_UIRoot_C');defaults=u.get_default_object(cls)
art={str(k):v for k,v in defaults.get_editor_property('tool_icon_art').items()};imported=[]
for name,key in [('Spray','Tool.Spray'),('Brush','Tool.Brush'),('Pickaxe','Tool.Pickaxe'),('Knife','Tool.Knife'),('Watergun','ToolUpgrade.Watergun'),('MeshaBrush','ToolUpgrade.MeshaBrush'),('Chainsaw','ToolUpgrade.Chainsaw')]:
    path='/Game/Gameplay/UI/V2/Art/Tools/T_UI_PSD_'+name
    if not u.EditorAssetLibrary.does_asset_exist(path):
        task=u.AssetImportTask();task.filename=str(project/('ArtSource/UI/V2/Icons/T_UI_PSD_'+name+'.png'))
        task.destination_path='/Game/Gameplay/UI/V2/Art/Tools';task.destination_name='T_UI_PSD_'+name;task.automated=True;task.save=True
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);imported.append(path)
    tex=u.load_asset(path);assert tex,path
    tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property('mip_gen_settings',u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('lod_group',u.TextureGroup.TEXTUREGROUP_UI);tex.set_editor_property('never_stream',True)
    assert u.EditorAssetLibrary.save_loaded_asset(tex,only_if_is_dirty=False)
    if key not in art:art[key]=tex
# Perk icons intentionally stay in DT_Perks.Icon. PickaxeVariant/Buffer is not enabled.
defaults.set_editor_property('tool_icon_art',art)
defaults.set_editor_property('text_bindings',{str(k):str(v) for k,v in defaults.get_editor_property('text_bindings').items()})
fonts={'V2RunSummary':14,'V2CurrentEvent':14,'V2ToolCooldown':12,'V2ActionHint':18,**{'V2SkillStatus'+str(i):12 for i in range(1,5)}}
styles=list(defaults.get_editor_property('style_bindings'));known={str(x.widget_name) for x in styles}
for name,size in fonts.items():
    if name not in known:
        binding=u.MCUIStyleBinding();binding.widget_name=name;binding.color=u.MCUIColorToken.CREAM;binding.target=u.MCUIStyleTarget.TEXT;binding.font_size=size;binding.set_editor_property('bBold',True);styles.append(binding)
defaults.set_editor_property('style_bindings',styles)
for name,points,shadow in [('ReserveBlock',[(0,0),(.66,0),(.66,.5),(1,.5),(1,1),(0,1)],(16,18)),('ToolResourceBlock',[(0,0),(.30,0),(.30,.12),(1,.12),(1,1),(0,1)],(10,12))]:
    shape=u.find_object(None,prefix+name);assert shape
    if not shape.get_editor_property('contour_points'):shape.set_editor_property('contour_points',[u.Vector2D(*p) for p in points])
    shape.set_editor_property('corner_radius',12);shape.set_editor_property('outline_width',4);shape.set_editor_property('shadow_offset',u.Vector2D(*shadow))
for i in range(1,5):
    player=u.find_object(None,prefix+'V2Player'+str(i));player.set_editor_property('shadow_offset',u.Vector2D(-12,12));player.set_editor_property('outline_width',4)
    bar=u.find_object(None,prefix+'V2PlayerHP'+str(i));bar.set_editor_property('outline_width',1.5);bar.set_editor_property('corner_radius',5);bar.set_editor_property('content_padding',u.Margin(2,2,2,2))
    card=u.find_object(None,prefix+'V2Skill'+str(i));card.set_editor_property('content_padding',u.Margin(8,8,8,8));card.slot.set_size(u.Vector2D(94,94))
    status=u.find_object(None,prefix+'V2SkillStatus'+str(i));status.slot.set_position(u.Vector2D(0,60));status.slot.set_size(u.Vector2D(78,18));status.set_editor_property('justification',u.TextJustify.CENTER)
event=u.find_object(None,prefix+'NextEventBlock');event.set_editor_property('shadow_offset',u.Vector2D(-12,12));event.set_editor_property('outline_width',4)
reserve=u.find_object(None,prefix+'ReserveBlock');reserve.slot.set_size(u.Vector2D(340,152))
# Keep all current labels outside the art silhouette rather than growing its PSD frame.
summary=u.find_object(None,prefix+'V2RunSummary');summary.slot.set_position(u.Vector2D(50,194));summary.slot.set_size(u.Vector2D(340,46))
# Right-aligned player group should shrink to the number actually present.
players=u.find_object(None,prefix+'PlayerCards');players.slot.set_auto_size(True)
key=u.find_object(None,prefix+'V2SkillKey1');row=key.get_parent().get_parent();row.slot.set_size(u.Vector2D(420,140))
for name in ('V2ToolName','V2PlayerName','V2NextEvent','V2CurrentEvent'):
    u.find_object(None,prefix+name).set_clipping(u.WidgetClipping.CLIP_TO_BOUNDS_ALWAYS)
# Contact progress is a native editable thin UMG bar; no shader/material dependency.
contact=u.find_object(None,prefix+'V2ContactProgress');contact.set_percent(0);contact.set_visibility(u.SlateVisibility.COLLAPSED)
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
cls=u.load_class(None,'/Game/Gameplay/UI/V2/WBP_UIRoot.WBP_UIRoot_C');theme=u.load_asset('/Game/Gameplay/UI/V2/Theme/DA_UITheme_V2')
roles={str(k):v for k,v in theme.get_editor_property('widget_classes').items()};roles['InterfaceRoot']=cls;theme.set_editor_property('widget_classes',roles)
assert u.EditorAssetLibrary.save_loaded_asset(theme,only_if_is_dirty=False);u.MCGameplayUIFactory.invalidate_new_theme()
out=Path('E:/CodexWork/MessControl/Validation/UIRootHUD_20261103');out.mkdir(parents=True,exist_ok=True)
report={'root':bp.get_path_name(),'imported':imported,'tool_art_keys':[str(x) for x in art],'perk_icons':'DT_Perks.Icon unchanged','source_psd':'D:/sda/visua2.psd','geometry':'editable UMG contour and shape properties','saved':True}
(out/'Authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('UNIFIED_HUD_AUTHORED',json.dumps(report,ensure_ascii=False))
