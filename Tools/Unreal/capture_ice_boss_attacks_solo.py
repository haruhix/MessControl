"""Short owned solo visual capture of the real ice boss bomb and ground wave.

Run in an idle L_Mouth Editor after compilation. Reuses the phase probe's PIE
ownership/settings restoration and normal first-perk/F3 bootstrap. A temporary
far warm circle and review camera frame the whole boss and player. Only ability
ready timestamps and PIE placement are fixtures; the scheduler, animation,
flight, damage/push, standing wave hit and held-primary Pickaxe remain native.
No health, ice-health, tool damage or attack helper is changed/called.

Writes genuine Shot SHOWUI frames at 6 Hz to a separate run/report. This is a
visual supplement, not another clock/balance/network test. No asset is saved.
Cancel: builtins.mc_ice_boss_attacks_solo.finish('Stopped by operator').
"""
import builtins
import math
import runpy
import time
import traceback
from pathlib import Path

import unreal as u


KEY = 'mc_ice_boss_attacks_solo'
OUTPUT = Path(globals().get('MC_ICE_ATTACK_OUTPUT',
    Path(u.Paths.project_saved_dir())/'Checks/IceBossAttacksSolo')).resolve()
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/check_ice_boss_phases_solo.py'),
    run_name='ice_boss_attack_capture_support',
    init_globals={'MC_ICE_PHASE_OUTPUT': OUTPUT,
                  'MC_ICE_PHASE_CAPTURE_HZ': globals().get('MC_ICE_ATTACK_CAPTURE_HZ', 6)})
PhaseSolo, prop, vec, valid = (support[name] for name in ('IceBossPhasesSolo', 'prop', 'vec', 'valid'))


class IceBossAttacksSolo(PhaseSolo):
    def __init__(self):
        super().__init__()
        self.report.update(scope='One owned standalone PIE; whole boss/player visual capture of native bomb and cold wave',
            limitations=['Only phase one is captured here; the separate phase probe covers global cold and prison rescue.',
                'Temporary far warm-circle placement and ability ready times isolate readable casts; encounter balance is not assessed.',
                'No network validation, cook, package or performance test.'])
        self.capture_started_at = None
        self.bomb_serial = self.wave_serial = None
        self.bomb_health_before = self.bomb_health_after = None
        self.wave_ice_history = []
        self.original_safe_anchor = None
        self.original_auto_camera = None
        self.review_hero_path = None

    def attack_floor(self):
        # The ordinary phase probe chooses the first exposed point. A bomb
        # review also needs room for its real +X knockback with yaw=0; avoid an
        # otherwise valid point just before the green throat intake.
        self.outside_point()
        zones = []
        for zone in self.actors(u.MCFoodDisposal):
            if isinstance(zone, u.MCThroat):
                geometry, offset = zone.get_actor_transform(), prop(zone, 'zone_center')
                strip, radius = prop(zone, 'delivery_strip_half_extent'), float(prop(zone, 'zone_radius'))
                half = u.Vector(strip.x, strip.y, float(prop(zone, 'zone_height'))) \
                    if prop(zone, 'use_delivery_strip') else u.Vector(radius, radius, float(prop(zone, 'zone_height')))
            else:
                volume = prop(zone, 'volume')
                geometry, offset, half = volume.get_world_transform(), u.Vector(0, 0, 0), volume.get_unscaled_box_extent()
            zones.append((geometry, offset, half))

        def delivery(point):
            for geometry, offset, half in zones:
                local, scale = geometry.inverse_transform_location(point)-offset, geometry.scale3d
                if abs(local.x) <= half.x+130./max(.01, abs(scale.x)) \
                        and abs(local.y) <= half.y+130./max(.01, abs(scale.y)):
                    return True
            return False

        transform = self.tongue.get_actor_transform()
        boss = transform.transform_location(prop(self.ice, 'candy_anchor'))
        old_safe = transform.transform_location(prop(self.ice, 'safe_anchor'))
        aura, circle = float(prop(self.ice, 'boss_frost_radius')), float(prop(self.ice, 'circle_radius'))
        for distance in (aura+180., aura+260., aura+350.):
            for angle in (math.pi*.5, -math.pi*.5, math.pi*.65, -math.pi*.65):
                floor = self.floor(boss+u.Vector(math.cos(angle)*distance, math.sin(angle)*distance, 0))
                if floor is None or self.ice.is_inside_cold_throne(floor) or delivery(floor) \
                        or math.hypot(floor.x-old_safe.x, floor.y-old_safe.y) <= circle+90.:
                    continue
                path = [floor+u.Vector(push, lateral, 0)
                        for push in (0., 250., 500., 750.) for lateral in (-100., 0., 100.)]
                supports = [self.floor(point) for point in path]
                if any(point is None or delivery(point) or abs(point.z-floor.z)>90. for point in supports):
                    continue
                self.report['outside_fixture'] = dict(floor_cm=vec(floor), boss_distance_cm=distance,
                    strong_aura_radius_cm=aura, original_safe_circle_cm=vec(old_safe),
                    expected_knockback_clearance_cm=750., expected_push_direction='World +X from yaw=0',
                    selection='Supported lateral point plus 750 cm push corridor outside both actual native intake geometries')
                return floor
        raise RuntimeError('No lateral bomb-review placement with supported safe knockback corridor')

    def ready(self, ability, now):
        values = dict(NextIceBombAt=now+300., NextColdWaveAt=now+300., NextIcePrisonAt=now+300.)
        if ability:
            values[ability] = now
        for name, value in values.items():
            self.ice.set_editor_property(name, value)
        self.report['fixture_tuning'].append(dict(game_seconds=now, **values,
            reason='Only the named cast is ready; normal authoritative scheduler and cast timings remain unchanged'))

    def wide_camera(self):
        boss = self.tongue.get_actor_transform().transform_location(prop(self.ice, 'candy_anchor'))
        hero = self.warm_floor
        distance = ((hero.x-boss.x)**2+(hero.y-boss.y)**2)**.5
        ray = u.Vector((hero.x-boss.x)/max(1., distance), (hero.y-boss.y)/max(1., distance), 0)
        side = u.Vector(-ray.y, ray.x, 0)
        _center, extent, _radius = u.SystemLibrary.get_component_bounds(prop(self.ice, 'body'))
        target = (boss+hero)*.5+u.Vector(0, 0, 100)
        span = max(1050., distance*.95)
        location = target+ray*(span*.35)+side*span+u.Vector(0, 0, max(650., extent.z+350.))
        self.frame_camera(location, target)
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 90.)
        self.report['review_camera'] = dict(location_cm=vec(location), target_cm=vec(target), fov=90.,
            boss_floor_cm=vec(boss), player_floor_cm=vec(hero), boss_player_separation_cm=distance)

    def keep_review_camera(self):
        if not valid(self.camera):
            return
        if self.original_auto_camera is None:
            self.original_auto_camera = bool(prop(self.pc, 'bAutoManageActiveCameraTarget'))
        self.pc.set_editor_property('bAutoManageActiveCameraTarget', False)
        self.pc.set_view_target_with_blend(self.camera, 0.)

    def camera_evidence(self):
        target = self.pc.get_view_target()
        actual = prop(self.pc, 'player_camera_manager').get_camera_location()
        wanted = self.camera.get_actor_location()
        error = ((actual.x-wanted.x)**2+(actual.y-wanted.y)**2+(actual.z-wanted.z)**2)**.5
        return dict(view_target=target.get_path_name() if valid(target) else None,
            review_camera=self.camera.get_path_name(), camera_manager_location_cm=vec(actual),
            requested_camera_location_cm=vec(wanted), location_error_cm=error)

    def screenshot(self, name, sequence=False):
        self.keep_review_camera()
        super().screenshot(name, sequence)
        self.report['frames' if sequence else 'screenshots'][-1]['camera'] = self.camera_evidence()

    def tick(self, delta):
        if self.done:
            return
        # These parent phases use the actual first-perk UI and F3 request; they
        # do not enter the parent's independent meter/prison scenario.
        if self.phase in ('waiting_pie', 'first_perk', 'perk_resolution'):
            super().tick(delta)
            return
        try:
            self.require(time.monotonic()-self.started < 180., 'Visual capture exceeded wall-time bound')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            self.require(self.world in worlds and len(worlds) == 1, 'Owned standalone PIE ended or was replaced')
            current = self.pc.get_controlled_pawn()
            self.require(valid(current) and isinstance(current, u.MCToothCharacter), 'Normal tooth hero is required')
            self.require(self.review_hero_path is None or current.get_path_name() == self.review_hero_path,
                'Owned review pawn was replaced; stopping the visual fixture immediately')
            self.hero = current
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            age = now-self.phase_at
            self.keep_review_camera()
            if self.phase == 'arrival':
                self.require(age < 10., 'F3 ice arrival did not become active')
                if self.ice is None:
                    events = self.actors(u.MCIceEvent)
                    self.require(len(events) == 1, 'F3 must create one ice owner')
                    self.ice = events[0]
                    self.tongue = prop(self.ice, 'tongue')
                    self.require(self.ice.uses_cold_throne_contract(), 'Normal ice contract is required')
                    self.ready(None, now)
                    self.review_hero_path = self.hero.get_path_name()
                    self.warm_floor = self.attack_floor()
                    old_anchor = prop(self.ice, 'safe_anchor')
                    # UE Python reflected structs can alias their property.
                    # Keep an independent value before mutating SafeAnchor.
                    self.original_safe_anchor = u.Vector(old_anchor.x, old_anchor.y, old_anchor.z)
                    anchor = self.tongue.get_actor_transform().inverse_transform_location(self.warm_floor)
                    self.ice.set_editor_property('SafeAnchor', anchor)
                    self.move_to(self.warm_floor)
                    self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
                    self.wide_camera()
                    self.report['fixture_tuning'].append(dict(game_seconds=now, SafeAnchor=vec(anchor),
                        reason='Temporary supported warm circle outside the strong aura keeps the only player alive for visual review'))
                if prop(self.ice, 'stage') == u.MCIceEventStage.ACTIVE:
                    self.capture_started_at = now
                    self.stage('local_aura')
                return
            self.require(now-self.capture_started_at < 40., 'Visual capture exceeded 40 game seconds')
            self.require(self.health() > 0. and not self.hero.is_freezing_to_death(), 'Review player died')
            self.require(not self.pc.is_reward_menu_open(), 'Unexpected reward interrupted the visual capture')
            if now >= self.sample_at:
                self.sample_at = now+.1
                self.sample(now)
            if self.capture_hz and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            attack = prop(self.ice, 'boss_attack')
            kind, serial = prop(attack, 'kind'), int(prop(attack, 'serial'))
            if self.phase == 'local_aura' and age >= 1.5:
                self.require(not prop(self.ice, 'second_phase') and self.safe(), 'Phase-one far warm-circle fixture is not safe')
                camera = self.camera_evidence()
                self.require(camera['view_target'] == camera['review_camera'] and camera['location_error_cm'] < 2.,
                    'Actual camera manager did not adopt the wide review camera: '+str(camera))
                self.report['checks']['actual_wide_camera_view_target_and_location'] = True
                self.screenshot('01_whole_boss_local_aura.png')
                self.bomb_health_before = self.health()
                self.ready('NextIceBombAt', now)
                self.stage('bomb_flight_and_impact')
            elif self.phase == 'bomb_flight_and_impact':
                self.require(age < 12., 'Ready native ice bomb was not observed')
                if kind == u.MCCandyAttack.MINI_CANDY:
                    self.bomb_serial = serial
                    if prop(attack, 'resolved') and now >= float(prop(attack, 'ends_at'))+.4:
                        self.bomb_health_after = self.health()
                        self.ready(None, now)
                        self.report['checks']['native_bomb_scheduler_and_resolve_observed'] = True
                        self.report['bomb'] = dict(serial=serial, health_before=self.bomb_health_before,
                            health_after=self.bomb_health_after, hero_after_cm=vec(self.hero.get_actor_location()),
                            target_radius_cm=float(prop(attack, 'target_radius')))
                        self.screenshot('02_bomb_after_native_impact.png')
                        self.stage('bomb_recovery')
            elif self.phase == 'bomb_recovery':
                self.require(age < 10., 'Native recovery did not return the player to standing')
                physics = prop(self.hero, 'tooth_physics')
                if age >= 1. and physics.get_body_state() == u.MCBodyState.STANDING:
                    self.move_to(self.warm_floor)
                    # Warmth intentionally blocks the real floor-wave hit. Put
                    # the native circle back before observing the standing hit;
                    # phase-one exposure outside the strong aura adds no meter.
                    self.ice.set_editor_property('SafeAnchor', self.original_safe_anchor)
                    self.report['fixture_tuning'].append(dict(game_seconds=now,
                        SafeAnchor=vec(self.original_safe_anchor),
                        reason='Restore the original distant circle before the wave; its native warm-zone immunity remains unchanged'))
                    self.ready('NextColdWaveAt', now)
                    self.stage('cold_wave')
            elif self.phase == 'cold_wave':
                self.require(age < 10., 'Ready wave did not freeze the standing player')
                if kind == u.MCCandyAttack.FLOOR_HIT:
                    self.wave_serial = serial
                if self.hero.has_frozen_legs():
                    self.ready(None, now)
                    self.report['checks']['native_ground_wave_froze_standing_player'] = True
                    self.wave_ice_history = [float(prop(self.hero, 'ice_leg_health'))]
                    self.screenshot('03_ground_wave_frozen_feet.png')
                    self.stage('wave_crest_continues')
            elif self.phase == 'wave_crest_continues' and age >= 1.:
                self.primary_input(True)
                self.stage('native_pickaxe_rescue')
            elif self.phase == 'native_pickaxe_rescue':
                self.require(age < 8., 'Held native Pickaxe did not clear wave-frozen feet')
                ice_health = float(prop(self.hero, 'ice_leg_health'))
                if ice_health != self.wave_ice_history[-1]:
                    self.wave_ice_history.append(ice_health)
                if not self.hero.has_frozen_legs():
                    self.primary_input(False)
                    self.report['checks']['native_primary_pickaxe_freed_wave_frozen_feet'] = True
                    self.report['wave'] = dict(serial=self.wave_serial, observed_ice_health_changes=self.wave_ice_history)
                    self.screenshot('04_wave_native_pickaxe_rescue.png')
                    self.stage('capture_flush')
            elif self.phase == 'capture_flush' and age >= .7:
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        if self.original_auto_camera is not None and valid(self.pc) \
                and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            self.pc.set_editor_property('bAutoManageActiveCameraTarget', self.original_auto_camera)
        super().finish(error)
        u.log('MC_ICE_BOSS_ATTACKS_'+('FAIL ' if error or not self.report.get('passed') else 'PASS ')
            +str(self.output/'Report.json'))


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('Ice attack capture already running')
    probe = IceBossAttacksSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
