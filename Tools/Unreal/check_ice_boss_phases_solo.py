"""Owned solo PIE integration check for the approved two-stage ice boss.

Run in the idle Editor on L_Mouth after the native changes are compiled. This
uses the normal first-perk UI, F3 IceEvent and held primary Pickaxe input. The
health threshold and next-ability timestamps are explicitly controlled PIE
fixtures: attacks cannot spoil the brief meter/warmth observation, and prison
does not require waiting for an arbitrary scheduler choice. Prison foot-ice
health, escape time, tool damage and the native hit/cancel paths stay unchanged.

The focused MCIceBossPhaseTests cover the full 40/40 clock, fatal meter and
stationary prison punishment. This short gameplay probe checks the actual HUD,
phase transition, warming and self rescue, and records genuine SHOWUI frames.
No content is saved, build is started, network peer or broad suite is run.
Optional MC_ICE_PHASE_OUTPUT and MC_ICE_PHASE_CAPTURE_HZ (default 6, 0=stills).
Cancel: builtins.mc_ice_boss_phases_solo.finish('Stopped by operator').
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


KEY = 'mc_ice_boss_phases_solo'
BASE_OUTPUT = Path(globals().get('MC_ICE_PHASE_OUTPUT',
    Path(u.Paths.project_saved_dir())/'Checks/IceBossPhasesSolo')).resolve()
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='ice_boss_phase_probe_support',
    init_globals={'MC_ENV_OUTPUT': BASE_OUTPUT,
                  'MC_ENV_CAPTURE_HZ': globals().get('MC_ICE_PHASE_CAPTURE_HZ', 6)})
OwnedSolo = support['ApprovedEnvironmentSolo']
prop, vec = support['prop'], support['vec']


def valid(obj):
    if obj is None:
        return False
    try:
        return u.SystemLibrary.is_valid(obj)
    except (TypeError, ReferenceError):
        # A destroyed PIE actor can leave a Python wrapper whose UObject
        # pointer cannot be converted even for IsValid. Treat it as expired.
        return False


class IceBossPhasesSolo(OwnedSolo):
    def __init__(self):
        super().__init__()
        self.root_output = self.output
        run_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8]
        self.output = self.root_output/'Runs'/run_id
        self.output.mkdir(parents=True, exist_ok=False)
        self.scheduler = None
        self.scheduler_old_tick = None
        self.prison_ids = set()
        self.prison_deadline = self.prison_health = None
        self.ice_health_history = []
        self.gauge_before_warm = None
        self.warm_floor = self.outside_floor = None
        self.report = dict(status='running', passed=False, checks={}, samples=[], screenshots=[], frames=[],
            scope='One owned standalone PIE; F3 ice boss and held primary Pickaxe self rescue',
            fixture_tuning=[], limitations=[
                'Short isolated mechanic check; ordinary encounter balance and a full 40/40 cycle are not gameplay-tested here.',
                'Fatal meter, stationary prison impact and exact clock/ability boundaries require the separate focused native tests.',
                'No multiplayer/network validation, cook or package.'])

    def write(self):
        body = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(body, encoding='utf8')
        (self.root_output/'Report.json').write_text(body, encoding='utf8')

    def health(self):
        return float(prop(prop(prop(self.hero, 'status'), 'state'), 'health'))

    def freeze(self):
        state = next((player for player in prop(self.ice, 'players')
                      if prop(player, 'hero') == self.hero), None)
        return float(prop(state, 'amount')) if state is not None else 0.

    def safe(self):
        state = next((player for player in prop(self.ice, 'players')
                      if prop(player, 'hero') == self.hero), None)
        return bool(prop(state, 'safe')) if state is not None else False

    def defer_other_abilities(self, now):
        for name in ('NextIceBombAt', 'NextColdWaveAt'):
            self.ice.set_editor_property(name, now+300.)
        self.report['fixture_tuning'].append(dict(game_seconds=now,
            NextIceBombAt=now+300., NextColdWaveAt=now+300.,
            reason='Isolate meter/warmth and prison from unrelated bomb/wave damage in this PIE only'))

    def outside_point(self):
        transform = self.tongue.get_actor_transform()
        candy = transform.transform_location(prop(self.ice, 'candy_anchor'))
        safe = transform.transform_location(prop(self.ice, 'safe_anchor'))
        frost_radius = float(prop(self.ice, 'boss_frost_radius'))
        circle_radius = float(prop(self.ice, 'circle_radius'))
        zones = []
        for zone in self.actors(u.MCFoodDisposal):
            # Whole actor bounds include the decorative mouth and the large
            # procedural delivery-outline mesh. They are not intake bounds.
            # Mirror only the saved geometry of native GetDeliveryZoneGeometry.
            if isinstance(zone, u.MCThroat):
                geometry = zone.get_actor_transform()
                offset = prop(zone, 'zone_center')
                strip = prop(zone, 'delivery_strip_half_extent')
                radius = float(prop(zone, 'zone_radius'))
                half = u.Vector(strip.x, strip.y, float(prop(zone, 'zone_height'))) \
                    if prop(zone, 'use_delivery_strip') else u.Vector(radius, radius, float(prop(zone, 'zone_height')))
            else:
                volume = prop(zone, 'volume')
                geometry = volume.get_world_transform()
                offset = u.Vector(0, 0, 0)
                half = volume.get_unscaled_box_extent()
            zones.append((zone, geometry, offset, half))
        center, extent, _radius = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        candidates = []
        for distance in (frost_radius+180., frost_radius+350., frost_radius+650., frost_radius+1000.):
            for index in range(32):
                angle = index*math.tau/32.
                candidates.append(candy+u.Vector(math.cos(angle)*distance, math.sin(angle)*distance, 0))
        candidates.extend(center+u.Vector(extent.x*x, extent.y*y, 0)
            for x in (.2, -.2, .4, -.4, .6, -.6, .75, -.75)
            for y in (0., .2, -.2, .4, -.4))
        rejected = dict(no_floor=0, warm_circle=0, strong_aura=0, delivery=0, unsupported_footprint=0)
        self.report['outside_placement_geometry'] = dict(tongue_center_cm=vec(center),
            tongue_half_extent_cm=vec(extent), boss_cm=vec(candy), safe_cm=vec(safe),
            strong_aura_radius_cm=frost_radius, safe_circle_radius_cm=circle_radius,
            delivery_geometry=[dict(actor=zone.get_path_name(), center_cm=vec(geometry.transform_location(offset)),
                unscaled_half_extent_cm=vec(half), scale=vec(geometry.scale3d))
                for zone, geometry, offset, half in zones], rejected=rejected)
        for candidate in candidates:
            floor = self.floor(candidate)
            if floor is None:
                rejected['no_floor'] += 1
                continue
            if math.hypot(floor.x-safe.x, floor.y-safe.y) <= circle_radius+90.:
                rejected['warm_circle'] += 1
                continue
            if self.ice.is_inside_cold_throne(floor):
                rejected['strong_aura'] += 1
                continue
            near_delivery = False
            for _zone, geometry, offset, half in zones:
                local = geometry.inverse_transform_location(floor)-offset
                scale = geometry.scale3d
                if abs(local.x) <= half.x+100./max(.01, abs(scale.x)) \
                    and abs(local.y) <= half.y+100./max(.01, abs(scale.y)):
                    near_delivery = True
                    break
            if near_delivery:
                rejected['delivery'] += 1
                continue
            supported = [self.floor(floor+u.Vector(dx, dy, 0))
                         for dx, dy in ((60, 0), (-60, 0), (0, 60), (0, -60))]
            if any(point is None or abs(point.z-floor.z)>40. for point in supported):
                rejected['unsupported_footprint'] += 1
                continue
            self.report['outside_fixture'] = dict(floor_cm=vec(floor),
                boss_distance_cm=math.hypot(floor.x-candy.x, floor.y-candy.y),
                strong_aura_radius_cm=frost_radius, safe_circle_cm=vec(safe),
                selection='Supported tongue footprint outside strong aura, warm circle and oriented native delivery geometry')
            return floor
        raise RuntimeError('No supported exposed tongue point outside boss aura and warm circle')

    def sample(self, now):
        if not valid(self.ice):
            return
        attack = prop(self.ice, 'boss_attack')
        self.report['samples'].append(dict(game_seconds=now, phase=self.phase, health=self.health(),
            freeze_amount=self.freeze(), safe=self.safe(), hero_cm=vec(self.hero.get_actor_location()),
            boss_phase_two=bool(prop(self.ice, 'second_phase')),
            global_cold=bool(self.ice.is_global_cold_active()),
            global_seconds_remaining=float(self.ice.cold_throne_seconds_remaining()),
            global_cooldown_remaining=float(self.ice.cold_throne_cooldown_remaining()),
            attack=str(prop(attack, 'kind')), attack_serial=int(prop(attack, 'serial')),
            frozen_legs=bool(self.hero.has_frozen_legs()), ice_leg_health=float(prop(self.hero, 'ice_leg_health')),
            primary_held=self.primary,
            prison_strikes=[dict(id=int(prop(strike, 'id')), impact_at=float(prop(strike, 'impact_at')),
                impacted=bool(prop(strike, 'impacted')),
                cancelled=bool(prop(strike, 'cancelled')),
                prisoner=prop(strike, 'prisoner').get_path_name() if valid(prop(strike, 'prisoner')) else None)
                for strike in prop(self.ice, 'strikes') if prop(strike, 'prison_punishment')]))

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 180., 'Ice phase probe exceeded wall-time bound')
            if self.pie_start is not None:
                self.require(not prop(self.pie_start, 'Error'), 'StartPIE: '+str(prop(self.pie_start, 'Error')))
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_standalone(worlds[0]),
                    'Expected exactly one owned standalone PIE')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if pc is None or pc.get_controlled_pawn() is None:
                    return
                self.world, self.pc = worlds[0], pc
                self.report['world'] = self.world.get_path_name()
                self.stage('first_perk')
                return
            self.require(self.world in worlds, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'Normal tooth hero is required')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            age = now-self.phase_at
            if self.phase == 'first_perk':
                self.require(age < 25., 'Normal first-perk UI did not appear')
                if now >= 1. and self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                return
            if self.phase == 'perk_resolution':
                self.require(age < 10., 'Normal first perk did not resolve')
                if self.pc.is_reward_menu_open():
                    return
                state = u.GameplayStatics.get_game_state(self.world)
                self.scheduler = prop(state, 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_old_tick = self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
                state.set_editor_property('bDevManualEvents', True)
                self.pc.call_method('RequestDevAction', args=(u.MCDevAction.ICE_EVENT, -1))
                self.report['checks']['normal_first_perk_and_f3_start'] = True
                self.stage('arrival')
                return
            if self.phase == 'arrival':
                self.require(age < 10., 'Normal ice arrival did not become active')
                if self.ice is None:
                    events = self.actors(u.MCIceEvent)
                    self.require(len(events) == 1, 'F3 must create one winter owner')
                    self.ice = events[0]
                    self.tongue = prop(self.ice, 'tongue')
                    self.require(valid(self.tongue), 'Ice owner has no actual tongue')
                    self.require(self.ice.uses_cold_throne_contract(), 'F3 is using an old lab-only winter contract')
                    self.defer_other_abilities(now)
                    self.ice.set_editor_property('NextIcePrisonAt', now+300.)
                    self.outside_floor = self.outside_point()
                    position = self.move_to(self.outside_floor)
                    self.frame_camera(position+u.Vector(600, 400, 480), position-u.Vector(0, 0, 50))
                if prop(self.ice, 'stage') == u.MCIceEventStage.ACTIVE:
                    self.stage('phase_one_exposed')
                return
            self.require(self.health() > 0. and not self.hero.is_freezing_to_death(),
                'The isolated player died or reached fatal full freeze')
            if self.pc.is_reward_menu_open():
                self.primary_input(False)
                self.pc.choose_reward_perk(0)
                raise RuntimeError('Unexpected reward interrupted this isolated no-score phase check')
            if now >= self.sample_at:
                self.sample_at = now+.1
                self.sample(now)
            if self.capture_hz and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase == 'phase_one_exposed' and age >= 1.5:
                self.require(not prop(self.ice, 'second_phase') and not self.ice.is_global_cold_active(),
                    'Global cold began above the 50 percent threshold')
                self.require(self.freeze() <= .001, 'Phase-one distant player accumulated global freeze')
                self.report['checks']['phase_one_distant_player_has_no_global_meter_gain'] = True
                self.screenshot('01_phase_one_local_aura_only.png')
                threshold = float(prop(self.ice, 'max_candy_health'))*.5
                self.ice.set_editor_property('CandyHealth', threshold)
                self.report['fixture_tuning'].append(dict(game_seconds=now, CandyHealth=threshold,
                    reason='Exercise the actual <=50 percent phase threshold without a long boss damage playthrough'))
                self.stage('phase_two_exposed')
            elif self.phase == 'phase_two_exposed' and age >= .9:
                self.require(prop(self.ice, 'second_phase') and self.ice.is_global_cold_active(),
                    'Crossing 50 percent did not start global cold')
                self.require(0. < self.freeze() < .6, 'Weak global cold must accumulate gradually without instant full freeze')
                self.require(35. < self.ice.cold_throne_seconds_remaining() <= 40., 'Global cold must use the real 40 second clock')
                self.report['checks']['half_health_starts_gradual_global_cold'] = True
                self.screenshot('02_phase_two_global_meter.png')
                self.gauge_before_warm = self.freeze()
                safe = self.tongue.get_actor_transform().transform_location(prop(self.ice, 'safe_anchor'))
                self.warm_floor = self.floor(safe)
                self.require(self.warm_floor is not None, 'Current warm circle has no supported floor')
                self.require(not self.ice.is_inside_cold_throne(self.warm_floor),
                    'The authored far warm-circle fixture is inside the strong aura')
                position = self.move_to(self.warm_floor)
                self.frame_camera(position+u.Vector(470, 310, 340), position-u.Vector(0, 0, 30))
                self.stage('warm_meter_decrease')
            elif self.phase == 'warm_meter_decrease' and age >= 1.5:
                self.require(self.safe(), 'Actual warm circle did not mark the player safe')
                self.require(self.freeze() < self.gauge_before_warm-.025, 'Warm circle did not reduce existing freeze')
                self.report['checks']['warm_circle_reduces_existing_meter'] = True
                self.report['warmth'] = dict(amount_before=self.gauge_before_warm, amount_after=self.freeze())
                self.screenshot('03_warm_circle_meter_decreases.png')
                self.defer_other_abilities(now)
                self.ice.set_editor_property('NextIcePrisonAt', now)
                self.report['fixture_tuning'].append(dict(game_seconds=now, NextIcePrisonAt=now,
                    reason='Make only the phase-two prison ready; normal boss scheduler, warning and resolve remain active'))
                self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
                self.stage('prison_warning')
            elif self.phase == 'prison_warning':
                self.require(age < 15., 'The ready prison did not trap the only live player')
                if self.hero.has_frozen_legs():
                    strikes = [strike for strike in prop(self.ice, 'strikes')
                        if prop(strike, 'prison_punishment') and prop(strike, 'prisoner') == self.hero
                        and not prop(strike, 'impacted')]
                    self.require(len(strikes) == 1, 'Native prison must create one punishment for the trapped player')
                    self.prison_ids = {int(prop(strike, 'id')) for strike in strikes}
                    self.prison_deadline = float(prop(strikes[0], 'impact_at'))
                    self.require(3. <= self.prison_deadline-now <= 5., 'Prison does not allow the approved 3 to 5 second escape')
                    self.prison_health = self.health()
                    self.ice_health_history = [float(prop(self.hero, 'ice_leg_health'))]
                    self.report['checks']['phase_two_prison_has_3_to_5_second_warning'] = True
                    self.screenshot('04_prison_before_self_rescue.png')
                    self.primary_input(True)
                    self.stage('pickaxe_self_rescue')
            elif self.phase == 'pickaxe_self_rescue':
                self.require(now < self.prison_deadline, 'Native pickaxe did not rescue before prison deadline')
                ice_health = float(prop(self.hero, 'ice_leg_health'))
                if ice_health != self.ice_health_history[-1]:
                    self.ice_health_history.append(ice_health)
                if not self.hero.has_frozen_legs():
                    self.primary_input(False)
                    self.require(len(self.ice_health_history) >= 2 and self.ice_health_history[0] > 0.,
                        'Held primary input did not reduce native foot-ice health')
                    pending = [strike for strike in prop(self.ice, 'strikes')
                        if int(prop(strike, 'id')) in self.prison_ids
                        and not prop(strike, 'impacted') and not prop(strike, 'cancelled')]
                    self.require(not pending, 'Successful foot rescue retained its prison punishment')
                    self.report['checks']['held_primary_pickaxe_cancels_prison_punishment'] = True
                    self.report['self_rescue'] = dict(escape_game_seconds=now, deadline=self.prison_deadline,
                        observed_ice_health_changes=self.ice_health_history,
                        ice_leg_health=float(prop(self.hero, 'ice_leg_health')),
                        prison_strike_ids=sorted(self.prison_ids))
                    self.screenshot('05_pickaxe_rescue_cancels_icicle.png')
                    self.stage('after_prison_deadline')
            elif self.phase == 'after_prison_deadline' and now >= self.prison_deadline+.5:
                self.require(abs(self.health()-self.prison_health)<.01, 'Cancelled prison still dealt icicle damage')
                self.require(not self.hero.has_frozen_legs(), 'The rescued player became trapped again')
                self.report['checks']['no_icicle_damage_after_rescued_prison_deadline'] = True
                self.screenshot('06_rescued_after_deadline.png')
                self.stage('capture_flush')
            elif self.phase == 'capture_flush' and age >= .5:
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        if valid(self.scheduler) and self.scheduler_old_tick is not None:
            self.scheduler.set_actor_tick_enabled(self.scheduler_old_tick)
        super().finish(error)
        u.log('MC_ICE_BOSS_PHASES_'+('FAIL ' if error or not self.report.get('passed') else 'PASS ')
              +str(self.output/'Report.json'))


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('Ice phase probe already running')
    probe = IceBossPhasesSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
