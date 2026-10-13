"""Owned solo proof: real F3 continuous nut rain, natural two-boss arrival,
native scheduled wizard rain/summon and a Pickaxe kill before the natural fuse.
Run in idle L_Mouth after compilation. Position-only dodging, review camera and
boss ability readiness timestamps are explicit PIE fixtures. No attack helper,
fake damage, clock/fuse shortening, network peer or package save.
Genuine SHOWUI frames at 6 Hz have actual game timestamps and separate clips.
Cancel: builtins.mc_nut_fight_redesign_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import json
import math
import runpy
import time
import traceback
import uuid
from pathlib import Path
import unreal as u

KEY = 'mc_nut_fight_redesign_solo'
OUTPUT = Path(globals().get('MC_NUT_FIGHT_OUTPUT',
    Path(u.Paths.project_saved_dir())/'Checks/NutFightRedesignSolo')).resolve()
RUN = OUTPUT/'Runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='nut_fight_probe_support', init_globals={'MC_ENV_OUTPUT': RUN,
    'MC_ENV_CAPTURE_HZ': globals().get('MC_NUT_FIGHT_CAPTURE_HZ', 6)})
Base, prop, vec = (support[name] for name in ('ApprovedEnvironmentSolo', 'prop', 'vec'))


def valid(actor):
    return actor is not None and u.SystemLibrary.is_valid(actor)


class NutFightRedesignSolo(Base):
    def __init__(self):
        super().__init__()
        self.event = self.mage = self.kill_target = None
        self.dodge_at = 0.
        self.rain_start = self.rain_cast_at = self.arm_seen_at = None
        self.last_airborne_at = None
        self.clip = None
        self.before_summon = []
        self.expiry_seen = False
        self.report.update(scope='One owned standalone PIE; native F3 rain and scheduled wizard spells',
            fixture_tuning={}, run_directory=str(RUN), clips={'storm_arrival': [], 'wizard_summon': []},
            limitations=['Position-only PIE dodging and review camera preserve the observer; this is not a balance/skill test.',
                'Boss readiness timestamps isolate real wizard rain and summon; native attack states and fuse durations remain unchanged.',
                'Held primary Pickaxe input tests killing a summon; no Python damage or native attack helper is called.',
                'No network, full battle, build, cook, package, saved content, shield or Knight validation here.'])

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report.json').write_text(data, encoding='utf8')

    def screenshot(self, name, sequence=False):
        super().screenshot(name, sequence)
        record = self.report['frames' if sequence else 'screenshots'][-1]
        record['clip_window'] = self.clip
        if sequence and self.clip:
            self.report['clips'][self.clip].append(record)

    def food_support(self, food):
        """Observe actual support/speed; Falling alone is not airborne evidence.

        A native food/obstacle contact can end mouth entry while the enum remains
        Falling. Native entry is not reflected: geometry and velocity below are
        observational evidence, rather than a claim to read that private flag.
        """
        phase = prop(food, 'phase')
        position = food.get_actor_location()
        body = prop(food, 'body')
        floor = self.floor(position)
        bottom = position.z-float(body.get_scaled_box_extent().z)
        velocity = food.get_velocity()
        speed = math.sqrt(velocity.x**2+velocity.y**2+velocity.z**2)
        height = bottom-floor.z if floor is not None else None
        settings = prop(self.event, 'settings')
        supported = floor is not None and -45. <= height <= float(prop(settings, 'large_nut_height'))+45. and speed <= 180.
        ground = phase in (u.MCFoodPhase.FREE, u.MCFoodPhase.STUCK) or phase == u.MCFoodPhase.FALLING and supported
        return dict(path=food.get_path_name(), phase=str(phase), position_cm=vec(position),
            supported_floor_cm=vec(floor) if floor is not None else None,
            body_bottom_height_above_floor_cm=height, speed_cm_per_second=speed,
            resting_support=bool(supported), classified_ground=bool(ground),
            classification='Actual tongue support/velocity supplement the native phase; private mouth-entry flag is not exposed')

    def foods(self, airborne=False):
        foods = []
        for food in prop(self.event, 'nuts'):
            if not valid(food) or prop(food, 'fragment') or prop(food, 'stack_carrier') or prop(food, 'holders'):
                continue
            phase = prop(food, 'phase')
            if phase not in (u.MCFoodPhase.FALLING, u.MCFoodPhase.FREE, u.MCFoodPhase.STUCK):
                continue
            state = self.food_support(food)
            matches = phase == u.MCFoodPhase.FALLING and not state['classified_ground'] if airborne else state['classified_ground']
            if matches:
                foods.append(food)
        return foods

    def summons(self):
        return self.actors(u.MCNutKamikaze)

    def sample(self, now):
        air = self.foods(True)
        if air:
            self.last_airborne_at = now
        arms = [dict(path=actor.get_path_name(), health=float(prop(actor, 'health')),
            armed=bool(prop(actor, 'armed')), exploded=bool(prop(actor, 'exploded')),
            remaining=float(actor.get_fuse_seconds_remaining()),
            fuse_started_at=float(prop(actor, 'fuse_started_at')), fuse_explodes_at=float(prop(actor, 'fuse_explodes_at')),
            target=prop(actor, 'target').get_path_name() if valid(prop(actor, 'target')) else None)
            for actor in self.summons()]
        if any(item['exploded'] for item in arms):
            self.expiry_seen = True
        self.report['samples'].append(dict(game_seconds=now, phase=self.phase,
            event_stage=str(prop(self.event, 'stage')), rain_age=now-self.rain_start,
            nuts_spawned=int(prop(self.event, 'nuts_spawned')), airborne=len(air), ground_whole=len(self.foods()),
            bosses_left=int(prop(self.event, 'bosses_left')), player_health=float(prop(prop(self.hero, 'status'), 'state').health),
            mage_attack=str(prop(self.mage, 'attack')) if valid(self.mage) else None,
            mage_state=str(prop(self.mage, 'state')) if valid(self.mage) else None,
            mage_state_started_at=float(prop(self.mage, 'state_started_at')) if valid(self.mage) else None,
            mage_resolve_at=float(prop(self.mage, 'resolve_at')) if valid(self.mage) else None,
            mage_attack_end_at=float(prop(self.mage, 'attack_end_at')) if valid(self.mage) else None,
            summons=arms,
            camera_evidence=self.camera_evidence() if valid(self.camera) else None,
            food_support_observations=[self.food_support(food) for food in prop(self.event, 'nuts') if valid(food)
                and prop(food, 'phase') in (u.MCFoodPhase.FALLING, u.MCFoodPhase.FREE, u.MCFoodPhase.STUCK)]))

    def overview(self):
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        floor = self.floor(center)
        self.require(floor is not None, 'Review camera has no supported central tongue point')
        # Keep the camera inside the mouth's side walls and below its roof.
        # Component AABB centre may be below the visible surface: aim from the
        # actual floor so both full-height bosses and airborne nuts stay framed.
        target = floor+u.Vector(0, 0, 210)
        location = floor+u.Vector(extent.x*.12, -extent.y*.55, 850.)
        self.frame_camera(location, target)
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 100.)
        self.keep_review_camera()
        self.report['review_camera'] = dict(location_cm=vec(location), target_cm=vec(target), fov=100.,
            placement='Supported-floor height; inside tongue side extents; 850 cm above floor')

    def keep_review_camera(self):
        if not valid(self.camera):
            return
        # Controller world-presentation/spectating resets can restore a live
        # pawn target during the initial bootstrap. Reassert the owned review
        # target after every tick instead of relying on the one setup call.
        self.pc.set_editor_property('bAutoManageActiveCameraTarget', False)
        self.pc.set_view_target_with_blend(self.camera, 0.)

    def camera_evidence(self):
        target = self.pc.get_view_target()
        manager = prop(self.pc, 'player_camera_manager')
        actual = manager.get_camera_location()
        actual_rotation = manager.get_camera_rotation()
        requested = self.camera.get_actor_location()
        requested_rotation = self.camera.get_actor_rotation()
        error = math.sqrt((actual.x-requested.x)**2+(actual.y-requested.y)**2+(actual.z-requested.z)**2)
        delta = lambda a, b: (a-b+180.)%360.-180.
        rotation_error = max(abs(delta(getattr(actual_rotation, axis), getattr(requested_rotation, axis)))
            for axis in ('pitch', 'yaw', 'roll'))
        return dict(view_target=target.get_path_name() if valid(target) else None,
            requested_view_target=self.camera.get_path_name(), location_error_cm=error,
            actual_camera_cm=vec(actual), requested_camera_cm=vec(requested), rotation_error_degrees=rotation_error,
            actual_rotation_degrees={axis:float(getattr(actual_rotation, axis)) for axis in ('pitch', 'yaw', 'roll')},
            requested_rotation_degrees={axis:float(getattr(requested_rotation, axis)) for axis in ('pitch', 'yaw', 'roll')})

    def dodge(self, now):
        if now < self.dodge_at:
            return
        self.dodge_at = now+.2
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        hazards = [(actor.get_actor_location(), float(prop(actor, 'radius'))+110.)
            for actor in self.actors(u.MCNutLandingShadow) if actor.is_warning_active()]
        hero_radius = float(self.hero.capsule_component.get_scaled_capsule_radius())
        hazards += [(actor.get_actor_location(), float(prop(actor, 'body').get_scaled_sphere_radius())+hero_radius+70.)
            for actor in self.actors(u.MCNutBoss)]
        hazards += [(actor.get_actor_location(), float(prop(actor, 'blast_radius'))+140.)
            for actor in self.summons() if prop(actor, 'armed')]
        choices = []
        for x in (-.6, -.45, -.3, -.15, 0., .15, .3, .45, .6):
            for y in (-.6, -.45, -.3, -.15, 0., .15, .3, .45, .6):
                floor = self.floor(center+u.Vector(extent.x*x, extent.y*y, 0))
                if floor is None:
                    continue
                margin = min((math.hypot(floor.x-p.x, floor.y-p.y)-radius for p, radius in hazards), default=10000.)
                if margin > 80.:
                    choices.append((margin, floor))
        self.require(choices, 'No supported observer point clear of the actual warning footprints')
        chosen_margin, chosen_floor = max(choices, key=lambda item: item[0])
        self.move_to(chosen_floor)
        self.report.setdefault('observer_placements', []).append(dict(game_seconds=now,
            floor_cm=vec(chosen_floor), smallest_actual_hazard_clearance_cm=chosen_margin,
            fixture='Position only; actual warning/blast radii and boss body dimensions, no health or damage changes'))

    def ready(self, ability, now):
        for boss in self.actors(u.MCNutBoss):
            for name in ('NextMeleeAt', 'NextChargeAt', 'NextJumpAt', 'NextRollAt',
                'NextFireballAt', 'NextSummonAt', 'NextRainAt', 'NextTeleportAt', 'NextSpecialAt'):
                boss.set_editor_property(name, now+300.)
        if ability:
            self.mage.set_editor_property(ability, now)
            self.mage.set_editor_property('NextSpecialAt', now)
        self.report.setdefault('readiness_fixtures', []).append(dict(game_seconds=now, ability=ability,
            explanation='Readiness only; native scheduler drives tell, release, recovery, food and fuse'))

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 150., 'Nut probe exceeded 150 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one standalone authority world')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if self.pc is None or self.pc.get_controlled_pawn() is None:
                    self.world = None
                    return
                self.report['world'] = self.world.get_path_name()
                self.stage('first_perk')
                return
            self.require(self.world in worlds and len(worlds) == 1, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'Solo controller has no tooth character')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.phase_at
            if self.phase == 'first_perk':
                self.require(elapsed < 25., 'Normal first-perk UI did not become ready')
                if now >= 1. and self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                return
            if self.phase == 'perk_resolution':
                self.require(elapsed < 10., 'Normal first-perk choice did not resolve')
                if not self.pc.is_reward_menu_open():
                    self.pc.request_dev_action(u.MCDevAction.NUT_ENCOUNTER, -1)
                    self.stage('arrival')
                return
            if self.phase == 'arrival':
                events = [event for event in self.actors(u.MCNutRainEvent) if event.actor_has_tag('MC_DevKeyEvent')]
                self.require(len(events) == 1, 'F3 did not create exactly one owned nut event')
                self.event = events[0]
                self.tongue = self.actors(u.MCTongue)[0]
                self.rain_start = float(prop(self.event, 'stage_started_at'))
                settings = prop(self.event, 'settings')
                self.duration = float(prop(settings, 'rain_seconds'))
                self.require(20. <= self.duration <= 25., 'Rain duration is outside the approved 20–25 seconds')
                times = list(prop(self.event, 'planned_launch_times'))
                gaps = [b-a for a, b in zip(times, times[1:])]
                self.require(gaps and all(1.499 <= gap <= 3.001 for gap in gaps), 'Native drop gaps violate 1.5–3 seconds')
                self.report['native_rain'] = dict(duration=self.duration, planned_launch_times=times,
                    flight_seconds=float(prop(prop(settings, 'entry'), 'flight_seconds')))
                self.report['checks']['native_individual_drop_intervals_1_5_to_3_seconds'] = True
                self.overview()
                self.stage('rain')
            self.keep_review_camera()
            if self.phase not in ('summon_kill', 'capture_flush'):
                self.dodge(now)
            self.keep_review_camera()
            self.require(prop(self.hero, 'status').is_alive(), 'Observer died; native damage was left unchanged')
            if not self.report.get('early_review_camera_verified'):
                evidence = self.camera_evidence()
                if self.pc.get_view_target() == self.camera and evidence['location_error_cm'] < 2. and evidence['rotation_error_degrees'] < 2.:
                    self.screenshot('00_review_camera_verified.png')
                    self.report['early_review_camera_verified'] = evidence
                    self.report['checks']['actual_review_target_location_rotation_verified'] = True
                    self.write()
            if now >= self.sample_at:
                self.sample_at = now+.1
                self.sample(now)
            if self.capture_hz and self.clip and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                camera = self.camera_evidence()
                if self.pc.get_view_target() == self.camera and camera['location_error_cm'] < 2. and camera['rotation_error_degrees'] < 2.:
                    self.screenshot('%05d.png'%len(self.report['frames']), True)
                    self.report['frames'][-1]['camera_evidence'] = camera
                else:
                    self.report.setdefault('skipped_camera_requests', []).append(dict(game_seconds=now, phase=self.phase,
                        clip_window=self.clip, reason='Player camera manager has not yet adopted the reasserted review target',
                        camera_evidence=camera))
            if self.phase == 'rain':
                if now-self.rain_start >= self.duration-5.:
                    self.clip = 'storm_arrival'
                bosses = self.actors(u.MCNutBoss)
                if bosses:
                    self.require(len(bosses) == 2 and not self.foods(True), 'Expected two bosses only after the last nut landed')
                    age = now-self.rain_start
                    self.require(19.99 <= age <= 26., 'Natural arrival missed the 20–25 second clock')
                    self.require(self.foods(), 'Boss transition removed all remaining whole nuts')
                    self.require(len(self.foods()) == int(prop(self.event, 'nuts_spawned')),
                                 'Opening nuts fractured or disappeared without a player breaking them')
                    self.report['checks']['opening_nuts_persist_without_player_breaks'] = True
                    self.report['checks']['two_bosses_after_last_landing_without_cleanup_gate'] = True
                    self.report['boss_arrival'] = dict(game_seconds=now, rain_age=age,
                        last_airborne_sample_at=self.last_airborne_at, remaining_whole=len(self.foods()))
                    self.mage = next(boss for boss in bosses if prop(boss, 'boss_role') == u.MCNutBossRole.MAGE)
                    self.ready(None, now)
                    self.stage('boss_arrival')
            elif self.phase == 'boss_arrival' and elapsed >= 3.:
                self.clip = 'wizard_summon'
                self.rain_before = int(prop(self.event, 'nuts_spawned'))
                self.ready('NextRainAt', now)
                self.stage('wizard_rain')
            elif self.phase == 'wizard_rain':
                self.require(elapsed < 20., 'Native wizard rain did not resolve')
                mage_attack, mage_state = prop(self.mage, 'attack'), prop(self.mage, 'state')
                if mage_attack == u.MCNutBossAttack.NUT_RAIN:
                    if mage_state in (u.MCNutBossState.TELEGRAPH, u.MCNutBossState.EXECUTING):
                        if self.rain_cast_at is None:
                            self.rain_cast_at = now
                            self.report['actual_rain_cast_start'] = dict(game_seconds=now,
                                mage_state=str(mage_state), state_started_at=float(prop(self.mage, 'state_started_at')),
                                resolve_at=float(prop(self.mage, 'resolve_at')), attack_end_at=float(prop(self.mage, 'attack_end_at')),
                                evidence='Native NutRain attack entered Telegraph or Executing; Recovery alone is not a cast')
                    elif mage_state == u.MCNutBossState.RECOVERY and self.rain_cast_at is None:
                        started = float(prop(self.mage, 'state_started_at'))
                        aborted = self.report.setdefault('rain_recoveries_without_observed_cast', [])
                        if not aborted or aborted[-1]['state_started_at'] != started:
                            aborted.append(dict(game_seconds=now, mage_attack=str(mage_attack), mage_state=str(mage_state),
                                state_started_at=started, resolve_at=float(prop(self.mage, 'resolve_at')),
                                attack_end_at=float(prop(self.mage, 'attack_end_at')),
                                interpretation='Recovery carries the attack label but no Telegraph/Executing was observed; not counted as a released rain cast'))
                if self.rain_cast_at is not None and now-self.rain_cast_at >= 8. and not self.foods(True):
                    self.require(int(prop(self.event, 'nuts_spawned')) > self.rain_before, 'Wizard rain produced no real persistent food')
                    self.before_summon = [food.get_path_name() for food in self.foods()]
                    self.require(self.before_summon, 'No surviving nuts available for summon')
                    self.require(len(self.before_summon) == int(prop(self.event, 'nuts_spawned')),
                                 'Wizard rain removed persistent whole nuts without player action')
                    self.report['checks']['wizard_rain_preserves_existing_whole_nut_supply'] = True
                    self.report['checks']['wizard_rain_spawns_real_persistent_nuts'] = True
                    self.report['pre_summon_nuts'] = self.before_summon
                    self.ready('NextSummonAt', now)
                    self.stage('summon_cast')
            elif self.phase == 'summon_cast':
                self.require(elapsed < 12., 'Native summon created no kamikazes')
                armed = [actor for actor in self.summons() if prop(actor, 'armed')]
                if armed:
                    self.arm_seen_at = now
                    self.require(len(armed) == len(self.before_summon), 'Summon did not convert every available nut')
                    self.require(all(actor.get_fuse_seconds_remaining() > .1 for actor in armed), 'Summons were not armed while pursuing')
                    self.report['checks']['all_available_nuts_become_already_armed_summons'] = True
                    self.report['summon_count'] = dict(before=len(self.before_summon), actual=len(armed))
                    self.kill_target = min(armed, key=lambda actor: actor.get_distance_to(self.hero))
                    prop(self.hero, 'inventory').server_select(u.MCToolSlot.PICKAXE)
                    self.primary_input(True)
                    self.stage('summon_kill')
            elif self.phase == 'summon_kill':
                self.require(elapsed < 4., 'Real held Pickaxe input did not kill the summon before its normal fuse')
                if not valid(self.kill_target) or prop(self.kill_target, 'defeated'):
                    self.primary_input(False)
                    self.require(not valid(self.kill_target) or not prop(self.kill_target, 'exploded'), 'Killed summon detonated')
                    self.report['checks']['real_primary_pickaxe_kills_armed_summon_before_detonation'] = True
                    self.stage('summon_expiry')
                else:
                    point = self.kill_target.get_actor_location()
                    floor = self.floor(point+u.Vector(130, 0, 0))
                    self.require(floor is not None, 'No supporting floor for Pickaxe approach')
                    self.move_to(floor, math.degrees(math.atan2(point.y-floor.y, point.x-floor.x)))
            elif self.phase == 'summon_expiry' and now-self.arm_seen_at >= 7.:
                self.require(self.expiry_seen, 'No real native summon detonation was observed')
                self.report['checks']['unchanged_native_fuses_detonate_remaining_summons'] = True
                self.stage('capture_flush')
            elif self.phase == 'capture_flush' and elapsed >= .6:
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('Nut probe already active; finish it before starting another')
    probe = NutFightRedesignSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
