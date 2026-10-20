"""Observe the real resource barrier in one owned standalone PIE session.

Run only in an idle, saved MessControl Editor launched with -MCHubOwned (the
existing -MCFirstRelaxOwned wrapper is accepted). Requires the new Editor DLL.
Optional globals: MC_RESOURCE_LOADING_OUTPUT (fresh E: folder),
MC_RESOURCE_LOADING_MAP ('L_Mouth', 'Hub' or 'L_MainMenu'),
MC_RESOURCE_LOADING_CAPTURE_HZ (0..8, default 4),
MC_RESOURCE_LOADING_SELECT_PERK (default True, L_Mouth only).

The L_Mouth case observes actual subsystem phases, the opaque loading widget,
the Training barrier, and its removal before the ordinary first perk. It can
then invoke the normal ChooseRewardPerk(0) callback and wait for naturally
served first food. No profiles, state, actors, camera or clock are overridden.
Other maps observe local preparation and normal UI reveal, without travel.
The first-Relax helper supplies the established owned-editor, settings/map
restoration and cancellation safety. No previous probe is executed.
Captures are actual game-window PNGs with wall timings, not synthetic frames.
Cancel only this probe: builtins.mc_resource_loading_solo.cancel().
"""
import builtins
import hashlib
import importlib.util
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_RESOURCE_LOADING_OUTPUT',
    'E:/CodexWork/MessControlResourceLoading_20261020/Solo01')).resolve()
MAP = str(globals().get('MC_RESOURCE_LOADING_MAP', 'L_Mouth'))
KEY = 'mc_resource_loading_solo'
require_map = ('L_Mouth', 'Hub', 'L_MainMenu')
spec = importlib.util.spec_from_file_location('mc_resource_loading_helpers',
    ROOT / 'Tools/Unreal/check_first_relax_solo.py')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
helpers.OUT = OUT
valid, path, prop, enum_name, require = (helpers.valid, helpers.path,
    helpers.prop, helpers.enum_name, helpers.require)


class ResourceLoadingSolo(helpers.FirstRelaxSolo):
    def __init__(self):
        require(MAP in require_map, 'Unsupported observation map: ' + MAP)
        require(hasattr(u, 'MCWorldPreloadSubsystem') and hasattr(u, 'MCLoadingWidget'),
            'The resource-loading Editor DLL has not been built/loaded')
        super().__init__()
        self.preparation = None
        self.ready_seen_at = self.perk_seen_at = None
        self.loading_seen = self.preparing_seen = self.training_seen = False
        self.ready_phase_seen = False
        self.capture_hz = max(0., min(8., float(globals().get('MC_RESOURCE_LOADING_CAPTURE_HZ', 4))))
        self.select_perk = bool(globals().get('MC_RESOURCE_LOADING_SELECT_PERK', True))
        self.report.update(scope=__doc__, map=MAP, select_normal_first_perk=self.select_perk,
            source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            limitations=[
                'One local authority PIE; no multiplayer, packaged game, cold driver cache or cook verification.',
                'An already warm editor can finish too early to observe every phase. Unobserved phases are not invented.',
                'A visible widget/component flag does not establish visual quality; review the actual PNGs/video.',
                'The optional perk selection uses the normal PlayerController callback, not physical input delivery.',
                'No forced compilation, cache deletion, profile edits, teleports, fixtures, camera changes or fixed-step simulation.'])

    def start(self):
        require(self.levels.load_level('/Game/Maps/' + MAP), 'Could not load selected saved map')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(not helpers.dirty(), 'Loading selected map unexpectedly dirtied packages')
        require(u.ToolsetLibrary.set_object_properties(self.play, helpers.json.dumps(helpers.PLAY)),
            'Standalone settings rejected')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.console('t.IdleWhenNotForeground 0')
        self.pie_start = u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
            helpers.json.dumps({'options': {'bSimulate': False,
                'playMode': 'PlayMode_InEditorFloating', 'warmupSeconds': 0.}}))
        require(self.pie_start is not None, 'StartPIE returned no async result')
        self.requested_pie = True
        require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
        self.stage('observe_preparation')

    def refresh_world(self):
        worlds = [world for world in u.EditorLevelLibrary.get_pie_worlds(True) if valid(world)]
        if not worlds and not self.instance_path:
            return False
        require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),
            'Expected one standalone authority world; external PIE is never adopted')
        world = worlds[0]
        require(u.GameplayStatics.get_current_level_name(world, True) == MAP, 'Unexpected map/travel')
        instance = u.GameplayStatics.get_game_instance(world)
        require(valid(instance), 'PIE has no GameInstance identity')
        if self.instance_path is None:
            self.instance_path, self.world_path = path(instance), path(world)
            self.report.update(owned_game_instance=self.instance_path, owned_world=self.world_path)
        require(path(instance) == self.instance_path and path(world) == self.world_path,
            'Owned world was replaced; replacement must be preserved')
        self.world = world
        self.pc = u.GameplayStatics.get_player_controller(world, 0)
        self.gs = u.GameplayStatics.get_game_state(world)
        if not valid(self.pc):
            return False
        self.hero = self.pc.get_controlled_pawn()
        self.sequence = prop(self.gs, 'single_day_director') if isinstance(self.gs, u.MCGameState) else None
        matches = [item for item in u.ObjectIterator(u.MCWorldPreloadSubsystem.static_class())
            if valid(item) and item.get_outer() == world]
        require(len(matches) <= 1, 'Multiple preparation subsystems for one world')
        self.preparation = matches[0] if matches else None
        return valid(self.preparation)

    def capture(self, name, sequence=False):
        require(valid(self.world) and valid(self.pc), 'No owned world to capture')
        destination = OUT / ('frames' if sequence else 'screenshots') / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        request, game_time = time.monotonic() - self.started, self.now()
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(destination)),
            'Owned game-window capture failed')
        data = destination.read_bytes()
        require(len(data) > 24 and data.startswith(b'\x89PNG\r\n\x1a\n'), 'Capture is not a PNG')
        entry = dict(file=str(destination), phase=self.phase, world=path(self.world), game_seconds=game_time,
            capture_request_wall_seconds=request, capture_completed_wall_seconds=time.monotonic() - self.started,
            width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'),
            bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.report['frames' if sequence else 'screenshots'].append(entry)
        if sequence:
            self.frame_bytes += len(data)

    def observe(self):
        preparation = self.preparation
        phase = enum_name(preparation.get_phase())
        ready = bool(preparation.is_ready())
        overlay = helpers.owned_root_widget(u.MCLoadingWidget, self.pc)
        stage = enum_name(prop(self.sequence, 'stage')) if valid(self.sequence) else None
        row = dict(wall_seconds=time.monotonic() - self.started, game_seconds=self.now(), world=path(self.world),
            preparation_phase=phase, ready=ready, failed=bool(preparation.has_failed()),
            status=str(preparation.get_status_text()), detail=str(preparation.get_detail_text()),
            asset_progress=float(preparation.get_asset_progress()), loading_widget=path(overlay), native_stage=stage,
            reward_menu_open=bool(self.pc.is_reward_menu_open()) if isinstance(self.pc, u.MCPlayerController) else False)
        require(not row['failed'], 'Preparation failed: ' + row['detail'])
        require(ready == (phase == 'READY'), 'IsReady disagrees with the actual subsystem phase')
        if not ready:
            self.preparing_seen = True
            require(valid(overlay), 'Preparing world became visible without its loading cover')
            self.loading_seen = True
            if MAP == 'L_Mouth' and valid(self.sequence):
                require(stage == 'TRAINING', 'Director advanced before local visual readiness: ' + str(stage))
                self.training_seen = True
        else:
            self.ready_phase_seen = True
            if self.ready_seen_at is None:
                self.ready_seen_at = time.monotonic()
                self.report['ready_observed_wall_seconds'] = row['wall_seconds']
        if time.monotonic() >= self.sample_at:
            self.sample_at = time.monotonic() + .15
            self.report['samples'].append(row)
        return row, overlay

    def advance_loading(self, row, overlay):
        if self.phase == 'observe_preparation':
            if valid(overlay) and not self.report.get('loading_screenshot'):
                self.capture('01_real_loading_screen.png')
                self.report['loading_screenshot'] = True
            if not row['ready']:
                return
            if MAP == 'L_Mouth':
                if valid(overlay) or not self.pc.is_reward_menu_open():
                    return
                choice = helpers.owned_root_widget(u.MCPerkChoiceWidget, self.pc)
                if not valid(choice):
                    return
                if self.perk_seen_at is None:
                    self.perk_seen_at = time.monotonic()
                if time.monotonic() - self.perk_seen_at < .6:
                    return
                self.check('first_perk_visible_after_loading_cover_removed', True, '')
                self.capture('02_first_perk_uncovered.png')
                if self.select_perk:
                    self.report['requests'].append(dict(method='ChooseRewardPerk', choice_index=0,
                        wall_seconds=time.monotonic() - self.started))
                    self.pc.choose_reward_perk(0)
                    self.stage('await_natural_food')
                else:
                    self.complete_checks()
            elif not valid(overlay):
                ui_class = u.MCHubWidget if MAP == 'Hub' else u.MCMainMenuWidget
                require(valid(helpers.owned_root_widget(ui_class, self.pc)), 'Normal map UI did not reveal')
                if time.monotonic() - self.ready_seen_at > 1.:
                    self.capture('02_normal_ui_uncovered.png')
                    self.check('normal_ui_visible_after_cover_removed', True, '')
                    self.complete_checks()
        elif self.phase == 'await_natural_food':
            require(time.monotonic() - self.phase_wall < 55., 'Natural first food did not start after normal perk choice')
            require(row['ready'] and not valid(overlay), 'Loading readiness regressed during normal food start')
            foods = self.food_rows() if valid(self.sequence) else []
            if foods:
                self.report['naturally_served_food'] = foods
                self.capture('03_natural_food_after_ready.png')
                self.check('food_starts_normally_only_after_preparation', row['native_stage'] == 'FIRSTMEAL',
                    'Food appeared outside normal first meal')
                self.complete_checks()
        elif self.phase == 'capture_flush' and time.monotonic() - self.phase_wall >= .3:
            self.begin_finish()

    def complete_checks(self):
        self.check('observed_real_loading_overlay', self.loading_seen, 'Warm run missed the loading overlay; not a verified loading observation')
        self.check('observed_nonready_then_ready', self.preparing_seen and self.ready_phase_seen,
            'Warm run missed the preparing state; repeat observation without claiming a pass')
        if MAP == 'L_Mouth':
            self.check('director_remains_training_until_ready', self.training_seen, 'Training barrier was not observed')
        self.check('readiness_matches_native_phase', True, '')
        self.stage('capture_flush')

    def tick(self, _delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            if self.phase == 'ending_pie':
                if not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True):
                    self.report['cleanup']['owned_pie_stopped'] = True
                    self.finish()
                elif time.monotonic() - self.phase_wall > 12.:
                    self.error = self.error or 'Owned PIE did not stop within twelve seconds'
                    self.finish()
                return
            require(time.monotonic() - self.started < 180., 'Resource loading solo exceeded its 180-second wall bound')
            if self.phase == 'warming_editor':
                if time.monotonic() - self.started >= 3.:
                    self.start()
                return
            require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
            if not self.refresh_world():
                require(time.monotonic() - self.phase_wall < 45., 'Owned world/controller/subsystem did not appear')
                return
            row, overlay = self.observe()
            current = time.monotonic()
            if self.capture_hz and current >= self.frame_at:
                if len(self.report['frames']) < 480 and self.frame_bytes < 350 * 1024 * 1024:
                    self.frame_at = current + 1. / self.capture_hz
                    self.capture('%05d.png' % len(self.report['frames']), sequence=True)
                else:
                    self.report['capture_limit_reached'] = True
            self.advance_loading(row, overlay)
        except Exception:
            self.begin_finish(traceback.format_exc())
        finally:
            self.busy = False


def main():
    old = getattr(builtins, KEY, None)
    require(old is None or getattr(old, 'done', True), 'Another resource-loading probe is active')
    prior = getattr(builtins, helpers.KEY, None)
    require(prior is None or getattr(prior, 'done', True), 'An existing First Relax probe is active')
    probe = ResourceLoadingSolo()
    setattr(builtins, KEY, probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback = u.register_slate_post_tick_callback(probe.tick)


if __name__ == '__main__':
    main()
