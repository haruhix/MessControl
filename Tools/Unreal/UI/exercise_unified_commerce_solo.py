"""Bounded owned-PIE commerce action check; requires the temporary Hub profile flag.
Actions use real source acceptance and the new widget's actual bound delegates.
No source assets or persistent profile files are edited.
"""
import json
from pathlib import Path
import unreal as u
out=Path('E:/CodexWork/MessControl/Validation/UICommerce_20261104')
assert '-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower()
worlds=u.EditorLevelLibrary.get_pie_worlds(True);assert len(worlds)==1
world=worlds[0];assert u.GameplayStatics.get_current_level_name(world,True)=='Hub'
pc=u.GameplayStatics.get_player_controller(world,0);hero=pc.get_controlled_pawn()
econ=pc.get_editor_property('player_state').get_editor_property('economy')
def obj(cls,viewport=False):
    return next(x for x in u.ObjectIterator(cls) if x.get_world()==world and x.get_owning_player()==pc and (not viewport or x.is_in_viewport()))
def widget(owner,name):
    tree=next(t for t in u.ObjectIterator(u.WidgetTree) if t.get_outer()==owner)
    return next(w for w in u.ObjectIterator(u.Widget) if w.get_outer()==tree and w.get_name()==name)
def activate(owner,name):
    shape=widget(owner,name);assert shape.get_is_enabled(),name
    surface=widget(shape,'UniversalSurface');surface.on_clicked.broadcast()
def economy():
    return json.loads(u.ToolsetLibrary.get_object_properties(econ,['Coins','OwnedCosmetics','PendingPurchases','ShopRotationId','ShopRotationSeed','ShopStockIDs','EquippedLoadout']))
action=globals().get('ACTION','read');result={'action':action,'passed':False}
if action=='restore_economy':
    original=json.loads((out/'OriginalHubState.json').read_text(encoding='utf-8'))['economy']
    current=economy()
    for name in ('ShopRotationId','ShopRotationSeed'):original[name]=current[name]
    econ.set_editor_property('ShopStockIDs',[u.Name(x) for x in original['ShopStockIDs']])
    assert u.ToolsetLibrary.set_object_properties(econ,json.dumps(original))
    result['restoredEconomy']=economy()
elif action=='open_wardrobe':
    assert pc.try_open_wardrobe();assert pc.is_wardrobe_open()
elif action=='select_cowboy':
    view=obj(u.MCUnifiedWardrobeWidget)
    viewtree=next(t for t in u.ObjectIterator(u.WidgetTree) if t.get_outer()==view)
    prices=[w for w in u.ObjectIterator(u.TextBlock) if w.get_outer()==viewtree and w.get_name().startswith('WardrobeV2Product') and w.get_name().endswith('Price') and str(w.get_text()).replace(' ','').startswith('80')]
    assert len(prices)==1
    activate(view,prices[0].get_name()[:-5])
    source=obj(u.MCHubWardrobeWidget,True)
    draft=[str(s.get_editor_property('ItemId')) for s in source.get_draft_loadout().get_editor_property('Slots')]
    assert 'Hat_Head_CowboyHat' in draft
    assert not econ.owns_cosmetic('Hat_Head_CowboyHat')
    result.update(draft=draft,paid=False)
elif action=='back_wardrobe':
    activate(obj(u.MCUnifiedWardrobeWidget),'WardrobeV2Back');assert not pc.is_wardrobe_open()
elif action=='open_shop':
    assert pc.try_open_hub_shop();assert pc.is_hub_shop_open()
elif action=='prepare_purchase':
    assert u.ToolsetLibrary.set_object_properties(econ,json.dumps({'Coins':800}))
    source=obj(u.MCHubShopWidget,True);source.configure(econ);source.select_catalog_item('Active_Consumable_Grenade')
    result['fixture']='800 temporary coins; original state retained for cleanup'
elif action=='purchase':
    before=economy();activate(obj(u.MCUnifiedShopWidget),'ShopV2Buy');after=economy()
    assert before['Coins']-after['Coins']==30,(before,after)
    assert len(after['PendingPurchases'])>len(before['PendingPurchases'])
    result.update(before=before,after=after,acceptedBy='existing normal controller/economy path in real shop volume')
elif action=='back_shop':
    activate(obj(u.MCUnifiedShopWidget),'ShopV2Back');assert not pc.is_hub_shop_open()
elif action=='restore_user':
    original=json.loads((out/'OriginalHubState.json').read_text(encoding='utf-8'))
    data=dict(original['economy']);current=economy()
    for name in ('ShopRotationId','ShopRotationSeed'):data[name]=current[name]
    econ.set_editor_property('ShopStockIDs',[u.Name(x) for x in data['ShopStockIDs']])
    assert u.ToolsetLibrary.set_object_properties(econ,json.dumps(data))
    hero.character_movement.stop_movement_immediately()
    loc=original['location'];rot=original['rotation']
    hero.set_actor_location_and_rotation(u.Vector(**loc),hero.get_actor_rotation(),False,True)
    pc.set_control_rotation(u.Rotator(**rot))
    play=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
    assert u.ToolsetLibrary.set_object_properties(play,json.dumps(original['play']))
    perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));perf.set_editor_property('bThrottleCPUWhenNotForeground',original['throttle'])
    current=economy()
    for name in ('Coins','OwnedCosmetics','PendingPurchases','ShopStockIDs','EquippedLoadout'):assert current[name]==original['economy'][name],name
    result.update(economy=current,positionRestored=True,playRestored=True,throttleRestored=True,rotationNote='Controller rotation restored; native non-reflected camera orbit resets with Editor restart.',technicalRotation='New session shop rotation identity retained while original stock IDs restored.')
else:raise AssertionError(action)
result['passed']=True
(out/(action+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False))
