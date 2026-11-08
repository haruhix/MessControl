"""One owned functional Hub -> Day 1 -> Main Menu readiness check.

Use a clean Editor with -MCHubOwned -MCHubTransientProfile and current DLLs.
MC_DAY1_LOGFILE names this Editor's E: abslog; optional MC_DAY1_OUTPUT chooses
a fresh E: directory. Do not invoke before the root releases Editor ownership.
Cancel only this run: builtins.mc_day1_start_solo.cancel().
"""
import builtins
import hashlib
import importlib.util
import json
import time
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
TASK = Path('E:/CodexWork/MessControl/Validation/Day1Start_20261108')
OUT = Path(globals().get('MC_DAY1_OUTPUT', TASK/'Solo01')).resolve()
LOG = Path(globals().get('MC_DAY1_LOGFILE', TASK/'SoloEditor.log')).resolve()
KEY = 'mc_day1_start_solo'
spec = importlib.util.spec_from_file_location('mc_day1_hub_helpers', ROOT/'Tools/Unreal/Hub/check_hub_solo.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
h.OUT = OUT
valid, path, prop, require = h.valid, h.path, h.prop, h.require


class Day1Solo(h.HubSolo):
    def __init__(self):
        require('mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(), 'Transient profile required')
        require(LOG.drive.upper() == 'E:' and LOG.is_file(), 'Pass the actual owned Editor abslog on E:')
        super().__init__()
        self.original_camera = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_level_viewport_camera_info()
        baseline_path = TASK/'BeforeEditor.json'
        if baseline_path.is_file():
            baseline = json.loads(baseline_path.read_text(encoding='utf-8-sig'))
            camera = baseline['camera']
            self.original_map = baseline['world'].split('.')[0]
            self.original_camera = (u.Vector(*camera['location']), u.Rotator(
                pitch=camera['rotation'][0], yaw=camera['rotation'][1], roll=camera['rotation'][2]))
            self.report['original_editor_map'] = self.original_map
            self.report['user_camera_baseline'] = str(baseline_path)
        self.log_offset = LOG.stat().st_size
        self.capture_hz = self.max_frames = 0
        self.preparation = None
        self.menu_gc_at = None
        self.native_failures = []
        self.day_ready_since = None
        self.report.update(scope='One solo functional Hub readiness -> normal Day1 launch -> normal return to menu -> owned GC',
            source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), log_file=str(LOG),
            limits=['Standalone local authority only; no four-player transport or ping certification.',
                    'Normal PlayerController request callbacks, not physical keyboard/mouse delivery.',
                    'One controlled placement of the owned pawn inside the saved ready zone; no readiness/state override.',
                    'Saved maps, assets, authored scale, profiles and progress are not modified or saved.',
                    'Editor PSO policy may differ from packaged Windows. A passing Editor return/GC is not packaged PSO-crash certification.'])

    def start(self):
        # Retain the helper's ownership/settings lifecycle without changing viewport realtime preferences.
        require(self.levels.load_level('/Game/Maps/Hub'), 'Could not load saved Hub')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(not h.dirty(), 'Loading Hub unexpectedly dirtied packages')
        require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(h.PLAY)), 'Standalone PIE settings rejected')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.console('t.IdleWhenNotForeground 0')
        self.console('net.AllowPIESeamlessTravel 1')
        self.pie_start = u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps({'options': {'bSimulate': False, 'playMode': 'PlayMode_InEditorFloating', 'warmupSeconds': 0.}}))
        require(self.pie_start is not None and not prop(self.pie_start, 'Error'), 'Owned StartPIE request failed')
        self.requested_pie = True
        self.stage('await_hub')

    def log_lines(self):
        with LOG.open('rb') as stream:
            stream.seek(self.log_offset)
            return stream.read().decode('utf-8', errors='replace').splitlines()

    def preparation_state(self):
        if not valid(self.preparation) or self.preparation.get_outer() != self.world:
            self.preparation = None
            found = [x for x in u.ObjectIterator(u.MCWorldPreloadSubsystem.static_class()) if valid(x) and x.get_outer() == self.world]
            require(len(found) <= 1, 'Multiple preparation subsystems for the owned world')
            self.preparation = found[0] if found else None
        if not valid(self.preparation):
            return None
        row = dict(world=path(self.world), map=self.last_context_map, phase=str(self.preparation.get_phase()),
            ready=bool(self.preparation.is_ready()), failed=bool(self.preparation.has_failed()),
            detail=str(self.preparation.get_detail_text()), wall_seconds=time.monotonic()-self.started)
        require(not row['failed'], 'Real preload failed: '+row['detail'])
        return row

    def sample(self):
        self.native_failures = [x for x in self.log_lines() if 'MC_PRELOAD_FAILED' in x or 'Freeze shared profile is not resident' in x]
        row = self.preparation_state()
        if row:
            self.report['samples'].append(row)
            self.write()

    def advance(self):
        age = time.monotonic()-self.phase_wall
        require(not self.native_failures, 'Native preload failure: '+' | '.join(self.native_failures))
        row = self.preparation_state()
        if self.phase == 'await_hub':
            require(age < 135, 'Hub did not become ready')
            if not row or not row['ready'] or not isinstance(self.gs, u.MCHubGameState) or not valid(self.hero):
                return
            if not self.gs.are_all_loaded():
                return
            zones = self.actors(u.MCHubReadyZone)
            require(len(zones) == 1, 'Expected one saved ready zone')
            self.zone = zones[0]
            zone = prop(self.zone, 'zone')
            center = zone.get_world_location()
            target = u.Vector(center.x, center.y, self.hero.get_actor_location().z)
            self.hero.set_actor_location(target, False, True)
            self.hero.get_movement_component().stop_movement_immediately()
            self.report['ready_zone_placement'] = dict(zone=path(self.zone), target=h.vec(target))
            self.check('hub_preload_ready', True, '')
            self.stage('await_ready_zone')
        elif self.phase == 'await_ready_zone':
            require(age < 12, 'Real ready zone did not register the owned pawn')
            if self.gs.are_all_ready() and self.gs.are_all_loaded() and self.zone.contains_pawn(self.hero):
                self.request('request_hub_open_selection')
                self.stage('await_selection')
        elif self.phase == 'await_selection':
            require(age < 45, 'Normal Hub selection did not become loaded')
            if not isinstance(self.gs, u.MCHubGameState) or prop(self.gs, 'phase') != u.MCHubPhase.SELECTING or not self.gs.are_all_loaded():
                return
            self.request('request_hub_select_day', 1)
            self.stage('await_day_selected')
        elif self.phase == 'await_day_selected':
            require(age < 15, 'Normal Day1 selection did not settle')
            if int(prop(self.gs, 'selected_day')) != 1 or not self.gs.are_all_loaded():
                return
            # No Python reference to the old Hub actor should pin its world during later GC.
            self.zone = None
            self.request('request_hub_launch')
            self.stage('travel_day')
        elif self.phase == 'travel_day':
            require(age < 145, 'Day1 did not finish normal preload/ACK')
            if self.last_context_map != 'L_Mouth' or not isinstance(self.gs, u.MCGameState) or not row or not row['ready']:
                self.day_ready_since = None
                return
            if not valid(self.hero) or bool(prop(self.gs, 'lobby_waiting')) or not self.pc.is_world_presentation_ready():
                self.day_ready_since = None
                return
            # Data readiness can precede the next rendered frame and removal of
            # the waiting cover. Observe the actual cover, then allow two full
            # wall seconds of stable Ready/ACK state before capture or travel.
            if valid(h.owned_root_widget(u.MCLoadingWidget, self.pc)):
                self.day_ready_since = None
                return
            if self.day_ready_since is None:
                self.day_ready_since = time.monotonic()
                return
            settled = time.monotonic()-self.day_ready_since
            if settled < 2:
                return
            self.report['day1_ready'] = dict(**row, gameplay_load_id=str(prop(self.gs, 'gameplay_load_id')),
                ice_scope='Protected C++ property; not exposed to this Python probe. Native enrollment tests and actual FreezeSources/Ready logs are separate evidence.',
                pawn=path(self.hero), reward_menu=bool(self.pc.is_reward_menu_open()),
                settled_wall_seconds=settled, loading_widget_visible=False,
                gameplay_control_not_claimed='CanWork is not reflected; an ordinary first-perk modal may legitimately remain open.')
            self.check('day1_real_preload_and_lobby_ack_ready', True, '')
            self.check('day1_loading_cover_removed_and_ready_settled', True, '')
            self.capture('Day1Ready.png')
            self.request('return_to_main_menu')
            self.stage('travel_menu')
        elif self.phase == 'travel_menu':
            require(age < 60, 'Normal return to menu did not finish preparation')
            if self.last_context_map != 'L_MainMenu' or not row or not row['ready']:
                return
            self.check('normal_return_to_menu_ready', True, '')
            # This is the owned disposable PIE, after travel finished and local resources are Ready.
            self.menu_gc_at = time.monotonic()
            self.report['menu_gc_request'] = dict(world=path(self.world), method='SystemLibrary.collect_garbage', wall_seconds=self.menu_gc_at-self.started)
            u.SystemLibrary.collect_garbage()
            self.stage('menu_gc_tail')
        elif self.phase == 'menu_gc_tail':
            require(self.last_context_map == 'L_MainMenu' and row and row['ready'], 'Menu readiness regressed after owned GC')
            if age >= 3:
                self.check('menu_remained_ready_after_owned_gc', True, '')
                self.check('no_freeze_preload_failure_logged', True, '')
                self.report['native_lines'] = [x for x in self.log_lines() if 'MC_HUB_TRAVEL' in x or 'MC_PRELOAD_READY' in x or 'MC_PRELOAD_SCOPED' in x]
                self.begin_finish()

    def finish(self):
        super().finish()
        if not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(*self.original_camera)
                self.report['cleanup']['user_camera_restored'] = True
            except Exception as error:
                self.report.update(passed=False, status='failed', error=str(error))
            self.write()


previous = getattr(builtins, KEY, None)
require(previous is None or getattr(previous, 'done', True), 'A Day1 run is already active')
probe = Day1Solo()
setattr(builtins, KEY, probe)
u.EditorPythonScripting.set_keep_python_script_alive(True)
probe.write()
probe.callback = u.register_slate_post_tick_callback(probe.tick)
