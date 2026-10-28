"""Owned Hub shop check wrapping the proven generic-stock solo runner.

Prepare current Editor/saved tables and open saved Hub first; root runs this.
No map loading, network, microphone, package/source edits or persistent profiles.
The base runner opens the real shop with E, buys one item through the normal
controller RPC, verifies its authored price and next-run queue, and reopens it.
The disclosed wallet is 500 transient coins. Added checks prohibit retired IDs.
Cancel: builtins.mc_batch1_hub_shop.begin_finish('cancelled explicitly').
"""
import builtins
import hashlib
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path(globals().get('MC_BATCH1_SHOP_OUTPUT',
    'E:/CodexWork/MessControl/PassivesBatch1_20261028/HubShopA')).resolve()
assert ROOT.as_posix().lower() == 'e:/devgame/messcontrol'
assert OUT.drive.upper() == 'E:'
assert not OUT.exists(), 'Use a fresh MC_BATCH1_SHOP_OUTPUT; the owned Hub base creates it'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved assets'
KEY = 'mc_batch1_hub_shop'
previous = getattr(builtins, KEY, None)
assert previous is None or getattr(previous, 'done', True), 'Batch1 Hub probe already active'
IDS = {'B1_HeavyHit', 'B1_LightFoot', 'B1_DeepBreath', 'B1_BigPortion',
       'B1_ConfidentThrow', 'B1_FreshDelivery', 'B1_GreedyHands',
       'B1_WideBristles', 'B1_UnderPressure'}
RECIPE = ROOT/'Tools/Unreal/check_roguelike_shop_solo.py'
source = RECIPE.read_text(encoding='utf8')
marker = '\nprobe=ShopSolo();builtins.mc_roguelike_shop_solo=probe\n'
assert source.count(marker) == 1, 'Base recipe changed: re-review its start boundary before executing'
# Load class/helpers only. Suppress the original module's final auto-start.
namespace = {'__name__': 'batch1_generic_shop_support', '__file__': str(RECIPE),
             'MC_ROGUELIKE_SHOP_OUTPUT': str(OUT)}
exec(compile(source.split(marker, 1)[0], str(RECIPE), 'exec'), namespace)
ShopSolo = namespace['ShopSolo']


class Batch1ShopSolo(ShopSolo):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__,
            base_recipe_sha256=hashlib.sha256(RECIPE.read_bytes()).hexdigest(),
            wrapper_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())

    def stage(self, name, clip=None):
        if name == 'at_shop':
            entries = list(self.economy.get_shop_catalog())
            ids = [str(entry.get_editor_property('Definition').get_editor_property('PerkID'))
                   for entry in entries]
            self.report['batch1_shop_perk_ids'] = ids
            self.check('Every stocked gameplay definition is a current B1 passive',
                bool(ids) and len(ids) == len(set(ids)) and set(ids).issubset(IDS))
            self.check('Every stocked reward has passive kind', all(
                entry.get_editor_property('PerkKind') == u.MCPerkKind.PASSIVE for entry in entries))
            self.check('Authored current shop prices are positive and affordable in disclosed wallet',
                all(0 < int(entry.get_editor_property('Definition').get_editor_property('Price')) <= 500
                    for entry in entries))
            self.check('Owned diagnostic profile begins with no prepared purchases',
                not list(self.economy.get_editor_property('PendingPurchases')))
        super().stage(name, clip)


probe = Batch1ShopSolo()
setattr(builtins, KEY, probe)
setattr(builtins, 'mc_roguelike_shop_solo', probe)
try:
    probe.start()
except Exception:
    probe.begin_finish(namespace['traceback'].format_exc())
    raise
