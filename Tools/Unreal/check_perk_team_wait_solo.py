"""Owned solo PIE: two authoritative identities, local perk choice and team wait.

This is not a network/multiplayer test. A second unconnected controller owns a
real PlayerState/pawn/earned offer in the same standalone world. Its selection
uses the normal validated server RPC locally. No packages are written.
Frames use wall time because gameplay time is deliberately paused while choosing.
"""
import builtins
import datetime
import hashlib
import json
import runpy
import time
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_PERK_WAIT_OUTPUT', ROOT/'Saved/Feedback5_11_20261018/Perks')).resolve()
RUN = OUTPUT/'Runs'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='perk_team_wait_support', init_globals={'MC_ENV_OUTPUT': RUN, 'MC_ENV_CAPTURE_HZ': 8})
Base, prop = support['ApprovedEnvironmentSolo'], support['prop']
KEY = 'mc_perk_team_wait_solo'


class PerkTeamWaitSolo(Base):
    def __init__(self):
        super().__init__()
        self.other = self.other_state = self.other_hero = None
        self.capture_started = self.phase_real = None
        self.report.update(scope='One standalone PIE; two authoritative controller/PlayerState fixtures; one local UI',
            fixture_tuning={'directors': 'disabled in owned PIE only', 'second_controller': 'unconnected same-world authority fixture'},
            tuning_reason='Show actual local choice, ready 1/2, frozen game time, and resume without starting network peers',
            time_base='real elapsed seconds for video; game_time is stored separately',
            limitations=['Not a network replication, latency, packet loss or multiplayer test.',
                'Second chooser has no local player or remote connection; its real earned offer is resolved through the server RPC locally.',
                'Genuine Slate game-window frames require separate visual review; no packages/build/cook/package are run.'],
            samples=[], frames=[], screenshots=[], checks={})

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report.json').write_text(data, encoding='utf8')

    def check(self, name, condition):
        self.require(condition, name)
        self.report['checks'][name] = True

    def stage_real(self, phase):
        self.phase = phase
        self.phase_real = time.monotonic()
        self.report['last_phase'] = phase
        self.write()

    def spawn(self, cls, point):
        pose = u.Transform(location=point)
        actor = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, cls, pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(actor is not None, 'Could not spawn '+str(cls))
        self.statics.call_method('FinishSpawningActor', args=(actor, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        return actor

    def setup(self):
        self.check('one standalone authority world', u.SystemLibrary.is_server(self.world)
                   and len(u.EditorLevelLibrary.get_pie_worlds(True)) == 1)
        mode = u.GameplayStatics.get_game_mode(self.world)
        mode.set_actor_tick_enabled(False)
        for component in mode.get_components_by_class(u.ActorComponent):
            if component.get_class().get_name() == 'MCDayDirectorComponent': component.set_component_tick_enabled(False)
        for cls in (u.MCDayDirector, u.MCSingleDayDirector, u.MCGameDirector):
            for actor in self.actors(cls): actor.set_actor_tick_enabled(False)
        self.gs = u.GameplayStatics.get_game_state(self.world)
        self.gs.set_editor_properties(dict(Phase=u.MCShiftPhase.WORKING, bLobbyWaiting=False,
            bTutorialActive=False, dev_manual_events=True, phase_ends_at=0.))
        self.progress = self.gs.get_component_by_class(u.MCProgressionComponent)
        self.require(self.progress is not None, 'Gameplay progression component is required')
        self.progress.reset_progression()
        self.hero.character_movement.disable_movement()
        point = self.hero.get_actor_location()+u.Vector(300, 0, 20)
        self.other = self.spawn(u.MCPlayerController, point)
        self.require(not self.other.is_local_controller(), 'The second fixture must not own another local viewport')
        old_state = self.other.player_state
        if old_state: old_state.set_editor_property('bIsSpectator', True)
        self.other_state = self.spawn(u.MCPlayerState, point)
        self.other_state.set_owner(self.other)
        self.other.set_editor_property('player_state', self.other_state)
        self.other_hero = self.spawn(u.MCToothCharacter, point)
        self.other.possess(self.other_hero)
        self.other_hero.character_movement.disable_movement()
        players = list(prop(self.gs, 'player_array'))
        if self.other_state not in players:
            self.gs.set_editor_property('player_array', players+[self.other_state])
        self.report['second_identity'] = self.other_state.get_path_name()
        self.report['second_controller_has_connection'] = False
        center=(self.hero.get_actor_location()+point)*.5
        self.frame_camera(center+u.Vector(400,-650,360),center)
        self.pc.set_editor_property('bAutoManageActiveCameraTarget',False)
        self.pc.call_method('ServerSetPerkChoicePause',args=(False,))
        self.settle_game_time=float(u.GameplayStatics.get_time_seconds(self.world))
        self.stage_real('camera_settle')

    def earn_choice(self):
        gained = self.progress.add_experience(self.progress.get_experience_to_next_level())
        self.check('one real team level earned', gained == 1)
        self.check('both real personal queues contain one choice',
                   prop(self.pc.player_state, 'pending_level_choices') == 1 and prop(self.other_state, 'pending_level_choices') == 1)
        # Server RPCs without BlueprintCallable are reflected but do not receive
        # a generated Python snake_case method. Invoke their exact UFUNCTION.
        self.pc.call_method('ServerSetPerkChoicePause', args=(True,))
        self.capture_started = time.monotonic()
        self.stage_real('capture_warmup')

    def capture(self, now):
        if now < self.frame_at: return
        self.frame_at = now+.125
        self.screenshot('%05d.png'%len(self.report['frames']), True)
        frame = self.report['frames'][-1]
        frame['game_time'] = frame['time']
        frame['time'] = self.started+frame['capture_request_wall_seconds']-self.capture_started
        self.report['samples'].append(dict(time=frame['time'], game_time=frame['game_time'], phase=self.phase,
            menu=self.pc.is_reward_menu_open(), waiting=self.pc.is_waiting_for_perk_team(),
            ready=self.pc.get_perk_choice_ready_players(), total=self.pc.get_perk_choice_total_players(),
            paused=u.GameplayStatics.is_game_paused(self.world)))

    def tick(self, _dt):
        if self.done: return
        try:
            self.require(time.monotonic()-self.started < 65., 'Perk waiting probe exceeded its time budget')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds: return
                self.require(len(worlds) == 1, 'Preserve unrelated PIE worlds')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not self.pc or not self.pc.get_controlled_pawn(): self.world = None;return
                self.hero = self.pc.get_controlled_pawn()
                self.require(isinstance(self.hero, u.MCToothCharacter), 'Local pawn is not a tooth')
            if self.other is None:
                self.setup();return
            now = time.monotonic()
            self.require(self.world in worlds, 'Owned PIE was replaced or stopped')
            elapsed = now-self.phase_real
            if self.phase == 'camera_settle':
                self.pc.set_view_target_with_blend(self.camera,0.)
                manager=prop(self.pc,'player_camera_manager')
                settled=manager.get_camera_location().distance(self.camera.get_actor_location())<2.
                advanced=float(u.GameplayStatics.get_time_seconds(self.world))-self.settle_game_time
                if elapsed>=1. and advanced>=.25 and settled:
                    self.check('review camera settled before the real choice pauses gameplay',True)
                    self.earn_choice()
                    return
                self.require(elapsed<8.,'The real review camera did not settle before the perk pause')
                return
            if self.phase == 'capture_warmup':
                # Window creation and the first UI asset load invalidate the
                # editor once even under its realtime override. Let that draw
                # finish before requesting any gameplay screenshots.
                if elapsed < 2.: return
                self.capture_started=now
                self.stage_real('choosing')
                return
            if self.phase == 'capture_flush':
                if elapsed < .5: return
                dimensions = set()
                for frame in self.report['frames']:
                    data = Path(frame['file']).read_bytes()
                    self.require(data.startswith(b'\x89PNG\r\n\x1a\n'), 'A genuine engine screenshot is missing')
                    frame['sha256'] = hashlib.sha256(data).hexdigest()
                    frame['width'], frame['height'] = int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')
                    dimensions.add((frame['width'],frame['height']))
                    self.require((frame['width'],frame['height']) in ((1280,720),(1286,760)), 'An editor viewport consumed a gameplay screenshot')
                self.check('all screenshots use one gameplay viewport size', len(dimensions) == 1)
                self.check('captured actual choice waiting and resume frames', len(self.report['frames']) >= 30)
                self.finish();return
            self.capture(now)
            if self.phase == 'choosing':
                if elapsed > 1.:
                    self.check('local earned offer is visible', self.pc.is_reward_menu_open())
                    self.pc.choose_reward_perk(0)
                    self.stage_real('wait_for_state')
            elif self.phase == 'wait_for_state':
                if self.pc.is_waiting_for_perk_team():
                    self.check('first chooser sees one of two ready', self.pc.get_perk_choice_ready_players() == 1 and self.pc.get_perk_choice_total_players() == 2)
                    self.check('waiting remains a visible modal', self.pc.is_reward_menu_open())
                    self.check('waiting blocks move and look input', self.pc.is_move_input_ignored() and self.pc.is_look_input_ignored())
                    self.paused_game_time = float(u.GameplayStatics.get_time_seconds(self.world))
                    self.stage_real('waiting')
                self.require(elapsed < 4., 'The local chooser never entered team waiting')
            elif self.phase == 'waiting' and elapsed > 3.:
                self.check('gameplay remains frozen while the other chooser waits',
                    u.GameplayStatics.is_game_paused(self.world)
                    and abs(float(u.GameplayStatics.get_time_seconds(self.world))-self.paused_game_time) < .001)
                offer = prop(self.other_state, 'level_up_offer')
                self.other.call_method('ServerChooseLevelPerk', args=(prop(offer, 'offer_id'), 0))
                self.stage_real('resuming')
            elif self.phase == 'resuming' and elapsed > .5:
                self.check('the last real choice resumes gameplay', not u.GameplayStatics.is_game_paused(self.world))
                self.check('waiting and cards close after everyone finishes', not self.pc.is_waiting_for_perk_team() and not self.pc.is_reward_menu_open())
                self.check('gameplay movement and look input unlock after everyone finishes',
                           not self.pc.is_move_input_ignored() and not self.pc.is_look_input_ignored())
                self.check('gameplay time advances after both choices', float(u.GameplayStatics.get_time_seconds(self.world)) > self.paused_game_time)
                self.stage_real('gameplay')
            elif self.phase == 'gameplay' and elapsed > 1.:
                # Retain a short final presentation hold before validating PNGs
                # and restoring the editor's temporary realtime overrides.
                self.stage_real('capture_flush')
        except Exception:
            self.finish(traceback.format_exc())


if __name__ == '__main__':
    old = getattr(builtins, KEY, None)
    if old and not getattr(old, 'done', True): raise RuntimeError('Preserve active perk wait probe')
    probe = PerkTeamWaitSolo()
    setattr(builtins, KEY, probe)
    try: probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
