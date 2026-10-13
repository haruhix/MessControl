"""Short owned solo render check of slick tongue ice versus local deadly frost.

Run in an idle L_Mouth Editor after the native/material changes are compiled.
Normal first-perk and F3 paths create the ice event. Ability ready timestamps
are delayed only in this PIE; player placement and a review camera are explicit
fixtures. The optional phase-two comparison crosses the actual 50% HP threshold
without accelerating its native 40-second timer. No damage, freeze rate, foot-ice
health, material parameter or locomotion rule is overridden by this script.
Entering phase two can make the native prison ready immediately; its normal
damage is allowed and recorded during that comparison. Screenshot requests get
a brief game-time flush before changing player placement or phase threshold.

Genuine SHOWUI frames default to 6 Hz, with actual MID/MPC and gameplay evidence
in Saved/Checks/IceVisualSplitSolo/Runs/<run>/Report.json. Rendered readability
needs visual inspection; parameter checks do not substitute for that inspection.
No asset is saved, build started or network peer run. All owned PIE/settings
cleanup comes from the existing guarded phase/attack probes.
Optional globals: MC_ICE_VISUAL_OUTPUT, MC_ICE_VISUAL_CAPTURE_HZ,
MC_ICE_VISUAL_PHASE_TWO (default True).
Cancel: builtins.mc_ice_visual_split_solo.finish('Stopped by operator').
"""
import builtins
import math
import runpy
import time
import traceback
from pathlib import Path

import unreal as u


KEY = 'mc_ice_visual_split_solo'
OUTPUT = Path(globals().get('MC_ICE_VISUAL_OUTPUT',
    Path(u.Paths.project_saved_dir())/'Checks/IceVisualSplitSolo')).resolve()
COMPARE_PHASE_TWO = bool(globals().get('MC_ICE_VISUAL_PHASE_TWO', True))
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/capture_ice_boss_attacks_solo.py'),
    run_name='ice_visual_split_capture_support',
    init_globals={'MC_ICE_ATTACK_OUTPUT': OUTPUT,
                  'MC_ICE_ATTACK_CAPTURE_HZ': globals().get('MC_ICE_VISUAL_CAPTURE_HZ', 6)})
AttackSolo, PhaseSolo, prop, vec, valid = (support[name] for name in
    ('IceBossAttacksSolo', 'PhaseSolo', 'prop', 'vec', 'valid'))


class IceVisualSplitSolo(AttackSolo):
    def __init__(self):
        super().__init__()
        self.report.update(scope='One owned standalone PIE; rendered phase-one slick ice/local frost split',
            limitations=[
                'Temporary player/review-camera placement and delayed ability timestamps isolate surface readability.',
                'Phase two, when enabled, uses an explicitly recorded 50% boss-HP PIE fixture; no battle/balance claim.',
                'The native phase-two transition can reset prison readiness; normal attack damage during comparison is allowed and recorded.',
                'MID/MPC reads verify native state only; the rendered tongue and walls require visual inspection.',
                'No network, cook, package, broad suite or asset save is performed.'])
        self.report['package_save_calls'] = 0
        self.material_library = u.get_default_object(u.load_class(None, '/Script/Engine.KismetMaterialLibrary'))
        self.climate = u.load_asset('/Game/Gameplay/Cold/MPC_MouthClimate')
        self.require(valid(self.material_library) and valid(self.climate), 'Native climate read API/collection unavailable')
        self.inside_floor = None
        self.inside_gauge = None
        self.initial_health = None
        self.phase_one_material = None

    def defer_all_abilities(self, now):
        values = {name: now+300. for name in ('NextIceBombAt', 'NextColdWaveAt', 'NextIcePrisonAt')}
        for name, value in values.items():
            self.ice.set_editor_property(name, value)
        self.report['fixture_tuning'].append(dict(game_seconds=now, **values,
            reason='Only ready timestamps are postponed to isolate surface/cold-zone readability in this PIE'))

    def material_state(self):
        mid = prop(self.ice, 'frost_surface').get_material(0)
        self.require(valid(mid) and isinstance(mid, u.MaterialInstanceDynamic), 'Native frost MID is not ready')
        scalars = {name: float(mid.call_method('K2_GetScalarParameterValue', args=(name,)))
                   for name in ('IceAmount', 'Frost Zone Enabled', 'Frost Zone Radius', 'Frost Zone Height')}
        center = mid.call_method('K2_GetVectorParameterValue', args=('Frost Zone Center',))
        # KismetMaterialLibrary.h exposes the explicit world-context UFUNCTION.
        # call_method avoids a Python convenience wrapper selecting editor world.
        climate = self.material_library.call_method('GetScalarParameterValue',
            args=(self.world, self.climate, 'ColdAmount'))
        return dict(mid=mid.get_path_name(), scalars=scalars,
            frost_zone_center_cm=[float(center.r), float(center.g), float(center.b)],
            climate_collection=self.climate.get_path_name(), global_cold_amount=float(climate))

    def split_assertions(self):
        state = self.material_state()
        scalars = state['scalars']
        self.require(abs(scalars['Frost Zone Enabled']-1.) < .001, 'Phase one did not enable the local frost material mask')
        self.require(abs(scalars['Frost Zone Radius']-float(prop(self.ice, 'boss_frost_radius'))) < .01,
            'Visual frost radius differs from native deadly aura radius')
        self.require(abs(scalars['Frost Zone Height']-180.) < .01, 'Local frost height differs from the native aura height')
        self.require(scalars['IceAmount'] >= .99, 'Whole tongue ice was disabled while separating local frost')
        self.require(abs(state['global_cold_amount']) < .001, 'Phase-one global environment frost is still enabled')
        boss = self.tongue.get_actor_transform().transform_location(prop(self.ice, 'candy_anchor'))
        center = state['frost_zone_center_cm']
        self.require(math.hypot(center[0]-boss.x, center[1]-boss.y) < 2., 'Visual frost is not centred on actual boss anchor')
        floors = [actor for actor in self.actors(u.MCLocomotionSurface)
                  if actor.get_owner() == self.ice and prop(actor, 'surface') == u.MCGroundSurface.SLIPPERY]
        self.require(len(floors) == 1, 'The native whole-arena slippery surface was removed or duplicated')
        self.require(prop(self.hero.get_movement_component(), 'ground_surface') == u.MCGroundSurface.SLIPPERY,
            'Far player is not using the actual native slippery ground response')
        state['slippery_owner'] = floors[0].get_owner().get_path_name()
        state['slippery_actor'] = floors[0].get_path_name()
        state['player_native_ground_surface'] = str(prop(self.hero.get_movement_component(), 'ground_surface'))
        return state

    def find_inside_floor(self):
        boss = self.tongue.get_actor_transform().transform_location(prop(self.ice, 'candy_anchor'))
        radius = float(prop(self.ice, 'boss_frost_radius'))
        for fraction in (.65, .72, .8):
            for index in range(24):
                angle = index*math.tau/24.
                floor = self.floor(boss+u.Vector(math.cos(angle)*radius*fraction, math.sin(angle)*radius*fraction, 0))
                if floor is None or not self.ice.is_inside_cold_throne(floor):
                    continue
                supports = [self.floor(floor+u.Vector(dx, dy, 0))
                            for dx, dy in ((60, 0), (-60, 0), (0, 60), (0, -60))]
                if any(point is None or abs(point.z-floor.z)>40. for point in supports):
                    continue
                self.report['inside_fixture'] = dict(floor_cm=vec(floor), boss_cm=vec(boss),
                    distance_cm=math.hypot(floor.x-boss.x, floor.y-boss.y), native_radius_cm=radius)
                return floor
        raise RuntimeError('No supported inner-aura tongue footprint clear of the boss centre')

    def overview_camera(self):
        center, extent, _radius = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        target = center+u.Vector(0, 0, 80)
        # Stay below the mouth roof while opening a broad view of both tongue
        # zones and adjacent teeth/gums. The actual view target is also checked.
        span = max(1050., extent.x*1.3, extent.y*1.3)
        location = target+u.Vector(span*.28, -span*.8, min(950., max(650., span*.55)))
        self.frame_camera(location, target)
        prop(self.camera, 'camera_component').set_editor_property('FieldOfView', 100.)
        self.report['review_camera'] = dict(location_cm=vec(location), target_cm=vec(target), fov=100.,
            tongue_center_cm=vec(center), tongue_half_extent_cm=vec(extent))
        self.keep_review_camera()

    def sample(self, now):
        super().sample(now)
        self.report['samples'][-1]['visual_split'] = self.material_state()
        self.report['samples'][-1]['native_ground_surface'] = str(prop(self.hero.get_movement_component(), 'ground_surface'))

    def tick(self, delta):
        if self.done:
            return
        if self.phase in ('waiting_pie', 'first_perk', 'perk_resolution'):
            PhaseSolo.tick(self, delta)
            return
        try:
            self.require(time.monotonic()-self.started < 120., 'Visual split check exceeded 120 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            self.require(self.world in worlds and len(worlds) == 1, 'Owned standalone PIE ended or was replaced')
            current = self.pc.get_controlled_pawn()
            self.require(valid(current) and isinstance(current, u.MCToothCharacter), 'Normal tooth hero is required')
            self.require(self.review_hero_path is None or current.get_path_name() == self.review_hero_path,
                'Owned review pawn was replaced; stopping visual fixtures')
            self.hero = current
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            age = now-self.phase_at
            self.keep_review_camera()
            if self.phase == 'arrival':
                self.require(age < 10., 'F3 winter arrival did not become active')
                if self.ice is None:
                    events = self.actors(u.MCIceEvent)
                    self.require(len(events) == 1, 'F3 must create exactly one winter owner')
                    self.ice = events[0]
                    self.tongue = prop(self.ice, 'tongue')
                    self.require(valid(self.tongue) and self.ice.uses_cold_throne_contract(), 'Normal winter contract required')
                    self.require(not self.actors(u.MCColdColaEvent), 'A concurrent cola would confound global climate evidence')
                    self.review_hero_path = self.hero.get_path_name()
                    self.initial_health = self.health()
                    self.defer_all_abilities(now)
                    self.outside_floor = self.outside_point()
                    self.inside_floor = self.find_inside_floor()
                    self.move_to(self.outside_floor)
                    self.overview_camera()
                if prop(self.ice, 'stage') == u.MCIceEventStage.ACTIVE:
                    self.capture_started_at = now
                    self.stage('phase_one_overview')
                return
            self.require(now-self.capture_started_at < 20., 'Visual split check exceeded 20 game seconds')
            self.require(self.health() > 0. and not self.hero.is_freezing_to_death(), 'Review player reached fatal cold')
            if not prop(self.ice, 'second_phase'):
                self.require(abs(self.health()-self.initial_health) < .01,
                    'An unrelated hazard damaged the phase-one visual-review player')
            self.require(not self.pc.is_reward_menu_open(), 'Unexpected reward interrupted visual review')
            if now >= self.sample_at:
                self.sample_at = now+.1
                self.sample(now)
            if self.capture_hz and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase == 'phase_one_overview' and age >= 4.:
                self.require(self.freeze() <= .001 and not self.safe(), 'Far exposed player accumulated phase-one cold')
                self.phase_one_material = self.split_assertions()
                self.report['phase_one_visual_state'] = self.phase_one_material
                camera = self.camera_evidence()
                self.require(camera['view_target'] == camera['review_camera'] and camera['location_error_cm'] < 2.,
                    'Actual camera manager did not adopt overview camera: '+str(camera))
                self.report['checks']['actual_overview_camera_confirmed'] = True
                self.report['checks']['native_phase_one_local_frost_global_slick_ice'] = True
                self.report['checks']['far_player_has_zero_freeze_and_slippery_ground'] = True
                self.screenshot('01_phase_one_slick_tongue_local_frost.png')
                self.stage('phase_one_still_flush')
            elif self.phase == 'phase_one_still_flush' and age >= .35:
                self.move_to(self.inside_floor)
                self.stage('inside_local_aura')
            elif self.phase == 'inside_local_aura' and age >= .8:
                self.inside_gauge = self.freeze()
                self.require(.04 < self.inside_gauge < .5 and not self.safe(), 'Inside aura did not build gradual native cold')
                self.report['checks']['only_local_aura_builds_phase_one_freeze'] = True
                self.report['inside_aura_observation'] = dict(freeze_amount=self.inside_gauge,
                    game_seconds=now, hero_cm=vec(self.hero.get_actor_location()))
                self.screenshot('02_inside_local_frost_meter.png')
                self.stage('inside_still_flush')
            elif self.phase == 'inside_still_flush' and age >= .35:
                # Freezing continues natively while the still renders. Use the
                # actual departure amount for the later outside-growth check.
                self.inside_gauge = self.freeze()
                self.report['inside_aura_observation']['freeze_at_departure'] = self.inside_gauge
                self.report['inside_aura_observation']['departure_game_seconds'] = now
                self.move_to(self.outside_floor)
                self.stage('retreat_to_slick_ice')
            elif self.phase == 'retreat_to_slick_ice' and age >= 3.:
                self.require(abs(self.freeze()-self.inside_gauge) < .035 and not self.safe(),
                    'Freeze kept building outside phase-one aura or fixture unexpectedly entered warmth')
                self.split_assertions()
                self.report['checks']['retreat_stops_freeze_gain_without_disabling_slippery_floor'] = True
                self.report['checks']['phase_one_review_health_unchanged'] = True
                self.screenshot('03_outside_frost_only_slick_ice.png')
                self.stage('retreat_still_flush')
            elif self.phase == 'retreat_still_flush' and age >= .35:
                if COMPARE_PHASE_TWO:
                    threshold = float(prop(self.ice, 'max_candy_health'))*.5
                    self.ice.set_editor_property('CandyHealth', threshold)
                    self.report['fixture_tuning'].append(dict(game_seconds=now, CandyHealth=threshold,
                        reason='Optional comparison triggers the actual phase-two threshold; native 40-second clock stays unchanged'))
                    self.stage('phase_two_comparison')
                else:
                    self.stage('capture_flush')
            elif self.phase == 'phase_two_comparison' and age >= 4.:
                self.require(prop(self.ice, 'second_phase') and self.ice.is_global_cold_active(), 'Native phase two did not start')
                state = self.material_state()
                self.require(abs(state['scalars']['Frost Zone Enabled']) < .001, 'Phase-two frost is still limited by phase-one mask')
                self.require(state['scalars']['IceAmount'] >= .99 and state['global_cold_amount'] >= .99,
                    'Phase-two global tongue/environment frost did not return')
                self.require(34. < self.ice.cold_throne_seconds_remaining() <= 40., 'Native 40-second timer was shortened')
                self.require(self.freeze() > self.inside_gauge+.1, 'Phase-two weak global cold did not affect distant player')
                self.report['checks']['phase_two_global_frost_and_native_forty_second_clock_preserved'] = True
                self.report['phase_two_visual_state'] = state
                self.report['phase_two_native_attacks'] = dict(
                    health_before=self.initial_health, health_at_comparison=self.health(),
                    observations=[dict(game_seconds=sample['game_seconds'], kind=sample['attack'],
                        serial=sample['attack_serial'], frozen_legs=sample['frozen_legs'], health=sample['health'])
                        for sample in self.report['samples'] if sample['phase'] == 'phase_two_comparison'],
                    explanation='Native phase transition can reset NextIcePrisonAt to now; its ordinary damage remains enabled')
                self.screenshot('04_phase_two_existing_global_frost.png')
                self.stage('capture_flush')
            elif self.phase == 'capture_flush' and age >= .7:
                self.report['capture_game_seconds'] = now-self.capture_started_at
                self.report['health_before_and_after'] = [self.initial_health, self.health()]
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        super().finish(error)
        u.log('MC_ICE_VISUAL_SPLIT_'+('FAIL ' if error or not self.report.get('passed') else 'PASS ')
            +str(self.output/'Report.json'))


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('Ice visual split check already running')
    probe = IceVisualSplitSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
