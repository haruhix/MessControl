"""One owned standalone PIE: visible initial arrival, then genuine team Pickaxe pressure.

No package saves, network peers, damage/state/counter trigger helpers, or forged
counter timing. Natural initial loading/perk barriers stay active. The combat
fixture adds one non-player striker, sufficient shield durability and blocked
ordinary ability cooldowns; every counted hit uses SetPrimaryInputHeld.
"""
import builtins
import datetime
import hashlib
import json
import runpy
import time
import traceback
import uuid
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_ARRIVAL_COUNTER_OUTPUT',
    'E:/CodexWork/MessControlStageRewardsVoice_20261021/ArrivalCounter')).resolve()
assert OUTPUT.drive.upper() == 'E:'
RUN = OUTPUT/'Runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='arrival_counter_support', init_globals={'MC_ENV_OUTPUT': RUN, 'MC_ENV_CAPTURE_HZ': 12})
Base, prop, vec = (support[name] for name in ('ApprovedEnvironmentSolo', 'prop', 'vec'))
KEY = 'mc_stage_arrival_counter_solo'


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


class ArrivalCounterSolo(Base):
    def __init__(self):
        super().__init__()
        self.boss = self.striker = None
        self.arrival_released_at = self.arrival_landed_at = self.counter_at = self.counter_resolve = None
        self.last_shield = self.initial_shield = None
        self.hit_count = self.last_striker = 0
        self.next_press_at = 0.
        self.extra_attempted = self.counter_hit_seen = False
        self.max_push_cm = 0.
        self.report.update(scope=__doc__, mode='initial_arrival_then_team_counter',
            checks={}, samples=[], frames=[], clips={'01_visible_initial_arrival': [], '02_knight_team_counter': []},
            primary_inputs=[], accepted_hits=[], fixture_tuning={},
            limitations=['One standalone PIE; an extra authority-only striker is a disclosed actor fixture, not a second network player.',
                'Initial pawn spawn, loading cover, first perk completion and arrival movement use their normal gameplay paths.',
                'The first mandatory perk is selected normally through ChooseRewardPerk(0); the world is not forcibly unpaused.',
                'Combat fixture uses high shield durability to survive 10–12 real Pickaxe contacts and suppresses unrelated normal ability readiness.',
                'Combat actors are positioned once; no position/movement correction is applied after the counter starts.',
                'The private arrival-held flag is not exposed to Python: the hold/release observation uses public CharacterMovement MovementMode None -> Falling/Walking, supported height and unchanged health/reserve.',
                'True game-window PNGs include the unchanged HUD; this is not a full encounter balance playthrough.'])
        files = ['Source/MessControl/Private/MCGameMode.cpp', 'Source/MessControl/Private/MCToothCharacter.cpp',
                 'Source/MessControl/Private/MCNutBoss.cpp', 'Source/MessControl/Private/MCNutBossTypes.cpp',
                 'Tools/Unreal/check_stage_arrival_counter_solo.py']
        self.report['source_sha256'] = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        text = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(text, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report.json').write_text(text, encoding='utf-8')

    def check(self, name, value):
        self.require(value, name)
        self.report['checks'][name] = True

    def now(self):
        return float(u.GameplayStatics.get_time_seconds(self.world))

    def health(self, hero):
        return float(prop(prop(prop(hero, 'status'), 'state'), 'health'))

    def reserve(self):
        return [dict(path=t.get_path_name(), lost=bool(prop(prop(t, 'state'), 'lost')),
                     consumed=bool(prop(prop(t, 'state'), 'consumed')))
                for t in self.actors(u.MCArenaTooth)]

    def loading_cover_visible(self):
        # LoadingWidget is a protected native property. Inspect the actual owned
        # UMG root through its public API, as the existing loading check does.
        return any(valid(cover) and cover.get_owning_player() == self.pc
            and cover.is_in_viewport() and cover.is_visible()
            for cover in u.ObjectIterator(u.MCLoadingWidget.static_class()))

    def arrival_movement_is_held(self):
        # bArenaArrivalHeld and the non-reflected IsArenaArrivalHeld getter are
        # intentionally private to gameplay. Toolset GetObjectProperties also
        # rejects non-Edit/non-Blueprint-visible fields (PropertyAccessUtil).
        return prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_NONE

    def camera_evidence(self):
        if not valid(self.camera):
            return None
        manager = prop(self.pc, 'player_camera_manager')
        actual, expected = manager.get_camera_location(), self.camera.get_actor_location()
        rotation, target = manager.get_camera_rotation(), self.camera.get_actor_rotation()
        angle = max(abs((a-b+180.)%360.-180.) for a, b in
                    ((rotation.pitch, target.pitch), (rotation.yaw, target.yaw), (rotation.roll, target.roll)))
        return dict(view_target=self.pc.get_view_target().get_path_name(), expected_target=self.camera.get_path_name(),
                    actual_cm=vec(actual), expected_cm=vec(expected), location_error_cm=actual.distance(expected),
                    rotation_error_degrees=angle)

    def capture(self, now, clip):
        if now < self.frame_at:
            return
        self.frame_at = now+1./12.
        evidence = self.camera_evidence()
        if not evidence or evidence['view_target'] != evidence['expected_target'] or evidence['location_error_cm'] >= 12. or evidence['rotation_error_degrees'] >= 5.:
            return
        self.screenshot('%05d.png'%len(self.report['frames']), True)
        frame = self.report['frames'][-1]
        frame.update(clip_window=clip, camera_evidence=evidence, loading_cover_visible=self.loading_cover_visible())
        self.report['clips'][clip].append(frame)

    def begin_arrival(self):
        self.check('one owned standalone authority world', u.SystemLibrary.is_server(self.world)
                   and len(u.EditorLevelLibrary.get_pie_worlds(True)) == 1)
        self.tongue = self.actors(u.MCTongue)[0]
        self.mode = u.GameplayStatics.get_game_mode(self.world)
        floor = self.floor(self.hero.get_actor_location())
        self.require(floor is not None, 'Initial pawn has no tongue support')
        self.arrival_floor = floor
        self.arrival_health, self.arrival_reserve = self.health(self.hero), self.reserve()
        self.arrival_release_health = None
        self.held_start = self.hero.get_actor_location()
        self.held_seen = self.arrival_movement_is_held()
        self.report['arrival'] = dict(method='unmodified natural first gameplay pawn',
            floor_cm=vec(floor), initial_cm=vec(self.held_start), initial_health=self.arrival_health,
            expected_clearance_cm=float(prop(self.mode, 'arena_arrival_height')),
            initial_reserve=self.arrival_reserve,
            hold_observation='Public CharacterMovement MovementMode None before natural release; private flag is not read')
        self.pc.set_editor_property('bAutoManageActiveCameraTarget', False)
        self.frame_camera(floor+u.Vector(760, -1050, 650), floor+u.Vector(0, 0, 280))
        prop(self.camera, 'camera_component').set_editor_property('field_of_view', 60.)
        self.stage('natural_arrival')

    def setup_counter(self):
        # Keep GameMode tick enabled: arrival release and ordinary respawn rules
        # must remain real. Only competing director/event schedules are isolated.
        self.check('game mode tick remains enabled', self.mode.is_actor_tick_enabled())
        for cls in (u.MCDayDirector, u.MCSingleDayDirector, u.MCGameDirector):
            for actor in self.actors(cls):
                actor.set_actor_tick_enabled(False)
        for cls in (u.MCCoffeeFlood, u.MCNutBoss):
            for actor in self.actors(cls):
                actor.destroy_actor()
        gs = u.GameplayStatics.get_game_state(self.world)
        gs.set_editor_properties(dict(Phase=u.MCShiftPhase.WORKING, dev_manual_events=True,
                                     phase_ends_at=0., bLobbyWaiting=False, bTutorialActive=False))
        prop(self.pc.player_state, 'perks').reset_perks()
        center, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        floor = self.floor(center)
        self.require(floor is not None, 'No supported interior combat fixture point')
        for food in self.actors(u.MCFoodActor):
            if food.get_actor_location().distance(floor) < 1500.:
                food.destroy_actor()
        pose = u.Transform(location=floor+u.Vector(0, 0, 204))
        self.boss = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, u.MCNutBoss, pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(self.boss), 'Could not create disclosed Knight fixture')
        equipment = prop(self.hero.inventory, 'profile')
        self.require(isinstance(equipment, u.MCEquipmentProfile), 'Saved equipment profile is not loaded')
        self.damage = max(1., float(prop(equipment, 'pickaxe_damage')))
        settings = u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain').get_editor_property('settings').get_editor_property('boss').copy()
        self.report['saved_boss_settings'] = dict(shield_max_health=float(prop(settings, 'shield_max_health')),
            counter_hits_min=int(prop(settings, 'tank_counter_hits_min')), counter_hits_max=int(prop(settings, 'tank_counter_hits_max')),
            counter_damage=float(prop(settings, 'tank_counter_damage')), counter_radius=float(prop(settings, 'tank_counter_radius')))
        self.initial_shield = self.damage*24.
        settings.set_editor_property('shield_max_health', self.initial_shield)
        base = u.MCNutEnemySettings()
        base.set_editor_properties(dict(BodyRadius=200., MaxHealth=1400., MoveSpeed=160., HopHeight=4.))
        self.boss.set_editor_properties(dict(Tongue=self.tongue, BossRole=u.MCNutBossRole.TANK, BossSettings=settings,
            Settings=base, Health=1400., ShieldHealth=self.initial_shield, State=u.MCNutBossState.IDLE, Target=self.hero))
        self.statics.call_method('FinishSpawningActor', args=(self.boss, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        prop(self.boss, 'label').set_hidden_in_game(True)
        for name in ('NextMeleeAt', 'NextChargeAt', 'NextJumpAt', 'NextRollAt', 'NextSpecialAt'):
            self.boss.set_editor_property(name, self.now()+300.)
        # The second striker shares the same native primary-input bridge. It is
        # an unpossessed authority fixture, never a fabricated network client.
        side_floor = self.floor(floor+u.Vector(-280, -100, 0))
        self.require(side_floor is not None, 'Second striker has no support')
        side_pose = u.Transform(location=side_floor+u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height()+3))
        self.striker = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, self.hero.get_class(), side_pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.statics.call_method('FinishSpawningActor', args=(self.striker, side_pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(self.striker), 'Second striker fixture failed')
        self.strikers = [self.hero, self.striker]
        for actor, offset in zip(self.strikers, (u.Vector(280, 100, 0), u.Vector(-280, -100, 0))):
            point = self.floor(floor+offset)
            self.require(point is not None, 'Pickaxe fixture lost tongue support')
            facing = u.MathLibrary.find_look_at_rotation(point, self.boss.get_actor_location())
            actor.set_actor_location_and_rotation(point+u.Vector(0, 0, actor.capsule_component.get_scaled_capsule_half_height()+3),
                u.Rotator(yaw=facing.yaw), False, True)
            actor.get_movement_component().stop_movement_immediately()
            actor.get_movement_component().disable_movement()
            actor.inventory.server_select(u.MCToolSlot.PICKAXE)
            self.require(not actor.inventory.is_using_buffer(), 'Ordinary Pickaxe is required')
        self.pc.set_control_rotation(self.hero.get_actor_rotation())
        self.initial_boss_hp = float(prop(self.boss, 'health'))
        self.counter_health = self.health(self.hero)
        self.last_shield = self.initial_shield
        self.frame_camera(self.boss.get_actor_location()+u.Vector(640, -980, 490), self.boss.get_actor_location()+u.Vector(0, 0, 40))
        self.report['fixture_tuning'].update(shield_max_health_and_initial=self.initial_shield,
            equipment=equipment.get_path_name(), primary_input='two alternating authority actors, ordinary Pickaxe',
            initial_state='Idle, configured before FinishSpawningActor', body_radius_cm=200.,
            ordinary_ability_readiness='now+300s; counter readiness/threshold/state remain native',
            competing_directors='tick disabled after successful natural arrival; GameMode tick retained')
        self.stage('counter_warmup')

    def press(self, index, label):
        actor = self.strikers[index]
        self.require(prop(actor, 'tooth_physics').get_body_state() == u.MCBodyState.STANDING, 'Striker cannot act before the counter')
        actor.call_method('SetPrimaryInputHeld', args=(True,))
        actor.call_method('SetPrimaryInputHeld', args=(False,))
        self.report['primary_inputs'].append(dict(time=self.now(), actor=actor.get_path_name(), label=label))

    def sample(self, now):
        sample = dict(time=now, phase=self.phase, hero_cm=vec(self.hero.get_actor_location()),
            velocity_cm_s=vec(self.hero.get_velocity()), health=self.health(self.hero),
            movement_mode=str(prop(self.hero.get_movement_component(), 'movement_mode')),
            arrival_movement_disabled=self.arrival_movement_is_held(), loading_cover_visible=self.loading_cover_visible())
        if valid(self.boss):
            sample.update(boss_state=str(prop(self.boss, 'state')), boss_attack=str(prop(self.boss, 'attack')),
                shield=float(prop(self.boss, 'shield_health')), boss_health=float(prop(self.boss, 'health')),
                resolve_at=float(prop(self.boss, 'resolve_at')), phase_end=float(prop(self.boss, 'attack_end_at')),
                hit_count=self.hit_count, hero_body_state=str(prop(self.hero, 'tooth_physics').get_body_state()))
        self.report['samples'].append(sample)

    def tick(self, _dt):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 180., 'Arrival/counter probe exceeded wall budget')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1, 'Preserve unrelated PIE worlds')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()):
                    self.world = None
                    return
                self.hero = self.pc.get_controlled_pawn()
                self.require(isinstance(self.hero, u.MCToothCharacter), 'No natural gameplay pawn')
                self.begin_arrival()
            self.require(self.world in worlds and valid(self.hero) and self.pc.get_controlled_pawn() == self.hero,
                         'Owned initial pawn or PIE changed')
            # Preserve the native team-pause lifetime; only a real personal
            # selection is submitted, and its normal code resumes gameplay.
            if self.pc.is_reward_menu_open():
                self.report.setdefault('natural_perk_choices', []).append(dict(time=self.now(), index=0))
                self.pc.choose_reward_perk(0)
            now = self.now()
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            self.pc.set_view_target_with_blend(self.camera, 0.)
            if now >= self.sample_at:
                self.sample_at = now+.04
                self.sample(now)
            if self.phase == 'natural_arrival':
                held = self.arrival_movement_is_held()
                self.held_seen |= held
                self.capture(now, '01_visible_initial_arrival')
                if not held and self.arrival_released_at is None:
                    self.arrival_released_at = now
                    self.arrival_release_health = self.health(self.hero)
                    self.check('initial arrival hold was observed before release', self.held_seen)
                    self.check('preheat completed before gravity release', self.pc.is_world_presentation_ready())
                    self.report['arrival']['release_at'] = now
                    self.report['arrival']['release_cm'] = vec(self.hero.get_actor_location())
                    self.report['arrival']['health_after_natural_perk'] = self.arrival_release_health
                if self.arrival_released_at is not None:
                    self.require(self.health(self.hero) == self.arrival_release_health, 'Natural arrival lost player HP')
                    if prop(self.hero.get_movement_component(), 'movement_mode') == u.MovementMode.MOVE_WALKING:
                        if self.arrival_landed_at is None:
                            self.arrival_landed_at = now
                            self.report['arrival']['landed_at'] = now
                        if now-self.arrival_landed_at >= .8:
                            self.check('arrival consumes no tooth reserve', self.reserve() == self.arrival_reserve)
                            visible = [f for f in self.report['clips']['01_visible_initial_arrival']
                                       if f['time'] >= self.arrival_released_at and not f['loading_cover_visible']]
                            self.check('natural falling frames are visible outside loading cover', len(visible) >= 4
                                and visible[0]['time'] < self.arrival_landed_at)
                            self.setup_counter()
                    self.require(now-self.arrival_released_at < 6., 'Natural arrival did not land')
                self.require(now-self.phase_at < 60., 'Native preparation/first perk did not release arrival')
                return
            if self.phase == 'counter_warmup':
                alert = any(widget.get_owning_player() == self.pc and widget.is_presenting_alert()
                            for widget in u.ObjectIterator(u.MCGameplayAlertWidget))
                if now-self.phase_at >= 4.25 and not alert:
                    self.counter_health = self.health(self.hero)
                    self.stage('team_hits');self.next_press_at = now+.2
                self.require(now-self.phase_at < 12., 'Gameplay alert did not finish before combat capture')
                return
            if self.phase == 'capture_flush':
                if now-self.phase_at >= .5:
                    self.check('both feedback clips contain camera-verified gameplay frames', all(
                        len(values) >= 12 for values in self.report['clips'].values()))
                    self.check('single gameplay viewport capture size', len({(f['width'], f['height']) for f in self.report['frames']}) == 1)
                    self.finish()
                return
            self.capture(now, '02_knight_team_counter')
            shield = float(prop(self.boss, 'shield_health'))
            attack, state = prop(self.boss, 'attack'), prop(self.boss, 'state')
            if shield < self.last_shield-.01:
                delta = self.last_shield-shield
                self.report['accepted_hits'].append(dict(time=now, damage=delta, state=str(state), attack=str(attack)))
                self.check('ordinary Pickaxe durability impact', abs(delta-self.damage) < .01)
                if self.counter_at is None:
                    self.hit_count += 1
                else:
                    self.counter_hit_seen = True
                self.last_shield = shield
            self.require(shield > 0., 'Fixture shield broke before pressure could be observed')
            self.require(abs(float(prop(self.boss, 'health'))-self.initial_boss_hp) < .01, 'Shield failed to protect boss HP')
            if self.counter_at is None and attack == u.MCNutBossAttack.COUNTER_SHOVE:
                self.counter_at = now
                self.counter_resolve = float(prop(self.boss, 'resolve_at'))
                self.pre_push_position = self.hero.get_actor_location()
                self.check('counter starts after ten to twelve accepted team hits', 10 <= self.hit_count <= 12)
                self.check('counter begins with a real native telegraph', state == u.MCNutBossState.TELEGRAPH)
                self.report['counter'] = dict(started_at=now, resolve_at=self.counter_resolve,
                    accepted_team_hits=self.hit_count, pre_push_cm=vec(self.pre_push_position), initial_player_health=self.counter_health)
                self.stage('counter')
            if self.phase == 'team_hits':
                self.require(self.hit_count <= 12, 'No counter after twelve accepted contacts')
                if now >= self.next_press_at:
                    self.last_striker = len(self.report['primary_inputs']) % 2
                    self.press(self.last_striker, 'team_pickaxe')
                    self.next_press_at = now+.85
                self.require(now-self.phase_at < 24., 'Real Pickaxe contacts did not reach the threshold')
            elif self.phase == 'counter':
                if not self.extra_attempted and now-self.counter_at >= .025:
                    self.extra_attempted = True
                    other = 1-self.last_striker
                    if prop(self.strikers[other], 'tooth_physics').get_body_state() == u.MCBodyState.STANDING:
                        self.press(other, 'attempt_hit_during_committed_counter')
                if now < self.counter_resolve:
                    self.require(attack == u.MCNutBossAttack.COUNTER_SHOVE and state == u.MCNutBossState.TELEGRAPH,
                                 'A primary hit interrupted the committed warning')
                    self.require(abs(float(prop(self.boss, 'resolve_at'))-self.counter_resolve) < .001,
                                 'A primary hit restarted the committed warning')
                self.max_push_cm = max(self.max_push_cm, self.hero.get_actor_location().distance(self.pre_push_position))
                if now-self.counter_at >= 1.8:
                    expected = self.report['saved_boss_settings']['counter_damage']
                    self.check('counter delivers its single small player hit', abs(self.counter_health-self.health(self.hero)-expected) < .01)
                    self.check('counter visibly displaces the worker', self.max_push_cm > 25.)
                    self.check('counter is nonlethal', prop(self.hero, 'status').is_alive())
                    self.check('counter resolves into its native recovery and exits', attack != u.MCNutBossAttack.COUNTER_SHOVE)
                    self.report['counter'].update(final_health=self.health(self.hero), maximum_push_cm=self.max_push_cm,
                        accepted_additional_hit_during_counter=self.counter_hit_seen,
                        warning_not_restarted=True, post_counter_attack=str(attack), post_counter_state=str(state))
                    self.stage('capture_flush')
        except Exception:
            self.finish(traceback.format_exc())


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve the active arrival/counter probe')
    probe = ArrivalCounterSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
