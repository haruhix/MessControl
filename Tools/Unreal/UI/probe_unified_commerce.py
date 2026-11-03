"""Read-only runtime commerce probe and actual owned game-window screenshot."""
import json
from pathlib import Path
import unreal as u
out=Path('E:/CodexWork/MessControl/Validation/UICommerce_20261104')
worlds=u.EditorLevelLibrary.get_pie_worlds(True);assert len(worlds)==1
world=worlds[0];pc=u.GameplayStatics.get_player_controller(world,0)
root=next(x for x in u.ObjectIterator(u.MCUnifiedUIWidget) if x.get_world()==world and x.is_in_viewport())
views=[x for cls in (u.MCUnifiedShopWidget,u.MCUnifiedWardrobeWidget,u.MCHubShopWidget,u.MCHubWardrobeWidget) for x in u.ObjectIterator(cls) if x.get_world()==world and x.get_owning_player()==pc]
def widgets(owner):
    tree=next((t for t in u.ObjectIterator(u.WidgetTree) if t.get_outer()==owner),None)
    return [w for w in u.ObjectIterator(u.Widget) if tree and w.get_outer()==tree]
report={'version':str(u.MCGameplayUIFactory.get_presentation_version()),'rootVisible':root.is_visible(),'objects':{},'openWardrobe':pc.is_wardrobe_open(),'openShop':pc.is_hub_shop_open()}
for owner in [root]+views:
    d={'path':owner.get_path_name(),'viewport':owner.is_in_viewport(),'visible':owner.is_visible(),'controls':{}}
    for w in widgets(owner):
        name=w.get_name()
        if name=='InterfaceScreens' and isinstance(w,u.WidgetSwitcher):report['screen']=w.get_active_widget_index()
        if isinstance(w,u.TextBlock) and name.startswith(('ShopV2','WardrobeV2','ShopSelected','ShopTransaction','ShopBuyLabel','WardrobeTransaction')):d['controls'][name]={'text':str(w.get_text()),'visible':w.is_visible()}
        if isinstance(w,u.MCUniversalUIWidget) and name.startswith(('ShopV2','WardrobeV2')):d['controls'][name]={'label':str(w.get_editor_property('label')),'enabled':w.get_is_enabled(),'visible':w.is_visible()}
        if isinstance(w,u.UniformGridPanel):d['controls'][name]={'children':len(w.get_all_children())}
    report['objects'][str(type(owner))]=d
econ=pc.get_editor_property('player_state').get_editor_property('economy')
report['economy']=json.loads(u.ToolsetLibrary.get_object_properties(econ,['Coins','OwnedCosmetics','PendingPurchases','ShopStockIDs','EquippedLoadout']))
(out/'LiveLatest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert u.MCGameplayUIEditorLibrary.capture_pie_game_window(world,str(out/'LiveLatest.png'))
print(json.dumps({'screen':report.get('screen'),'openWardrobe':report['openWardrobe'],'openShop':report['openShop'],'rootVisible':report['rootVisible']}))
