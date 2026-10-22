"""Observe saved startup -> actual visible Play callback -> Hub in one owned solo PIE.

Requires the approved/current Editor DLL, saved authoring, idle editor and
-MCHubOwned (or -MCFirstRelaxOwned). Optional globals: MC_FRONTEND_HUB_OUTPUT
(fresh E: folder), MC_FRONTEND_HUB_CAPTURE_HZ (0..6; default6),
MC_FRONTEND_HUB_RETURN_TO_MENU (defaultTrue). Hard wall limit:185s, plus12s
owned-session cleanup. No Steam call, external peer, mic test, asset save,
readiness override, teleport, fixture or replacement camera. The actual offline
Play route may create its normal local listen driver; no peer is connected.

Prefer the saved 3D PlayButton when that presentation is actually live;
otherwise use the saved native menu's current CreateLobby/Play action button.
Broadcasting its actual OnClicked delegate proves the normal event route.
It does not prove physical pointer delivery. Screenshots
are real game-window PNGs; the sparse ~6Hz intro capture verifies integration,
not animation smoothness. Separate 60fps art renders assess the latter.
World arrival and native resource readiness are recorded separately. Each
preparation gate may use the configured native timeout (at most120s) plus10s
observation slack; the probe never raises that timeout or bypasses the gate.
Cancel only this probe: builtins.mc_frontend_hub_solo.cancel().
"""
import builtins
import hashlib
import importlib.util
import math
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_FRONTEND_HUB_OUTPUT',
    'E:/CodexWork/MessControlHubLogoTab_20261021/FrontendSolo01')).resolve()
KEY = 'mc_frontend_hub_solo'
spec = importlib.util.spec_from_file_location('mc_frontend_owned_helpers',
    ROOT / 'Tools/Unreal/check_first_relax_solo.py')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
helpers.OUT = OUT
valid, path, prop, require = helpers.valid, helpers.path, helpers.prop, helpers.require


def effectively_visible(widget):
    for _ in range(24):
        if not valid(widget):
            return True
        if not widget.is_visible():
            return False
        widget = widget.get_parent()
    raise RuntimeError('Unexpected cyclic/over-deep widget ancestry')


class FrontendHubSolo(helpers.FirstRelaxSolo):
    def __init__(self):
        require(hasattr(u, 'MCStudioSplashWidget') and hasattr(u, 'MCHubWidget'),
            'Current approved Editor DLL must be loaded before this probe')
        super().__init__()
        self.capture_hz = max(0., min(6., float(globals().get('MC_FRONTEND_HUB_CAPTURE_HZ', 6))))
        self.return_to_menu = bool(globals().get('MC_FRONTEND_HUB_RETURN_TO_MENU', True))
        self.current_map = None
        self.preparation = self.menu = self.splash = self.hub = None
        self.splash_seen_at = self.splash_last_seen_at = self.splash_gone_at = None
        self.last_observation_at = None
        self.max_observation_gap = 0.
        self.hub_ready_at = self.return_ready_at = None
        self.hub_arrived_at = self.hub_assets_ready_at = None
        self.play_requested = False
        self.return_observed_at = None
        preload_class = u.load_class(None, '/Script/MessControl.MCPreloadSettings')
        require(preload_class is not None, 'Native preparation settings class is unavailable')
        self.native_timeout = float(prop(u.get_default_object(preload_class), 'TimeoutSeconds'))
        require(math.isfinite(self.native_timeout), 'Native preparation timeout is invalid')
        self.readiness_budget = min(120., max(10., self.native_timeout)) + 10.
        self.report.update(scope=__doc__, source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            observe_seconds=None, microphone_test=False, return_to_menu_requested=self.return_to_menu,
            native_preparation_timeout_seconds=self.native_timeout,
            observation_readiness_budget_seconds=self.readiness_budget,
            worlds=[], readiness_samples=[], splash_samples=[],
            limitations=[
                'One standalone authority PIE with no external peers. The normal offline Play fallback opens a local listen Hub.',
                'The actual visible saved 3D PlayButton or native CreateLobby/Play button OnClicked is broadcast; no physical input is injected.',
                'Ready-zone guidance is observed at the real spawn; this short check does not walk into Ready or launch a day.',
                'Sparse actual PNG frames validate integration/timing only; smooth animation quality requires the separate60fps renders.',
                'An already warm Editor may be ready before the intro ends. Cold cache/packaged loading is not established.',
                'No Steam lobby/invite, real speech, network synchronization, cook/package, profile change or level playthrough.'])

    def stage(self, name):
        self.phase, self.phase_wall, self.phase_game = name, time.monotonic(), self.now()
        self.report['stages'].append(dict(name=name, wall_seconds=self.phase_wall-self.started,
            game_seconds=self.phase_game, world=path(self.world)))
        self.write()
        u.log('MC_FRONTEND_HUB_STAGE ' + name)

    def start(self):
        require(self.levels.load_level('/Game/Maps/L_MainMenu'), 'Could not open saved startup map')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(not helpers.dirty(), 'Loading saved startup map unexpectedly dirtied packages')
        require(u.ToolsetLibrary.set_object_properties(self.play, helpers.json.dumps(helpers.PLAY)),
            'Standalone PIE settings rejected')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.console('t.IdleWhenNotForeground 0')
        self.report['pie_request_wall_seconds'] = time.monotonic()-self.started
        self.pie_start = u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
            helpers.json.dumps({'options': {'bSimulate': False,
                'playMode': 'PlayMode_InEditorFloating', 'warmupSeconds': 0.}}))
        require(self.pie_start is not None, 'StartPIE returned no async result')
        self.requested_pie = True
        require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
        self.stage('observe_startup')

    def refresh_world(self):
        worlds = [world for world in u.EditorLevelLibrary.get_pie_worlds(True) if valid(world)]
        if self.instance_path is None:
            if not worlds:
                return False
            require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),
                'Expected one owned standalone authority world; external PIE is not adopted')
            require(u.GameplayStatics.get_current_level_name(worlds[0], True) == 'L_MainMenu',
                'Initial startup did not use saved L_MainMenu')
            instance = u.GameplayStatics.get_game_instance(worlds[0])
            if not valid(instance):
                return False
            self.instance_path = path(instance)
            self.report['owned_game_instance'] = self.instance_path
        owned = [world for world in worlds
            if path(u.GameplayStatics.get_game_instance(world)) == self.instance_path]
        require(len(owned) == len(worlds), 'An unrelated/replacement PIE is preserved, not adopted')
        if len(owned) != 1:
            require(self.phase.startswith('travel_'), 'Owned world disappeared/duplicated outside requested travel')
            self.world = self.pc = self.gs = self.hero = None
            return False
        world = owned[0]
        require(u.SystemLibrary.is_server(world), 'Owned world is no longer local authority')
        current_map = u.GameplayStatics.get_current_level_name(world, True)
        require(current_map in ('L_MainMenu', 'Hub'), 'Unexpected map in the short front-end route: ' + current_map)
        pc = u.GameplayStatics.get_player_controller(world, 0)
        if not valid(pc):
            return False
        if path(world) != self.world_path:
            self.report['worlds'].append(dict(world=path(world), map=current_map,
                game_instance=self.instance_path, wall_seconds=time.monotonic()-self.started))
            self.world_path = path(world)
        self.world, self.pc, self.current_map = world, pc, current_map
        self.gs = u.GameplayStatics.get_game_state(world)
        self.hero = pc.get_controlled_pawn()
        self.menu = helpers.owned_root_widget(u.MCMainMenuWidget, pc)
        self.hub = helpers.owned_root_widget(u.MCHubWidget, pc)
        if current_map == 'Hub' and self.hub_arrived_at is None:
            require(self.play_requested, 'Hub appeared before the saved Play action was requested')
            self.hub_arrived_at = time.monotonic()
            self.report['hub_arrival'] = dict(wall_seconds=self.hub_arrived_at-self.started,
                map=current_map, world=path(world), game_instance=self.instance_path,
                controller=path(pc), native_readiness_required=True)
            self.check('real_play_routes_directly_to_hub', True, '')
            self.check('same_game_instance_after_play', path(u.GameplayStatics.get_game_instance(world))==self.instance_path,
                'Play replaced the application GameInstance')
            self.stage('await_hub_preparation')
        preparations = [item for item in u.ObjectIterator(u.MCWorldPreloadSubsystem.static_class())
            if valid(item) and item.get_outer() == world]
        require(len(preparations) <= 1, 'More than one preparation subsystem belongs to the owned world')
        self.preparation = preparations[0] if preparations else None
        return valid(self.preparation)

    def capture(self, name, sequence=False):
        require(valid(self.world) and valid(self.pc), 'Cannot capture a missing owned PIE window')
        destination = OUT / ('frames' if sequence else 'screenshots') / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        requested = time.monotonic()-self.started
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(destination)),
            'Actual owned PIE window capture failed')
        data = destination.read_bytes()
        require(len(data)>24 and data.startswith(b'\x89PNG\r\n\x1a\n'), 'Capture is not a PNG')
        entry = dict(file=str(destination), phase=self.phase, world=path(self.world), map=self.current_map,
            game_seconds=self.now(), capture_request_wall_seconds=requested,
            capture_completed_wall_seconds=time.monotonic()-self.started,
            width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'),
            bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.report['frames' if sequence else 'screenshots'].append(entry)
        if sequence:
            self.frame_bytes += len(data)

    def control(self, cls, name, root):
        matches = [item for item in u.ObjectIterator(cls.static_class())
            if valid(item) and item.get_name() == name and helpers.has_outer(item, root)]
        require(len(matches) <= 1, 'Ambiguous live authored control: ' + name)
        return matches[0] if matches else None

    def current_splash(self):
        return helpers.owned_root_widget(u.MCStudioSplashWidget, self.pc)

    def readiness(self):
        ready = bool(self.preparation.is_ready())
        loading = helpers.owned_root_widget(u.MCLoadingWidget, self.pc)
        require(not self.preparation.has_failed(), 'Native preparation failed: ' + str(self.preparation.get_detail_text()))
        splash = self.current_splash()
        if not ready:
            require(valid(loading) or valid(splash), 'Non-ready world exposed without loading/intro cover')
        current = time.monotonic()
        if self.last_observation_at is not None:
            self.max_observation_gap = max(self.max_observation_gap, current-self.last_observation_at)
        self.last_observation_at = current
        if current >= self.sample_at:
            self.sample_at = current+.10
            self.report['readiness_samples'].append(dict(wall_seconds=current-self.started,
                map=self.current_map, phase=str(self.preparation.get_phase()), ready=ready,
                loading_widget=path(loading), splash_widget=path(splash),
                mouse_cursor=bool(prop(self.pc, 'show_mouse_cursor'))))
        # Public gates only: no test recording, capture device enumeration or fake speech.
        instance = u.GameplayStatics.get_game_instance(self.world)
        voices = [item for item in u.ObjectIterator(u.MCVoiceChatSubsystem.static_class())
            if valid(item) and item.get_outer() == instance]
        for voice in voices:
            require(not voice.is_mic_test_active(), 'A microphone test unexpectedly became active')
            require(not voice.is_voice_available(), 'Steam voice unexpectedly became available in solo PIE')
        return ready, loading, splash

    def observe_intro(self, splash):
        current = time.monotonic()
        if valid(splash):
            require(self.splash_gone_at is None, 'Studio ident unexpectedly replayed during initial menu')
            if self.splash_seen_at is None:
                self.splash_seen_at = current
                self.splash = splash
                self.check('saved_studio_widget', splash.get_class().get_path_name()
                    == '/Game/Gameplay/UI/WBP_StudioSplash.WBP_StudioSplash_C', 'Native fallback studio widget was used')
                self.check('three_second_authored_duration', abs(float(prop(splash, 'duration'))-3.)<.01,
                    'Saved studio duration is not three seconds')
                self.report['splash_first_seen_wall_seconds'] = current-self.started
            require(path(splash) == path(self.splash), 'Startup intro instance was replaced')
            require(not bool(prop(self.pc, 'show_mouse_cursor')), 'Menu cursor became available during studio intro')
            self.splash_last_seen_at = current
            self.report['splash_samples'].append(dict(wall_seconds=current-self.started,
                widget=path(splash), finished=bool(splash.is_finished())))
        elif self.splash_seen_at is not None and self.splash_gone_at is None:
            self.splash_gone_at = current
            self.check('splash_ended_through_native_timeline', valid(self.splash) and self.splash.is_finished(),
                'Intro disappeared without native completion')
            visible = current-self.splash_seen_at
            self.report['splash_timing'] = dict(first_seen_wall_seconds=self.splash_seen_at-self.started,
                last_seen_wall_seconds=self.splash_last_seen_at-self.started,
                first_absent_wall_seconds=current-self.started, observed_visible_span_seconds=visible,
                max_sample_gap_seconds=self.max_observation_gap,
                limitation='First/last frame observations bound visibility; not an instrumented exact creation timestamp.')
            self.check('approximately_three_real_seconds', 2.35<=visible<=4.1,
                'Three-second intro was not observed close enough to realtime: %.3fs (inspect sample gaps)' % visible)

    def click_saved_play(self):
        # Inspect only live widgets. Loading the unused 3D presentation just to
        # enumerate it would add unrelated asset work to the real startup path.
        class_path = '/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C'
        matches = [item for item in u.ObjectIterator(u.UserWidget.static_class())
            if valid(item) and item.get_owning_player() == self.pc and item.is_visible()
            and item.get_class().get_path_name() == class_path
            and (item.is_in_viewport() or (valid(item.get_parent())
                and valid(self.menu) and helpers.has_outer(item.get_parent(), self.menu)))]
        require(len(matches) <= 1, 'More than one live owned saved 3D menu')
        action = active_page = None
        if matches:
            menu = matches[0]
            button = prop(menu, 'PlayButton')
            method = 'WBP_MainMenu3D.PlayButton.OnClicked.broadcast'
            presentation = 'saved_3d_menu'
        else:
            menu = self.menu
            require(valid(menu) and menu.get_class().get_path_name()
                == '/Game/Gameplay/UI/WBP_MainMenu.WBP_MainMenu_C',
                'Expected the actual saved native main menu, not a newly created fallback fixture')
            active_page = menu.get_active_menu_page_root()
            require(valid(active_page), 'Saved native menu has no current active page')

            def in_current_page(item):
                for _ in range(24):
                    if not valid(item):
                        return False
                    if item == active_page:
                        return True
                    item = item.get_parent()
                return False

            buttons = [item for item in u.ObjectIterator(u.MCMainMenuActionButton.static_class())
                if valid(item) and helpers.has_outer(item, menu) and in_current_page(item)
                and effectively_visible(item) and item.get_is_enabled()
                and helpers.enum_name(prop(item, 'action')) == 'CREATELOBBY']
            require(len(buttons) == 1, 'Expected one current saved native CreateLobby/Play action button')
            button = buttons[0]
            action = str(prop(button, 'action'))
            method = 'MCMainMenuActionButton.OnClicked.broadcast'
            presentation = 'saved_native_menu'
        require(isinstance(button, u.Button) and effectively_visible(button) and button.get_is_enabled(),
            'The actual saved PlayButton is unavailable')
        self.report['requests'].append(dict(method=method, presentation=presentation, action=action,
            widget=path(menu), button=path(button), blueprint_class=menu.get_class().get_path_name(),
            control_class=button.get_class().get_path_name(), active_page=path(active_page),
            wall_seconds=time.monotonic()-self.started, game_instance=self.instance_path,
            preparation_ready=bool(self.preparation.is_ready()), physical_pointer=False))
        self.play_requested = True
        self.stage('travel_hub')
        button.on_clicked.broadcast()

    def verify_hub(self):
        require(isinstance(self.gs, u.MCHubGameState), 'Hub did not use its own GameState')
        require(isinstance(self.pc, u.MCPlayerController), 'Hub did not use the normal game controller')
        if not valid(self.hero) or not valid(self.hub) or not self.gs.are_all_loaded() or not self.hub.is_scene_ready():
            return False
        zones = [item for item in self.actors(u.MCHubReadyZone) if bool(prop(item, 'enabled'))]
        require(len(zones) == 1, 'Expected one enabled authored Ready zone')
        zone = zones[0]
        require(not zone.contains_pawn(self.hero), 'Authored spawn is already inside Ready; no waypoint should be shown')
        hint = self.control(u.TextBlock, 'HubReadinessHint', self.hub)
        waypoint = self.control(u.Border, 'HubReadyWaypoint', self.hub)
        label = self.control(u.TextBlock, 'HubReadyWaypointLabel', self.hub)
        require(valid(hint) and valid(waypoint) and valid(label), 'New Hub guidance controls are absent')
        if not effectively_visible(waypoint):
            return False
        text, waypoint_text = str(hint.get_text()), str(label.get_text())
        self.check('hub_ready_instruction_visible', effectively_visible(hint) and 'READY' in text.upper(),
            'Ready instruction is hidden or does not identify the Ready zone')
        self.check('hub_ready_waypoint_visible', 'READY' in waypoint_text.upper(), 'Waypoint label has no Ready destination')
        require(isinstance(waypoint.slot, u.CanvasPanelSlot), 'Ready waypoint lacks its movable canvas layout')
        position = waypoint.slot.get_position()
        self.check('hub_ready_waypoint_has_native_position', all(math.isfinite(float(v)) and abs(float(v))<10000.
            for v in (position.x, position.y)), 'Waypoint native position is invalid')
        self.check('hub_has_one_real_participant', self.gs.get_participant_count()==1,
            'Solo Hub unexpectedly has extra real participants')
        self.report['hub_guidance'] = dict(hint=text, waypoint=waypoint_text, zone=path(zone),
            zone_location=helpers.vec(zone.get_actor_location()), pawn_location=helpers.vec(self.hero.get_actor_location()),
            native_canvas_position=[float(position.x), float(position.y)], widget=path(self.hub))
        return True

    def advance(self, ready, loading, splash):
        current = time.monotonic()
        if self.phase == 'observe_startup':
            require(self.current_map == 'L_MainMenu', 'Map changed before the real Play action')
            self.observe_intro(splash)
            require(current-self.phase_wall<self.readiness_budget,
                'Initial menu resource preparation did not complete: phase=' + str(self.preparation.get_phase())
                + '; detail=' + str(self.preparation.get_detail_text()))
            if self.splash_gone_at is None or not ready or valid(loading):
                return
            self.check('loading_barrier_not_bypassed', True, '')
            self.check('menu_cursor_restored_after_native_ready', bool(prop(self.pc, 'show_mouse_cursor')),
                'Cursor was not restored after the actual loading+intro barriers')
            self.capture('02_saved_main_menu_ready.png')
            self.click_saved_play()
        elif self.phase == 'travel_hub':
            require(current-self.phase_wall<40., 'Saved Play callback has not arrived in the Hub world within40s')
        elif self.phase == 'await_hub_preparation':
            require(self.current_map=='Hub', 'Arrived Hub was replaced during native resource preparation')
            require(current-self.hub_arrived_at<self.readiness_budget,
                'Hub world arrived, but its native assets/scene readiness gate did not complete within%.1fs: phase=%s; detail=%s'
                % (self.readiness_budget, self.preparation.get_phase(), self.preparation.get_detail_text()))
            if not ready or valid(loading):
                if valid(loading) and current-self.hub_arrived_at>=1. and not self.report.get('hub_loading_screenshot'):
                    self.capture('03a_hub_native_loading_cover.png')
                    self.report['hub_loading_screenshot']=True
                return
            if self.hub_assets_ready_at is None:
                self.hub_assets_ready_at=current
                self.report['hub_native_preparation_ready'] = dict(wall_seconds=current-self.started,
                    seconds_after_world_arrival=current-self.hub_arrived_at,
                    phase=str(self.preparation.get_phase()), loading_widget_removed=True)
                self.check('hub_native_loading_barrier_completed', True, '')
            self.check('studio_not_replayed_in_hub', not valid(splash), 'Studio ident replayed in the Hub')
            if self.verify_hub():
                self.hub_ready_at = current
                self.capture('03_hub_ready_guidance.png')
                self.stage('observe_hub')
        elif self.phase == 'observe_hub' and current-self.phase_wall>=1.5:
            require(self.verify_hub(), 'Hub Ready guidance regressed during the short observation')
            if self.return_to_menu:
                self.report['requests'].append(dict(method='AMCPlayerController.ReturnToMainMenu',
                    controller=path(self.pc), wall_seconds=current-self.started, physical_input=False))
                self.stage('travel_return_menu')
                self.pc.return_to_main_menu()
            else:
                self.report['not_exercised'] = ['Returning to the menu and once-per-GI no-replay observation were disabled explicitly.']
                self.stage('capture_flush')
        elif self.phase == 'travel_return_menu':
            require(current-self.phase_wall<self.readiness_budget,
                'Normal return-to-menu arrival/resource preparation did not settle: map=' + str(self.current_map)
                + '; phase=' + str(self.preparation.get_phase()))
            if self.current_map != 'L_MainMenu':
                return
            require(not valid(splash), 'Studio ident replayed after returning to menu in the same GameInstance')
            if self.return_observed_at is None:
                self.return_observed_at = current
            if ready and not valid(loading) and valid(self.menu):
                self.return_ready_at = current
                self.stage('observe_return_menu')
        elif self.phase == 'observe_return_menu':
            require(self.current_map=='L_MainMenu' and not valid(splash), 'Same-session return replayed the studio ident')
            if current-self.return_observed_at>=4.:
                self.check('studio_once_per_game_instance_runtime', True, '')
                self.check('menu_returns_normally_same_game_instance', ready and not valid(loading), 'Menu return bypassed readiness')
                self.capture('04_return_menu_without_studio_replay.png')
                self.stage('capture_flush')
        elif self.phase == 'capture_flush' and current-self.phase_wall>=.25:
            self.begin_finish()

    def tick(self, _delta):
        if self.done or self.busy:
            return
        self.busy = True
        try:
            if self.phase == 'ending_pie':
                if not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True):
                    self.report['cleanup']['owned_pie_stopped'] = True
                    self.finish()
                elif time.monotonic()-self.phase_wall>12.:
                    self.error = self.error or 'Owned PIE did not stop within twelve seconds'
                    self.finish()
                return
            require(time.monotonic()-self.started<185.,
                'Front-end observation exceeded its185s wall limit: map=' + str(self.current_map)
                + '; probe_phase=' + self.phase)
            if self.phase == 'warming_editor':
                if time.monotonic()-self.started>=2.:
                    self.start()
                return
            require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
            if not self.refresh_world():
                budget=self.readiness_budget if self.hub_arrived_at is not None else 35.
                require(time.monotonic()-self.phase_wall<budget,
                    'Owned world/controller/preparation object did not settle: map=' + str(self.current_map)
                    + '; probe_phase=' + self.phase)
                return
            ready, loading, splash = self.readiness()
            now = time.monotonic()
            if self.capture_hz and now>=self.frame_at and (valid(splash) or self.phase=='observe_hub'):
                require(len(self.report['frames'])<80 and self.frame_bytes<240*1024*1024, 'Sparse capture budget exceeded')
                self.frame_at=now+1./self.capture_hz
                self.capture('%05d.png' % len(self.report['frames']), sequence=True)
                # Slate's screenshot draw can advance a widget's real-time
                # timeline. Refresh after capture instead of comparing an old
                # splash pointer with the already restored menu cursor.
                ready, loading, splash = self.readiness()
            self.advance(ready, loading, splash)
        except Exception:
            self.begin_finish(traceback.format_exc())
        finally:
            self.busy=False

    def finish(self):
        super().finish()
        u.log('MC_FRONTEND_HUB_' + ('FAIL ' if self.error else 'PASS ') + str(OUT/'Report.json'))


def main():
    for name in (KEY, helpers.KEY, 'mc_resource_loading_solo', 'mc_hub_solo'):
        old=getattr(builtins,name,None)
        require(old is None or getattr(old,'done',True), 'Another owned probe is active: '+name)
    probe=FrontendHubSolo()
    setattr(builtins,KEY,probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback=u.register_slate_post_tick_callback(probe.tick)


if __name__=='__main__':
    main()
