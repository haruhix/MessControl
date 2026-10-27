"""Short solo proof of current rotating shop; uses real E and purchase RPC.
Only a transient diagnostic wallet and an inspected player placement are fixtures.
The Editor must be idle on saved Hub with -MCHubOwned -MCHubTransientProfile.
"""
import builtins,importlib.util,json,time,traceback
from pathlib import Path
import unreal as u
ROOT=Path('E:/DEVGAME/MessControl');OUT=Path(globals().get('MC_ROGUELIKE_SHOP_OUTPUT','E:/CodexWork/MessControl/RoguelikeFoundation_20261027/HubShopSolo'))
spec=importlib.util.spec_from_file_location('roguelike_shop_helpers',ROOT/'Tools/Unreal/Hub/check_hub_wardrobe_shop_solo.py')
helper=importlib.util.module_from_spec(spec);helper.MCHUB_WARDROBE_SHOP_OUTPUT=str(OUT);spec.loader.exec_module(helper)
base=helper.base;require,valid,path=base.require,base.valid,base.path
class ShopSolo(helper.HubWardrobeShopSolo):
 def __init__(self):
  base.HubPierSolo.__init__(self);self.widget=None;self.shop=None;self.economy=None
  self.slate=u.get_default_object(u.load_class(None,'/Script/SlateInspectorToolset.SlateInspectorToolset'))
  self.report.update(scope=__doc__,review_fixtures=[],limitations=['Owned standalone PIE only; no network or packaged profile restart.',
   'Diagnostic wallet is 500 coins in transient PIE; it is not an earned objective reward.'])
 def write(self):
  OUT.mkdir(parents=True,exist_ok=True);(OUT/'Report.json').write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')
 def start(self):base.HubPierSolo.start(self)
 def finish(self):base.HubPierSolo.finish(self)
 def stock(self):return [str(x) for x in self.economy.get_editor_property('ShopStockIDs')]
 def tick(self,delta):
  if self.done or self.busy:return
  self.busy=True
  try:
   worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
   if self.finishing:
    if not worlds:self.finish()
    return
   require(time.monotonic()-self.started<100,'Bounded solo shop probe')
   if self.world is None:
    if not worlds:return
    require(len(worlds)==1,'One owned solo PIE');self.world=worlds[0]
   require(len(worlds)==1 and worlds[0]==self.world,'Owned PIE was not replaced')
   self.pc=u.GameplayStatics.get_player_controller(self.world,0)
   self.hero=self.pc.get_controlled_pawn() if self.pc else None
   if not isinstance(self.hero,u.MCToothCharacter) or not self.pc.is_world_presentation_ready() or self.now()<2:return
   if self.phase=='loading':
    self.ps=u.GameplayStatics.get_player_state(self.world,0);self.economy=self.ps.get_component_by_class(u.MCHubEconomyComponent)
    self.economy.set_editor_property('Coins',500);self.shop=self.station(u.MCHubShopStation);self.place_at(self.shop)
    entries=list(self.economy.get_shop_catalog());self.report['stock_before']=self.stock()
    self.check('Nonempty bounded shop assortment',0<len(entries)<=8)
    self.check('At most four passive positions',sum(e.get_editor_property('PerkKind')==u.MCPerkKind.PASSIVE for e in entries)<=4)
    self.entry=entries[0];self.item=self.entry.get_editor_property('ItemID');definition=self.entry.get_editor_property('Definition')
    self.price=int(definition.get_editor_property('Price'));self.perk_id=definition.get_editor_property('PerkID')
    self.report['purchase_fixture']=dict(item=str(self.item),price=self.price,perk=str(self.perk_id),wallet=500)
    self.stage('at_shop')
   elif self.phase=='at_shop' and time.monotonic()-self.phase_wall>.6:
    self.press_e();self.stage('opening')
   elif self.phase=='opening':
    self.widget=self.owned_widget(u.MCHubShopWidget)
    if not self.widget:
     require(time.monotonic()-self.phase_wall<4,'E opens actual shop');return
    self.capture('01_rotating_shop.png')
    self.pc.call_method('PurchaseHubItemInSlot',args=(self.item,-1,u.Name('None'),u.Guid()))
    self.stage('purchased')
   elif self.phase=='purchased' and time.monotonic()-self.phase_wall>1:
    self.check('Native purchase debits authored price once',self.economy.get_editor_property('Coins')==500-self.price)
    purchases=list(self.economy.get_editor_property('PendingPurchases'))
    self.check('Purchase prepares one next-run item',len(purchases)==1)
    perks=self.ps.get_component_by_class(u.MCPerkComponent)
    self.check('Prepared purchase does not grant a hub runtime perk',not perks.owns_perk(self.perk_id))
    self.check('Purchase does not reroll stock',self.stock()==self.report['stock_before'])
    self.capture('02_next_run_purchase.png');self.slate.call_method('PressKey',args=('Escape',));self.stage('closed')
   elif self.phase=='closed' and time.monotonic()-self.phase_wall>.5:
    self.check('Escape closes owned shop',not self.pc.is_hub_shop_open());self.press_e();self.stage('reopened')
   elif self.phase=='reopened' and time.monotonic()-self.phase_wall>.7:
    self.check('E reopens shop',self.pc.is_hub_shop_open())
    self.check('Reopen preserves exact assortment',self.stock()==self.report['stock_before'])
    self.check('Reopen does not charge again',self.economy.get_editor_property('Coins')==500-self.price)
    self.capture('03_stable_reopened_shop.png');self.begin_finish()
  except Exception:self.begin_finish(traceback.format_exc())
  finally:self.busy=False
probe=ShopSolo();builtins.mc_roguelike_shop_solo=probe
try:probe.start()
except Exception:probe.begin_finish(traceback.format_exc());raise
