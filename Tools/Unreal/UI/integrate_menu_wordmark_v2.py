"""Import the original menu wordmark and keep the native title/action widgets."""
import unreal as u
from pathlib import Path
ROOT = Path('E:/DEVGAME/MessControl')
BASE = '/Game/UI/MenuShowcase/AnatomyV2'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
lib = u.EditorAssetLibrary
path = BASE + '/UI/Textures/T_WashNSmashWordmark'
texture = u.load_asset(path)
if not texture:
    task = u.AssetImportTask()
    task.filename = str(ROOT / 'ArtSource/UI/MenuSceneV2_20261022/Wordmark/WashNSmash_Wordmark_2048.png')
    task.destination_path = BASE + '/UI/Textures'
    task.destination_name = 'T_WashNSmashWordmark'
    task.automated = True
    task.save = False
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = u.load_asset(path)
assert isinstance(texture, u.Texture2D)
texture.set_editor_property('lod_group', u.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property('mip_gen_settings', u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property('srgb', True)
texture.set_editor_property('never_stream', True)
assert lib.save_loaded_asset(texture)
asset = u.load_asset(BASE + '/UI/WBP_WashNSmashMenu_AdaptiveV2')
widgets = {}
for obj in u.ObjectIterator(u.Widget):
    outer = obj.get_outer()
    while outer:
        if outer == asset:
            widgets[obj.get_name()] = obj
            break
        outer = outer.get_outer()
title = widgets['MenuTitle']
parent = title.get_parent()
assert isinstance(parent, u.VerticalBox)
tree = title.get_outer()
image = widgets.get('MenuWordmarkV2')
if not image:
    asset.modify()
    image = u.new_object(u.Image, outer=tree, name='MenuWordmarkV2')
    size = u.new_object(u.SizeBox, outer=tree, name='MenuWordmarkSizeV2')
    size.set_content(image)
    size.set_height_override(212.)
    children = list(parent.get_all_children())
    slots = [{p: child.slot.get_editor_property(p) for p in ['padding', 'size', 'horizontal_alignment', 'vertical_alignment']} for child in children]
    for child in children:
        child.remove_from_parent()
    slot = parent.add_child_to_vertical_box(size)
    slot.set_padding(u.Margin(0, 0, 0, 20))
    for child, values in zip(children, slots):
        slot = parent.add_child_to_vertical_box(child)
        for prop, value in values.items():
            slot.set_editor_property(prop, value)
image.set_brush_from_texture(texture, False)
image.set_color_and_opacity(u.LinearColor(1, 1, 1, 1))
title.set_visibility(u.SlateVisibility.COLLAPSED)
u.BlueprintEditorLibrary.compile_blueprint(asset)
assert lib.save_loaded_asset(asset)
u.log('MENU_WORDMARK_INTEGRATED_PENDING_NATIVE_REVIEW')
