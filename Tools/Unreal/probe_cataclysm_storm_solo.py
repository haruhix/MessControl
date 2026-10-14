"""One owned solo storm episode, with genuine brush rowing and native log contact.

Run in idle L_Mouth after the cold Editor integration. MC_STORM_FOCUS is
'combined' (default), 'rowing', or 'debris'; never starts a series of PIE runs.
Position fixtures are disclosed. No force/damage/brace assignment, clock jumps,
health/reserve edits, asset saves or network clients. Cancel through
builtins.mc_cataclysm_storm_solo.finish('Stopped by operator').
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

KEY = 'mc_cataclysm_storm_solo'
ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_STORM_OUTPUT', Path(u.Paths.project_saved_dir()) / 'Checks/CataclysmStormSolo')).resolve()
FOCUS = str(globals().get('MC_STORM_FOCUS', 'combined')).lower()
if FOCUS not in ('combined', 'rowing', 'debris'):
    raise RuntimeError('MC_STORM_FOCUS must be combined, rowing, or debris')
support = runpy.run_path(str(ROOT / 'Tools/Unreal/probe_cataclysm_solo.py'),
    run_name='cataclysm_storm_support', init_globals={'MC_CATACLYSM_MODE': 'rafts',
        'MC_CATACLYSM_OUTPUT': OUTPUT, 'MC_CATACLYSM_CAPTURE_HZ': globals().get('MC_STORM_CAPTURE_HZ', 6)})
Parent, prop, vec, valid, path, enum_name = (
    support[name] for name in ('CataclysmSolo', 'prop', 'vec', 'valid', 'path', 'enum_name'))


def length(vector):
    return math.sqrt(vector.x**2 + vector.y**2 + getattr(vector, 'z', 0.)**2)


class CataclysmStormSolo(Parent):
    def __init__(self):
        super().__init__()
        self.liquid = self.bot = self.log = self.scheduler = None
        self.scheduler_tick = None
        self.event_tick = None
        self.episode_started_at = None
        self.menu_opened = False
        self.bot_direction_at = 0.
        self.log_before = None
        self.log_observations = []
        self.contact_mode = None
        self.position_before = None
        self.report.update(mode='storm', focus=FOCUS, clips={}, frames=[], screenshots=[], samples=[], checks={},
            scope='One owned standalone authority PIE; real F3 CataclysmRafts (64), normal brush-rowing input and native event debris',
            clip_labels={'storm_brush_rowing': 'Шторм и гребля щётками',
                         'debris_rail': 'Удар обломка: держимся за борт',
                         'debris_unbraced': 'Удар обломка без захвата борта'},
            limitations=['The local pawn and one nonspectator allied AI pawn are position fixtures; neither is a network client.',
                'The actual F3 event runs its own water, drifting boats and bounded incoming-debris scheduler. The unrelated single-day scheduler is paused.',
                'During the short rowing measurement only, the Cataclysm event actor tick is paused before its first debris spawn, avoiding an unrelated knockoff during vector measurement. Water, raft, CMC and game clocks continue normally. Native event tick is restored before both contact trials.',
                'Flood timing is a PIE-only 3s fill / 26s hold / 3s drain. The episode stops within 40 game seconds after F3 and 240 wall seconds.',
                'Two rowing forces are inferred from the actual summed native RowingDirection and its source formula. Recorded velocities also include gusts, drag, banks and real debris.',
                'The allied authority AI pawn uses SetRowingInputHeld and the same ServerRowingDirection input RPC at 10Hz; no raft force or rowing-state assignment is used.',
                'Debris contact uses an already spawned native event log, with one disclosed upstream position fixture. Its normal warning, floating, contact and retirement paths remain active.',
                'Both contact preparations hold the real rail against unrelated incoming logs; the unbraced trial releases native RMB immediately before its selected log approach.',
                'No direct SetRowingInput, ApplyDebrisImpulse, ApplyHit, ConfigureDebris, damage helper, brace assignment or physics state assignment is used.',
                'Default swimming reserve and drowning grace are untouched; natural depletion/death remains the separate focused drowning probe.',
                'Rendered brush poses, storm readability and splashes need visual review; telemetry does not establish visual quality.'])
        for source in ('Source/MessControl/Public/MCCataclysmDebris.h', 'Source/MessControl/Private/MCCataclysmDebris.cpp',
                       'Source/MessControl/Public/MCCoffeeFlood.h', 'Source/MessControl/Private/MCCoffeeFlood.cpp',
                       'Source/MessControl/Private/MCCataclysmStormMath.h', 'Source/MessControl/Public/MCToothCharacter.h',
                       'Source/MessControl/Private/MCToothAnimInstance.cpp', 'Source/MessControl/Private/MCInventoryComponent.cpp',
                       'Source/MessControl/Private/MCGripComponent.cpp', 'Source/MessControl/Private/MCToothCharacter.cpp',
                       'Tools/Unreal/probe_cataclysm_storm_solo.py'):
            self.report['source_sha256'][source] = hashlib.sha256((ROOT / source).read_bytes()).hexdigest()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output / 'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT / 'Report_storm.json').write_text(data, encoding='utf-8')

    def rowing(self, pawn, held):
        pawn.call_method('SetRowingInputHeld', args=(bool(held),))
        self.report.setdefault('input_actions', []).append(dict(time=float(u.GameplayStatics.get_time_seconds(self.world)),
            pawn=path(pawn), action='SetRowingInputHeld', held=bool(held)))

    def bracing(self, held):
        self.hero.call_method('SetBraceInputHeld', args=(bool(held),))
        self.brace = bool(held)
        self.report.setdefault('input_actions', []).append(dict(time=float(u.GameplayStatics.get_time_seconds(self.world)),
            pawn=path(self.hero), action='SetBraceInputHeld', held=bool(held)))

    def base(self, pawn):
        return pawn.get_movement_component().call_method('GetMovementBaseObject', args=())

    def standing(self, pawn):
        return enum_name(prop(pawn, 'tooth_physics').get_body_state()) == 'STANDING'

    def rail(self):
        grip = prop(self.hero, 'grip')
        return grip.is_bracing() and grip.brace_target() == self.raft and self.base(self.hero) == prop(self.raft, 'deck')

    def make_bot(self):
        controller = self.spawn(u.MCPlaytestBotController, u.Transform())
        controller.set_actor_tick_enabled(False)
        identity = prop(controller, 'player_state')
        self.require(valid(identity) and not identity.call_method('IsSpectator', args=())
                     and not identity.call_method('IsOnlyASpectator', args=()), 'Second rower needs a real nonspectator PlayerState')
        mode = u.GameplayStatics.get_game_mode(self.world)
        self.bot = self.spawn(prop(mode, 'default_pawn_class'), u.Transform(location=self.hero.get_actor_location() + u.Vector(400, 0, 120)), controller)
        controller.possess(self.bot)
        self.require(prop(self.bot, 'player_state') == identity, 'Second rower did not inherit its native identity')
        self.bots.append(self.bot)
        self.bot_controllers.append(controller)
        self.report['second_rower'] = dict(pawn=path(self.bot), controller=path(controller),
            player_state=path(identity), ai_decisions_tick=False, network_client=False)

    def place_crew(self, edge=False):
        pose = prop(self.raft, 'deck').get_world_transform()
        direction = self.liquid.storm_stream_direction()
        local = pose.inverse_transform_location(self.raft.get_actor_location() + direction * 100.)
        if edge:
            if abs(local.x) >= abs(local.y):
                x, y = (-108. if local.x > 0. else 108.), 0.
            else:
                x, y = 0., (-70. if local.y > 0. else 70.)
        else:
            x, y = -45., -32.
        foot = self.deck_point(x, y)
        facing = u.MathLibrary.find_look_at_rotation(foot, self.raft.get_actor_location()).yaw
        self.place_pawn(self.hero, foot, facing)
        if valid(self.bot):
            # The edge observer and partner must not overlap their two 34 cm
            # capsules. Put the partner downstream across the same deck.
            bx, by = (45., 32.)
            if edge:
                if abs(x) > abs(y): bx, by = (-x * .55, 0.)
                else: bx, by = (0., -y * .55)
            self.place_pawn(self.bot, self.deck_point(bx, by), facing)

    def camera_for_stage(self):
        center = self.raft.get_actor_location()
        direction = self.liquid.storm_stream_direction()
        side = u.Vector(-direction.y, direction.x, 0.)
        if self.phase.startswith('contact') or self.phase.startswith('wait_log'):
            location = center - direction * 400. - side * 360. + u.Vector(0, 0, 340.)
        else:
            location = center + direction * 250. - side * 350. + u.Vector(0, 0, 300.)
        self.frame_camera(location, center + u.Vector(0, 0, 40.))
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 85.)

    def sample(self, now):
        if not valid(self.raft) or not valid(self.liquid):
            return
        position = self.raft.get_actor_location()
        current = self.liquid.storm_current_at(position)
        direction = prop(self.raft, 'rowing_direction')
        movement = self.hero.get_movement_component()
        foot = self.hero.get_actor_location() - u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height())
        debris = []
        for actor in prop(self.event, 'floating_debris'):
            if valid(actor):
                debris.append(dict(path=path(actor), position_cm=vec(actor.get_actor_location()),
                    velocity_cm_s=vec(prop(actor, 'drift_velocity')), warning=actor.is_warning(),
                    impacts=int(prop(actor, 'impact_count')), player_impacts=int(prop(actor, 'player_impact_count')),
                    raft_impacts=int(prop(actor, 'raft_impact_count')), braced_contacts=int(prop(actor, 'braced_contact_count'))))
        self.report['samples'].append(dict(game_seconds=now, phase=self.phase,
            event_phase=enum_name(prop(self.event, 'phase')), water_phase=enum_name(self.liquid.get_phase()),
            storm_current_cm_s2=vec(current), storm_strength=float(self.liquid.storm_strength()),
            macro_wave_height_cm=float(self.liquid.storm_wave_height_at(position)),
            raft_cm=vec(position), raft_velocity_cm_s=vec(self.raft.get_velocity()),
            rowing_count=int(prop(self.raft, 'rowing_count')), rowing_direction=[direction.x, direction.y],
            rowing_vector_magnitude=length(direction), rowing_players=[path(p) for p in prop(self.raft, 'rowing_players')],
            hero_rowing=bool(self.hero.is_rowing()), bot_rowing=bool(self.bot.is_rowing()) if valid(self.bot) else False,
            hero_cm=vec(self.hero.get_actor_location()), hero_foot_in_deck_cm=vec(prop(self.raft, 'deck').get_world_transform().inverse_transform_location(foot)),
            hero_body_state=enum_name(prop(self.hero, 'tooth_physics').get_body_state()),
            hero_health=float(prop(prop(self.hero, 'status'), 'state').health), movement_base=path(self.base(self.hero)),
            movement_mode=enum_name(prop(movement, 'movement_mode')), rail_contact=self.rail(),
            brace_target=path(prop(self.hero, 'grip').brace_target()), brace_active=bool(prop(self.hero, 'grip').is_bracing()),
            requested_brace=self.brace,
            swim_reserve=float(movement.get_swim_energy()), deep_water=bool(movement.is_in_deep_water()),
            input_ignored=bool(self.pc.is_move_input_ignored()), native_debris_spawned=int(prop(self.event, 'debris_spawned')),
            debris=debris))

    def stage_samples(self, stage):
        return [entry for entry in self.report['samples'] if entry['phase'] == stage]

    def restore_event_tick(self):
        if self.event_tick is not None and valid(self.event):
            self.event.set_actor_tick_enabled(self.event_tick)
            self.report['rowing_scheduler_isolation']['restored_at'] = float(u.GameplayStatics.get_time_seconds(self.world))
            self.event_tick = None

    def begin_contact(self, braced):
        self.rowing(self.hero, False)
        self.rowing(self.bot, False)
        self.bracing(False)
        self.place_crew(edge=True)
        # Native event logs keep arriving during setup. Protect both trials
        # while the crew settles; release through real input for the unbraced
        # trial immediately before its selected log begins the recorded run.
        self.bracing(True)
        self.contact_mode = 'rail' if braced else 'unbraced'
        self.log = None
        self.log_observations = []
        self.stage('settle_contact_' + self.contact_mode)

    def select_log(self, now):
        for actor in prop(self.event, 'floating_debris'):
            if not valid(actor) or actor == self.log or int(prop(actor, 'impact_count')) != 0:
                continue
            age = now - float(prop(actor, 'spawned_at'))
            if actor.is_warning() or age > 5.5:
                continue
            direction = self.liquid.storm_stream_direction()
            position = self.raft.get_actor_location() - direction * 360.
            position.z = actor.get_actor_location().z
            self.log = actor
            self.log_before = dict(path=path(actor), original_cm=vec(actor.get_actor_location()), fixture_cm=vec(position),
                native_spawned_at=float(prop(actor, 'spawned_at')), age_seconds=age,
                health_before=float(prop(prop(self.hero, 'status'), 'state').health),
                hero_before_cm=vec(self.hero.get_actor_location()), raft_before_cm=vec(self.raft.get_actor_location()))
            if self.contact_mode == 'unbraced': self.bracing(False)
            actor.set_actor_location(position, False, True)
            self.report.setdefault('native_log_position_fixtures', []).append(dict(time=now, mode=self.contact_mode, **self.log_before))
            self.begin_clip('debris_' + self.contact_mode, 6.)
            self.stage('contact_' + self.contact_mode)
            return True
        return False

    def finish_contact(self, now):
        self.require(self.log_observations, 'Native log produced no observations')
        impacts = [entry for entry in self.log_observations if entry['impacts'] > 0]
        if not impacts:
            return False
        impact = impacts[0]
        first = self.log_observations[0]
        travelled = math.dist(first['position_cm'][:2], impact['position_cm'][:2])
        self.require(travelled >= 50., 'Contact did not include a meaningful visible incoming approach')
        health = float(prop(prop(self.hero, 'status'), 'state').health)
        self.require(abs(health - self.log_before['health_before']) < .001, 'Log contact unexpectedly changed health')
        self.report.setdefault('native_contacts', {})[self.contact_mode] = dict(fixture=self.log_before,
            approach_distance_cm=travelled, actual_contact=impact, samples=list(self.log_observations), health_after=health)
        self.report['checks']['native_log_approach_' + self.contact_mode] = True
        self.report['checks']['native_log_contact_zero_hp_' + self.contact_mode] = True
        self.stage('after_contact_' + self.contact_mode)
        return True

    def update_episode(self, now, elapsed):
        if self.phase == 'wait_hold':
            self.require(elapsed < 24., 'Natural raft branch/holding phase did not arrive')
            if enum_name(prop(self.event, 'phase')) != 'FLOODING':
                return
            self.liquid = prop(self.event, 'liquid')
            if not valid(self.liquid) or enum_name(self.liquid.get_phase()) != 'HOLDING':
                return
            rafts = [r for r in prop(self.event, 'rafts') if valid(r)]
            self.require(len(rafts) == 2 and self.liquid.is_cataclysm_storm(), 'Expected the native storm with exactly two rafts')
            if FOCUS != 'debris':
                self.require(not any(valid(d) for d in prop(self.event, 'floating_debris')), 'Rowing isolation must start before any incoming log; existing native hazards are not removed')
                self.event_tick = self.event.is_actor_tick_enabled()
                self.event.set_actor_tick_enabled(False)
                self.report['rowing_scheduler_isolation'] = dict(actor=path(self.event), original_tick=self.event_tick,
                    paused_at=now, reason='Measure rowing vectors without an unrelated deck-loss cancellation; water, raft and CMC continue ticking')
            tongue_center, _ = self.tongue.get_actor_bounds(False)
            self.raft = min(rafts, key=lambda r: length(r.get_actor_location() - tongue_center))
            self.make_bot()
            self.place_crew()
            self.report['checks']['real_f3_64_starts_storm_rafts'] = True
            self.stage('settle_crew')
            self.begin_clip('storm_brush_rowing', 8.)
        elif self.phase == 'settle_crew' and elapsed >= .7:
            self.require(self.base(self.hero) == prop(self.raft, 'deck') and self.base(self.bot) == prop(self.raft, 'deck'), 'Crew did not naturally settle on walkable deck')
            if FOCUS == 'debris':
                self.begin_contact(True)
            else:
                self.position_before = self.raft.get_actor_location()
                self.stage('idle_drift')
        elif self.phase == 'idle_drift' and elapsed >= 1.4:
            entries = self.stage_samples('idle_drift')
            self.require(entries and all(length(u.Vector(*e['storm_current_cm_s2'])) > 10. for e in entries), 'Holding current stopped between old shock fronts')
            delta = self.raft.get_actor_location() - self.position_before
            displacement = math.hypot(delta.x, delta.y)
            self.require(displacement >= 5., 'Water did not move the native raft')
            self.report['idle_drift_distance_cm'] = displacement
            self.report['checks']['hold_current_continuous_and_raft_drifts'] = True
            self.rowing(self.hero, True)
            self.stage('one_brush')
        elif self.phase == 'one_brush' and elapsed >= 1.5:
            entries = self.stage_samples('one_brush')
            stable = [e for e in entries if e['rowing_count'] == 1 and e['hero_rowing']]
            self.require(len(stable) >= 8 and any(.9 <= e['rowing_vector_magnitude'] <= 1.1 for e in stable), 'One actual held rowing input did not produce one native force vector')
            self.report['checks']['one_brush_native_rowing_vector'] = True
            self.rowing(self.bot, True)
            self.stage('two_brushes')
        elif self.phase == 'two_brushes' and elapsed >= 1.5:
            entries = self.stage_samples('two_brushes')
            stable = [e for e in entries if e['rowing_count'] == 2 and e['hero_rowing'] and e['bot_rowing']]
            self.require(len(stable) >= 8 and any(e['rowing_vector_magnitude'] >= 1.8 for e in stable), 'Two native brush-rowing inputs did not sum')
            self.report['checks']['two_brushes_sum_native_rowing_vectors'] = True
            self.rowing(self.bot, False)
            self.bracing(True)
            self.stage('rmb_stops_rowing')
        elif self.phase == 'rmb_stops_rowing' and elapsed >= .35:
            self.require(not self.hero.is_rowing() and int(prop(self.raft, 'rowing_count')) == 0, 'RMB failed to stop brush rowing')
            self.report['checks']['rmb_stops_rowing'] = True
            self.bracing(False)
            self.rowing(self.hero, True)
            self.stage('menu_prepare')
        elif self.phase == 'menu_prepare' and elapsed >= .25:
            self.require(self.hero.is_rowing(), 'Rowing did not restart before menu checkpoint')
            self.pc.toggle_dev_panel() # Native F3-bound menu action; no input-ignore fixture.
            self.menu_opened = True
            self.stage('menu_stops_rowing')
        elif self.phase == 'menu_stops_rowing' and elapsed >= .3:
            self.require(self.pc.is_move_input_ignored() and not self.hero.is_rowing()
                and int(prop(self.raft, 'rowing_count')) == 0, 'Native menu failed to cancel rowing')
            self.report['checks']['native_menu_stops_rowing'] = True
            self.pc.toggle_dev_panel()
            self.menu_opened = False
            self.stage('offdeck_prepare')
        elif self.phase == 'offdeck_prepare' and elapsed >= .3:
            self.rowing(self.hero, True)
            self.require(self.hero.is_rowing(), 'Rowing did not restart before leaving deck')
            self.place_pawn(self.hero, self.water_foot(-230.), self.raft.get_actor_rotation().yaw, True)
            self.stage('offdeck_stops_rowing')
        elif self.phase == 'offdeck_stops_rowing' and elapsed >= .3:
            self.require(not self.hero.is_rowing() and int(prop(self.raft, 'rowing_count')) == 0, 'Leaving the deck failed to clear rowing')
            self.report['checks']['offdeck_stops_rowing'] = True
            self.restore_event_tick()
            if FOCUS == 'rowing': self.stage('storm_flush')
            else: self.begin_contact(True)
        elif self.phase.startswith('settle_contact_'):
            self.require(elapsed < 3., 'Contact observer failed to settle on deck')
            if elapsed >= .6 and self.standing(self.hero) and self.base(self.hero) == prop(self.raft, 'deck'):
                self.bracing(True)
                self.stage('wait_log_' + self.contact_mode)
        elif self.phase.startswith('wait_log_'):
            self.require(elapsed < 6., 'No fresh native event log available for the focused contact fixture')
            self.require(elapsed < 2.5 or self.rail(), 'Actual RMB did not acquire the real raft rail for trial preparation')
            if not self.rail(): return
            self.select_log(now)
        elif self.phase.startswith('contact_'):
            self.require(elapsed < 3., 'Native log approach failed to contact the real raft/player')
            if valid(self.log):
                self.log_observations.append(dict(time=now, position_cm=vec(self.log.get_actor_location()),
                    impacts=int(prop(self.log, 'impact_count')), player_impacts=int(prop(self.log, 'player_impact_count')),
                    raft_impacts=int(prop(self.log, 'raft_impact_count')), braced_contacts=int(prop(self.log, 'braced_contact_count'))))
            self.finish_contact(now)
        elif self.phase == 'after_contact_rail' and elapsed >= .65:
            self.require(self.standing(self.hero) and self.base(self.hero) == prop(self.raft, 'deck') and self.rail(), 'Actual rail holder fell after the confirmed impact')
            self.report['checks']['actual_rail_guards_confirmed_native_contact'] = True
            # Retain actual aftermath through the requested six-second clip.
            if now >= self.clip_until:
                self.begin_contact(False)
        elif self.phase == 'after_contact_unbraced' and elapsed >= .65:
            foot = self.hero.get_actor_location() - u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height())
            local = prop(self.raft, 'deck').get_world_transform().inverse_transform_location(foot)
            moved = length(self.hero.get_actor_location() - u.Vector(*self.log_before['hero_before_cm']))
            lost_deck = self.base(self.hero) != prop(self.raft, 'deck')
            self.report['unbraced_result'] = dict(moved_cm=moved, local_foot_cm=vec(local),
                lost_deck=lost_deck, body_state=enum_name(prop(self.hero, 'tooth_physics').get_body_state()),
                deep_water=bool(self.hero.get_movement_component().is_in_deep_water()))
            self.require(lost_deck or not self.standing(self.hero) or abs(local.x) > 120. or abs(local.y) > 85.,
                'Confirmed unbraced contact did not visibly dislodge the edge rider')
            self.report['checks']['native_unbraced_contact_dislodges_player'] = True
            self.stage('unbraced_preview_hold')
        elif self.phase == 'unbraced_preview_hold' and now >= self.clip_until:
            self.stage('storm_flush')
        elif self.phase == 'storm_flush' and elapsed >= .7:
            for entry in self.report['frames']:
                frame = Path(entry['file'])
                self.require(frame.is_file(), 'Engine frame missing: ' + str(frame))
                data = frame.read_bytes()
                self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Engine capture is not PNG')
                entry.update(sha256=hashlib.sha256(data).hexdigest(), width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
            required = ('storm_brush_rowing',) if FOCUS == 'rowing' else ('debris_rail', 'debris_unbraced')
            if FOCUS == 'combined': required = ('storm_brush_rowing', 'debris_rail', 'debris_unbraced')
            if self.capture_hz:
                self.require(all(len(self.report['clips'].get(name, [])) >= 8 for name in required), 'Required actual clip has fewer than eight verified camera frames')
            self.report['manual_checkpoints'] = ['Inspect actual storm, brush handles/arms and confirmed-contact splashes in the clips.',
                'Assess default 35-second hold, unmodified 100-unit swimming reserve, 3.5 units/s idle drain plus 1.3 units/s while stroking, and six-second drowning grace with the existing separate focused drowning probe.',
                'Full cleaning and multiplayer remain outside this episode.']
            self.finish()

    def tick(self, _delta):
        if self.done: return
        try:
            self.require(time.monotonic() - self.started < 240., 'Owned storm probe exceeded 240 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds: return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one owned authority PIE world')
                self.pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()): return
                self.world = worlds[0]
                self.hero = self.pc.get_controlled_pawn()
                self.stage('first_perk')
                return
            self.require(len(worlds) == 1 and self.world in worlds, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'Local pawn changed')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            elapsed = now - self.phase_at
            # UI choices must be handled even while their real native pause stops game time.
            if self.phase == 'first_perk':
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                return
            if self.phase == 'perk_resolution':
                if not self.pc.is_reward_menu_open():
                    self.pc.request_dev_action(u.MCDevAction.CATACLYSM_RAFTS, -1)
                    self.stage('launch')
                return
            if self.last_game_time is not None and now <= self.last_game_time: return
            self.last_game_time = now
            if self.phase == 'launch':
                events = [e for e in self.actors(u.MCCataclysmEvent) if enum_name(prop(e, 'phase')) not in ('DORMANT', 'COMPLETE', 'STOPPED')]
                self.require(elapsed < 3. and len(events) <= 1, 'F3 failed to create exactly one Cataclysm')
                if not events: return
                self.event = events[0]
                self.tongue = self.actors(u.MCTongue)[0]
                self.episode_started_at = now
                self.event.set_editor_properties({'FloodHoldSeconds': 26., 'FloodFillSeconds': 3., 'FloodDrainSeconds': 3.})
                self.report['fixture_tuning'] = dict(FloodHoldSeconds=26., FloodFillSeconds=3., FloodDrainSeconds=3.)
                state = u.GameplayStatics.get_game_state(self.world)
                self.scheduler = prop(state, 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_tick = self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
                self.stage('wait_hold')
                return
            self.require(now - self.episode_started_at < 40., 'Focused storm episode exceeded 40 game seconds')
            self.require(valid(self.event) and not prop(self.event, 'failed'), 'Native event failed')
            self.require(prop(self.hero, 'status').is_alive(), 'Subject died; damage and drowning were not overridden')
            if valid(self.bot) and self.bot.is_rowing() and now >= self.bot_direction_at:
                self.bot_direction_at = now + .1
                direction = self.liquid.storm_stream_direction()
                self.bot.call_method('ServerRowingDirection', args=(u.Vector2D(-direction.x, -direction.y),))
            if valid(self.raft):
                self.camera_for_stage()
                self.keep_camera()
                if now >= self.sample_at:
                    self.sample_at = now + .05
                    self.sample(now)
                if self.clip and now > self.clip_until: self.clip = None
                if self.capture_hz and self.clip and now >= self.frame_at:
                    self.frame_at = now + 1. / self.capture_hz
                    evidence = self.camera_evidence()
                    if evidence and evidence['view_target'] == evidence['expected_target'] and evidence['location_error_cm'] < 12. and evidence['rotation_error_degrees'] < 5.:
                        self.screenshot('%05d.png' % len(self.report['frames']), True)
            self.update_episode(now, elapsed)
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if not self.done and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                for pawn in [self.hero, self.bot]:
                    if valid(pawn): pawn.call_method('SetRowingInputHeld', args=(False,))
                if valid(self.hero): self.hero.call_method('SetBraceInputHeld', args=(False,))
                if self.menu_opened and valid(self.pc):
                    self.pc.toggle_dev_panel()
                    self.menu_opened = False
                if valid(self.scheduler) and self.scheduler_tick is not None:
                    self.scheduler.set_actor_tick_enabled(self.scheduler_tick)
                self.restore_event_tick()
                if self.episode_started_at is not None:
                    self.report['episode_game_seconds'] = float(u.GameplayStatics.get_time_seconds(self.world)) - self.episode_started_at
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


def main():
    for key in (KEY, 'mc_cataclysm_solo', 'mc_cataclysm_drowning_solo', 'mc_approved_environment_solo'):
        old = getattr(builtins, key, None)
        if old is not None and not getattr(old, 'done', True):
            raise RuntimeError('Existing owned probe is active; it was preserved: ' + key)
    probe = CataclysmStormSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
