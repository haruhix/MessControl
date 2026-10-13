"""Owned solo proof of actual 40-second cold expiry and its reverse frost front.

Normal first-perk/F3 bootstrap and the real 50% boss-HP threshold start the cold
cycle. Its clock is never shortened. After the first five seconds of outgoing
presentation, explicit PIE placement follows the actual native warm-circle
centre so the only player can survive until expiry. Ability ready timestamps
are delayed again for the long wait; the ordinary initial prison remains
enabled. No player HP, freeze rate, circle anchor/radius, collision, tool or
material parameter is overridden. The native 40-second rest is observed, not
accelerated or played through completely.

Genuine 6 Hz SHOWUI frames are captured only for entry and from two seconds
before actual expiry through reverse completion/rest. Report.clips.entry and
Report.clips.return preserve separate frame groups with their real game times;
an encoder must encode each group separately to avoid holding the 35-second
capture gap. The full frames/samples remain available in Report.json.

Reuses owned PIE/settings guards and cleanup. No build, asset save, network peer
or broad suite. Run in idle L_Mouth after the native patch is usable.
Optional globals: MC_ICE_RETURN_OUTPUT, MC_ICE_RETURN_CAPTURE_HZ.
Cancel: builtins.mc_ice_phase_return_solo.finish('Stopped by operator').
"""
import builtins
import math
import runpy
import time
import traceback
from pathlib import Path

import unreal as u


KEY = 'mc_ice_phase_return_solo'
OUTPUT = Path(globals().get('MC_ICE_RETURN_OUTPUT',
    Path(u.Paths.project_saved_dir())/'Checks/IcePhaseReturnSolo')).resolve()
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/check_ice_phase_transition_solo.py'),
    run_name='ice_phase_return_capture_support',
    init_globals={'MC_ICE_TRANSITION_OUTPUT': OUTPUT,
                  'MC_ICE_TRANSITION_CAPTURE_HZ': globals().get('MC_ICE_RETURN_CAPTURE_HZ', 6)})
TransitionSolo, VisualSolo, prop, vec, valid = (support[name] for name in
    ('IcePhaseTransitionSolo', 'VisualSolo', 'prop', 'vec', 'valid'))


class IcePhaseReturnSolo(TransitionSolo):
    def __init__(self):
        super().__init__()
        self.report.update(scope='One owned standalone PIE; real 40-second expiry and reverse frost presentation',
            limitations=[
                'Actual boss-HP threshold, delayed ability timestamps, player/review-camera placement are explicit PIE fixtures.',
                'Warmth uses the actual moving/shrinking native circles; neither warmth nor player HP is overridden.',
                'The ordinary initial prison is allowed; later attack readiness is deferred only for this long clock observation.',
                'Exact native 40-second active expiry and early rest are observed; no full battle, 80-second cycle or network claim.',
                'MID/MPC and gameplay reads require rendered-footage inspection for visual readability.',
                'No build, cook, package, broad suite or content-package save.'])
        self.report['clips'] = dict(entry=[], **{'return': []})
        self.report['native_warm_follow'] = dict(placement_count=0, centres=[], no_circle_property_overrides=True)
        self.outgoing_verified = False
        self.warm_follow_at = 0.
        self.last_warm_identity = None
        self.warm_centre_changed_at = 0.
        self.return_samples = []
        self.expiry_observed = False
        self.return_mid_still = False
        self.return_verified = False

    def capture_window(self, now):
        if self.native_latch_at is None or now-self.native_latch_at <= 5.5:
            return 'entry'
        if self.clock_end is not None and now >= self.clock_end-2.:
            return 'return'
        return None

    def screenshot(self, name, sequence=False):
        now = float(u.GameplayStatics.get_time_seconds(self.world))
        clip = self.capture_window(now)
        super().screenshot(name, sequence)
        entry = self.report['frames' if sequence else 'screenshots'][-1]
        entry['clip_window'] = clip
        entry['cold_expiry_game_seconds'] = self.clock_end
        if sequence and clip is not None:
            self.report['clips'][clip].append(entry)

    def follow_actual_warm_circle(self, now, force=False):
        if not force and now < self.warm_follow_at:
            return
        self.warm_follow_at = now+.25
        mid = prop(self.ice, 'frost_surface').get_material(0)
        # These flags are authored by native IsCircleBlocked/AnchorFloor. Do
        # not invent a safe zone or call a non-reflected gameplay helper.
        candidates = [('Warm Enabled', 'SafeAnchor', int(prop(self.ice, 'circle_index')))]
        if prop(self.ice, 'next_circle'):
            candidates.append(('Next Warm Enabled', 'NextSafeAnchor', int(prop(self.ice, 'circle_index'))+1))
        transform = self.tongue.get_actor_transform()
        chosen = None
        for enabled, anchor_name, index in candidates:
            if float(mid.call_method('K2_GetScalarParameterValue', args=(enabled,))) <= .99:
                continue
            anchor = prop(self.ice, anchor_name)
            floor = self.floor(transform.transform_location(anchor))
            if floor is None or self.ice.is_inside_cold_throne(floor):
                continue
            chosen = (floor, anchor, anchor_name, index)
            break
        self.require(chosen is not None, 'No actual unblocked native warm-circle centre for the long survival wait')
        floor, anchor, anchor_name, index = chosen
        self.move_to(floor)
        identity = (anchor_name, index, round(anchor.x, 2), round(anchor.y, 2))
        follow = self.report['native_warm_follow']
        follow['placement_count'] += 1
        if force or identity != self.last_warm_identity:
            self.warm_centre_changed_at = now
            follow['centres'].append(dict(game_seconds=now, circle_index=index, source_anchor=anchor_name,
                local_anchor_cm=vec(anchor), actual_supported_floor_cm=vec(floor),
                explanation='Position-only PIE fixture follows the current native safe centre; no circle/warmth property is changed'))
        self.last_warm_identity = identity

    def sample(self, now):
        if not self.outgoing_verified:
            TransitionSolo.sample(self, now)
        else:
            VisualSolo.sample(self, now)
            self.report['samples'][-1]['bursts'] = self.burst_state()
        sample = self.report['samples'][-1]
        sample['seconds_since_native_phase_latch'] = None if self.native_latch_at is None else now-self.native_latch_at
        sample['seconds_since_native_cold_expiry'] = None if self.clock_end is None else now-self.clock_end
        sample['next_cold_throne_at'] = float(prop(self.ice, 'next_cold_throne_at'))
        sample['clip_window'] = self.capture_window(now)
        if self.clock_end is not None and now >= self.clock_end:
            state = sample['visual_split']
            self.return_samples.append(dict(game_seconds=now, return_age=now-self.clock_end,
                global_cold=bool(sample['global_cold']), global_seconds_remaining=sample['global_seconds_remaining'],
                global_cooldown_remaining=sample['global_cooldown_remaining'],
                zone_enabled=state['scalars']['Frost Zone Enabled'], radius_cm=state['scalars']['Frost Zone Radius'],
                sweep=state['scalars']['Frost Sweep'], global_cold_amount=state['global_cold_amount'],
                ice_amount=state['scalars']['IceAmount'], freeze_amount=sample['freeze_amount'], safe=sample['safe']))

    def clock_assertions(self, now):
        self.require(abs(float(prop(self.ice, 'cold_throne_ends_at'))-self.clock_end) < .001,
            'The native active deadline changed during the actual 40-second observation')
        next_at = float(prop(self.ice, 'next_cold_throne_at'))
        self.require(abs(next_at-self.clock_end-40.) < .001, 'Native rest no longer lasts the original 40 seconds')
        if now < self.clock_end:
            self.require(self.ice.is_global_cold_active(), 'Native global cold stopped before actual expiry')
            self.require(abs(float(self.ice.cold_throne_seconds_remaining())-(self.clock_end-now)) < .15,
                'Native remaining cold seconds do not follow the unchanged clock')
        else:
            self.require(not self.ice.is_global_cold_active() and self.ice.cold_throne_seconds_remaining() <= .001,
                'Gameplay global cold remained active after its real deadline')
            self.require(abs(float(self.ice.cold_throne_cooldown_remaining())-(next_at-now)) < .15,
                'Native cooldown does not follow the unchanged rest deadline')

    def validate_reverse(self):
        active = [sample for sample in self.return_samples if sample['sweep'] > .99
                  and sample['zone_enabled'] > .99 and not sample['global_cold']]
        self.require(len(active) >= 5, 'Too few actual MID samples during the native reverse front')
        radii = [sample['radius_cm'] for sample in active]
        self.require(all(math.isfinite(radius) for radius in radii), 'Reverse frost radius became non-finite')
        self.require(all(b <= a+.5 for a, b in zip(radii, radii[1:])), 'Frost radius grew during the reverse front')
        decreasing = sum(b < a-1. for a, b in zip(radii, radii[1:]))
        self.require(decreasing >= 4 and radii[0]-radii[-1] > 250., 'Reverse front did not show several distinct shrinking radii')
        climates = [sample['global_cold_amount'] for sample in self.return_samples]
        self.require(all(b <= a+.015 for a, b in zip(climates, climates[1:])),
            'Global environment climate grew after actual cold expiry')
        self.require(any(.02 < amount < .95 for amount in climates), 'Global climate skipped its gradual reverse fade')
        self.require(all(sample['global_cold_amount'] <= .015 for sample in self.return_samples if sample['return_age'] >= .8),
            'Global climate did not clear after its native 0.65-second reverse fade')
        state = self.split_assertions()
        self.require(state['scalars']['Frost Sweep'] <= .001, 'Completed rest retained the reverse-front material flag')
        self.require(not self.ice.is_global_cold_active(), 'Native gameplay global cold remained active in rest')
        self.require(self.safe() and self.freeze() <= .001, 'Real native warm circle did not keep the review player warm and alive')
        self.report['checks']['actual_forty_second_gameplay_expiry_stops_global_cold_immediately'] = True
        self.report['checks']['native_reverse_front_has_multiple_monotonically_decreasing_radii'] = True
        self.report['checks']['reverse_environment_climate_reaches_zero'] = True
        self.report['checks']['rest_retains_local_aura_slick_ice_and_native_warmth'] = True
        self.report['return_evidence'] = dict(samples=self.return_samples, enabled_reverse_samples=len(active),
            decreasing_radius_steps=decreasing, first_radius_cm=radii[0], last_sweep_radius_cm=radii[-1],
            final_radius_cm=state['scalars']['Frost Zone Radius'], final_global_cold_amount=state['global_cold_amount'],
            final_sweep=state['scalars']['Frost Sweep'], final_zone_enabled=state['scalars']['Frost Zone Enabled'],
            final_freeze_amount=self.freeze(), final_safe=self.safe(), final_visual_state=state)

    def tick(self, delta):
        if self.done:
            return
        if self.phase in ('waiting_pie', 'first_perk', 'perk_resolution', 'arrival'):
            VisualSolo.tick(self, delta)
            return
        try:
            self.require(time.monotonic()-self.started < 180., 'Real-expiry probe exceeded 180 wall seconds')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            self.require(self.world in worlds and len(worlds) == 1, 'Owned standalone PIE ended or was replaced')
            current = self.pc.get_controlled_pawn()
            self.require(valid(current) and isinstance(current, u.MCToothCharacter), 'Normal tooth hero required')
            self.require(current.get_path_name() == self.review_hero_path, 'Owned review pawn was replaced')
            self.hero = current
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            age = now-self.phase_at
            self.keep_review_camera()
            self.require(now-self.capture_started_at < 55., 'Real-expiry probe exceeded 55 game seconds')
            self.require(self.health() > 0. and not self.hero.is_freezing_to_death(), 'Native attack/cold killed the review player')
            self.require(not self.pc.is_reward_menu_open(), 'Unexpected reward interrupted real-expiry review')
            if prop(self.ice, 'second_phase') and self.native_latch_at is None:
                active_seconds = float(prop(self.ice, 'cold_throne_active_seconds'))
                self.require(abs(active_seconds-40.) < .001, 'Native global cold is not using the actual 40-second duration')
                self.clock_end = float(prop(self.ice, 'cold_throne_ends_at'))
                self.native_latch_at = self.clock_end-active_seconds
                self.require(self.threshold_at is not None and abs(self.native_latch_at-self.threshold_at) < .2,
                    'Native cold latch did not follow the actual boss-HP threshold')
                self.report['native_phase_latch'] = dict(game_seconds=self.native_latch_at,
                    cold_throne_ends_at=self.clock_end, active_seconds=active_seconds,
                    next_cold_throne_at=float(prop(self.ice, 'next_cold_throne_at')), actual_clock_not_shortened=True)
            if self.native_latch_at is not None:
                self.clock_assertions(now)
            if self.phase in ('wait_in_native_warm_zone', 'expiry_and_reverse'):
                self.follow_actual_warm_circle(now)
                # Player.bSafe is updated by the next native game tick after
                # a position fixture follows a newly handed-off circle.
                if age > .4 and now-self.warm_centre_changed_at > .35:
                    self.require(self.safe(), 'Following the actual warm-circle centre did not produce native player warmth')
            if now >= self.sample_at:
                self.sample_at = now+(.25 if self.capture_window(now) is None else .1)
                self.sample(now)
            if self.capture_hz and self.capture_window(now) is not None and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase == 'phase_one_overview' and age >= 1.:
                self.require(self.freeze() <= .001 and not self.safe(), 'Exposed phase-one player accumulated global cold')
                self.require(abs(self.health()-self.initial_health) < .01, 'Unexpected phase-one damage')
                self.report['phase_one_visual_state'] = self.split_assertions()
                camera = self.camera_evidence()
                self.require(camera['view_target'] == camera['review_camera'] and camera['location_error_cm'] < 2.,
                    'Actual camera manager did not adopt the expiry overview')
                self.report['checks']['phase_one_local_frost_global_slick_ice_confirmed'] = True
                self.report['checks']['actual_overview_camera_confirmed'] = True
                self.screenshot('01_phase_one_before_actual_cold_cycle.png')
                self.stage('phase_one_still_flush')
            elif self.phase == 'phase_one_still_flush' and age >= .35:
                threshold = float(prop(self.ice, 'max_candy_health'))*.5
                self.ice.set_editor_property('CandyHealth', threshold)
                self.threshold_at = now
                self.report['fixture_tuning'].append(dict(game_seconds=now, CandyHealth=threshold,
                    reason='Actual <=50% boss-HP threshold; active/rest clocks and visual durations are never changed'))
                self.stage('outgoing_cold_front')
            elif self.phase == 'outgoing_cold_front' and self.native_latch_at is not None:
                elapsed = now-self.native_latch_at
                if elapsed >= 1.6 and not self.mid_still_taken:
                    self.mid_still_taken = True
                    self.screenshot('02_outgoing_native_radial_front.png')
                if elapsed >= 5.:
                    self.validate_transition()
                    self.report['full_global_visual_state'] = self.full_phase_assertions(now)
                    self.report['checks']['native_outgoing_front_completed_before_real_clock_wait'] = True
                    self.outgoing_verified = True
                    self.screenshot('03_global_cold_before_warm_survival_wait.png')
                    self.stage('outgoing_still_flush')
            elif self.phase == 'outgoing_still_flush' and age >= .35:
                self.defer_all_abilities(now)
                self.follow_actual_warm_circle(now, force=True)
                self.report['health_after_initial_native_prison'] = self.health()
                self.stage('wait_in_native_warm_zone')
            elif self.phase == 'wait_in_native_warm_zone' and now >= self.clock_end-2.:
                self.report['checks']['actual_clock_wait_survived_in_native_warm_circle'] = True
                self.screenshot('04_before_actual_forty_second_expiry.png')
                self.stage('expiry_and_reverse')
            elif self.phase == 'expiry_and_reverse':
                return_age = now-self.clock_end
                if return_age >= 0. and not self.expiry_observed:
                    self.expiry_observed = True
                    self.report['actual_expiry_observation'] = dict(game_seconds=now, deadline=self.clock_end,
                        elapsed_since_latch=now-self.native_latch_at, gameplay_global_cold=bool(self.ice.is_global_cold_active()),
                        cold_seconds_remaining=float(self.ice.cold_throne_seconds_remaining()),
                        native_cooldown_seconds=float(self.ice.cold_throne_cooldown_remaining()))
                if return_age >= 1.6 and not self.return_mid_still:
                    self.return_mid_still = True
                    self.screenshot('05_native_reverse_radial_front.png')
                if return_age >= 3.4:
                    self.validate_reverse()
                    self.return_verified = True
                    self.screenshot('06_reverse_complete_local_aura_and_slick_ice.png')
                    self.stage('rest_local_aura_hold')
            elif self.phase == 'rest_local_aura_hold' and age >= .6:
                self.require(self.return_verified and not self.ice.is_global_cold_active() and self.freeze() <= .001,
                    'Native rest retained global cold or accumulated freeze outside the local boss aura')
                state = self.split_assertions()
                self.require(state['scalars']['Frost Sweep'] <= .001, 'Native rest retained the reverse-front sweep flag')
                self.report['rest_hold_visual_state'] = state
                self.report['rest_hold_player_state'] = dict(freeze_amount=self.freeze(), safe=self.safe(),
                    explanation='After reverse completion global cold is off; native warm-circle handoff does not require the player to remain warm')
                self.report['health_before_and_after'] = [self.initial_health, self.health()]
                self.report['clock_evidence'] = dict(active_start=self.native_latch_at, actual_expiry=self.clock_end,
                    observed_active_duration=self.clock_end-self.native_latch_at,
                    next_active_at=float(prop(self.ice, 'next_cold_throne_at')),
                    observed_rest_duration=float(prop(self.ice, 'next_cold_throne_at'))-self.clock_end)
                self.screenshot('07_rest_after_actual_global_cold_expiry.png')
                self.stage('capture_flush')
            elif self.phase == 'capture_flush' and age >= .5:
                self.report['capture_game_seconds'] = now-self.capture_started_at
                self.report['captured_game_windows'] = {name: dict(frame_count=len(frames),
                    start_game_seconds=frames[0]['time'] if frames else None,
                    end_game_seconds=frames[-1]['time'] if frames else None)
                    for name, frames in self.report['clips'].items()}
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        super().finish(error)
        u.log('MC_ICE_PHASE_RETURN_'+('FAIL ' if error or not self.report.get('passed') else 'PASS ')
            +str(self.output/'Report.json'))


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('Ice actual-expiry capture already running')
    probe = IcePhaseReturnSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
