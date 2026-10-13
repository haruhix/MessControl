"""Owned short solo capture of the native phase-I -> phase-II frost sweep.

Run in idle L_Mouth after native/material authoring has finished. Reuses the
guarded first-perk/F3 winter path, camera and owned-PIE/settings restoration of
check_ice_visual_split_solo.py. Ready timestamps are delayed in this PIE, and
boss HP is set to its real 50% threshold after the phase-one overview. Player
HP, damage, material parameters, transition duration and 40-second clock remain
native. Phase-two prison can become ready at the native latch; its ordinary
damage is observed rather than suppressed or considered a failure.

Record ~2 seconds of phase one, 5 seconds of the unaccelerated transition and
2 seconds of full phase two, plus short screenshot flushes. Genuine SHOWUI
frames default to 6 Hz. Actual MID Frost Zone Enabled/Radius, Frost Sweep and
MPC ColdAmount accompany native phase/meter/clock and burst-telemetry availability.
No parameter/pixel check replaces visual inspection of the resulting footage.
No asset saves, build, network peers or full 80-second-cycle check.
Optional globals: MC_ICE_TRANSITION_OUTPUT, MC_ICE_TRANSITION_CAPTURE_HZ.
Cancel: builtins.mc_ice_phase_transition_solo.finish('Stopped by operator').
"""
import builtins
import math
import runpy
import time
import traceback
from pathlib import Path

import unreal as u


KEY = 'mc_ice_phase_transition_solo'
OUTPUT = Path(globals().get('MC_ICE_TRANSITION_OUTPUT',
    Path(u.Paths.project_saved_dir())/'Checks/IcePhaseTransitionSolo')).resolve()
support = runpy.run_path(str(Path(u.Paths.project_dir())/'Tools/Unreal/check_ice_visual_split_solo.py'),
    run_name='ice_phase_transition_capture_support',
    init_globals={'MC_ICE_VISUAL_OUTPUT': OUTPUT,
                  'MC_ICE_VISUAL_CAPTURE_HZ': globals().get('MC_ICE_TRANSITION_CAPTURE_HZ', 6)})
VisualSolo, prop, vec, valid = (support[name] for name in ('IceVisualSplitSolo', 'prop', 'vec', 'valid'))


class IcePhaseTransitionSolo(VisualSolo):
    def __init__(self):
        super().__init__()
        self.report.update(scope='One owned standalone PIE; native radial phase-I to phase-II frost transition',
            limitations=[
                'Player/camera placement, delayed ability timestamps and the actual 50% boss-HP threshold are explicit PIE fixtures.',
                'Native phase-two prison readiness and damage remain enabled; player health is not overridden.',
                'A short visual-timing check; no complete battle, 80-second cycle, balance or network claim.',
                'Actual MID/MPC readings need rendered-footage inspection; protected native burst counts are unavailable to this probe.',
                'No build, cook, package, broad suite or content-package save.'])
        self.native_latch_at = None
        self.threshold_at = None
        self.mid_still_taken = False
        self.transition_samples = []
        self.clock_end = None

    def material_state(self):
        state = super().material_state()
        mid = prop(self.ice, 'frost_surface').get_material(0)
        for name in ('Frost Sweep', 'Frost Aura Radius'):
            state['scalars'][name] = float(mid.call_method('K2_GetScalarParameterValue', args=(name,)))
        return state

    def burst_state(self):
        # Actor inventory is public evidence in this owned world. EventVFX and
        # Bursts are protected properties; the native-only burst getter is not
        # callable from Python. Report that limit without reflection bypasses.
        presentations = [actor for actor in self.actors(u.MCIceEventVFX) if valid(actor)]
        paths = [actor.get_path_name() for actor in presentations]
        return dict(present=bool(paths), actor=paths[0] if len(paths) == 1 else None,
            actors=paths, active_burst_count=None, available=False,
            reason='Protected EventVFX/Bursts properties and no public reflected burst-count getter; actor presence only')

    def sample(self, now):
        super().sample(now)
        sample = self.report['samples'][-1]
        sample['bursts'] = self.burst_state()
        sample['seconds_since_native_phase_latch'] = None if self.native_latch_at is None else now-self.native_latch_at
        if prop(self.ice, 'second_phase'):
            self.transition_samples.append(dict(game_seconds=now,
                phase_seconds=None if self.native_latch_at is None else now-self.native_latch_at,
                zone_enabled=sample['visual_split']['scalars']['Frost Zone Enabled'],
                radius_cm=sample['visual_split']['scalars']['Frost Zone Radius'],
                sweep=sample['visual_split']['scalars']['Frost Sweep'],
                global_cold_amount=sample['visual_split']['global_cold_amount'],
                active_burst_count=sample['bursts']['active_burst_count']))

    def validate_transition(self):
        sweep = [sample for sample in self.transition_samples
                 if sample['zone_enabled'] > .99 and sample['sweep'] > .99]
        self.require(len(sweep) >= 5, 'Too few actual MID samples during the native radial transition')
        radii = [sample['radius_cm'] for sample in sweep]
        self.require(all(math.isfinite(radius) for radius in radii), 'Native transition produced a non-finite radius')
        self.require(all(b >= a-.5 for a, b in zip(radii, radii[1:])),
            'Native Frost Zone Radius shrank while the phase-two sweep was active')
        growing = sum(b > a+1. for a, b in zip(radii, radii[1:]))
        self.require(growing >= 4 and radii[-1]-radii[0] > 250.,
            'The native transition did not show several distinct outward-growing radii')
        climates = [sample['global_cold_amount'] for sample in sweep]
        self.require(all(-.001 <= amount <= 1.001 for amount in climates), 'Native global climate left its valid range')
        self.require(all(amount <= .015 for amount in climates),
            'Global environment frost covered the arena before the radial front completed')
        all_climates = [sample['global_cold_amount'] for sample in self.transition_samples]
        self.require(all(b >= a-.015 for a, b in zip(all_climates, all_climates[1:])),
            'Global ColdAmount moved backwards during the one-time phase transition')
        self.require(any(.02 < sample['global_cold_amount'] < .95 and sample['zone_enabled'] <= .001
                         and sample['sweep'] <= .001 for sample in self.transition_samples),
            'Global environment climate skipped the gradual ramp after the radial front')
        self.report['checks']['native_phase_two_sweep_has_multiple_monotonic_radii'] = True
        self.report['checks']['native_global_climate_waits_for_front_then_ramps'] = True
        available_burst_counts = [sample['active_burst_count'] for sample in self.transition_samples
                                  if sample['active_burst_count'] is not None]
        self.report['transition_evidence'] = dict(actual_samples=self.transition_samples,
            enabled_sweep_samples=len(sweep), increasing_radius_steps=growing,
            first_radius_cm=radii[0], last_radius_cm=radii[-1],
            min_global_cold_amount=min(all_climates), max_global_cold_amount=max(all_climates),
            maximum_observed_burst_count=max(available_burst_counts) if available_burst_counts else None,
            burst_count_available=bool(available_burst_counts))

    def full_phase_assertions(self, now):
        state = self.material_state()
        self.require(prop(self.ice, 'second_phase') and self.ice.is_global_cold_active(), 'Native global phase two is not active')
        self.require(state['scalars']['Frost Zone Enabled'] <= .001 and state['scalars']['Frost Sweep'] <= .001,
            'Completed transition retained its local material mask/sweep')
        self.require(state['scalars']['IceAmount'] >= .99 and state['global_cold_amount'] >= .99,
            'Full global tongue/environment frost was not restored after the transition')
        expected = self.clock_end-now
        remaining = float(self.ice.cold_throne_seconds_remaining())
        self.require(expected > 25. and abs(remaining-expected) < .15,
            'Native global cold no longer matches its original 40-second clock')
        self.require(abs(float(prop(self.ice, 'cold_throne_ends_at'))-self.clock_end) < .001,
            'Visual transition moved the native cold-phase deadline')
        return state

    def tick(self, delta):
        if self.done:
            return
        if self.phase in ('waiting_pie', 'first_perk', 'perk_resolution', 'arrival'):
            # Only the parent's normal bootstrap is used. It does not enter
            # the parent's independent local-aura/retreat comparison scenario.
            VisualSolo.tick(self, delta)
            return
        try:
            self.require(time.monotonic()-self.started < 120., 'Transition capture exceeded 120 wall seconds')
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
            self.require(now-self.capture_started_at < 20., 'Transition capture exceeded 20 game seconds')
            self.require(self.health() > 0. and not self.hero.is_freezing_to_death(), 'Native attack/cold killed the review player')
            self.require(not self.pc.is_reward_menu_open(), 'Unexpected reward interrupted transition review')
            if prop(self.ice, 'second_phase') and self.native_latch_at is None:
                active_seconds = float(prop(self.ice, 'cold_throne_active_seconds'))
                self.require(abs(active_seconds-40.) < .001, 'The actual native cold phase is not 40 seconds')
                self.clock_end = float(prop(self.ice, 'cold_throne_ends_at'))
                self.native_latch_at = self.clock_end-active_seconds
                self.require(self.threshold_at is not None and abs(self.native_latch_at-self.threshold_at) < .2,
                    'Actual phase latch did not follow the real 50% boss-HP threshold')
                self.report['native_phase_latch'] = dict(game_seconds=self.native_latch_at,
                    cold_throne_ends_at=self.clock_end, active_seconds=active_seconds,
                    observed_game_seconds=now, threshold_game_seconds=self.threshold_at)
            if now >= self.sample_at:
                self.sample_at = now+.1
                self.sample(now)
            if self.capture_hz and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase == 'phase_one_overview' and age >= 2.:
                self.require(self.freeze() <= .001 and not self.safe(), 'Exposed far phase-one player accumulated global cold')
                self.require(abs(self.health()-self.initial_health) < .01, 'Unexpected phase-one damage')
                state = self.split_assertions()
                self.require(state['scalars']['Frost Sweep'] <= .001, 'Phase one already has a phase-two sweep')
                self.require(abs(state['scalars']['Frost Aura Radius']-float(prop(self.ice, 'boss_frost_radius'))) < .01,
                    'Strong aura material radius differs from actual native aura')
                camera = self.camera_evidence()
                self.require(camera['view_target'] == camera['review_camera'] and camera['location_error_cm'] < 2.,
                    'Actual camera manager did not adopt the transition overview')
                self.report['phase_one_visual_state'] = state
                self.report['checks']['phase_one_local_frost_global_slick_ice_confirmed'] = True
                self.report['checks']['actual_overview_camera_confirmed'] = True
                self.screenshot('01_before_native_phase_transition.png')
                self.stage('phase_one_still_flush')
            elif self.phase == 'phase_one_still_flush' and age >= .35:
                threshold = float(prop(self.ice, 'max_candy_health'))*.5
                self.ice.set_editor_property('CandyHealth', threshold)
                self.threshold_at = now
                self.report['fixture_tuning'].append(dict(game_seconds=now, CandyHealth=threshold,
                    reason='Actual <=50% threshold starts native phase latch and its unaccelerated visual transition'))
                self.stage('native_phase_transition')
            elif self.phase == 'native_phase_transition':
                self.require(age < 8., 'Native phase transition did not settle inside the short capture')
                if self.native_latch_at is None:
                    return
                elapsed = now-self.native_latch_at
                self.require(self.ice.is_global_cold_active() and self.freeze() > 0.,
                    'Native weak global cold must begin at the phase latch without waiting for cosmetic sweep')
                if elapsed >= 1.6 and not self.mid_still_taken:
                    self.mid_still_taken = True
                    self.screenshot('02_during_native_radial_frost_sweep.png')
                if elapsed >= 5.:
                    self.validate_transition()
                    self.report['full_phase_two_visual_state'] = self.full_phase_assertions(now)
                    self.report['checks']['native_transition_finishes_as_full_global_frost'] = True
                    self.report['checks']['gameplay_global_cold_and_original_forty_second_deadline_preserved'] = True
                    self.screenshot('03_native_transition_complete_global_frost.png')
                    self.stage('full_phase_two_hold')
            elif self.phase == 'full_phase_two_hold' and age >= 2.:
                self.full_phase_assertions(now)
                self.report['native_attack_observation'] = dict(health_before=self.initial_health,
                    health_after=self.health(), frozen_legs=bool(self.hero.has_frozen_legs()),
                    explanation='Normal native phase-two prison and icicle damage were allowed; player HP was never overridden')
                self.screenshot('04_full_phase_two_after_unaccelerated_sweep.png')
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
        u.log('MC_ICE_PHASE_TRANSITION_'+('FAIL ' if error or not self.report.get('passed') else 'PASS ')
            +str(self.output/'Report.json'))


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('Ice phase transition capture already running')
    probe = IcePhaseTransitionSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
