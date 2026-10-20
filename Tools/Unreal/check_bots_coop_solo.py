"""Natural saved-level bot probe in one explicitly owned solo PIE; never launches Editor.

MC_BOTS_MODE='coop' (default): normal resource preparation and first human perk,
then the ordinary MC.Bots 3 skilled coop console action, the new human perk,
and natural first-three-food processing until coffee/portion two. Success needs
real AI movement and confirmed contribution from its own native diagnostic CSV.
The human is stationary apart from ordinary perk choices made by this driver.

MC_BOTS_COOP_THROUGH_COFFEE=True extends only coop: wait for the first natural
coffee to finish, five foods in portion two, and at least ten game seconds
actually observed in portion two. Every bot must then be alive and show new
ordinary locomotion or confirmed work AFTER coffee in fresh native CSV rows.
This option defaults to/caps budgets at300 game /420 wall seconds. Old work
counters cannot satisfy the post-coffee check. Default coop/cycle are unchanged.

MC_BOTS_MODE='observe_cycle': MC.Bots 4 skilled observe; the human becomes an
ordinary spectator. Observe the saved Nut -> post-nut 5/8/9 -> small mint ->
Jellies -> Ice -> Fog chain through bAuthoredFragmentComplete. This is an AI
observation, not a human coop playthrough or a forced game-win assertion.

Requires an idle, saved owned Editor (-MCHubOwned or -MCFirstRelaxOwned), fresh
DLL and saved assets. No clock/stage/profile/nav/health/position changes, direct
damage/cleaning, actors spawned/deleted by Python, network peers, saves or builds.
Only ordinary MC.Bots / MC.Bots.Report / MC.Bots.Stop and the human's normal
ChooseRewardPerk(0) callback are driven. Physical keyboard/mouse is not tested.

Globals: MC_BOTS_OUTPUT (fresh E: folder), MC_BOTS_SEED (default41),
MC_BOTS_GAME_BUDGET_SECONDS (coop180, cycle1800; max1800),
MC_BOTS_WALL_BUDGET_SECONDS (coop300, cycle2400; max2400).
Milestone PNGs are capped at32 /96MiB. MC_BOTS_PREVIEW=True additionally
records at most16 seconds /64 frames /128MiB in coop, using the ordinary
camera orbit/zoom handlers to frame the first food; no camera rig replacement.
With THROUGH_COFFEE the optional16-second preview starts at the first actual
MEALCOFFEEWAVE instead, retaining the current camera without a replacement.
Native CSV stays in project Saved/Playtests and is copied into output at exit.
No -MCBotsSmoke flag: that older probe assumes the obsolete short nut fragment.
Cancel this probe only: builtins.mc_bots_coop_solo.cancel().
"""
import builtins
import csv
import hashlib
import importlib.util
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
MODE = str(globals().get('MC_BOTS_MODE', 'coop'))
OUT = Path(globals().get('MC_BOTS_OUTPUT',
    'E:/CodexWork/MessControlBots_20261020/Solo_' + MODE + '_01')).resolve()
KEY = 'mc_bots_coop_solo'
spec = importlib.util.spec_from_file_location('mc_bots_loading_support',
    ROOT / 'Tools/Unreal/check_resource_loading_solo.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
support.OUT = support.helpers.OUT = OUT
support.MAP = 'L_Mouth'
helpers = support.helpers
valid, path, prop, enum_name, require = support.valid, support.path, support.prop, support.enum_name, support.require


def file_hash(file):
    return hashlib.sha256(file.read_bytes()).hexdigest() if file.is_file() else None


class BotsCoopSolo(support.ResourceLoadingSolo):
    def __init__(self):
        require(MODE in ('coop', 'observe_cycle'), 'MC_BOTS_MODE must be coop or observe_cycle')
        require(hasattr(u, 'MCPlaytestSession') and hasattr(u, 'MCPlaytestBotController'),
            'The bot Editor DLL is missing')
        require('-MCBotsSmoke' not in u.SystemLibrary.get_command_line(),
            'The historical smoke driver must not run alongside this probe')
        super().__init__()
        self.capture_hz = 0.
        self.seed = int(globals().get('MC_BOTS_SEED', 41))
        require(-2147483648 <= self.seed <= 2147483647, 'Seed exceeds the native int32 range')
        self.cycle = MODE == 'observe_cycle'
        self.through_coffee = bool(globals().get('MC_BOTS_COOP_THROUGH_COFFEE', False)) and not self.cycle
        self.preview = bool(globals().get('MC_BOTS_PREVIEW', False)) and not self.cycle
        self.throw_debug = bool(globals().get('MC_BOTS_THROW_DEBUG', False))
        self.old_throw_debug = None
        self.preview_at = None
        self.preview_frame_at = 0.
        self.game_budget = max(60., min(300. if self.through_coffee else 1800., float(globals().get(
            'MC_BOTS_GAME_BUDGET_SECONDS', 300. if self.through_coffee else 1800. if self.cycle else 180.))))
        self.wall_budget = max(90., min(420. if self.through_coffee else 2400., float(globals().get(
            'MC_BOTS_WALL_BUDGET_SECONDS', 420. if self.through_coffee else 2400. if self.cycle else 300.))))
        self.first_coffee_game = self.coffee_finished_game = None
        self.second_portion_game = None
        self.second_portion_five = False
        self.second_portion_observed_seconds = 0.
        self.last_second_portion_game = None
        self.post_coffee_bots = {}
        self.post_coffee_pass = False
        self.next_coffee_check_at = 0.
        self.expected_bots = 4 if self.cycle else 3
        self.bot_session_path = None
        self.bot_start_game = self.bot_start_wall = None
        self.native_csv = None
        self.csv_offset = 0
        self.csv_pending = ''
        self.csv_header = None
        self.csv_row_count = 0
        self.csv_latest = {}
        self.csv_metrics = {}
        self.next_csv_at = self.next_sample_at = self.next_report_at = self.next_work_at = 0.
        self.perk_requested = set()
        self.perk_last_request_at = {}
        self.milestones = set()
        self.seen_stages = set()
        self.post_nut_portions = {}
        self.opening_portion_three = False
        self.seen_first_food_actors = set()
        self.finished_bot_session = False
        self.run_terminal = False
        self.profile_paths = [ROOT / 'Content/Gameplay/CoreLoop/DA_SingleDay.uasset'] + [
            ROOT / 'Config' / name for name in ('DefaultEngine.ini', 'DefaultGame.ini', 'DefaultInput.ini')]
        self.original_hashes = {str(file): file_hash(file) for file in self.profile_paths}
        self.native_csv_dir = Path(u.Paths.project_saved_dir()).resolve() / 'Playtests'
        self.existing_csvs = {file.resolve() for file in self.native_csv_dir.glob('Bots_*.csv')}
        self.report.update(scope=__doc__, mode=MODE, game_budget_seconds=self.game_budget,
            wall_budget_seconds=self.wall_budget, seed=self.seed, gameplay_overrides=False,
            controlled_runtime_fixture=False, expected_bots=self.expected_bots,
            coop_through_coffee=self.through_coffee,
            source_sha256=file_hash(Path(__file__)), native_csv_rows=0, bot_metrics={},
            milestones=[], post_nut_portions={},
            limitations=[
                'One standalone PIE process. AI teammates do not exercise network replication or latency.',
                'Coop has a stationary human whose normal perk callbacks are driven by this probe; physical input is not tested.',
                'observe_cycle replaces the human with a spectator through the ordinary bot command. It does not establish human coop comfort.',
                'A completed authored sequence is recorded separately from native Won / Lost. No victory flag is manufactured.',
                'Budget expiry, missing stages or absent native contribution produce an explicit failure, not a shortened pass.',
                'Native non-reflected bot counters/activity are read from the real session CSV; requests are not counted as accepted hits.',
                'Milestones use the existing camera. An optional coop preview uses only the normal player orbit/zoom inputs; no pawn or camera rig is relocated.',
                'No packaged game, cook, network or quantitative performance check.'])

    def request_perk(self, label):
        if not isinstance(self.pc, u.MCPlayerController) or not self.pc.is_reward_menu_open():
            return False
        widget = helpers.owned_root_widget(u.MCPerkChoiceWidget, self.pc)
        if not valid(widget) or self.pc.is_waiting_for_perk_team():
            return False
        player = prop(self.pc, 'player_state')
        require(valid(player), 'Human perk has no owning PlayerState')
        offer = prop(player, 'level_up_offer')
        if int(prop(player, 'pending_level_choices')) <= 0 or not prop(offer, 'perk_ids'):
            return False
        token = u.get_default_object(u.GuidLibrary).call_method('Conv_GuidToString', args=(prop(offer, 'offer_id'),))
        key = path(player) + ':' + token
        wall = time.monotonic()
        if key in self.perk_requested or wall-self.perk_last_request_at.get(key, 0.) < .3:
            return False
        self.perk_last_request_at[key] = wall
        before_pending = int(prop(player, 'pending_level_choices'))
        self.report['requests'].append(dict(method='ChooseRewardPerk', choice_index=0,
            label=label, offer_id=token, player=path(player), game_seconds=self.now(),
            wall_seconds=time.monotonic()-self.started))
        self.pc.choose_reward_perk(0)
        # IsShowingChoices is intentionally not reflected. A visible waiting
        # widget can lag its new offer by a frame; retry only if this ordinary
        # callback did not consume the real personal offer.
        after_offer = u.get_default_object(u.GuidLibrary).call_method('Conv_GuidToString',
            args=(prop(prop(player, 'level_up_offer'), 'offer_id'),))
        if int(prop(player, 'pending_level_choices')) < before_pending or after_offer != token:
            self.perk_requested.add(key)
        return True

    def milestone(self, name, detail=None):
        if name in self.milestones:
            return
        self.milestones.add(name)
        self.report['milestones'].append(dict(name=name, detail=detail,
            wall_seconds=time.monotonic()-self.started, game_seconds=self.now()))
        size = sum(item['bytes'] for item in self.report['screenshots'])
        if len(self.report['screenshots']) < 32 and size+8*1024*1024 <= 96*1024*1024:
            safe_name = ''.join(character if character.isalnum() else '_' for character in name)
            self.capture('%02d_%s.png' % (len(self.report['screenshots'])+1, safe_name))
        else:
            self.report['milestone_capture_limit_reached'] = True
        self.write()

    def launch_bots(self):
        require(not self.actors(u.MCPlaytestSession), 'An existing bot session must be preserved')
        command = 'MC.Bots %d skilled %s %d' % (
            self.expected_bots, 'observe' if self.cycle else 'coop', self.seed)
        if self.throw_debug:
            self.old_throw_debug = u.SystemLibrary.get_console_variable_int_value('mc.Bots.ThrowDebug')
            self.console('mc.Bots.ThrowDebug 1')
            self.report['throw_diagnostics_enabled'] = True
        self.report['requests'].append(dict(method='Ordinary console command', command=command,
            game_seconds=self.now(), wall_seconds=time.monotonic()-self.started))
        self.console(command)
        sessions = self.actors(u.MCPlaytestSession)
        require(len(sessions) == 1, 'The ordinary MC.Bots action did not create one session; inspect the native log')
        self.bot_session_path = path(sessions[0])
        self.bot_start_game, self.bot_start_wall = self.now(), time.monotonic()
        require(self.refresh_world(), 'The same owned world did not survive normal shift restart')
        require(valid(self.sequence), 'The normal bot restart has no single-day director')
        self.report.update(bot_session=self.bot_session_path, bot_director=path(self.sequence),
            console_command=command, native_run_seed=int(prop(self.gs, 'run_seed')))
        # Saved actor and artist component transforms can differ. Record the
        # actual authored intake axis before evaluating physical Q delivery.
        self.report['throat_geometry'] = []
        for throat in self.actors(u.MCThroat):
            geometry = dict(actor=path(throat), actor_transform=str(throat.get_actor_transform()),
                actor_forward=str(throat.get_actor_forward_vector()))
            mouth = prop(throat, 'authored_mouth')
            if valid(mouth):
                transform = mouth.get_world_transform()
                geometry['mouth_transform'] = str(transform)
                mesh = prop(mouth, 'skeletal_mesh_asset')
                geometry['mouth_mesh'] = path(mesh)
                try:
                    require(valid(mesh) and mesh.get_name() == 'SK_Exit' and bool(prop(throat, 'manual_intake')),
                        'Axis diagnostic applies only to the native SK_Exit manual profile')
                    inward = u.MathLibrary.transform_direction(transform, u.Vector(0., .9747185974, .2234360218))
                    forward = throat.get_actor_forward_vector()
                    denominator = ((inward.x**2+inward.y**2)*(forward.x**2+forward.y**2))**.5
                    geometry['intake_inward'] = str(inward)
                    geometry['forward_dot_intake_xy'] = (inward.x*forward.x+inward.y*forward.y)/denominator if denominator else None
                except Exception:
                    geometry['optional_axis_diagnostic_error'] = traceback.format_exc()
            self.report['throat_geometry'].append(geometry)
        require(int(prop(self.gs, 'run_seed')) == self.seed, 'The native run did not use the requested seed')
        self.read_csv(force=True)
        self.stage('observe_cycle' if self.cycle else 'observe_coop')
        self.milestone('real_bot_team_started')

    def read_csv(self, force=False):
        wall = time.monotonic()
        if not force and wall < self.next_csv_at:
            return
        self.next_csv_at = wall+2.
        if self.native_csv is None:
            new = {file.resolve() for file in self.native_csv_dir.glob('Bots_*.csv')} - self.existing_csvs
            require(len(new) <= 1, 'Several new native bot CSVs appeared; ownership is ambiguous')
            if not new:
                return
            self.native_csv = next(iter(new))
            self.report['native_csv_source'] = str(self.native_csv)
        with self.native_csv.open('r', encoding='utf-8-sig', newline='') as stream:
            stream.seek(self.csv_offset)
            chunk = stream.read()
            self.csv_offset = stream.tell()
        lines = (self.csv_pending+chunk).splitlines(keepends=True)
        self.csv_pending = ''
        if lines and not lines[-1].endswith(('\n', '\r')):
            self.csv_pending = lines.pop()
        for line in lines:
            values = next(csv.reader([line]))
            if not values:
                continue
            if self.csv_header is None:
                require('bot_id' in values and 'confirmed_hits' in values and 'activity' in values,
                    'Native bot CSV has an unsupported schema')
                self.csv_header = values
                continue
            require(len(values) == len(self.csv_header), 'A completed native CSV row has the wrong field count')
            row = dict(zip(self.csv_header, values))
            require(row['mode'] == ('observe' if self.cycle else 'coop'), 'Native CSV belongs to a different mode')
            require(int(row['decision_seed']) == self.seed, 'Native CSV belongs to a different decision seed')
            identity = row['bot_id']
            self.csv_latest[identity] = row
            metrics = self.csv_metrics.setdefault(identity, dict(max_points=0, max_confirmed_hits=0,
                max_brush_contacts=0, max_deaths=0, moved_cm=0., ordinary_distance_cm=0.,
                activities=[], first_position=None))
            position = [float(row['bot_'+axis]) for axis in ('x', 'y', 'z')]
            if metrics['first_position'] is None:
                metrics['first_position'] = position
            metrics['moved_cm'] = max(metrics['moved_cm'], sum((position[index]-metrics['first_position'][index])**2
                for index in (0, 1))**.5)
            # Count ordinary XY movement only between living snapshots in the
            # same pawn life. A reserve-tooth respawn must not become path evidence.
            deaths, alive = int(row['bot_deaths']), bool(int(row['bot_alive']))
            if metrics.get('last_alive') and alive and metrics.get('last_deaths') == deaths and float(row['bot_speed']) > 20:
                metrics['ordinary_distance_cm'] += sum((position[index]-metrics['last_position'][index])**2
                    for index in (0, 1))**.5
            metrics.update(last_position=position, last_alive=alive, last_deaths=deaths)
            metrics['max_speed_cm_s'] = max(metrics.get('max_speed_cm_s', 0.), float(row['bot_speed']))
            for dest, source in (('max_points', 'bot_points'), ('max_confirmed_hits', 'confirmed_hits'),
                    ('max_brush_contacts', 'brush_contacts'), ('max_deaths', 'bot_deaths')):
                metrics[dest] = max(metrics[dest], int(row[source]))
            if row['activity'] not in metrics['activities']:
                metrics['activities'].append(row['activity'])
            if self.coffee_finished_game is not None:
                self.observe_post_coffee_row(identity, row, position)
            self.csv_row_count += 1
        self.report.update(native_csv_rows=self.csv_row_count, bot_metrics=self.csv_metrics,
            latest_native_csv_rows=self.csv_latest, post_coffee_bots=self.post_coffee_bots)

    def observe_post_coffee_row(self, identity, row, position):
        elapsed = float(row['elapsed_seconds'])
        after = self.post_coffee_bots.get(identity)
        if after is None:
            return
        if elapsed <= max(after['baseline_elapsed'], self.coffee_finished_game-self.bot_start_game)+.001:
            return
        deaths, alive = int(row['bot_deaths']), bool(int(row['bot_alive']))
        same_life = deaths == after['last_deaths']
        active_task = row['activity'] in ('clean', 'physical_tooth_approach', 'repair', 'spray', 'break_food', 'collect_food',
            'pull_food', 'deliver', 'calculus', 'fight', 'rescue_ice', 'warm', 'fog_guard')
        if after['last_alive'] and alive and same_life and active_task and float(row['bot_speed']) > 20:
            after['ordinary_distance_cm'] += sum((position[index]-after['last_position'][index])**2
                for index in (0, 1))**.5
        points, hits, brushes = (int(row[key]) for key in ('bot_points', 'confirmed_hits', 'brush_contacts'))
        after['confirmed_work'] |= points > after['last_points'] or (same_life and
            (hits > after['last_hits'] or brushes > after['last_brushes']))
        after.update(last_elapsed=elapsed, last_position=position, last_deaths=deaths,
            last_alive=alive, last_points=points, last_hits=hits, last_brushes=brushes)

    def observe_coffee_extension(self, stage, portion, target, spawned):
        game = self.now()
        if stage == 'MEALCOFFEEWAVE' and portion == 1 and self.first_coffee_game is None:
            self.first_coffee_game = game
            self.milestone('first_natural_coffee_started')
            if self.preview and self.preview_at is None:
                self.preview_at = time.monotonic()
                self.report['preview_scope'] = 'First natural coffee wave, existing player camera, no relocation'
        if self.first_coffee_game is not None and self.coffee_finished_game is None and (
                stage == 'BETWEENMEALSPAUSE' or (stage == 'FIRSTMEAL' and portion == 2)):
            active = [item for item in self.actors(u.MCCoffeeFlood) if bool(prop(item, 'active'))]
            if not active:
                # Flush a fresh native baseline at the actual end. Pre-coffee
                # points/contacts and a reserve-tooth respawn cannot prove recovery.
                self.console('MC.Bots.Report')
                self.read_csv(force=True)
                require(len(self.csv_latest) == self.expected_bots, 'Coffee end lacks the complete native AI baseline')
                self.coffee_finished_game = game
                for identity, row in self.csv_latest.items():
                    self.post_coffee_bots[identity] = dict(baseline_elapsed=float(row['elapsed_seconds']),
                        last_elapsed=float(row['elapsed_seconds']), ordinary_distance_cm=0., confirmed_work=False,
                        last_position=[float(row['bot_'+axis]) for axis in ('x', 'y', 'z')],
                        last_deaths=int(row['bot_deaths']), last_alive=bool(int(row['bot_alive'])),
                        last_points=int(row['bot_points']), last_hits=int(row['confirmed_hits']),
                        last_brushes=int(row['brush_contacts']))
                self.milestone('first_natural_coffee_finished')
        in_second = stage == 'FIRSTMEAL' and portion == 2
        if in_second:
            require(target == 5, 'The natural second portion is no longer five foods')
            if self.second_portion_game is None:
                self.second_portion_game = game
                self.milestone('natural_second_portion_started')
            self.second_portion_five |= spawned == 5
            if self.last_second_portion_game is not None:
                self.second_portion_observed_seconds += max(0., game-self.last_second_portion_game)
            self.last_second_portion_game = game
        else:
            self.last_second_portion_game = None
        self.report['coffee_extension'] = dict(first_coffee_game=self.first_coffee_game,
            coffee_finished_game=self.coffee_finished_game, second_portion_game=self.second_portion_game,
            second_portion_five_issued=self.second_portion_five,
            observed_second_portion_game_seconds=self.second_portion_observed_seconds,
            post_coffee_bots=self.post_coffee_bots)

    def coffee_extension_passed(self):
        if self.post_coffee_pass:
            return True
        if not (self.coffee_finished_game is not None and self.second_portion_five
                and self.second_portion_observed_seconds >= 10.):
            return False
        wall = time.monotonic()
        if wall < self.next_coffee_check_at:
            return False
        self.next_coffee_check_at = wall+1.
        self.console('MC.Bots.Report')
        self.read_csv(force=True)
        # Real work-task locomotion excludes floating/idle/coffee-survival
        # velocities. Confirm the actual pawns have also returned to standing
        # rather than accepting a living ragdoll that was swept off the arena.
        live = []
        for bot in self.actors(u.MCPlaytestBotController):
            pawn = bot.get_controlled_pawn()
            standing = valid(pawn) and enum_name(prop(pawn, 'tooth_physics').get_body_state()) == 'STANDING'
            dry = valid(pawn) and not bool(prop(pawn, 'in_coffee'))
            walking = valid(pawn) and prop(pawn.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_WALKING
            live.append(dict(controller=path(bot), pawn=path(pawn), standing=standing, dry=dry, walking=walking))
        self.report['post_coffee_live_pawns'] = live
        active = len(self.post_coffee_bots) == self.expected_bots and all(
            row['last_alive'] and row['last_elapsed'] > row['baseline_elapsed']
            and (row['ordinary_distance_cm'] > 100. or row['confirmed_work'])
            for row in self.post_coffee_bots.values())
        self.post_coffee_pass = active and len(live) == self.expected_bots and all(
            row['standing'] and row['dry'] and row['walking'] for row in live)
        return self.post_coffee_pass

    def observe_bots(self):
        require(valid(self.sequence), 'The saved single-day director disappeared')
        sessions = self.actors(u.MCPlaytestSession)
        require(len(sessions) == 1 and path(sessions[0]) == self.bot_session_path,
            'The owned bot session was removed or replaced')
        bots = self.actors(u.MCPlaytestBotController)
        require(len(bots) == self.expected_bots, 'The ordinary AI controller roster changed unexpectedly')
        player = prop(self.pc, 'player_state')
        require(bool(player.is_only_a_spectator()) == self.cycle, 'Human coop/spectator status disagrees with the requested mode')
        require(bool(self.preparation.is_ready()), 'Resource readiness regressed during the bot run')
        require(not bool(prop(self.gs, 'dev_manual_events')), 'An unrelated manual event override interrupted the natural run')
        stage = enum_name(prop(self.sequence, 'stage'))
        phase = enum_name(prop(self.gs, 'phase'))
        require(phase != 'LOST', 'The ordinary bot run lost; reserve teeth / mouth health were not overridden')
        if phase == 'WON':
            self.report['native_game_won'] = True
        self.seen_stages.add(stage)
        if 'native_'+stage not in self.milestones:
            self.milestone('native_'+stage)
        portion = int(prop(self.sequence, 'meal_portion_number'))
        target = int(prop(self.sequence, 'meal_portion_target'))
        spawned = int(prop(self.sequence, 'meal_portion_spawned'))
        if stage == 'FIRSTMEAL' and portion == 1:
            require(target == 3, 'The saved first portion is no longer three whole foods')
            self.opening_portion_three |= spawned == 3
            self.seen_first_food_actors.update(item['actor'] for item in self.food_rows())
            if spawned == 3:
                self.milestone('opening_three_foods_issued')
                if self.preview and not self.through_coffee and self.preview_at is None:
                    self.last_food_rows = self.food_rows()
                    if self.last_food_rows:
                        self.aim_at_food()
                        self.preview_at = time.monotonic()+.5
        if stage == 'POSTNUTMEAL':
            entry = self.post_nut_portions.setdefault(str(portion), dict(target=target, max_spawned=0))
            require(entry['target'] == target, 'A post-nut portion target changed during play')
            entry['max_spawned'] = max(entry['max_spawned'], spawned)
            self.milestone('post_nut_portion_%d_target_%d' % (portion, target))
        if not self.cycle:
            self.request_perk('Human chooses only its own earned card during coop')
        if self.through_coffee:
            self.observe_coffee_extension(stage, portion, target, spawned)
        self.read_csv()
        wall = time.monotonic()
        if self.preview_at is not None and self.preview_at <= wall <= self.preview_at+16. and wall >= self.preview_frame_at:
            if len(self.report['frames']) < 64 and self.frame_bytes < 128*1024*1024:
                self.preview_frame_at = wall+.25
                self.capture('%05d.png' % len(self.report['frames']), sequence=True)
        if wall >= self.next_sample_at:
            self.next_sample_at = wall+1.
            row = dict(game_seconds=self.now(), elapsed_run_game_seconds=self.now()-self.bot_start_game,
                wall_seconds=wall-self.started, native_stage=stage, native_phase=phase,
                portion_number=portion, portion_target=target, portion_spawned=spawned,
                authored_fragment_complete=bool(prop(self.sequence, 'authored_fragment_complete')),
                tasks_left=int(prop(self.gs, 'tasks_left')), tasks_total=int(prop(self.gs, 'tasks_total')),
                mouth_health=float(prop(self.gs, 'mouth_health')),
                human_pawn=path(self.hero), human_spectator=bool(player.is_only_a_spectator()),
                pending_human_choices=int(prop(player, 'pending_level_choices')),
                bot_pawns=[dict(controller=path(bot), pawn=path(bot.get_controlled_pawn())) for bot in bots],
                instruction=str(prop(prop(self.gs, 'director_state'), 'instruction')))
            self.report['samples'].append(row)
        if wall >= self.next_report_at:
            self.next_report_at = wall+10.
            self.report.update(seen_native_stages=sorted(self.seen_stages),
                post_nut_portions=self.post_nut_portions,
                opening_three_foods_observed=self.opening_portion_three)
            self.write()
        return stage

    def finish_checks(self, cycle_complete):
        self.console('MC.Bots.Report')
        self.read_csv(force=True)
        self.check('native_csv_owns_complete_ai_roster', len(self.csv_latest) == self.expected_bots,
            'The native CSV does not contain every AI participant')
        self.check('native_four_player_participant_count', all(int(row['participants']) == 4
            for row in self.csv_latest.values()), 'The native run does not count the intended four-person team')
        self.check('ordinary_ai_movement', len(self.csv_metrics) == self.expected_bots and all(item['ordinary_distance_cm'] > 100
            for item in self.csv_metrics.values()), 'Ordinary movement was not confirmed for every bot')
        self.check('ordinary_ai_confirmed_contribution', len(self.csv_metrics) == self.expected_bots and all(item['max_points'] > 0
            or item['max_confirmed_hits'] > 0 or item['max_brush_contacts'] > 0
            for item in self.csv_metrics.values()), 'Accepted ordinary work was not confirmed for every bot')
        self.check('first_three_foods_naturally_issued', self.opening_portion_three,
            'The complete first-three-food portion was not observed')
        if cycle_complete:
            required = {'NUTS', 'POSTNUTMEAL', 'POSTNUTMINT', 'JELLIES', 'ICE', 'FOGBRAWL'}
            self.check('all_saved_key_stages_observed', required <= self.seen_stages,
                'Authored completion was signalled without observing stages: '+repr(sorted(required-self.seen_stages)))
            self.check('post_nut_targets_are_5_8_9', [self.post_nut_portions.get(str(index), {}).get('target')
                for index in (1, 2, 3)] == [5, 8, 9], 'The natural post-nut portions were not all 5/8/9')
            self.report.update(result='authored_cycle_complete', native_game_won=bool(self.report.get('native_game_won', False)),
                victory_interpretation='The native authored-fragment flag completed the saved sequence; this is not an invented Won state.')
            self.milestone('authored_cycle_complete')
        else:
            self.check('first_food_gate_naturally_released', True, '')
            if self.through_coffee:
                self.check('natural_first_coffee_finished', self.coffee_finished_game is not None, 'First coffee did not finish')
                self.check('natural_second_portion_issued_five', self.second_portion_five, 'Second portion did not issue five foods')
                self.check('ten_actual_game_seconds_in_second_portion', self.second_portion_observed_seconds >= 10.,
                    'Ten actual game seconds in portion two were not observed')
                self.check('every_bot_alive_and_active_after_coffee', self.coffee_extension_passed(),
                    'Every living AI must demonstrate new ordinary movement or accepted work after coffee')
                self.report['result'] = 'coop_coffee_survival_and_second_portion_confirmed'
                self.milestone('three_bots_active_after_coffee')
            else:
                self.report['result'] = 'coop_first_portion_and_contribution_confirmed'
                self.milestone('coop_first_portion_gate_released')
        self.run_terminal = True
        self.stage('capture_flush')

    def tick(self, _delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            wall = time.monotonic()
            if self.phase == 'ending_pie':
                if not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True):
                    self.report['cleanup']['owned_pie_stopped'] = True
                    self.finish()
                elif wall-self.phase_wall > 12.:
                    self.error = self.error or 'Owned PIE did not stop within twelve seconds'
                    self.finish()
                return
            require(wall-self.started < self.wall_budget, 'Bot probe exceeded its wall-time budget; no stages were forced')
            if self.phase == 'warming_editor':
                if wall-self.started >= 3.:
                    self.start()
                return
            require(not prop(self.pie_start, 'Error'), 'StartPIE: '+str(prop(self.pie_start, 'Error')))
            if wall < self.next_work_at:
                return
            self.next_work_at = wall+.1
            if not self.refresh_world():
                require(wall-self.phase_wall < 45., 'Owned world/controller/preloader did not appear')
                return
            if self.phase == 'observe_preparation':
                row, overlay = self.observe()
                if valid(overlay) and 'real_loading_screen' not in self.milestones:
                    self.milestone('real_loading_screen')
                if not row['ready'] or valid(overlay) or not self.pc.is_reward_menu_open():
                    return
                self.check('preparation_ready_before_bot_start', True, '')
                self.milestone('ordinary_initial_human_perk')
                if self.request_perk('Initial normal map perk before the F3-equivalent bot action'):
                    self.stage('await_initial_perk_close')
            elif self.phase == 'await_initial_perk_close':
                require(wall-self.phase_wall < 15., 'The normal initial human perk did not close')
                if not self.pc.is_reward_menu_open():
                    self.launch_bots()
                else:
                    self.request_perk('Retry ordinary initial perk only if the actual offer remains unclaimed')
            elif self.phase in ('observe_coop', 'observe_cycle'):
                require(self.now()-self.bot_start_game < self.game_budget,
                    'Bot probe exceeded its game-time budget; remaining stages/gates are reported unchanged')
                stage = self.observe_bots()
                if self.cycle and bool(prop(self.sequence, 'authored_fragment_complete')):
                    self.finish_checks(True)
                elif self.through_coffee and self.opening_portion_three and self.coffee_extension_passed():
                    self.finish_checks(False)
                elif not self.cycle and not self.through_coffee and self.opening_portion_three and (
                        stage in ('MEALCOFFEEWAVE', 'BETWEENMEALSPAUSE')
                        or (stage == 'FIRSTMEAL' and int(prop(self.sequence, 'meal_portion_number')) >= 2)):
                    self.finish_checks(False)
            elif self.phase == 'capture_flush' and wall-self.phase_wall >= .3:
                self.begin_finish()
        except Exception:
            self.begin_finish(traceback.format_exc())
        finally:
            self.busy = False

    def begin_finish(self, error=None):
        if self.done or self.phase == 'ending_pie':
            return
        self.error = self.error or error
        worlds = self.owned_worlds()
        # Stop only the actual bot session created by our ordinary command in
        # our own PIE world. Never adopt/stop a pre-existing or replacement PIE.
        if len(worlds) == 1 and path(worlds[0]) == self.world_path and self.bot_session_path:
            try:
                sessions = self.actors(u.MCPlaytestSession)
                if len(sessions) == 1 and path(sessions[0]) == self.bot_session_path:
                    self.console('MC.Bots.Report')
                    self.console('MC.Bots.Stop')
                    self.finished_bot_session = True
                    self.report['cleanup']['ordinary_bots_stop_requested'] = True
                    self.read_csv(force=True)
                    if self.native_csv and self.native_csv.is_file():
                        copy = OUT / 'NativeBots.csv'
                        data = self.native_csv.read_bytes()
                        copy.write_bytes(data)
                        self.report['native_csv_copy'] = dict(file=str(copy), bytes=len(data),
                            sha256=hashlib.sha256(data).hexdigest())
                else:
                    self.report['cleanup']['replacement_bot_session_preserved'] = True
            except Exception:
                self.error = self.error or 'Ordinary bot-stop / final CSV collection failed: '+traceback.format_exc()
        self.report.update(seen_native_stages=sorted(self.seen_stages), post_nut_portions=self.post_nut_portions,
            opening_three_foods_observed=self.opening_portion_three, bot_metrics=self.csv_metrics)
        super().begin_finish(self.error)

    def finish(self):
        if self.done:
            return
        if self.old_throw_debug is not None:
            self.console('mc.Bots.ThrowDebug %d' % self.old_throw_debug)
            self.old_throw_debug = None
        current = {str(file): file_hash(file) for file in self.profile_paths}
        self.report['protected_file_hashes_before'] = self.original_hashes
        self.report['protected_file_hashes_after'] = current
        if current != self.original_hashes:
            self.error = self.error or 'A protected saved profile/config file changed during the observation'
        super().finish()
        u.log('MC_BOTS_COOP_SOLO_'+('FAIL ' if self.error else 'PASS ')+str(OUT / 'Report.json'))


def main():
    for key in (KEY, support.KEY, helpers.KEY, 'mc_small_mint_solo'):
        previous = getattr(builtins, key, None)
        require(previous is None or getattr(previous, 'done', True), 'Another owned solo probe is active: '+key)
    probe = BotsCoopSolo()
    setattr(builtins, KEY, probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback = u.register_slate_post_tick_callback(probe.tick)


if __name__ == '__main__':
    main()
