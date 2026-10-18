"""One owned solo PIE: guard impact, shield break, five-second stun and resume.

Real primary Pickaxe input drives native damage/state/animation. A two-hit shield
durability fixture avoids repetitive swings. No damage, pose, stun or mesh-swap
helper; no network peers/package writes. Genuine Slate game-window frames use the
strict shared preview format, with camera evidence and recorded game timestamps.
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
OUTPUT = Path(globals().get('MC_KNIGHT_SHIELD_OUTPUT', ROOT/'Saved/Feedback5_11_20261018/Shield')).resolve()
RUN = OUTPUT/'Runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='knight_shield_support', init_globals={'MC_ENV_OUTPUT': RUN, 'MC_ENV_CAPTURE_HZ': 12})
Base, prop, vec = (support[name] for name in ('ApprovedEnvironmentSolo', 'prop', 'vec'))
KEY, MODE, CLIP = 'mc_knight_shield_solo', 'knight_shield', 'knight_shield_break'


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


class KnightShieldSolo(Base):
    def __init__(self):
        super().__init__()
        self.boss = None
        self.pressed = set()
        self.break_at = self.expiry = self.resume_at = self.clip_at = None
        self.stun_hit_seen = False
        self.report.update(mode=MODE, scope='One owned standalone PIE on L_Mouth; real primary Pickaxe hits and native Knight scheduler',
            tuning_reason='Two-hit shield durability shortens repeated swings; saved damage, five-second stun and animation/state paths remain active',
            clips={CLIP: []}, samples=[], frames=[], checks={},
            fixture_tuning={'shield_max_health_and_initial_health': 'two saved Pickaxe hits',
                            'ability_readiness': 'blocked during guard/stun proof; melee ready after native expiry'},
            limitations=['One stationary observer and one Knight on L_Mouth; placement/readiness isolate the mechanic.',
                'Shield maximum/current durability are set to two saved Pickaxe hits before spawning; real input drives both impacts.',
                'Full damage, five-second native state and mesh disappearance are checked; network peers are not.',
                'Genuine engine screenshots at requested 12 Hz; output repeats frames at recorded game timestamps.',
                'Rendered visual quality requires separate review; no package saves, build/cook/package or broad suite.'])
        self.report['capture_preparation'] = 'Wait at least 4.25 game seconds after spawn, then until the real gameplay alert finishes; HUD and clocks are unchanged.'
        sources = ['Tools/Unreal/check_knight_shield_break_solo.py', 'Source/MessControl/Private/MCNutBoss.cpp',
                   'Source/MessControl/Private/MCNutBossTypes.cpp', 'Source/MessControl/Public/MCNutBossTypes.h']
        self.report['source_sha256'] = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report.json').write_text(data, encoding='utf-8')

    def check(self, name, condition):
        self.require(condition, name)
        self.report['checks'][name] = True

    def now(self):
        return float(u.GameplayStatics.get_time_seconds(self.world))

    def ready(self, melee=False):
        for name in ('NextMeleeAt', 'NextChargeAt', 'NextJumpAt', 'NextRollAt', 'NextSpecialAt'):
            self.boss.set_editor_property(name, self.now()+300.)
        if melee: self.boss.set_editor_property('NextMeleeAt', self.now())

    def setup(self):
        self.check('standalone authority only', u.SystemLibrary.is_server(self.world) and len(u.EditorLevelLibrary.get_pie_worlds(True)) == 1)
        mode = u.GameplayStatics.get_game_mode(self.world)
        mode.set_actor_tick_enabled(False)
        for component in mode.get_components_by_class(u.ActorComponent):
            if component.get_class().get_name() == 'MCDayDirectorComponent': component.set_component_tick_enabled(False)
        for cls in (u.MCDayDirector, u.MCSingleDayDirector, u.MCGameDirector):
            for director in self.actors(cls): director.set_actor_tick_enabled(False)
        for cls in (u.MCCoffeeFlood, u.MCNutBoss):
            for actor in self.actors(cls): actor.destroy_actor()
        gs = u.GameplayStatics.get_game_state(self.world)
        gs.set_editor_properties(dict(Phase=u.MCShiftPhase.WORKING, dev_manual_events=True,
                                     phase_ends_at=0., bLobbyWaiting=False, bTutorialActive=False))
        self.pc.player_state.get_editor_property('Perks').reset_perks()
        self.hero.set_editor_property('bInCoffee', False)
        self.primary_input(False)
        self.hero.inventory.server_select(u.MCToolSlot.PICKAXE)
        self.hero.character_movement.stop_movement_immediately()
        self.hero.character_movement.disable_movement()
        self.tongue = self.actors(u.MCTongue)[0]
        center, _, _ = u.SystemLibrary.get_component_bounds(prop(self.tongue, 'surface'))
        floor = self.floor(center)
        self.require(floor is not None, 'Supported interior boss fixture is required')
        for food in self.actors(u.MCFoodActor):
            if food.get_actor_location().distance(floor) < 1500.: food.destroy_actor()
        pose = u.Transform(location=floor+u.Vector(0, 0, 204))
        self.boss = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, u.MCNutBoss, pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(self.boss), 'Could not create owned Knight fixture')
        settings = u.load_asset('/Game/Gameplay/CoreLoop/DA_NutRain').get_editor_property('settings').get_editor_property('boss').copy()
        # BeginPlay synchronously resolves this public soft profile into the
        # private Settings cache. Python cannot read that private cache.
        equipment = prop(self.hero.inventory, 'profile')
        self.require(isinstance(equipment, u.MCEquipmentProfile), 'Inventory public equipment Profile is not loaded')
        self.require(not self.hero.inventory.is_using_buffer(), 'Use the ordinary Pickaxe, without its drill upgrade')
        self.damage = max(1., float(prop(equipment, 'pickaxe_damage')))
        self.report['equipment_profile'] = equipment.get_path_name()
        self.report['saved_shield_max_health'] = float(prop(settings, 'shield_max_health'))
        self.initial_shield = self.damage*2.
        settings.set_editor_property('ShieldMaxHealth', self.initial_shield)
        base = u.MCNutEnemySettings()
        for key, value in dict(BodyRadius=200., MaxHealth=1400., MoveSpeed=160., HopHeight=4.).items():
            base.set_editor_property(key, value)
        self.boss.set_editor_properties(dict(Tongue=self.tongue, BossRole=u.MCNutBossRole.TANK,
            BossSettings=settings, Settings=base, Health=1400., ShieldHealth=self.initial_shield, State=u.MCNutBossState.IDLE, Target=self.hero))
        self.statics.call_method('FinishSpawningActor', args=(self.boss, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        prop(self.boss, 'label').set_hidden_in_game(True)
        self.ready()
        self.check('saved stun duration is five seconds', abs(prop(settings, 'tank_stun_seconds')-5.) < .001)
        self.guard_health = float(prop(self.boss, 'health'))
        model = prop(self.boss, 'tank_model')
        self.initial_scale, self.initial_anchor = prop(model, 'relative_scale3d'), prop(model, 'relative_location')
        anchor = self.boss.get_actor_location()
        self.frame_camera(anchor+u.Vector(600, -900, 430), anchor+u.Vector(40, 0, 45))
        prop(self.camera, 'camera_component').set_editor_property('field_of_view', 60.)
        self.pc.set_editor_property('bAutoManageActiveCameraTarget', False)
        self.report['saved_pickaxe_damage'] = self.damage
        self.stage('alert_warmup')

    def press(self, label):
        if label in self.pressed: return
        if prop(self.hero, 'tooth_physics').get_body_state() != u.MCBodyState.STANDING: return
        self.pressed.add(label)
        self.primary_input(True)
        self.primary_input(False)
        self.report.setdefault('primary_inputs', []).append(dict(label=label, time=self.now()))

    def alert_presenting(self):
        # Read the public widget API. Do not clear, hide or alter gameplay HUD.
        return any(widget.get_owning_player() == self.pc and widget.is_presenting_alert()
                   for widget in u.ObjectIterator(u.MCGameplayAlertWidget))

    def camera_evidence(self):
        if not valid(self.camera): return None
        manager = prop(self.pc, 'player_camera_manager')
        actual, expected = manager.get_camera_location(), self.camera.get_actor_location()
        rotation, target = manager.get_camera_rotation(), self.camera.get_actor_rotation()
        angle = max(abs((a-b+180.)%360.-180.) for a, b in
                    ((rotation.pitch, target.pitch), (rotation.yaw, target.yaw), (rotation.roll, target.roll)))
        return dict(view_target=self.pc.get_view_target().get_path_name(), expected_target=self.camera.get_path_name(),
                    actual_cm=vec(actual), expected_cm=vec(expected), location_error_cm=actual.distance(expected),
                    rotation_error_degrees=angle)

    def capture(self, now):
        if now < self.frame_at: return
        self.frame_at = now+1./12.
        evidence = self.camera_evidence()
        if not evidence or evidence['view_target'] != evidence['expected_target'] or evidence['location_error_cm'] >= 12. or evidence['rotation_error_degrees'] >= 5.: return
        if self.clip_at is None: self.clip_at = now
        self.screenshot('%05d.png'%len(self.report['frames']), True)
        entry = self.report['frames'][-1]
        entry.update(clip_window=CLIP, camera_evidence=evidence)
        self.report['clips'][CLIP].append(entry)

    def sample(self, now):
        model = prop(self.boss, 'tank_model')
        mesh = model.get_skeletal_mesh_asset()
        state, attack = prop(self.boss, 'state'), prop(self.boss, 'attack')
        self.report['samples'].append(dict(time=now, phase=self.phase, state=str(state), attack=str(attack),
            health=float(prop(self.boss, 'health')), shield=float(prop(self.boss, 'shield_health')),
            mesh=mesh.get_path_name(), position=vec(self.boss.get_actor_location()),
            state_started_at=float(prop(self.boss, 'state_started_at')), state_end=float(prop(self.boss, 'attack_end_at')),
            hand_l=vec(model.get_socket_location('hand_l')), hand_r=vec(model.get_socket_location('hand_r'))))
        self.require((prop(model, 'relative_scale3d')-self.initial_scale).length() < .001, 'Mesh swap changed body scale')
        self.require((prop(model, 'relative_location')-self.initial_anchor).length() < .001, 'Mesh swap changed body anchor')
        if prop(self.boss, 'shield_health') <= 0:
            self.require(mesh.get_name() == 'SK_NutTank_NoShield', 'Broken shield still uses intact mesh')
            self.require(not prop(self.boss, 'shield').is_visible(), 'Fallback shield remains visible')
        if self.break_at is not None and now < self.expiry-.02:
            self.require(state == u.MCNutBossState.STUNNED and attack == u.MCNutBossAttack.NONE, 'Boss acted during stun')
            self.require(self.boss.get_actor_location().distance(self.stun_position) < .1, 'Boss moved during stun')

    def tick(self, _dt):
        if self.done: return
        try:
            self.require(time.monotonic()-self.started < 100., 'Shield probe exceeded wall-time budget')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds: return
                self.require(len(worlds) == 1, 'Preserve unrelated multiplayer worlds')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()):
                    self.world = None
                    return
                self.hero = self.pc.get_controlled_pawn()
                self.require(isinstance(self.hero, u.MCToothCharacter), 'Local controller has no tooth pawn')
            if self.pc.is_reward_menu_open():
                self.pc.choose_reward_perk(0)
                return
            self.pc.call_method('ServerSetPerkChoicePause', args=(False,))
            u.GameplayStatics.set_game_paused(self.world, False)
            now = self.now()
            if self.last_game_time is not None and now <= self.last_game_time: return
            self.last_game_time = now
            if self.boss is None:
                if now < 1.: return
                self.setup()
                return
            self.require(self.world in worlds and valid(self.boss) and self.pc.get_controlled_pawn() == self.hero
                         and prop(self.hero, 'status').is_alive(), 'Owned fixture ended unexpectedly')
            if self.phase == 'capture_flush':
                if now-self.phase_at >= .6:
                    dimensions = set()
                    for entry in self.report['frames']:
                        data = Path(entry['file']).read_bytes()
                        self.require(data.startswith(b'\x89PNG\r\n\x1a\n'), 'Missing genuine PNG')
                        entry.update(sha256=hashlib.sha256(data).hexdigest(), width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
                        dimensions.add((entry['width'],entry['height']))
                    self.check('all captured frames use one genuine gameplay viewport size',
                               len(dimensions) == 1 and next(iter(dimensions)) in ((1280,720),(1286,760)))
                    self.check('camera-verified client frames captured', len(self.report['frames']) >= 70)
                    span = self.report['frames'][-1]['time']-self.report['frames'][0]['time']
                    self.check('seven-to-nine-second captured game span', 7. <= span <= 9.)
                    self.finish()
                return
            self.pc.set_view_target_with_blend(self.camera, 0.)
            point = self.floor(self.boss.get_actor_location()+u.Vector(280, 100, 0))
            self.require(point is not None, 'Frontal Pickaxe fixture lost support')
            facing=u.MathLibrary.find_look_at_rotation(point,self.boss.get_actor_location())
            facing=u.Rotator(yaw=facing.yaw)
            self.hero.set_actor_location_and_rotation(point+u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height()+3), facing, False, True)
            self.pc.set_control_rotation(facing)
            if self.phase == 'alert_warmup':
                elapsed = now-self.phase_at
                if elapsed >= 4.25 and not self.alert_presenting():
                    self.report['alert_warmup_observed_seconds'] = elapsed
                    self.stage('guard')
                self.require(elapsed < 12., 'Gameplay alert did not finish naturally before capture')
                return
            self.capture(now)
            if now >= self.sample_at:
                self.sample_at = now+.05
                self.sample(now)
            elapsed = now-self.phase_at
            if self.phase == 'guard':
                if elapsed >= .2: self.press('guard_hit')
                if prop(self.boss, 'shield_health') < self.initial_shield-.01:
                    self.check('intact shield absorbs the entire hit without HP damage', abs(self.guard_health-prop(self.boss, 'health')) < .01)
                    self.check('guard impact registered', bool(prop(self.boss, 'last_tool_hit_blocked')))
                    self.stage('guard_settle')
                self.require(elapsed < 3., 'Real guard hit did not arrive')
            elif self.phase == 'guard_settle' and elapsed >= .55:
                self.break_health = float(prop(self.boss, 'health'))
                self.stage('breaking')
                self.press('breaking_hit')
            elif self.phase == 'breaking':
                if prop(self.boss, 'shield_health') <= 0:
                    self.break_at = float(prop(self.boss, 'state_started_at'))
                    self.expiry = float(prop(self.boss, 'attack_end_at'))
                    self.stun_position = self.boss.get_actor_location()
                    self.stun_health = float(prop(self.boss, 'health'))
                    self.check('breaking shield hit has no HP overflow', abs(self.break_health-self.stun_health) < .01)
                    self.check('native shield stun lasts exactly five seconds', abs(self.expiry-self.break_at-5.) < .001)
                    self.stage('stun')
                self.require(elapsed < 2., 'Actual Pickaxe did not break fixture shield')
            elif self.phase == 'stun':
                if now-self.break_at >= .9: self.press('stun_hit')
                if prop(self.boss, 'health') < self.stun_health-.01:
                    self.stun_hit_seen = True
                    self.check('post-break frontal hit deals full damage', abs(self.stun_health-prop(self.boss, 'health')-self.damage) < .01)
                    self.require(abs(prop(self.boss, 'attack_end_at')-self.expiry) < .001 or now >= self.expiry, 'A hit extended the stun')
                if now >= self.expiry+.05:
                    self.check('full-damage follow-up observed', self.stun_hit_seen)
                    self.check('stun ended naturally after five seconds', prop(self.boss, 'state') == u.MCNutBossState.IDLE)
                    self.ready(melee=True)
                    self.stage('resume')
            elif self.phase == 'resume':
                if prop(self.boss, 'state') == u.MCNutBossState.EXECUTING and prop(self.boss, 'attack') == u.MCNutBossAttack.MELEE:
                    if self.resume_at is None:
                        self.resume_at = now
                        self.report['native_melee_execution_observed_at'] = now
                        self.check('native attack resumes without shield', True)
                # Single-hit execution is shorter than .2s. Latch the real
                # execution, then retain its ordinary recovery in the video.
                if self.resume_at is not None and now-self.resume_at >= .2 and now-self.clip_at >= 7.1:
                    self.check('native resumed attack progresses into recovery',
                               prop(self.boss, 'state') == u.MCNutBossState.RECOVERY and prop(self.boss, 'attack') == u.MCNutBossAttack.MELEE)
                    self.check('shield removal preserves body scale and anchor', True)
                    self.stage('capture_flush')
                self.require(elapsed < 2., 'Native melee did not resume')
            self.require(self.clip_at is None or now-self.clip_at < 9., 'Capture exceeded nine seconds')
        except Exception:
            self.finish(traceback.format_exc())


if __name__ == '__main__':
    old = getattr(builtins, KEY, None)
    if old and not getattr(old, 'done', True): raise RuntimeError('Preserve active shield probe')
    probe = KnightShieldSolo()
    setattr(builtins, KEY, probe)
    try: probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
