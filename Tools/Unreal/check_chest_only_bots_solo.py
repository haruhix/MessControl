"""Observe chest-only startup and real three-bot food delivery in saved L_Mouth.

Use only after an approved Editor build, in an idle saved owned Editor. Starts
one standalone PIE, waits for native resource readiness, uncovered gameplay and
the director's real exit from Training (which requires the normal loading ACK),
then drives only the ordinary ``MC.Bots 3 skilled coop`` command. It never
waits for, creates or selects the removed initial/XP perk.

Read-only observations: actual retained food Pieces, physical detachments,
guarded native MC_BOT_THROW_RELEASE records and MCThroat.FoodSwallowed. Maximum
observed tower size is reported honestly; six pieces are not fabricated or a
condition for this narrow natural-food probe. Native fixtures cover capacity.
The stationary human can claim a chest only if normal gameplay actually opens
its cards. Otherwise stop at the first naturally landed stage chest without
moving the human or asserting a claim. An optional preview uses one ordinary
camera orbit/scroll input to widen the view toward the saved tongue centre;
native camera easing, collision and zoom limits remain active. No food, actors, positions, navigation,
profiles, stages, health, time or loading acknowledgements are forced.

Globals: MC_CHEST_ONLY_BOTS_OUTPUT (fresh E: folder), MC_CHEST_ONLY_BOTS_SEED
(41), MC_CHEST_ONLY_BOTS_GAME_SECONDS (max180), MC_CHEST_ONLY_BOTS_WALL_SECONDS
(max300), MC_CHEST_ONLY_BOTS_PREVIEW (False; max16 seconds/64 real frames),
MC_CHEST_ONLY_BOTS_CAMERA_PITCH (-16 degrees; ordinary native pitch limits apply).
The owned Editor needs its usual -MCHubOwned/-MCFirstRelaxOwned and -AbsLog=E:...
so accepted native Q events can be correlated with this exact PIE world. No
microphone, multiplayer, package saves, build or full-level verification.
Cancel only this probe: builtins.mc_chest_only_bots_solo.cancel().
"""
import builtins
import hashlib
import importlib.util
import math
import re
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_CHEST_ONLY_BOTS_OUTPUT',
    'E:/CodexWork/MessControlChestOnly_20261021/BotsSolo01')).resolve()
KEY = 'mc_chest_only_bots_solo'
spec = importlib.util.spec_from_file_location('mc_chest_only_bot_support',
    ROOT / 'Tools/Unreal/check_bots_coop_solo.py')
bots = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bots)
bots.OUT = bots.support.OUT = bots.helpers.OUT = OUT
bots.MODE = 'coop'
bots.MC_BOTS_SEED = int(globals().get('MC_CHEST_ONLY_BOTS_SEED', 41))
bots.MC_BOTS_THROW_DEBUG = True
bots.MC_BOTS_PREVIEW = False
bots.MC_BOTS_COOP_THROUGH_COFFEE = False
helpers = bots.helpers
valid, path, prop, enum_name, require = bots.valid, bots.path, bots.prop, bots.enum_name, bots.require
RELEASE = re.compile(r'MC_BOT_THROW_RELEASE hero=(\S+) food=(\S+) remaining=(\d+) manual=(\d+) time=([\d.]+)')


def vector(value):
    return [float(value.x), float(value.y), float(value.z)]


class ChestOnlyBotsSolo(bots.BotsCoopSolo):
    def __init__(self):
        super().__init__()
        self.game_budget = max(30., min(180., float(globals().get('MC_CHEST_ONLY_BOTS_GAME_SECONDS', 180.))))
        self.wall_budget = max(60., min(300., float(globals().get('MC_CHEST_ONLY_BOTS_WALL_SECONDS', 300.))))
        self.preview = bool(globals().get('MC_CHEST_ONLY_BOTS_PREVIEW', False))
        self.select_perk = False
        self.towers = {}
        self.previous_pieces = {}
        self.physical_release_candidates = []
        self.accepted_q_releases = []
        self.log_pending = ''
        self.log_bytes = 0
        self.live_bot_paths = set()
        self.throat_baseline = {}
        self.throat_counts = {}
        self.chests = {}
        self.chest_seen_at = None
        self.chest_requested = set()
        self.native_loading_exit_seen = False
        self.positions = dict(samples=0, max_min_pair_distance_cm=0.,
            separated_loaded_samples=0, simultaneous_loaded_samples=0)
        match = re.search(r'-abslog=(?:"([^"]+)"|(\S+))', u.SystemLibrary.get_command_line(), re.I)
        require(match is not None, 'The owned Editor must supply its actual -AbsLog=E:...')
        self.live_log = Path(match.group(1) or match.group(2)).resolve()
        require(self.live_log.drive.upper() == 'E:' and self.live_log.is_file(), 'Owned Editor log is unavailable on E:')
        self.log_offset = self.live_log.stat().st_size
        self.report.update(scope=__doc__, source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            game_budget_seconds=self.game_budget, wall_budget_seconds=self.wall_budget,
            native_release_log=str(self.live_log), initial_perk_requests=False,
            select_normal_first_perk=False,
            towers=self.towers, accepted_native_q_releases=self.accepted_q_releases,
            physical_release_candidates=self.physical_release_candidates,
            throat_food_swallowed=self.throat_counts, stage_chests=self.chests,
            pair_separation=self.positions, six_piece_tower_observed=False,
            limitations=[
                'Single authority PIE with three AI teammates; network replication, latency and human comfort are not established.',
                'Only actual observed tower sizes are reported. Missing natural six-piece stacks are not claimed as tested capacity.',
                'A physical detach candidate alone cannot prove Q; accepted manual releases require the guarded native log for an owned bot pawn.',
                'FoodSwallowed is the actual throat counter. It is not inferred from food disappearing, a throw request, points or phase changes.',
                'The stationary human is not moved to rewards. A natural chest can be observed without claiming it; later 5/8 portions can remain blocked by that genuine gate.',
                'Preview uses one normal camera-orbit/scroll request toward the saved tongue centre. The human and camera rig are not relocated; native framing may still exclude distant bots. Images require visual review.',
                'No complete 3/5/8 meal, full level, cook/package, microphone, multiplayer or performance test.'])

    def launch_bots(self):
        require(self.native_loading_exit_seen and self.preparation.is_ready() and self.pc.is_world_presentation_ready(),
            'Native loading and ACK barrier must finish before starting the ordinary bot session')
        require(not self.pc.is_reward_menu_open(), 'A removed initial perk unexpectedly opened before bots')
        require(not prop(prop(prop(self.pc, 'player_state'), 'perks'), 'active_perks'),
            'The natural human start received a free perk before any chest')
        self.log_offset = self.live_log.stat().st_size
        super().launch_bots()
        self.throat_baseline = {path(actor): int(prop(actor, 'food_swallowed')) for actor in self.actors(u.MCThroat)}
        for bot in self.actors(u.MCPlaytestBotController):
            require(not prop(prop(prop(bot, 'player_state'), 'perks'), 'active_perks'),
                'An ordinary fresh bot received a free starting perk')
        if self.preview:
            self.frame_saved_arena()
        self.report['throat_baseline'] = self.throat_baseline
        self.report['native_loading_ack_evidence'] = dict(
            resource_ready=True, world_presentation_ready=True, loading_overlay_removed=True,
            director_left_training_naturally=True,
            direct_ack_rpc_called=False,
            interpretation='The authoritative ordinary startup exited Training, whose native guard requires the existing human ACK. No private ACK field is exposed or changed.')

    def frame_saved_arena(self):
        """One ordinary mouse-orbit/wheel equivalent, never a view-target override."""
        completed_calls = []
        try:
            require(hasattr(self.hero, 'apply_camera_orbit_input') and hasattr(self.hero, 'zoom_camera'),
                'Public ordinary camera handlers are unavailable')
            tongues = self.actors(u.MCTongue)
            require(len(tongues) == 1, 'Saved tongue framing target is ambiguous')
            origin, extent = tongues[0].get_actor_bounds(False)
            require(extent.x > 50. and extent.y > 50., 'Saved tongue bounds are not usable')
            camera = prop(self.hero, 'camera')
            view = camera.get_world_rotation()
            pivot = self.hero.get_actor_location() + u.Vector(0., 0., float(prop(self.hero, 'camera_orbit_height')))
            desired = u.MathLibrary.find_look_at_rotation(pivot, origin)
            requested_pitch = float(globals().get('MC_CHEST_ONLY_BOTS_CAMERA_PITCH', -16.))
            require(math.isfinite(requested_pitch), 'Requested camera pitch must be finite')
            desired.pitch = requested_pitch
            sensitivity = max(.02, min(1., float(prop(self.hero, 'camera_orbit_sensitivity'))))
            yaw_delta = (float(desired.yaw)-float(view.yaw)+180.) % 360. - 180.
            delta = u.Vector2D(yaw_delta/sensitivity, (float(desired.pitch)-float(view.pitch))/sensitivity)
            before = dict(position_cm=vector(camera.get_world_location()), rotation=str(view))
            self.hero.apply_camera_orbit_input(delta)
            completed_calls.append('ApplyCameraOrbitInput')
            self.hero.zoom_camera(-20.)
            completed_calls.append('ZoomCamera')
            self.report['camera_framing'] = dict(method='Ordinary ApplyCameraOrbitInput / ZoomCamera handlers, once',
                completed_calls=completed_calls,
                tongue=path(tongues[0]), target_cm=vector(origin), bounds_half_extent_cm=vector(extent),
                requested_pitch=float(desired.pitch), requested_yaw=float(desired.yaw),
                orbit_input=[float(delta.x), float(delta.y)], scroll_input=-20.,
                native_zoom_limit_cm=1600, camera_before=before, pawn_position_cm=vector(self.hero.get_actor_location()),
                pawn_relocated=False, camera_rig_or_view_target_replaced=False)
        except Exception:
            # Framing is optional presentation evidence. Preserve gameplay and
            # report the real limitation instead of changing any camera property.
            self.report['camera_framing'] = dict(applied=bool(completed_calls), completed_calls=completed_calls, limitation=traceback.format_exc(),
                pawn_relocated=False, camera_rig_or_view_target_replaced=False)

    def capture(self, name, sequence=False):
        super().capture(name, sequence)
        entry = self.report['frames' if sequence else 'screenshots'][-1]
        camera = prop(self.hero, 'camera') if valid(self.hero) else None
        if valid(camera):
            entry['camera_position_cm'] = vector(camera.get_world_location())
            entry['camera_rotation'] = str(camera.get_world_rotation())
            entry['camera_transform'] = str(camera.get_world_transform())

    def read_release_log(self):
        require(self.live_log.stat().st_size >= self.log_offset, 'The owned Editor log was replaced or truncated')
        with self.live_log.open('rb') as stream:
            stream.seek(self.log_offset)
            chunk = stream.read()
            self.log_offset = stream.tell()
        self.log_bytes += len(chunk)
        require(self.log_bytes < 32*1024*1024, 'Unexpectedly large log tail; stop rather than scan unrelated output')
        lines = (self.log_pending + chunk.decode('utf-8', errors='replace')).splitlines(keepends=True)
        self.log_pending = ''
        if lines and not lines[-1].endswith(('\n', '\r')):
            self.log_pending = lines.pop()
        for line in lines:
            match = RELEASE.search(line)
            if not match:
                continue
            hero, food, remaining, manual, game = match.groups()
            if hero not in self.live_bot_paths:
                continue
            require(int(manual) == 1, 'Native bot Q detached food without its manual-throw flag')
            entry = dict(hero=hero, food=food, remaining_pieces=int(remaining), manual=True,
                game_seconds=float(game), observed_wall_seconds=time.monotonic()-self.started)
            self.accepted_q_releases.append(entry)

    def observe_delivery(self):
        controllers = self.actors(u.MCPlaytestBotController)
        require(len(controllers) == 3, 'The ordinary three-bot roster changed')
        player = prop(self.pc, 'player_state')
        require(not player.is_only_a_spectator(), 'This is a human coop probe, not spectator observation')
        require(int(prop(player, 'pending_level_choices')) == 0, 'The human received a removed XP/starting choice')
        progression = prop(self.gs, 'progression')
        require(valid(progression) and int(prop(progression, 'total_experience')) == 0
            and int(prop(progression, 'experience_in_level')) == 0,
            'The retired experience economy awarded XP during normal bot work')
        stage = enum_name(prop(self.sequence, 'stage'))
        self.seen_stages.add(stage)
        require(stage != 'FIRSTPERK', 'The removed initial-perk stage became active')
        phase = enum_name(prop(self.gs, 'phase'))
        require(phase != 'LOST', 'The unmodified ordinary run lost')
        require(self.preparation.is_ready(), 'Resource readiness regressed')
        require(not bool(prop(self.gs, 'dev_manual_events')), 'An unrelated manual event override interrupted this run')
        session = self.actors(u.MCPlaytestSession)
        require(len(session) == 1 and path(session[0]) == self.bot_session_path, 'Owned bot session was replaced')
        sample_bots = []
        for bot in controllers:
            pawn = bot.get_controlled_pawn()
            require(valid(pawn), 'A bot has no live pawn; respawn is not forced')
            identity = prop(bot, 'player_state')
            require(int(prop(identity, 'pending_level_choices')) == 0, 'A bot received a removed level choice')
            body = prop(pawn, 'food_collection')
            pieces = [piece for piece in prop(body, 'pieces') if valid(piece)]
            count = len(pieces)
            maximum = int(prop(body, 'max_pieces'))
            require(maximum == 6 and count <= maximum, 'Unboosted natural tower exceeded the ordinary six-piece capacity')
            pawn_path = path(pawn)
            self.live_bot_paths.add(pawn_path)
            name = str(identity.get_player_name())
            record = self.towers.setdefault(name, dict(max_pieces=0, capacity=maximum, pickup_growth_events=0,
                pawn_paths=[], first_pickup_game=None))
            record['max_pieces'] = max(record['max_pieces'], count)
            if pawn_path not in record['pawn_paths']:
                record['pawn_paths'].append(pawn_path)
            current = {path(piece): piece for piece in pieces}
            previous = self.previous_pieces.get(pawn_path, {})
            if len(current) > len(previous):
                record['pickup_growth_events'] += 1
                if record['first_pickup_game'] is None:
                    record['first_pickup_game'] = self.now()
            for removed, piece in previous.items():
                if removed in current or not valid(piece):
                    continue
                # This is only observational evidence of physical release. A
                # spill/collision can also detach food; Q proof comes from C++.
                if enum_name(prop(piece, 'phase')) == 'FREE' and not valid(prop(piece, 'stack_carrier')):
                    velocity = prop(piece, 'body').get_physics_linear_velocity()
                    if velocity.length() > 200.:
                        self.physical_release_candidates.append(dict(hero=pawn_path, food=removed,
                            game_seconds=self.now(), position_cm=vector(piece.get_actor_location()),
                            velocity_cm_s=vector(velocity)))
            self.previous_pieces[pawn_path] = current
            sample_bots.append(dict(controller=path(bot), hero=pawn_path, name=name,
                position_cm=vector(pawn.get_actor_location()), pieces=list(current), count=count,
                alive=float(prop(prop(prop(pawn, 'status'), 'state'), 'health')) > 0.))
        for throat in self.actors(u.MCThroat):
            key = path(throat)
            require(key in self.throat_baseline, 'An unexpected throat actor appeared during observation')
            value = int(prop(throat, 'food_swallowed'))
            require(value >= self.throat_baseline[key], 'The real throat delivery counter reset')
            self.throat_counts[key] = dict(initial=self.throat_baseline[key], current=value,
                delivered=value-self.throat_baseline[key])
        self.read_release_log()
        wall = time.monotonic()
        if wall >= self.next_sample_at:
            self.next_sample_at = wall + .5
            distances = [math.dist(a['position_cm'][:2], b['position_cm'][:2])
                for index, a in enumerate(sample_bots) for b in sample_bots[index+1:]]
            self.positions['samples'] += 1
            self.positions['max_min_pair_distance_cm'] = max(self.positions['max_min_pair_distance_cm'], min(distances, default=0.))
            loaded = [row for row in sample_bots if row['count']]
            if len(loaded) >= 2:
                self.positions['simultaneous_loaded_samples'] += 1
                loaded_distances = [math.dist(a['position_cm'][:2], b['position_cm'][:2])
                    for index, a in enumerate(loaded) for b in loaded[index+1:]]
                if min(loaded_distances) >= 150.:
                    self.positions['separated_loaded_samples'] += 1
            self.report['samples'].append(dict(game_seconds=self.now(), wall_seconds=wall-self.started,
                elapsed_run_game_seconds=self.now()-self.bot_start_game, native_stage=stage, native_phase=phase,
                portion_number=int(prop(self.sequence, 'meal_portion_number')),
                portion_target=int(prop(self.sequence, 'meal_portion_target')),
                portion_spawned=int(prop(self.sequence, 'meal_portion_spawned')),
                bot_pawns=sample_bots, pair_distances_xy_cm=distances,
                throat_counts={key: dict(value) for key, value in self.throat_counts.items()}))
        self.report['six_piece_tower_observed'] = any(row['max_pieces'] == 6 for row in self.towers.values())
        if self.preview and self.preview_at is None and self.accepted_q_releases:
            self.preview_at = wall
            self.report['preview_scope'] = 'Up to sixteen real wall seconds beginning when the first accepted manual Q release from an owned bot is observed in the native log; native gameplay camera after one ordinary orbit/scroll request. The first throw can already be in flight at capture start.'
        if self.preview_at is not None and self.preview_at <= wall <= self.preview_at+16. and wall >= self.preview_frame_at:
            if len(self.report['frames']) < 64 and self.frame_bytes < 128*1024*1024:
                self.preview_frame_at = wall+.25
                self.capture('%05d.png' % len(self.report['frames']), sequence=True)
        self.read_csv()
        return self.observe_chests()

    def observe_chests(self):
        landed = False
        for chest in self.actors(u.MCRewardChest):
            if not bool(prop(chest, 'team_stage_booster')):
                continue
            key, state = path(chest), enum_name(prop(chest, 'stage'))
            row = self.chests.setdefault(key, dict(first_seen_game_seconds=self.now(), states=[], claimed=False))
            if self.chest_seen_at is None:
                self.chest_seen_at = time.monotonic()
                self.milestone('first_natural_stage_reward_spawned')
            if state not in row['states']:
                row['states'].append(state)
            row['latest_state'] = state
            row['opening_player'] = path(prop(chest, 'opening_player'))
            row['distance_to_stationary_human_cm'] = math.dist(vector(chest.get_actor_location()), vector(self.hero.get_actor_location()))
            if state == 'OPEN' and prop(chest, 'opening_player') == self.hero:
                require(self.pc.is_reward_menu_open(), 'A naturally opened human chest has no reward menu')
                if key not in self.chest_requested:
                    self.report['requests'].append(dict(method='ChooseRewardPerk', choice_index=0,
                        source='Naturally approached human stage chest', chest=key,
                        game_seconds=self.now(), wall_seconds=time.monotonic()-self.started))
                    self.chest_requested.add(key)
                    self.pc.choose_reward_perk(0)
                # Only exhausted server state proves acceptance, not the request.
                row['claimed'] = enum_name(prop(chest, 'stage')) == 'EXHAUSTED'
            if state == 'EXHAUSTED':
                row['claimed'] = True
            landed |= state in ('LANDED', 'OPEN', 'EXHAUSTED')
        if self.pc.is_reward_menu_open():
            require(any(row.get('opening_player') == path(self.hero) and row.get('latest_state') == 'OPEN'
                for row in self.chests.values()) or self.pc.is_waiting_for_perk_team(),
                'A free initial/XP perk menu appeared without a genuine open chest')
        return landed

    def complete_narrow_checks(self, reason):
        self.console('MC.Bots.Report')
        self.read_csv(force=True)
        self.read_release_log()
        self.check('native_loading_ack_precedes_bot_start', self.native_loading_exit_seen, 'Native initial loading barrier was not observed')
        self.check('no_starting_perk_or_xp_choices', True, '')
        self.check('three_ordinary_ai_participants', len(self.csv_latest) == 3 and all(int(row['participants']) == 4
            for row in self.csv_latest.values()), 'The native CSV does not confirm the expected human plus three-bot team')
        self.check('real_multi_piece_tower_observed', any(row['max_pieces'] >= 3 for row in self.towers.values()),
            'No natural tower larger than the previous one/two-piece behaviour was observed')
        self.check('accepted_native_q_release', bool(self.accepted_q_releases), 'No accepted ordinary manual Q release was recorded')
        self.check('real_throat_delivery_counter_increased', any(row['delivered'] > 0 for row in self.throat_counts.values()),
            'Actual MCThroat.FoodSwallowed did not increase')
        self.check('ordinary_bot_movement', len(self.csv_metrics) == 3 and all(row['ordinary_distance_cm'] > 100.
            for row in self.csv_metrics.values()), 'Ordinary locomotion was not confirmed for every bot')
        self.report.update(result='natural_chest_only_bot_delivery_observed', stop_reason=reason,
            stage_chest_claims_verified=sum(bool(row['claimed']) for row in self.chests.values()),
            chest_scope='Only naturally spawned/landed/open/exhausted states above were observed. Later portions and a stationary-human approach are not forced.',
            position_scope='Pair distances are observational samples, not a universal no-crowding guarantee.',
            observed_capacity_scope='Actual maximum per bot is reported; natural six-piece observation is a separate flag.')
        self.run_terminal = True
        self.milestone('narrow_delivery_observation_finished')
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
            if wall-self.started >= self.wall_budget:
                if self.bot_session_path:
                    self.complete_narrow_checks('wall_budget_reached; any later meal/chest checks are deferred')
                    self.begin_finish()
                    return
                raise RuntimeError('Native startup did not finish within the wall budget')
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
                require(not self.pc.is_reward_menu_open(), 'An initial perk menu appeared instead of direct meal startup')
                stage = row['native_stage']
                if not row['ready'] or valid(overlay) or not self.pc.is_world_presentation_ready() or stage in (None, 'TRAINING'):
                    return
                require(stage != 'FIRSTPERK', 'The removed initial-perk stage is still active')
                self.native_loading_exit_seen = True
                self.check('normal_gameplay_uncovered_without_starting_perk', True, '')
                self.milestone('normal_gameplay_after_actual_loading_barrier')
                self.launch_bots()
            elif self.phase == 'observe_coop':
                landed_chest = self.observe_delivery()
                if landed_chest:
                    self.complete_narrow_checks('first_stage_chest_naturally_landed; stationary human not moved')
                elif self.now()-self.bot_start_game >= self.game_budget:
                    self.complete_narrow_checks('game_budget_reached; later meal/chest checks deferred')
                elif wall >= self.next_report_at:
                    self.next_report_at = wall+5.
                    self.write()
            elif self.phase == 'capture_flush' and wall-self.phase_wall >= .3:
                self.begin_finish()
        except Exception:
            self.begin_finish(traceback.format_exc())
        finally:
            self.busy = False


def main():
    for key in (KEY, bots.KEY, bots.support.KEY, helpers.KEY, 'mc_small_mint_solo'):
        prior = getattr(builtins, key, None)
        require(prior is None or getattr(prior, 'done', True), 'Another owned probe is active: '+key)
    probe = ChestOnlyBotsSolo()
    setattr(builtins, KEY, probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback = u.register_slate_post_tick_callback(probe.tick)


if __name__ == '__main__':
    main()
