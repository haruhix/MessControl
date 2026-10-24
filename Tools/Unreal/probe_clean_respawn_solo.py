"""Two sequential real reserve respawns in one owned standalone L_Mouth PIE.

Requires an idle, clean L_Mouth Editor launched with -MCDevUI and
-MCHubTransientProfile, after the requested native build. No packages are saved.
Transient fixtures: resolve any initial reward through ChooseRewardPerk(0),
ResetPerks on the local PlayerState, dirty/damage the next actual reserve to
25 HP, KillSelf through the ordinary development action, then grant P25 for
the second case. Respawn timing, reserve selection and possession stay native.
The current source retires the initial perk; absence of that UI is normal.
Cancel via builtins.mc_clean_respawn_solo.finish('cancelled').
"""
import builtins
import datetime
import hashlib
import json
import time
import traceback
import uuid
from pathlib import Path
import unreal as u

KEY = 'mc_clean_respawn_solo'
OUTPUT = Path('E:/DEVGAME/MessControl/Artifacts/CleanRespawn_20261024')
PLAY = dict(playNetMode='PIE_Standalone', playNumberOfClients=1,
            runUnderOneProcess=True, bLaunchSeparateServer=False,
            newWindowWidth=1280, newWindowHeight=720,
            clientFixedFPS=[60], serverFixedFPS=0)


def prop(obj, name):
    native = ''.join(s[:1].upper()+s[1:] for s in name.split('_'))
    error = None
    for key in (name, native, 'b'+native):
        try:
            return obj.get_editor_property(key)
        except Exception as caught:
            error = caught
    raise error


def valid(obj):
    try:
        return obj is not None and u.SystemLibrary.is_valid(obj)
    except (TypeError, RuntimeError):
        return False


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def status(hero):
    component = prop(hero, 'status')
    state = prop(component, 'state')
    return dict(health=float(prop(state, 'health')), maximum=float(prop(state, 'max_health')),
                coffee=int(prop(state, 'coffee_left')), repair=int(prop(state, 'repair_left')),
                loose=bool(component.is_loose()))


class CleanRespawnSolo:
    def __init__(self):
        self.done = False
        self.callback = self.pie_start = self.world = self.pc = self.identity = None
        self.requested_pie = False
        self.viewport_overrides = []
        self.phase = 'waiting_pie'
        self.phase_at = self.started = time.monotonic()
        self.ready_since = None
        self.choice_at = -100.0
        self.case = None
        self.editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        existing = list(u.EditorLevelLibrary.get_pie_worlds(True))
        require(not self.levels.is_in_play_in_editor() and not existing,
                'Preexisting PIE is active; it was left untouched')
        self.editor_world = self.editor.get_editor_world()
        require(self.editor_world.get_path_name() == '/Game/Maps/L_Mouth.L_Mouth', 'Open clean L_Mouth first')
        self.editor_path = self.editor_world.get_path_name()
        dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        require(not dirty, 'Unrelated dirty packages must be preserved; no authoring or SaveAll is performed')
        command = u.SystemLibrary.get_command_line().lower()
        require('-mcdevui' in command and '-mchubtransientprofile' in command,
                'Launch Editor with -MCDevUI -MCHubTransientProfile before this probe')
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(PLAY)))
        self.engine = next(o for o in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_throttle = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.old_idle = u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        run = datetime.datetime.now(datetime.timezone.utc).strftime('Run_%Y%m%dT%H%M%SZ_') + uuid.uuid4().hex[:6]
        self.output = OUTPUT/run
        self.output.mkdir(parents=True, exist_ok=False)
        self.report = dict(status='running', passed=False, cases=[], screenshots=[], phase=self.phase,
                           preexisting_pie=[], dirty_before=[], previous_play_settings=self.old_play,
                           editor_world=self.editor_path, network_tested=False, microphone_used=False,
                           first_perk_ui_seen=False, reward_choices=[],
                           fixtures=['ResetPerks on the temporary local PlayerState',
                                     'SetCoffee(1) and ReceiveArenaHit(current health minus 25) on each next reserve',
                                     'RequestDevAction(KillSelf), then native delay/selection/possession',
                                     'ServerGrantPerk(P25) before second death'],
                           limitations=['No production animation integration is tested.',
                                        'Screenshots use the actual PIE SWindow/HUD, with no staged camera.',
                                        'KillSelf suspends unrelated schedulers through the ordinary development action; the probe makes no direct health or expected-state writes.'])

    def write(self):
        self.report['phase'] = self.phase
        (self.output/'Report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False), encoding='utf8')

    def stage(self, phase):
        self.phase, self.phase_at = phase, time.monotonic()
        self.write()

    def start(self):
        require(not u.InputLibrary.modifier_keys_state_is_control_down(u.InputLibrary.get_modifier_keys_state()),
                'Release Ctrl first; Unreal otherwise requests a spectator PIE')
        for key in self.levels.get_viewport_config_keys():
            self.levels.editor_set_viewport_realtime(False, key)
            self.viewport_overrides.append(key)
        require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(PLAY)), 'Standalone settings rejected')
        self.engine.set_editor_property('FixedFrameRate', 60.0)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        u.SystemLibrary.execute_console_command(self.editor_world, 't.IdleWhenNotForeground 0')
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.requested_pie = True
        self.pie_start = u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps({'options': {'bSimulate': False, 'playMode': 'PlayMode_InEditorFloating', 'warmupSeconds': 0.0}}))
        require(self.pie_start is not None and not prop(self.pie_start, 'Error'), 'StartPIE request failed')
        self.write()
        u.log('MC_CLEAN_RESPAWN_START ' + str(self.output/'Report.json'))

    def ready(self):
        # Query the rendered UMG widgets; the controller's private transient
        # pointer is not readable through every installed Python wrapper.
        loading_widgets = u.ObjectIterator(u.load_class(None, '/Script/MessControl.MCLoadingWidget'))
        loading_open = any(valid(w) and w.is_in_viewport() and w.get_visibility() not in (u.SlateVisibility.COLLAPSED, u.SlateVisibility.HIDDEN) for w in loading_widgets)
        return (self.pc.is_world_presentation_ready() and not loading_open and not self.pc.is_reward_menu_open()
                and not self.pc.is_move_input_ignored() and not self.pc.is_look_input_ignored())

    def available(self):
        return sorted([t for t in prop(self.gs, 'arena_teeth') if valid(t) and t.is_available()],
                      key=lambda t: int(prop(prop(t, 'state'), 'tooth_id')))

    def prepare_case(self, maximum):
        hero = self.pc.get_controlled_pawn()
        require(valid(hero) and prop(hero, 'status').is_alive() and self.ready(), 'Alive, ready local pawn required')
        available = self.available()
        require(available, 'No reserve remains for this case')
        source = available[0]
        source_health = float(prop(prop(prop(source, 'status'), 'state'), 'health'))
        require(source_health > 25.0, 'Lowest available reserve already has <=25 HP; fixture cannot be repaired silently')
        source.set_coffee(1.0)
        require(source.receive_arena_hit(source_health-25.0, u.Vector(1, 0, 0)), 'Reserve fixture damage was rejected')
        fixture = status(source)
        require(abs(fixture['health']-25.0) < .001 and fixture['coffee'] > 0 and fixture['repair'] > 0 and fixture['loose'],
                'Reserve must remain alive at 25 HP, dirty and loose: ' + str(fixture))
        self.old_hero, self.source = hero, source
        self.expected_max = maximum
        self.case = dict(index=len(self.report['cases'])+1, expected_maximum=maximum,
                         old_pawn=hero.get_path_name(), player_state=self.identity.get_path_name(),
                         reserve=source.get_path_name(), source_id=int(prop(prop(source, 'state'), 'tooth_id')),
                         reserve_fixture=fixture, available_before=len(available), before_death=status(hero))
        self.report['cases'].append(self.case)
        self.pc.request_dev_action(u.MCDevAction.KILL_SELF, -1)
        require(not prop(hero, 'status').is_alive(), 'Native KillSelf action did not kill the pawn')
        respawn_at = float(prop(hero, 'respawn_at'))
        server_now = float(self.gs.get_server_world_time_seconds())
        require(respawn_at > server_now, 'Native death did not schedule the respawn timer')
        self.case.update(native_respawn_at=respawn_at, server_time_at_death=server_now,
                         native_delay_seconds=respawn_at-server_now)
        self.stage('waiting_replacement')

    def assert_replacement(self):
        hero = self.pc.get_controlled_pawn()
        observed = status(hero)
        require(hero != self.old_hero and prop(hero, 'player_state') == self.identity and prop(self.pc, 'player_state') == self.identity,
                'Replacement pawn/PlayerState identity was not preserved')
        require(abs(observed['maximum']-self.expected_max) < .001 and abs(observed['health']-observed['maximum']) < .001,
                'Replacement is not full at the expected maximum: ' + str(observed))
        require(observed['coffee'] == 0 and observed['repair'] == 0 and not observed['loose'],
                'Replacement inherited reserve dirt or looseness: ' + str(observed))
        require(int(prop(hero, 'respawn_source_id')) == self.case['source_id'], 'Native respawn selected a different source')
        require(bool(prop(prop(self.source, 'state'), 'consumed')) and not self.source.is_available(), 'Source was not consumed')
        require(len(self.available()) == self.case['available_before']-1, 'Reserve count changed by something other than one replacement')
        return observed

    def screenshot(self):
        target = self.output/('Respawn_%02d.png' % self.case['index'])
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(target)), 'Actual PIE SWindow capture failed')
        data = target.read_bytes()
        require(data[:8] == b'\x89PNG\r\n\x1a\n' and len(data) >= 24, 'Capture did not produce PNG')
        self.report['screenshots'].append(dict(file=str(target), sha256=hashlib.sha256(data).hexdigest(),
            width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'),
            method='MCGameplayUIEditorLibrary.capture_pie_game_window: exact PIE SWindow including UI/window chrome',
            world=self.world.get_path_name(), game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))))

    def tick(self, _dt):
        if self.done:
            return
        try:
            wall = time.monotonic()
            require(wall-self.started < 120.0, 'Owned solo probe exceeded 120 seconds')
            require(self.pie_start is None or not prop(self.pie_start, 'Error'), 'Async StartPIE failed')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one standalone authority PIE world')
                self.world = worlds[0]
                require('L_Mouth' in self.world.get_path_name(), 'PIE did not start L_Mouth')
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                self.gs = u.GameplayStatics.get_game_state(self.world)
                if not valid(self.pc) or not valid(self.gs):
                    self.world = None
                    return
                require(len(u.GameplayStatics.get_all_actors_of_class(self.world, u.PlayerController)) == 1, 'Additional players are outside this solo probe')
                self.report['world'] = self.world.get_path_name()
                self.stage('startup_ready')
            require(valid(self.world) and self.world in worlds, 'Owned PIE ended or was replaced')
            if self.pc.is_reward_menu_open():
                self.report['first_perk_ui_seen'] = self.phase == 'startup_ready' or self.report['first_perk_ui_seen']
                require(self.phase == 'startup_ready', 'An unrelated reward interrupted the narrow respawn case')
                if wall-self.choice_at >= .4:
                    self.pc.choose_reward_perk(0)
                    self.choice_at = wall
                    self.report['reward_choices'].append(dict(index=0, wall_seconds=wall-self.started))
                return
            hero = self.pc.get_controlled_pawn()
            if self.phase == 'startup_ready':
                if not valid(hero) or not self.ready() or prop(self.gs, 'tutorial_active') or prop(self.gs, 'lobby_waiting'):
                    self.ready_since = None
                    return
                self.ready_since = self.ready_since or wall
                if wall-self.ready_since < .5:
                    return
                self.identity = prop(self.pc, 'player_state')
                require(valid(self.identity) and isinstance(hero, u.MCToothCharacter), 'Expected normal local tooth/PlayerState')
                self.perks = prop(self.identity, 'perks')
                self.perks.reset_perks()
                require(not prop(prop(prop(hero, 'status'), 'settings'), 'inherit_reserve_status'), 'Reserve inheritance is still enabled')
                require(len(self.available()) >= 2, 'Two available reserves are required')
                self.report['baseline_after_reset'] = status(hero)
                self.stage('baseline_settle')
            elif self.phase == 'baseline_settle' and wall-self.phase_at >= .3:
                require(abs(status(hero)['maximum']-100.0) < .001, 'Baseline max HP is not 100')
                self.prepare_case(100.0)
            elif self.phase == 'waiting_replacement':
                if valid(hero) and hero != self.old_hero:
                    require(prop(hero, 'status').is_alive(), 'New pawn is already dead')
                    self.case['new_pawn'] = hero.get_path_name()
                    self.detected_at = float(u.GameplayStatics.get_time_seconds(self.world))
                    self.stage('replacement_settle')
            elif self.phase == 'replacement_settle':
                if float(u.GameplayStatics.get_time_seconds(self.world))-self.detected_at >= .65 and self.ready():
                    self.case['observed'] = self.assert_replacement()
                    self.screenshot()
                    self.checked_at = float(u.GameplayStatics.get_time_seconds(self.world))
                    self.stage('check_stability')
            elif self.phase == 'check_stability' and float(u.GameplayStatics.get_time_seconds(self.world))-self.checked_at >= .4:
                self.case['stable_observed'] = self.assert_replacement()
                self.case['passed'] = True
                if self.case['index'] == 2:
                    self.finish()
                    return
                before = status(hero)
                require(self.perks.server_grant_perk('P25'), 'P25 grant rejected')
                after = status(hero)
                require(abs(after['maximum']-115.0) < .001 and abs(after['health']-before['health']) < .001,
                        'P25 must increase the maximum without healing its grant')
                self.report['p25_grant'] = dict(before=before, after=after)
                self.prepare_case(115.0)
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        self.done = True
        errors = []
        def restore(label, action):
            try:
                action()
                return True
            except Exception:
                errors.append(label + ': ' + traceback.format_exc())
                return False
        if self.callback is not None:
            restore('unregister callback', lambda: u.unregister_slate_post_tick_callback(self.callback))
            self.callback = None
        try:
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            owned = valid(self.world) and any(valid(w) and w == self.world for w in worlds)
        except Exception:
            owned = False
            errors.append('identify owned PIE: ' + traceback.format_exc())
        if self.requested_pie and owned:
            self.report['owned_pie_stop_requested'] = restore('stop owned PIE', self.levels.editor_request_end_play)
        elif self.world is not None:
            error = error or 'Owned PIE ended externally; replacement sessions were preserved'
        for key in reversed(self.viewport_overrides):
            restore('viewport realtime override', lambda k=key: self.levels.editor_set_viewport_realtime(True, k))
        restore('PIE settings', lambda: require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(self.old_play)), 'PIE settings restore rejected'))
        restore('frame rate', lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]))
        restore('fixed frame enabled', lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]))
        restore('foreground throttle', lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_throttle))
        restore('foreground idle', lambda: u.SystemLibrary.execute_console_command(self.editor_world, 't.IdleWhenNotForeground '+str(self.old_idle)))
        self.report.update(status='failed' if error or errors else 'passed', passed=not bool(error or errors),
                           error=error, restoration_errors=errors, restored_settings=not errors,
                           wall_seconds=time.monotonic()-self.started, package_save_requested=False)
        self.write()
        u.log('MC_CLEAN_RESPAWN_' + self.report['status'].upper() + ' ' + str(self.output/'Report.json'))


def main():
    previous = getattr(builtins, KEY, None)
    require(previous is None or getattr(previous, 'done', True), 'A clean-respawn probe is already running')
    probe = CleanRespawnSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
    return probe


if __name__ == '__main__':
    main()
