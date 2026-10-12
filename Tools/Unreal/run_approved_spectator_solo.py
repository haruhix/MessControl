"""Own one short standalone PIE run for check_approved_spectator_solo.py.

Run from an idle Editor already showing L_Mouth, after the native changes are
loaded. Resolves the real first-perk choice before checking death spectating.
The single-day scheduler's tick is disabled only inside this temporary PIE to
isolate camera orbit from unrelated event arrivals. The nested check uses a
transient bot, KillSelf, and the real reserve-tooth respawn path.

This wrapper launches no network peers and saves no content or editor config.
It waits for nested cleanup, stops only its own PIE, then restores temporary
play-window, frame-rate and foreground settings. Genuine SHOWUI frames and
timestamped nested evidence remain under Saved/ApprovedTasks. Optional global
MC_SPECTATOR_OUTPUT chooses the wrapper report directory (not frame content).
Cancel: builtins.mc_approved_spectator_solo.cancel().
"""
import builtins
import json
import runpy
import time
import traceback
from pathlib import Path

import unreal as u


KEY = 'mc_approved_spectator_solo'
PLAY = dict(playNetMode='PIE_Standalone', playNumberOfClients=1,
            runUnderOneProcess=True, bLaunchSeparateServer=False,
            newWindowWidth=1280, newWindowHeight=720,
            clientFixedFPS=[60], serverFixedFPS=0)


def prop(obj, name):
    return obj.get_editor_property(name)


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


class ApprovedSpectatorSolo:
    def __init__(self):
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        if self.levels.is_in_play_in_editor() or u.EditorLevelLibrary.get_pie_worlds(True):
            raise RuntimeError('An existing PIE is active; it was left untouched')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        if '/Game/Maps/L_Mouth' not in self.editor_world.get_path_name():
            raise RuntimeError('Open L_Mouth first; this wrapper does not change the editor map')
        previous = getattr(builtins, 'mc_approved_spectator_check', None)
        if previous is not None and previous.callback is not None:
            raise RuntimeError('A spectator check is already running; it was left untouched')
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(PLAY)))
        self.engine = next(obj for obj in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if obj.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.old_idle = u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        default = Path(u.Paths.project_saved_dir())/'Checks/ApprovedSpectatorSolo'
        self.output = Path(globals().get('MC_SPECTATOR_OUTPUT', default)).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.started = time.monotonic()
        self.callback = None
        self.pie_start = None
        self.requested_pie = self.done = False
        self.world = self.pc = self.check = None
        self.scheduler = None
        self.old_scheduler_tick = None
        self.phase = 'waiting_pie'
        self.phase_at = self.started
        self.error = None
        self.restore_errors = []
        self.report = dict(status='running', passed=False, phase=self.phase,
                           scope='One owned floating 1280x720 standalone PIE; no network peers',
                           fixture='SingleDayDirector tick disabled after real first-perk resolution',
                           previous_play_settings=self.old_play, checks={}, cleanup={})

    @staticmethod
    def require(ok, message):
        if not ok:
            raise RuntimeError(message)

    def write(self):
        self.report['phase'] = self.phase
        (self.output/'Report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2,
                                                       allow_nan=False), encoding='utf8')

    def stage(self, name):
        self.phase = name
        self.phase_at = time.monotonic()
        self.write()
        u.log('MC_APPROVED_SPECTATOR_WRAPPER_STAGE '+name)

    def start(self):
        self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(PLAY)),
                     'Could not apply standalone PIE settings')
        self.engine.set_editor_property('FixedFrameRate', 60.)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        u.SystemLibrary.execute_console_command(self.editor_world, 't.IdleWhenNotForeground 0')
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.requested_pie = True
        # EditorRequestBeginPlay always attaches to the active editor viewport.
        # The engine tool's explicit PlayMode leaves DestinationSlateViewport
        # unset for a floating window. Its unexposed options struct is supplied
        # through the existing native tool JSON serializer.
        self.pie_start = u.ToolsetRegistry.execute_tool(
            'EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps({'Options': {'bSimulate': False, 'PlayMode': 1, 'WarmupSeconds': 0.}}))
        self.require(self.pie_start is not None, 'StartPIE returned no async result')
        self.require(not prop(self.pie_start, 'Error'), str(prop(self.pie_start, 'Error')))
        self.write()
        u.log('MC_APPROVED_SPECTATOR_WRAPPER_START '+str(self.output))

    def start_nested_check(self):
        state = u.GameplayStatics.get_game_state(self.world)
        self.require(not prop(state, 'bLobbyWaiting') and not prop(state, 'bTutorialActive'),
                     'Ordinary gameplay must be active before checking death spectating')
        self.scheduler = prop(state, 'SingleDayDirector')
        if valid(self.scheduler):
            self.old_scheduler_tick = self.scheduler.is_actor_tick_enabled()
            self.scheduler.set_actor_tick_enabled(False)
            self.report['checks']['isolated_single_day_scheduler'] = True
        self.require(not self.pc.is_look_input_ignored(), 'A gameplay menu still blocks the camera')
        path = Path(u.Paths.project_dir())/'Tools/Unreal/check_approved_spectator_solo.py'
        self.require(path.is_file(), 'Missing sibling spectator check script')
        runpy.run_path(str(path), run_name='__main__')
        self.check = getattr(builtins, 'mc_approved_spectator_check', None)
        self.require(self.check is not None, 'Nested spectator check did not initialize')
        self.report['nested_report'] = str(self.check.output/'Report.json')
        self.stage('spectator_check')

    def tick(self, _delta):
        if self.done:
            return
        try:
            now = time.monotonic()
            age = now-self.phase_at
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.phase == 'ending_pie':
                if not self.levels.is_in_play_in_editor():
                    self.report['cleanup']['owned_pie_stopped'] = True
                    self.finish()
                elif age > 8.:
                    self.error = self.error or 'Owned PIE did not stop within eight seconds'
                    self.finish()
                return
            if self.pie_start is not None:
                self.require(not prop(self.pie_start, 'Error'), 'StartPIE: '+str(prop(self.pie_start, 'Error')))
            if now-self.started > 100. and self.phase != 'nested_cleanup':
                self.cancel('Short spectator wrapper exceeded its wall-time limit')
                return
            if self.world is None:
                self.require(age < 25., 'Standalone PIE did not start within 25 seconds')
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),
                             'Expected exactly one standalone authority PIE world')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if pc is None or pc.get_controlled_pawn() is None:
                    return
                self.world, self.pc = worlds[0], pc
                self.report['world'] = self.world.get_path_name()
                self.stage('first_perk')
                return
            self.require(self.world in worlds, 'Owned PIE ended or was replaced')
            if self.phase == 'first_perk':
                self.require(age < 25., 'First-perk UI did not become ready on L_Mouth')
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.report['checks']['normal_first_perk_ready'] = True
                    self.stage('perk_resolution')
            elif self.phase == 'perk_resolution':
                self.require(age < 10., 'First-perk choice did not resolve')
                if not self.pc.is_reward_menu_open():
                    self.report['checks']['normal_first_perk_resolved'] = True
                    self.start_nested_check()
            elif self.phase in ('spectator_check', 'nested_cleanup'):
                if self.check is None:
                    self.check = getattr(builtins, 'mc_approved_spectator_check', None)
                if self.check and self.check.report.get('status') in ('complete', 'failed'):
                    self.report['nested_status'] = self.check.report['status']
                    self.report['nested_checks'] = self.check.report.get('checks', {})
                    self.report['nested_cleanup'] = self.check.report.get('cleanup', {})
                    self.report['frames'] = self.check.report.get('frames', [])
                    self.report['samples'] = self.check.report.get('samples', [])
                    if self.check.report['status'] != 'complete':
                        self.error = self.error or self.check.report.get('error') or 'Nested spectator check failed'
                    self.begin_end_pie()
                elif self.phase == 'nested_cleanup' and age > 10.:
                    if self.check is not None and self.check.callback is not None:
                        self.check.finish(self.error or 'Nested cleanup timed out')
                    self.begin_end_pie()
            self.write()
        except Exception:
            self.cancel(traceback.format_exc())

    def cancel(self, error='Stopped by operator'):
        if self.done:
            return
        self.error = self.error or error
        if self.phase == 'ending_pie':
            return
        if self.check is None:
            candidate = getattr(builtins, 'mc_approved_spectator_check', None)
            # Do not claim a check predating this wrapper if nested startup failed.
            if candidate is not None and candidate.started >= self.started:
                self.check = candidate
        if self.check is not None and self.check.callback is not None:
            if self.phase != 'nested_cleanup':
                self.check.begin_cleanup(self.error)
                self.stage('nested_cleanup')
            return
        self.begin_end_pie()

    def begin_end_pie(self):
        if self.phase == 'ending_pie':
            return
        if valid(self.scheduler) and self.old_scheduler_tick is not None:
            try:
                self.scheduler.set_actor_tick_enabled(self.old_scheduler_tick)
                self.report['cleanup']['scheduler_tick_restored'] = True
            except Exception:
                self.restore_errors.append(traceback.format_exc())
        worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
        if self.requested_pie and self.levels.is_in_play_in_editor():
            if self.world is not None and self.world in worlds:
                self.levels.editor_request_end_play()
                self.report['cleanup']['owned_pie_end_requested'] = True
                self.stage('ending_pie')
                return
            self.report['cleanup']['replacement_pie_preserved'] = True
        self.finish()

    def finish(self):
        if self.done:
            return
        self.done = True
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        actions = [
            lambda: self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(self.old_play)),
                                 'PIE settings restore rejected'),
            lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]),
            lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]),
            lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_perf),
            lambda: u.SystemLibrary.execute_console_command(self.editor_world, 't.IdleWhenNotForeground '+str(self.old_idle))]
        for action in actions:
            try:
                action()
            except Exception:
                self.restore_errors.append(traceback.format_exc())
        if self.restore_errors:
            self.error = self.error or 'Temporary setting restoration failed'
        self.phase = 'done'
        self.report.update(status='failed' if self.error else 'passed', passed=not bool(self.error),
                           error=self.error, restored_settings=not self.restore_errors,
                           restore_errors=self.restore_errors, wall_seconds=time.monotonic()-self.started)
        self.write()
        u.log('MC_APPROVED_SPECTATOR_WRAPPER_'+('FAIL ' if self.error else 'PASS ')+str(self.output/'Report.json'))


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('Spectator wrapper already active; finish it before starting another')
    wrapper = ApprovedSpectatorSolo()
    setattr(builtins, KEY, wrapper)
    try:
        wrapper.start()
    except Exception:
        wrapper.cancel(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
