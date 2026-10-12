"""Owned solo PIE comparison of standing versus jumping through a real frost nova.

Run in the idle Editor with L_Mouth open. The existing environment helper owns
standalone PIE setup, temporary review cameras, screenshots and full restoration.
This file never saves a package or calls native freeze/damage or nova helpers.
F3 starts IceEvent; its actual Tick/ResolveNova applies each scheduled attack.
Only PIE fixture timing and public player input are changed. ClearFrozenLegs is
the public reset between the standing control and the jump trial.

Optional globals: MC_FROST_OUTPUT, MC_FROST_CAPTURE_HZ (0 by default; up to 12).
Cancel: builtins.mc_approved_frost_jump_solo.finish('cancelled').
"""
import builtins
import importlib.util
import json
import time
import traceback
from pathlib import Path

import unreal as u

KEY = 'mc_approved_frost_jump_solo'

# Importing the helper does not start it: its entry point has a __main__ guard.
helper_path = Path(u.Paths.project_dir())/'Tools/Unreal/check_approved_environment_solo.py'
helper_spec = importlib.util.spec_from_file_location('mc_approved_env_frost_helper', helper_path)
helper = importlib.util.module_from_spec(helper_spec)
helper_spec.loader.exec_module(helper)
helper.MC_ENV_OUTPUT = globals().get('MC_FROST_OUTPUT', Path(u.Paths.project_saved_dir())/'Checks/ApprovedFrostJumpSolo')
helper.MC_ENV_CAPTURE_HZ = globals().get('MC_FROST_CAPTURE_HZ', 0)
prop, vec, squared_xy = helper.prop, helper.vec, helper.squared_xy


def set_fixture(obj, name, value):
    native = ''.join(part[:1].upper()+part[1:] for part in name.split('_'))
    error = None
    for key in (name, native, 'b'+native):
        try:
            obj.set_editor_property(key, value)
            return
        except Exception as caught:
            error = caught
    raise RuntimeError('PIE fixture field '+name+' is not accessible: '+str(error))


class ApprovedFrostJumpSolo(helper.ApprovedEnvironmentSolo):
    def __init__(self):
        super().__init__()
        self.jump_requested = self.jump_released = False
        self.impact_at = None
        self.impact_sample = None
        self.jump_peak = 0.
        self.settled_at = None
        self.report.update(
            scope='One owned standalone PIE on L_Mouth; real F3 IceEvent and legacy Nova Tick/ResolveNova',
            fixture_tuning=dict(FreezeSeconds=60., IcicleWarningSeconds=120.,
                                bUseAnimatedBossAttacks=False),
            tuning_reason='Isolate the standing/jumping nova comparison from unrelated attack impacts',
            scheduled_fields=['bNovaWarning', 'NovaImpactAt', 'NovaDirection'],
            deferred=['Animated radial ground-wave traversal', 'Network validation'])

    def write(self):
        (self.output/'Report.json').write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False), encoding='utf8')
        (self.output/'README.md').write_text(
            '# Approved frost jump solo check\n\n'
            'One owned standalone PIE on L_Mouth. F3 creates the actual IceEvent. '
            'A standing control must receive leg ice from a scheduled real legacy Nova; '
            'after public ClearFrozenLegs resets that control, the player uses ordinary '
            'SetJumpInputHeld around a second scheduled impact. The actual event Tick '
            'executes ResolveNova in both trials. No direct call to damage, FreezeLegs, '
            'ResolveNova, ClearsGroundWave, or any synthetic damage fixture is used.\n\n'
            'PIE-only fixture changes: FreezeSeconds=60, IcicleWarningSeconds=120, '
            'bUseAnimatedBossAttacks=false; bNovaWarning/NovaImpactAt schedule each '
            'impact 0.7 game seconds ahead and NovaDirection is cosmetic. Saved assets '
            'and native gameplay source are untouched. Standing and jump trials use '
            'the same supported cold tongue point outside delivery lanes and warm circles. '
            'The jump must have 12–150cm of sole clearance at the actual impact so it '
            'remains inside the nova area and tests the jump clearance condition.\n\n'
            'This validates the existing legacy full-circle nova branch only. Animated '
            'radial wave traversal and network behavior remain separate checks. '
            'Optional frames are genuine Shot SHOWUI captures with game timestamps; '
            'temporary cameras improve visibility. Only the owned PIE is stopped and '
            'the helper restores all play/frame/foreground settings. No packages are saved.\n',
            encoding='utf8')

    def phase_sample(self, now):
        floor = self.floor(self.hero.get_actor_location()) if self.tongue else None
        feet = self.hero.get_actor_location()-u.Vector(
            0, 0, self.hero.capsule_component.get_scaled_capsule_half_height())
        status = prop(prop(self.hero, 'status'), 'state')
        value = dict(game_seconds=now, phase=self.phase,
                     hero_cm=vec(self.hero.get_actor_location()),
                     movement_mode=str(prop(self.hero.get_movement_component(), 'movement_mode')),
                     body_state=str(prop(self.hero, 'tooth_physics').call_method('GetBodyState')),
                     frozen_legs=bool(self.hero.has_frozen_legs()),
                     ice_leg_health=float(prop(self.hero, 'ice_leg_health')),
                     health=float(prop(status, 'health')),
                     velocity_cm_s=vec(self.hero.get_velocity()),
                     feet_floor_clearance_cm=float(feet.z-floor.z) if floor else None)
        if self.ice:
            value.update(nova_warning=bool(prop(self.ice, 'nova_warning')),
                         nova_impact_at=float(prop(self.ice, 'nova_impact_at')))
            freeze = next((p for p in prop(self.ice, 'players') if prop(p, 'hero') == self.hero), None)
            if freeze:
                value.update(safe=bool(prop(freeze, 'safe')), freeze_amount=float(prop(freeze, 'amount')))
        return value

    def sample(self, now):
        self.report['samples'].append(self.phase_sample(now))

    def settled(self, now):
        ready = (prop(self.hero, 'tooth_physics').call_method('GetBodyState') == u.MCBodyState.STANDING
                 and prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_WALKING
                 and not self.hero.has_frozen_legs())
        if not ready:
            self.settled_at = None
            return False
        if self.settled_at is None:
            self.settled_at = now
        return now-self.settled_at >= .35

    def schedule_nova(self, now, jump):
        self.require(not self.hero.has_frozen_legs(), 'Nova trial started with frozen legs')
        self.impact_at = now+.7
        # Public reflected schedule fields are a transient fixture. Gameplay and
        # all hit decisions remain in the event's actual Tick/ResolveNova path.
        set_fixture(self.ice, 'nova_impact_at', self.impact_at)
        set_fixture(self.ice, 'nova_direction', u.Vector(1, 0, 0))
        set_fixture(self.ice, 'nova_warning', True)
        self.jump_requested = self.jump_released = False
        self.impact_sample = None
        self.jump_peak = 0.
        self.stage('jump_nova' if jump else 'standing_nova')
        self.report.setdefault('trials', {})[self.phase] = dict(
            impact_at=self.impact_at, start=self.phase_sample(now),
            trigger='PIE schedule fields; real IceEvent Tick executes ResolveNova')

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 70., 'Short frost probe exceeded wall-time limit')
            if self.pie_start is not None:
                self.require(not prop(self.pie_start, 'Error'), 'StartPIE: '+str(prop(self.pie_start, 'Error')))
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),
                             'Expected one standalone authority PIE world')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if self.pc is None or self.pc.get_controlled_pawn() is None:
                    self.world = None
                    return
                self.report['world'] = self.world.get_path_name()
                self.stage('first_perk')
                return
            self.require(self.world in worlds, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'No actual player tooth character')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.phase_at
            if now >= self.sample_at:
                self.sample_at = now+.05
                self.sample(now)
            if self.capture_hz and now >= self.frame_at and self.phase in (
                    'standing_nova', 'standing_result', 'settle_jump', 'jump_nova', 'capture_flush'):
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase == 'first_perk':
                self.require(elapsed < 25., 'Normal-map first-perk menu never became ready')
                if now >= 1. and self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
            elif self.phase == 'perk_resolution':
                self.require(elapsed < 10., 'First-perk choice did not resolve')
                if not self.pc.is_reward_menu_open():
                    self.pc.call_method('RequestDevAction', args=(u.MCDevAction.ICE_EVENT, -1))
                    self.stage('arrival')
            elif self.phase == 'arrival':
                if not self.ice:
                    events = self.actors(u.MCIceEvent)
                    self.require(events, 'F3 did not create IceEvent')
                    self.ice = events[0]
                    self.tongue = prop(self.ice, 'tongue')
                    self.require(self.tongue is not None, 'IceEvent has no tongue')
                    set_fixture(self.ice, 'freeze_seconds', 60.)
                    set_fixture(self.ice, 'icicle_warning_seconds', 120.)
                    set_fixture(self.ice, 'use_animated_boss_attacks', False)
                    self.hero.set_primary_input_held(False)
                    self.hero.set_handle_input_held(False)
                    self.hero.set_jump_input_held(False)
                    self.cold_floor_point = self.cold_floor()
                    candy = self.tongue.get_actor_transform().transform_location(prop(self.ice, 'candy_anchor'))
                    self.require(squared_xy(self.cold_floor_point, candy) <
                                 (float(prop(self.ice, 'nova_range'))-100.)**2,
                                 'Supported cold point is outside the actual nova range')
                    self.move_to(self.cold_floor_point)
                    hero = self.hero.get_actor_location()
                    self.frame_camera(hero+u.Vector(480, 350, 290), hero-u.Vector(0, 0, 25))
                if prop(self.ice, 'stage') == u.MCIceEventStage.ACTIVE:
                    self.stage('settle_standing')
            elif self.phase == 'settle_standing':
                self.require(elapsed < 4., 'Standing control did not settle')
                if self.settled(now):
                    self.schedule_nova(now, False)
            elif self.phase == 'standing_nova':
                if now >= self.impact_at and not prop(self.ice, 'nova_warning'):
                    self.require(self.hero.has_frozen_legs(),
                                 'Standing control did not receive leg ice from the real nova')
                    self.report['trials']['standing_nova']['impact'] = self.phase_sample(now)
                    self.report['checks']['standing_real_nova_freezes_legs'] = True
                    self.screenshot('01_standing_real_nova_frozen_legs.png')
                    self.stage('standing_result')
                self.require(now < self.impact_at+1., 'Scheduled standing nova did not resolve')
            elif self.phase == 'standing_result' and elapsed >= .3:
                self.hero.clear_frozen_legs()
                self.hero.set_jump_input_held(False)
                self.move_to(self.floor(self.cold_floor_point))
                self.settled_at = None
                self.stage('settle_jump')
            elif self.phase == 'settle_jump':
                self.require(elapsed < 4., 'Player did not recover walking after public ClearFrozenLegs')
                if self.settled(now):
                    self.schedule_nova(now, True)
            elif self.phase == 'jump_nova':
                if now >= self.impact_at-.18 and not self.jump_requested:
                    self.require(prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_WALKING,
                                 'Jump trial did not start on the floor')
                    self.hero.set_jump_input_held(True)
                    self.jump_requested = True
                    self.report['trials']['jump_nova']['jump_input_at'] = now
                if self.jump_requested and now >= self.impact_at-.04 and not self.jump_released:
                    self.hero.set_jump_input_held(False)
                    self.jump_released = True
                current = self.phase_sample(now)
                if current['feet_floor_clearance_cm'] is not None:
                    self.jump_peak = max(self.jump_peak, current['feet_floor_clearance_cm'])
                if now >= self.impact_at and not prop(self.ice, 'nova_warning') and self.impact_sample is None:
                    self.impact_sample = current
                    self.report['trials']['jump_nova']['impact'] = current
                    clearance = current['feet_floor_clearance_cm']
                    self.require(clearance is not None and 12. < clearance < 150.,
                                 'Jump impact must clear the ground yet remain in the actual nova area')
                    self.require(prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_FALLING,
                                 'Player was not actually airborne at nova impact')
                    self.require(not self.hero.has_frozen_legs(), 'Actual timed jump did not avoid leg ice')
                    self.screenshot('02_jump_avoids_real_frost_nova.png')
                if now >= self.impact_at+1.3:
                    self.require(self.impact_sample is not None, 'Scheduled jump nova did not resolve')
                    self.require(not self.hero.has_frozen_legs(), 'Leg ice appeared after landing')
                    self.require(prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_WALKING
                                 and current['feet_floor_clearance_cm'] is not None
                                 and abs(current['feet_floor_clearance_cm']) <= 12.,
                                 'After-landing check requires an actually supported, walking player')
                    self.report['trials']['jump_nova'].update(
                        peak_sole_clearance_cm=self.jump_peak, after_landing=current)
                    self.report['checks']['actual_jump_avoids_real_nova'] = True
                    self.report['checks']['no_leg_ice_after_landing'] = True
                    self.screenshot('03_landed_without_leg_ice.png')
                    self.stage('capture_flush')
            elif self.phase == 'capture_flush' and elapsed >= .4:
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if not self.done and self.hero and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                self.hero.set_jump_input_held(False)
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)
        u.log('MC_APPROVED_FROST_JUMP_'+('PASS ' if self.report['passed'] else 'FAIL ')+str(self.output/'Report.json'))


def main():
    previous = getattr(builtins, KEY, None)
    if previous is not None and not getattr(previous, 'done', True):
        raise RuntimeError('Frost jump probe already active; preserve it until finished')
    probe = ApprovedFrostJumpSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
