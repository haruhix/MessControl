import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
bp=u.load_asset('/Game/Gameplay/UI/V2/WBP_UIShop')
cls=u.load_class(None,'/Game/Gameplay/UI/V2/WBP_UIShop.WBP_UIShop_C');defaults=u.get_default_object(cls)
sizes={'ShopV2SelectedDescription':18,'ShopV2SelectedName':26,'ShopV2Balance':18,'ShopV2Hint':20}
styles=list(defaults.get_editor_property('style_bindings'))
for binding in styles:
    name=str(binding.get_editor_property('WidgetName'))
    if name in sizes:binding.set_editor_property('FontSize',sizes[name])
defaults.set_editor_property('style_bindings',styles)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
print('SAVED_SHOP_FONT_BINDINGS_UPDATED')
