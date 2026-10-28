"""SoloD: arrival-barrier-aware standalone L_Mouth presentation/authority check for the nine B1 passives.

Run ONLY after the current Editor binary and saved DT_Perks/DT_HubCatalog exist.
No Computer Use, OS input, source/content edits, persistent profile, network or
microphone. This recipe starts one PIE and stops only that owned session.

Disclosed fixtures: the existing native 'level' review action stops directors,
places the player and resets progression/status; passives use TryAcquirePerk.
The stage chest is deferred-spawned and initialized through the proven native
InitializeTeamStageReward path. Its falling/approach/offer/claim code is real;
this does not certify the director's earned-stage TrySpawnStageReward trigger.
Cancel: builtins.mc_batch1_solo.finish('cancelled explicitly').
"""
import builtins
import hashlib
import json
import math
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = Path(globals().get('MC_BATCH1_SOLO_OUTPUT',
    'E:/CodexWork/MessControl/PassivesBatch1_20261028/SoloD')).resolve()
KEY = 'mc_batch1_solo'
IDS = ('B1_HeavyHit', 'B1_LightFoot', 'B1_DeepBreath', 'B1_BigPortion',
       'B1_ConfidentThrow', 'B1_FreshDelivery', 'B1_GreedyHands',
       'B1_WideBristles', 'B1_UnderPressure')
RETIRED = ('P01', 'P02', 'P13', 'Active_BrushFury', 'Consumable_RepairKit',
           'Fallback_Mouthwash', 'Fallback_BrushFury', 'Fallback_Grenade',
           'Tool_Chainsaw_Rare')
assert ROOT.as_posix().lower() == 'e:/devgame/messcontrol'
assert OUT.drive.upper() == 'E:'
assert not (OUT/'Report.json').exists(), 'Retain evidence; supply a fresh MC_BATCH1_SOLO_OUTPUT'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved assets'
previous = getattr(builtins, KEY, None)
assert previous is None or getattr(previous, 'done', True), 'Batch1 probe already active'
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='batch1_solo_support', init_globals={'MC_ENV_OUTPUT': OUT, 'MC_ENV_CAPTURE_HZ': 0})
Base, prop = support['ApprovedEnvironmentSolo'], support['prop']


class Batch1SoloD(Base):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__, checks={}, stages=[],
            recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            limitations=[
                'Disclosed director-stop and diagnostic acquisition fixtures; not an earned full run.',
                'Chest initializer/lifecycle and controller choice are real; the director trigger is not exercised.',
                'Held primary is native gameplay input, not physical mouse hardware or viewport focus.',
                'Only one standalone player; no network, microphone, cook/package or clean-cache/PSO certification.',
                'This presentation probe complements native effect tests; it does not measure every passive in combat.',
                'Real screenshot cadence is recorded; screenshots and clips await visual review.'])
        self.report.pop('fixture_tuning', None)
        self.report.pop('tuning_reason', None)
        self.busy = False
        self.mode_at = self.record_at = 0.
        self.perks = self.inventory = self.niagara = self.chest = self.zone = None
        self.ps = None
        self.chosen_id = None

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        (self.output/'Report.json').write_text(json.dumps(self.report, ensure_ascii=False,
            indent=2, allow_nan=False), encoding='utf8')

    def check(self, name, condition):
        self.report['checks'][name] = bool(condition)
        self.require(condition, name)

    def enter(self, phase):
        self.phase = phase
        self.mode_at = time.monotonic()-self.started
        self.report['stages'].append(dict(name=phase, wall_seconds=self.mode_at,
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))))
        self.write()

    def passive_ids(self):
        return [str(value) for value in prop(self.perks, 'passive_perk_ids')]

    def slots(self):
        return [str(prop(slot, 'perk_id')) for slot in prop(self.perks, 'active_skill_slots')]

    def scale(self):
        value = self.niagara.get_world_scale()
        result = [float(value.x), float(value.y), float(value.z)]
        self.require(all(math.isfinite(x) for x in result), 'Finite actual Niagara world scale')
        return result

    def check_scale(self, name, expected):
        actual = self.scale()
        self.report.setdefault('spray_scales', {})[name] = dict(actual=actual, expected=list(expected),
            component=self.niagara.get_path_name(), asset=self.niagara.get_asset().get_path_name())
        self.check(name, all(abs(a-b)<.002 for a,b in zip(actual, expected)))

    def validate_current_table(self):
        table = self.perks.get_perk_table()
        self.require(table is not None, 'Saved perk table loaded')
        rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        enabled = sorted(row['Name'] for row in rows if row.get('bEnabled'))
        self.report['table'] = dict(path=table.get_path_name(), enabled=enabled, total=len(rows))
        # Exported FText is an NSLOCTEXT expression, not a display string.
        # Keep only plain stable FName keys here. Resolve real FText later with
        # the same public native localization function/table/culture as the UI.
        self.display_name_keys_by_id = {row['Name']: str(row.get('DisplayNameKey', ''))
            for row in rows if row['Name'] in IDS}
        self.require(all(self.display_name_keys_by_id.values()), 'Every B1 definition has a stable display-name key')
        self.check('Saved table enables exactly the nine current B1 IDs', enabled == sorted(IDS))
        for ident in RETIRED:
            self.check('Retired offer rejected: '+ident, not self.perks.can_offer_perk(ident))
            self.check('Retired acquisition rejected: '+ident, not self.perks.try_acquire_perk(ident))
        self.check('Rejected legacy grants leave active slots empty',
                   len(self.slots()) == 4 and all(ident in ('None', '') for ident in self.slots()))

    def input_snapshot(self):
        # Public reflected API only. bArenaArrivalHeld and reward widget
        # CurrentChoiceIDs are protected/private and deliberately not read.
        # Controller IsMove/LookInputIgnored, IsBossIntroPlaying and
        # IsWorldPresentationReady are declared UFUNCTIONs in current headers.
        loading = [widget for widget in u.ObjectIterator(u.MCLoadingWidget)
            if widget.get_owning_player() == self.pc and widget.is_in_viewport()
            and widget.get_visibility() not in (u.SlateVisibility.HIDDEN, u.SlateVisibility.COLLAPSED)]
        return dict(world_ready=bool(self.pc.is_world_presentation_ready()),
            reward_menu_open=bool(self.pc.is_reward_menu_open()),
            loading_visible=bool(loading), mouse_cursor=bool(prop(self.pc, 'show_mouse_cursor')),
            move_ignored=bool(self.pc.is_move_input_ignored()),
            look_ignored=bool(self.pc.is_look_input_ignored()),
            boss_intro=bool(self.pc.is_boss_intro_playing()),
            selected=str(prop(self.inventory, 'selected')),
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)))

    @staticmethod
    def outer_contains(obj, owner):
        # UObject.get_outer is a standard public Unreal Python object method.
        for _ in range(32):
            if obj is None:
                return False
            if obj == owner:
                return True
            obj = obj.get_outer()
        return False

    def actual_card_names(self, choice):
        # GetParent and TextBlock.GetText are public UFUNCTIONs. Only attached
        # live cards are used; retained orphan widgets from previous choices
        # cannot pass this parent/owner test. No private Presentation/IDs read.
        cards = [card for card in u.ObjectIterator(u.MCPerkCardWidget)
            if self.outer_contains(card, choice) and card.get_parent() is not None]
        names = []
        for card in cards:
            labels = [label for label in u.ObjectIterator(u.TextBlock)
                if label.get_name() == 'CardName' and self.outer_contains(label, card)]
            self.require(len(labels) == 1, 'One real CardName label per attached card')
            names.append(str(labels[0].get_text()))
        return names

    def spawn_chest(self):
        # Native normal stage-chest setup, without forcing Stage, offers, masks,
        # presentation geometry or receipt identities.
        floor = self.floor(self.hero.get_actor_location()+u.Vector(180, 0, 0))
        self.require(floor is not None, 'Stage chest fixture has real arena support')
        landing = floor+u.Vector(0, 0, 3)
        pose = u.Transform(location=landing)
        args = (self.world, u.MCRewardDropZone.static_class(), pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None,
            u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT)
        self.zone = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=args)
        self.require(self.zone is not None, 'Review drop-zone allocation')
        self.statics.call_method('FinishSpawningActor', args=(self.zone, pose,
            u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.zone.set_editor_properties(dict(allowed_landing_surface=self.tongue,
            allow_reward_drops=False, inward_margin=20.))
        prop(self.zone, 'area').set_box_extent(u.Vector(500, 500, 400), False)
        cls = u.load_class(None, '/Game/Gameplay/Roguelike/BP_RewardChest.BP_RewardChest_C')
        self.require(cls is not None, 'Saved real reward chest class')
        args = (self.world, cls, pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT)
        self.chest = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=args)
        self.require(self.chest is not None, 'Stage-chest allocation')
        self.chest.initialize_team_stage_reward(landing, landing+u.Vector(0, 0, 450),
            7721, self.perks.get_perk_table(), self.zone)
        self.statics.call_method('FinishSpawningActor', args=(self.chest, pose,
            u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.report['chest_fixture'] = dict(path=self.chest.get_path_name(),
            initializer='InitializeTeamStageReward', seed=7721,
            placement='180cm from owned player on actual tongue support',
            limit='Does not call private native TrySpawnStageReward or complete an earned stage')

    def tick(self, delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            wall = time.monotonic()-self.started
            self.require(wall < 120, 'Bounded 120-second standalone probe')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),
                    'Exactly one standalone authority PIE')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not self.pc or not self.pc.get_controlled_pawn():
                    self.world = None
                    return
                self.hero = self.pc.get_controlled_pawn()
                self.require(isinstance(self.hero, u.MCToothCharacter), 'Real gameplay tooth pawn')
                self.engine.set_editor_property('bUseFixedFrameRate', False)
                tongues = self.actors(u.MCTongue)
                self.require(bool(tongues), 'Actual mouth tongue present')
                self.tongue = tongues[0]
                self.report['world'] = self.world.get_path_name()
                self.enter('readiness')
                return
            self.require(self.owned_pie_is_active(worlds) and len(worlds) == 1,
                'Owned standalone PIE remains active and unreplaced')
            age = wall-self.mode_at
            if self.phase == 'readiness':
                if not self.pc.is_world_presentation_ready() or age < 2:
                    return
                self.report['fixture_result'] = u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world, 'level')
                self.require(not self.report['fixture_result'].startswith('REJECT'), 'Native level fixture accepted')
                self.ps = u.GameplayStatics.get_player_state(self.world, 0)
                self.perks = self.ps.get_component_by_class(u.MCPerkComponent)
                self.inventory = self.hero.get_component_by_class(u.MCInventoryComponent)
                self.require(self.perks is not None and self.inventory is not None, 'Actual perk/inventory components')
                self.perks.reset_perks()
                self.validate_current_table()
                self.report['baseline_max_stamina'] = float(self.hero.get_max_stamina())
                self.check('Baseline maximum stamina is 100', abs(self.report['baseline_max_stamina']-100.)<.01)
                # IsWorldPresentationReady certifies resource prewarm only.
                # The level fixture clears Training/tutorial/lobby gates, but
                # GameMode releases bArenaArrivalHeld on its NEXT world tick.
                # Do not issue and then silently lose ServerSelect in that tick.
                self.report['selection_before_arrival_release'] = self.input_snapshot()
                components = [component for component in self.hero.get_components_by_class(u.NiagaraComponent)
                    if component.get_name() == 'TreatmentSprayNiagara']
                self.check('One existing native treatment Niagara component', len(components) == 1)
                self.niagara = components[0]
                self.enter('fixture_release')
            elif self.phase == 'fixture_release':
                state = self.input_snapshot()
                self.report['selection_readiness'] = state
                self.require(age < 15, 'Normal GameMode arrival release after native fixture')
                if state['reward_menu_open']:
                    self.report.setdefault('ordinary_ui_resolutions', []).append(state)
                    self.pc.choose_reward_perk(0)
                    return
                # Waiting is observed, never overridden. The loading controller
                # must naturally release its cursor/input gates as well.
                if (age < 1. or not state['world_ready'] or state['loading_visible']
                        or state['mouse_cursor'] or state['move_ignored']
                        or state['look_ignored'] or state['boss_intro']):
                    return
                floor = self.floor(self.hero.get_actor_location())
                self.require(floor is not None, 'Inspected actual tongue support under owned fixture')
                location = self.move_to(floor, yaw=0.)
                target = location+u.Vector(100, 0, 15)
                camera = location+u.Vector(-330, -420, 250)
                self.frame_camera(camera, target)
                self.report['spray_review_camera'] = dict(camera=str(camera), target=str(target),
                    purpose='Same readable side view for baseline and pressure; actual held native input',
                    aim_policy='Non-Hero view target uses the native Hero-forward fallback; no aim snapshot is forged')
                self.next_select_at = 0.
                self.enter('native_selection')
            elif self.phase == 'native_selection':
                state = self.input_snapshot()
                self.report['selection_observed_state'] = state
                if prop(self.inventory, 'selected') == u.MCToolSlot.SPRAY:
                    self.enter('baseline_ready')
                    return
                self.require(age < 5., 'Native ServerSelect accepted within bounded five-second retry')
                if wall >= self.next_select_at:
                    self.next_select_at = wall+.25
                    self.report.setdefault('native_selection_requests', []).append(state)
                    self.inventory.server_select(u.MCToolSlot.SPRAY)
                return
            elif self.phase == 'baseline_ready' and age > .8:
                self.report['selection_observed_state'] = self.input_snapshot()
                self.check('Real spray tool selected', prop(self.inventory, 'selected') == u.MCToolSlot.SPRAY)
                self.check_scale('Unmodified existing spray scale', (1., 1., 1.))
                self.screenshot('01_baseline_hud_spray.png')
                self.primary_input(True)
                self.record_at = wall
                self.enter('baseline_spray')
            elif self.phase == 'baseline_spray':
                if age > .6:
                    self.check('Baseline native held spray emits', self.niagara.is_active())
                if age > 2.4:
                    self.screenshot('02_baseline_held_spray.png')
                    self.primary_input(False)
                    for ident in IDS:
                        self.check('Actual acquisition '+ident, self.perks.try_acquire_perk(ident))
                    self.report['acquired_ids'] = self.passive_ids()
                    self.check('Nine simultaneous distinct passives', sorted(self.passive_ids()) == sorted(IDS))
                    self.check('Nine passives leave four active slots empty',
                        len(self.slots()) == 4 and all(ident in ('None', '') for ident in self.slots()))
                    self.check('Owned B1 duplicate cannot be reacquired',
                        not self.perks.try_acquire_perk('B1_UnderPressure'))
                    self.report['max_stamina_with_passives'] = float(self.hero.get_max_stamina())
                    self.check('Deep breath yields actual 120 maximum stamina',
                        abs(self.report['max_stamina_with_passives']-120.)<.01)
                    self.enter('nine_hud')
            elif self.phase == 'nine_hud' and age > 1.1:
                self.check_scale('UnderPressure scales actual existing emitter', (1.5, 1.5, 1.25))
                self.screenshot('03_nine_passives_empty_active_slots.png')
                self.primary_input(True)
                self.record_at = wall
                self.enter('pressure_spray')
            elif self.phase == 'pressure_spray':
                if age > .6:
                    self.check('Pressure native held spray emits', self.niagara.is_active())
                if age > 3.:
                    self.screenshot('04_pressure_held_spray.png')
                    self.primary_input(False)
                    self.perks.reset_perks()
                    self.enter('reset')
            elif self.phase == 'reset' and age > 1.:
                self.check('Reset removes all nine passives', not self.passive_ids())
                self.check('Reset restores maximum stamina to 100', abs(float(self.hero.get_max_stamina())-100.)<.01)
                self.check_scale('Reset restores actual emitter scale', (1., 1., 1.))
                self.check('Reset releases primary and stops native spray', not self.niagara.is_active())
                self.inventory.server_select(u.MCToolSlot.BRUSH)
                self.spawn_chest()
                self.enter('chest')
            elif self.phase == 'chest':
                self.report['last_chest_state'] = dict(stage=str(prop(self.chest, 'stage')),
                    position=str(self.chest.get_actor_location()))
                if self.pc.is_reward_menu_open():
                    offers = [str(value) for value in self.chest.call_method('GetStageOfferIDs', args=(self.ps,))]
                    self.report['chest_offers'] = offers
                    self.check('Actual chest offers are nonempty, distinct and only B1 IDs',
                        bool(offers) and len(offers) == len(set(offers)) and set(offers).issubset(IDS))
                    widgets = [widget for widget in u.ObjectIterator(u.MCPerkChoiceWidget)
                        if widget.get_owning_player() == self.pc and widget.is_in_viewport()
                        and widget.get_visibility() not in (u.SlateVisibility.HIDDEN, u.SlateVisibility.COLLAPSED)]
                    self.check('Actual player-owned reward widget is visible', len(widgets) == 1)
                    names = self.actual_card_names(widgets[0])
                    self.report['visible_card_names'] = names
                    language = u.InternationalizationLibrary.get_current_language()
                    table = widgets[0].get_resolved_translation_table()
                    self.require(table is not None, 'Actual choice widget resolves a localization table')
                    expected = {ident: str(u.MCUIDataLibrary.resolve_text(table,
                        u.Name(self.display_name_keys_by_id[ident]), u.Text(''), language))
                        for ident in offers}
                    self.require(all(expected.values()), 'Native localization resolves every offered B1 name')
                    self.report['expected_card_names_native'] = dict(language=language,
                        table=table.get_path_name(), names_by_id=expected,
                        resolver='UMCUIDataLibrary::ResolveText, same as UMCPerkChoiceWidget::MakePresentation')
                    remaining = list(offers)
                    for name in names:
                        matches = [ident for ident in remaining if name == expected[ident]]
                        self.require(bool(matches), 'Visible real card name belongs to an authoritative B1 offer: '+name)
                        remaining.remove(matches[0])
                    self.check('Visible real cards match all authoritative B1 offers',
                        len(names) == len(offers) and not remaining)
                    self.chosen_id = offers[0]
                    self.screenshot('05_actual_stage_chest_cards.png')
                    self.enter('choice_capture')
                self.require(age < 30, 'Real stage chest offers open through normal nearby approach')
            elif self.phase == 'choice_capture' and age > 1.5:
                self.pc.choose_reward_perk(0)
                self.enter('claimed')
            elif self.phase == 'claimed' and age > 1.2:
                self.report['after_chest_passives'] = self.passive_ids()
                self.check('Native controller choice grants exactly the chosen B1 passive',
                    self.passive_ids() == [self.chosen_id])
                self.check('Native choice closes personal reward menu', not self.pc.is_reward_menu_open())
                self.check('Chest passive leaves four active slots empty',
                    len(self.slots()) == 4 and all(ident in ('None', '') for ident in self.slots()))
                self.screenshot('06_claimed_passive.png')
                self.finish()
                return
            if self.phase in ('baseline_spray', 'pressure_spray') and wall >= self.record_at:
                self.record_at = wall+.25
                self.screenshot('%05d.png' % len(self.report['frames']), sequence=True)
        except Exception:
            self.finish(traceback.format_exc())
        finally:
            self.busy = False


probe = Batch1SoloD()
setattr(builtins, KEY, probe)
try:
    probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
