"""One actual transient-profile pond catch in an EXISTING ready standalone Hub PIE.

Launch through run_owned_ui_recipe.py; the recipe registers a bounded Slate callback.
QA teleports only the existing pawn to traced floor inside the authored fishing zone.
Normal handle/primary input equips/casts; native delays, RNG, bite, automatic reel and
wallet credit remain untouched. No receipt RPC/PresentCatch, fixture, actor spawn,
rule/profile mutation, Apply, PIE start/stop or hardware-input claim. Cosmetic
New/Current/New uses the factory with save=False; physical F3 is tested separately.
Outputs: E:/CodexWork/MessControl/Validation/UICatch_20261104/Solo. Cancel only this
probe via builtins.mc_unified_catch_solo.cancel(). On error during an active line,
release input and leave real native retrieval running; record original pose for QA.
"""
import builtins
import hashlib
import json
import math
import time
import traceback
from pathlib import Path
import unreal as u

OUT = Path(globals().get('MC_UNIFIED_CATCH_OUTPUT','E:/CodexWork/MessControl/Validation/UICatch_20261104/Solo'))
assert OUT.resolve().drive.upper()=='E:'
CAPTURE_PNG = bool(globals().get('MC_UNIFIED_CATCH_CAPTURE_PNG',True))
OUT.mkdir(parents=True, exist_ok=True)
KEY = 'mc_unified_catch_solo'

def path(obj):
    return obj.get_path_name() if obj is not None else None

def vec(value):
    return [float(value.x), float(value.y), float(value.z)]

def rotation(value):
    return [float(value.pitch), float(value.yaw), float(value.roll)]

def tree(owner):
    for name in ('WidgetTree_0', 'WidgetTree'):
        candidate = u.find_object(owner, name)
        if isinstance(candidate, u.WidgetTree) and candidate.get_outer() == owner:
            return candidate
    matches = [candidate for candidate in u.ObjectIterator(u.WidgetTree) if candidate.get_outer() == owner]
    assert len(matches) == 1, ('Owned tree missing/ambiguous', path(owner))
    return matches[0]

def widget(owner, name):
    owned = tree(owner)
    result = u.find_object(owned, name)
    assert isinstance(result, u.Widget) and result.get_outer() == owned, (path(owner), name)
    return result

class CatchSolo:
    def __init__(self):
        self.callback = None
        self.world = self.pc = self.hero = self.rod = self.source = self.root = self.view = self.hub_view = None
        self.identity = self.zone = self.pond = self.movement = None
        self.started = time.monotonic()
        self.cast_at = self.accepted_at = None
        self.phase = 'preflight'
        self.last_sample = self.last_capture = -1.
        self.last_age = -1.
        self.capture_interval = .08
        self.changed_current = self.changed_new = self.endpoint_seen = self.teleported = False
        self.report = {'passed': False, 'scope': __doc__, 'phase': self.phase,
            'samples': [], 'frames': [], 'stages': [], 'cleanup': {}, 'castCount': 0,
            'qaTeleport': True, 'profileWrite': False, 'hardwareInput': False,
            'cosmeticSwitchOnly': True, 'maximumCastWallSeconds': 60, 'capturePNG': CAPTURE_PNG,
            'sourceScriptSHA256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}

    def owned(self, cls, viewport=False):
        found = [obj for obj in u.ObjectIterator(cls) if obj.get_world() == self.world
                 and obj.get_owning_player() == self.pc and (not viewport or obj.is_in_viewport())]
        assert len(found) == 1, (str(cls), len(found))
        return found[0]

    def wallet(self):
        return int(self.identity.get_editor_property('fish_caught'))

    def rod_state(self):
        return {name: bool(self.rod.get_editor_property(name)) for name in
                ('bEquipped', 'bFishingPose', 'bReeling', 'bLineOut', 'bFishHooked')}

    def write(self):
        self.report.update(phase=self.phase, wallSeconds=time.monotonic() - self.started)
        (OUT / 'SoloReport.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding='utf-8')

    def stage(self, name):
        self.phase = name
        self.report['stages'].append({'name': name, 'wall': time.monotonic() - self.started,
                                     'game': u.GameplayStatics.get_time_seconds(self.world)})
        self.write()

    def floor_pose(self):
        component = self.zone.get_editor_property('Zone')
        extent = component.get_unscaled_box_extent()
        transform = component.get_world_transform()
        pond_center, pond_extent = self.pond.get_actor_bounds(False)
        capsule = self.hero.get_component_by_class(u.CapsuleComponent)
        assert capsule is not None
        half_height = capsule.get_scaled_capsule_half_height()
        # Centre first, then a bounded interior grid. No cast or rule bypass occurs here.
        candidates = [(0., 0.)] + [(x, y) for x in (-.35, 0., .35) for y in (-.35, 0., .35) if x or y]
        traces = []
        for x, y in candidates:
            probe = u.MathLibrary.transform_location(transform, u.Vector(extent.x * x, extent.y * y, 0))
            hit = u.SystemLibrary.line_trace_single(self.world, probe + u.Vector(0, 0, 450),
                probe - u.Vector(0, 0, 650), u.TraceTypeQuery.ECC_VISIBILITY, False,
                [self.hero], u.DrawDebugTrace.NONE, True)
            data = u.get_default_object(u.GameplayStatics).call_method('BreakHitResult', args=(hit,)) if hit else None
            if not data or not data[0]:
                traces.append({'probe': vec(probe), 'blocking': False})
                continue
            impact, normal = data[5], data[7]
            location = impact + u.Vector(0, 0, half_height + 2)
            local = u.MathLibrary.inverse_transform_location(transform, location)
            inside = abs(local.x) <= extent.x and abs(local.y) <= extent.y and abs(local.z) <= extent.z
            dry = impact.z > pond_center.z + pond_extent.z - 12
            traces.append({'probe': vec(probe), 'impact': vec(impact), 'normal': vec(normal),
                           'insideZone': inside, 'aboveWater': dry, 'hitActor': path(data[9])})
            if inside and dry and normal.z >= .7:
                direction = pond_center - location
                assert direction.x * direction.x + direction.y * direction.y > 1
                yaw = math.degrees(math.atan2(direction.y, direction.x))
                self.report['poseDerivation'] = {'zone': path(self.zone), 'zoneLocation': vec(self.zone.get_actor_location()),
                    'zoneExtent': vec(extent), 'pond': path(self.pond), 'pondCenter': vec(pond_center),
                    'pondExtent': vec(pond_extent), 'traces': traces, 'pawnLocation': vec(location), 'castYaw': yaw,
                    'source': 'MCHubFishingZone.ContainsPawn + MCFishingRodComponent.FindPondCast; no hardcoded bank position'}
                return location, u.Rotator(pitch=0, yaw=yaw, roll=0)
        self.report['poseDerivation'] = {'traces': traces, 'reliablePose': False}
        raise AssertionError('No reliable dry walkable floor inside the saved fishing zone; no cast attempted')

    def start(self):
        assert '-mchubtransientprofile' in u.SystemLibrary.get_command_line().lower()
        worlds = u.EditorLevelLibrary.get_pie_worlds(True)
        assert len(worlds) == 1 and u.SystemLibrary.is_standalone(worlds[0])
        self.world = worlds[0]
        assert u.GameplayStatics.get_current_level_name(self.world, True) == 'Hub'
        assert u.MCGameplayUIFactory.get_presentation_version() == u.MCUIVersion.NEW
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        self.hero = u.GameplayStatics.get_player_character(self.world, 0)
        assert self.pc and self.hero and self.pc.is_world_presentation_ready() and not self.pc.is_move_input_ignored()
        hub = u.GameplayStatics.get_game_state(self.world)
        assert isinstance(hub, u.MCHubGameState) and hub.get_editor_property('phase') == u.MCHubPhase.GATHERING
        assert hub.get_participant_count() == 1
        self.identity = u.GameplayStatics.get_player_state(self.world, 0)
        self.rod = self.hero.get_component_by_class(u.MCFishingRodComponent)
        self.movement = self.hero.get_movement_component()
        assert self.identity and self.rod and self.movement
        self.source = self.owned(u.MCFishingRewardWidget, True)
        self.root = self.owned(u.MCUnifiedUIWidget, True)
        self.view = self.owned(u.MCUnifiedCatchWidget)
        self.hub_view = self.owned(u.MCUnifiedHubWidget)
        assert widget(self.root, 'InterfaceScreens').get_active_widget_index() == 2
        assert not self.source.is_animating() and not any(self.rod_state().values()), 'Require an idle, stowed rod'
        for name in ('get_animation_age', 'get_counter_icon_target', 'get_counter_card_target'):
            assert callable(getattr(self.source, name, None)), ('Current approved Editor API missing', name)
        zones = [zone for zone in u.GameplayStatics.get_all_actors_of_class(self.world, u.MCHubFishingZone)
                 if zone.get_editor_property('bEnabled')]
        ponds = [pond for pond in u.GameplayStatics.get_all_actors_of_class(self.world, u.PhysicsVolume)
                 if 'FishingPond' in [str(tag) for tag in pond.tags]]
        assert len(zones) == len(ponds) == 1, 'Need unique saved fishing zone and tagged pond'
        self.zone, self.pond = zones[0], ponds[0]
        self.initial_location = self.hero.get_actor_location()
        self.initial_rotation = self.hero.get_actor_rotation()
        self.initial_control = self.pc.get_control_rotation()
        self.initial_wallet = self.wallet()
        self.source_path = path(self.source)
        self.report['initial'] = {'world': path(self.world), 'pawn': path(self.hero), 'controller': path(self.pc),
            'source': self.source_path, 'newCatch': path(self.view), 'wallet': self.initial_wallet,
            'rod': self.rod_state(), 'pawnLocation': vec(self.initial_location),
            'pawnRotation': rotation(self.initial_rotation), 'controlRotation': rotation(self.initial_control)}
        self.report['restoreRecipeInputs'] = {'pawn': path(self.hero), 'location': vec(self.initial_location),
            'rotation': rotation(self.initial_rotation), 'controlRotation': rotation(self.initial_control),
            'stowOnlyAfterLineFinishes': True, 'wallet': 'Keep actual earned transient-profile reward; never write/revert wallet'}
        for name in ('CatchCardBounds', 'CatchTitle', 'CatchAmount', 'CatchRarity', 'CatchToken0', 'CatchToken1',
                     'CatchToken2', 'CatchArrivalRing'):
            widget(self.view, name)
        location, aim = self.floor_pose()
        self.write()
        self.hero.set_primary_input_held(False)
        self.hero.set_handle_input_held(False)
        self.movement.stop_movement_immediately()
        self.hero.set_actor_location(location, False, True)
        self.hero.set_actor_rotation(aim, True)
        self.pc.set_control_rotation(aim)
        self.teleported = True
        assert (self.hero.get_actor_location() - location).length() < 5 and self.zone.contains_pawn(self.hero)
        # The actual handle/E route reaches FishingRod.TryInteract after normal interaction gates.
        self.hero.set_handle_input_held(True)
        self.hero.set_handle_input_held(False)
        self.settled_at = time.monotonic()
        self.stage('settle_native_rod')
        self.callback = u.register_slate_post_tick_callback(self.tick)
        u.log('MC_UNIFIED_CATCH_SOLO_STARTED')

    def sample(self, age):
        now = time.monotonic()
        data = {'wall': now - self.started, 'game': u.GameplayStatics.get_time_seconds(self.world),
            'age': age, 'wallet': self.wallet(), 'version': str(u.MCGameplayUIFactory.get_presentation_version()),
            'source': path(self.source), 'sourceAnimating': self.source.is_animating(), 'rod': self.rod_state(),
            'counterIcon': path(self.source.get_counter_icon_target()), 'counterCard': path(self.source.get_counter_card_target())}
        self.report['samples'].append(data)
        self.last_sample = now
        return data

    def check_new_endpoint(self):
        icon, card = widget(self.hub_view, 'HubV2FishIcon'), widget(self.hub_view, 'HubV2WalletCard')
        assert self.source.get_counter_icon_target() == icon and self.source.get_counter_card_target() == card
        # Labels update on the ordinary UI cadence; accept only the actual wallet
        # value, without comparing two presenters from different tick phases.
        counter_copy = str(widget(self.hub_view, 'HubV2FishCount').get_text())
        digits = ''.join(character for character in counter_copy if character.isdecimal())
        if digits and int(digits) == self.wallet():
            self.report['counterSeen'] = {'text': counter_copy, 'wallet': self.wallet()}
        geometry = json.loads(u.MCGameplayUIEditorLibrary.inspect_pie_fishing_catch(self.source,self.view))
        assert geometry['valid'] and geometry['unifiedPrepared'],geometry
        self.report['lastNativeGeometry']=geometry
        if geometry['ringVisible'] and geometry['ringOpacity'] > .001:
            assert geometry['endpointDistance'] <= 4,geometry
            self.endpoint_seen = True
            self.report['endpoint'] = geometry

    def capture(self, age):
        if not CAPTURE_PNG:return
        now = time.monotonic()
        if len(self.report['frames']) >= 20 or now - self.last_capture < self.capture_interval:
            return
        # Avoid slow PNG work in the arrival interval until the endpoint was observed.
        if 1.25 < age < 2.1 and not self.endpoint_seen:
            return
        destination = OUT / ('Catch_%02d.png' % len(self.report['frames']))
        before = time.monotonic()
        assert u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(destination))
        cost = time.monotonic() - before
        self.report['frames'].append({'file': str(destination), 'age': age, 'wall': before - self.started,
                                     'captureWallSeconds': cost, 'version': str(u.MCGameplayUIFactory.get_presentation_version())})
        self.last_capture = time.monotonic()
        self.capture_interval = max(.08, min(.3, cost * 2))

    def tick(self, delta):
        try:
            assert self.world in u.EditorLevelLibrary.get_pie_worlds(True), 'Owned PIE ended/changed'
            assert self.pc.is_world_presentation_ready() and self.zone.contains_pawn(self.hero), 'Lost ready pier state'
            now = time.monotonic()
            if self.cast_at is not None and now - self.cast_at > 60:
                raise AssertionError('One real cast exceeded 60 seconds; no retry/rule override')
            if self.phase == 'settle_native_rod':
                if now - self.settled_at < .65:
                    return
                if not self.rod.can_cast_line():
                    assert now - self.settled_at < 3, 'Native CanCastLine rejected derived pose; no forcecast'
                    return
                self.cast_at = now
                self.hero.set_primary_input_held(True)
                self.hero.set_primary_input_held(False)
                assert self.rod.get_editor_property('bLineOut'), 'Actual primary input did not start a cast'
                self.report['castCount'] += 1
                self.stage('await_native_bite_reel_reward')
                return
            flags = self.rod_state()
            if flags['bFishHooked'] and 'hookedCatch' not in self.report:
                reward = self.rod.get_editor_property('HookedCatch')
                self.report['hookedCatch'] = {'rarity': str(reward.rarity), 'fishCoins': int(reward.fish_coins),
                    'reeling': flags['bReeling'], 'actualBiteDelay': float(self.rod.get_editor_property('LastBiteDelay'))}
                assert flags['bReeling'], 'Native bite should begin automatic retrieval'
            if now - self.last_sample >= .1:
                self.sample(self.source.get_animation_age())
            if self.accepted_at is None:
                if self.wallet() <= self.initial_wallet:
                    return
                assert 'hookedCatch' in self.report and self.source.is_animating(), 'Wallet changed without observed native catch/feedback'
                self.accepted_at = now
                self.report['acceptedReward'] = {'walletBefore': self.initial_wallet, 'walletAfter': self.wallet(),
                    'delta': self.wallet() - self.initial_wallet, 'age': self.source.get_animation_age()}
                self.stage('observe_real_receipt')
            assert path(self.owned(u.MCFishingRewardWidget, True)) == self.source_path, 'Receipt owner replaced'
            age = float(self.source.get_animation_age())
            assert age + .001 >= self.last_age, ('Animation replayed/reset', self.last_age, age)
            self.last_age = age
            version = u.MCGameplayUIFactory.get_presentation_version()
            if version == u.MCUIVersion.NEW and self.source.is_animating() and age > .02:
                glyph = widget(widget(self.view, 'CatchToken0'), 'CatchGlyphImage')
                resource = glyph.get_editor_property('brush').get_editor_property('resource_object')
                assert isinstance(resource, u.MaterialInstanceConstant), 'Actual PSD glyph static MI missing'
                self.report.setdefault('iconResource', path(resource))
                self.check_new_endpoint()
            if age >= 1.10 and not self.changed_current and self.source.is_animating():
                before = self.source.get_animation_age()
                assert u.MCGameplayUIFactory.set_presentation_version(u.MCUIVersion.CURRENT, False)
                assert self.source.get_animation_age() == before and path(self.source) == self.source_path
                self.changed_current = True
                self.report['switchToCurrentAge'] = before
            elif age >= 1.35 and self.changed_current and not self.changed_new and self.source.is_animating():
                before = self.source.get_animation_age()
                assert u.MCGameplayUIFactory.set_presentation_version(u.MCUIVersion.NEW, False)
                assert self.source.get_animation_age() == before and path(self.source) == self.source_path
                self.changed_new = True
                self.report['switchToNewAge'] = before
            self.capture(age)
            if not self.source.is_animating():
                assert self.changed_current and self.changed_new and self.endpoint_seen
                assert self.report.get('iconResource') and self.report.get('counterSeen') and (not CAPTURE_PNG or self.report['frames'])
                assert self.wallet() > self.initial_wallet and not flags['bLineOut'] and not flags['bFishHooked']
                self.report['passed'] = True
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def cancel(self):
        self.finish('Cancelled by the owning QA author')

    def finish(self, error=None):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        if error:
            self.report['passed'], self.report['error'] = False, error
        try:
            if self.hero is not None and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
                self.hero.set_primary_input_held(False)
                self.hero.set_handle_input_held(False)
                if u.MCGameplayUIFactory.get_presentation_version() != u.MCUIVersion.NEW:
                    u.MCGameplayUIFactory.set_presentation_version(u.MCUIVersion.NEW, False)
                flags = self.rod_state()
                if self.teleported and not flags['bLineOut'] and not flags['bFishHooked']:
                    if flags['bEquipped']:
                        self.rod.toggle_equipped()  # Existing native stow path; no caught fish remains.
                    self.movement.stop_movement_immediately()
                    self.hero.set_actor_location(self.initial_location, False, True)
                    self.hero.set_actor_rotation(self.initial_rotation, True)
                    self.pc.set_control_rotation(self.initial_control)
                    self.report['cleanup']['pawnRestored'] = (self.hero.get_actor_location() - self.initial_location).length() < 5
                else:
                    self.report['cleanup']['pawnRestored'] = False
                    self.report['cleanup']['note'] = 'Active real line/retrieval preserved; original pose is in restoreRecipeInputs.'
                self.report['cleanup'].update(heldInputReleased=True, callbackUnregistered=True,
                    finalWallet=self.wallet(), finalRod=self.rod_state(), profileWrite=False)
        except Exception:
            self.report['passed'] = False
            self.report['cleanup']['error'] = traceback.format_exc()
        self.phase = 'done'
        self.write()
        u.log('MC_UNIFIED_CATCH_SOLO_DONE ' + str(self.report['passed']))
        if getattr(builtins, KEY, None) is self:
            delattr(builtins, KEY)
        for name in ('world', 'pc', 'hero', 'rod', 'source', 'root', 'view', 'hub_view', 'identity', 'zone', 'pond', 'movement'):
            setattr(self, name, None)

assert not hasattr(builtins, KEY), 'Another owned catch probe is already active'
probe = CatchSolo()
setattr(builtins, KEY, probe)
try:
    probe.start()
except Exception:
    probe.finish(traceback.format_exc())
