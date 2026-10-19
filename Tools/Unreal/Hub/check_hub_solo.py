"""Owned, removable normal Hub -> selector -> day-one solo PIE smoke.

Requires -MCHubOwned and an idle, saved MessControl editor. No test actors,
readiness overrides, teleports, director changes, network peers or package saves.
Optional globals: MCHUB_OUTPUT (an empty E: directory), MCHUB_CAPTURE_HZ (0..8),
MCHUB_MAX_FRAMES (default 900). Stop only this probe via builtins.mc_hub_solo.cancel().
The requested controller/RPC methods are real gameplay paths; physical keyboard
delivery and multiplayer synchronization are outside this solo check.
"""
import builtins
import csv
import hashlib
import json
import math
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MCHUB_OUTPUT', 'E:/CodexWork/MessControlHub_20261019/Solo')).resolve()
KEY = 'mc_hub_solo'
PLAY = dict(playNetMode='PIE_Standalone', playNumberOfClients=1,
            runUnderOneProcess=True, bLaunchSeparateServer=False,
            newWindowWidth=1280, newWindowHeight=720,
            clientFixedFPS=[60], serverFixedFPS=0)


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def prop(obj, name):
    native = ''.join(part[:1].upper() + part[1:] for part in name.split('_'))
    errors = []
    for key in (name, native, 'b' + native):
        try:
            return obj.get_editor_property(key)
        except Exception as caught:
            errors.append(key + ': ' + str(caught))
    raise RuntimeError('No readable property ' + repr(name) + ' on ' + str(obj) + '; attempts: ' + ' | '.join(errors))


def valid(obj):
    try:
        return obj is not None and u.SystemLibrary.is_valid(obj)
    except (TypeError, RuntimeError):
        return False


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def dirty():
    return sorted(p.get_path_name() for p in
                  list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                  list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def owned_root_widget(cls, controller):
    """Find the live viewport root without reading private PC references."""
    matches = [widget for widget in u.ObjectIterator(cls.static_class())
               if valid(widget) and widget.get_owning_player() == controller
               and widget.is_in_viewport() and widget.is_visible()]
    require(len(matches) <= 1, 'More than one live owned viewport widget for ' + str(cls))
    return matches[0] if matches else None


def has_widget_outer(widget, root):
    """Attached nested widgets belong to a WidgetTree whose outer is the root.

    A removed former-travel widget can retain the same owning controller until
    GC. Parent/outer membership distinguishes it from the current presentation.
    """
    current = widget
    for _ in range(16):
        if not valid(current):
            return False
        if current == root:
            return True
        current = current.get_outer()
    return False


def owned_selection_presentation(controller, root):
    if not valid(root):
        return None
    cls = u.load_class(None, '/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
    require(cls is not None, 'Saved selection presentation class is unavailable')
    matches = []
    for widget in u.ObjectIterator(cls):
        if not valid(widget) or widget.get_owning_player() != controller or not widget.is_visible():
            continue
        parent = widget.get_parent()
        if valid(parent) and has_widget_outer(parent, root):
            matches.append(widget)
    require(len(matches) <= 1, 'More than one attached mouth presentation belongs to the current Hub root')
    return matches[0] if matches else None


class HubSolo:
    def __init__(self):
        require('-MCHubOwned' in u.SystemLibrary.get_command_line(), 'Explicit -MCHubOwned launch required')
        require(Path(u.Paths.project_dir()).resolve() == ROOT.resolve(), 'Wrong project checkout')
        require(OUT.drive.upper() == 'E:', 'All output must remain on E:')
        require(not (OUT / 'Report.json').exists(), 'Preserving an earlier report; choose a fresh MCHUB_OUTPUT')
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        require(not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True),
                'Existing PIE is active and will not be adopted')
        require(not dirty(), 'Preserve unsaved editor packages before starting this check')
        require(not u.InputLibrary.modifier_keys_state_is_control_down(u.InputLibrary.get_modifier_keys_state()),
                'Ctrl is held; Unreal would launch a spectator instead of the real pawn')
        editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(valid(editor_world), 'No editor world to restore')
        self.original_map = editor_world.get_path_name().split('.')[0]
        self.editor_world = editor_world
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(PLAY)))
        self.engine = next(obj for obj in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if obj.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.old_cvars = {name: u.SystemLibrary.get_console_variable_int_value(name)
                          for name in ('t.IdleWhenNotForeground', 'net.AllowPIESeamlessTravel')}
        self.started = time.monotonic()
        self.phase = 'warming_editor'
        self.phase_wall = self.started
        self.phase_game = 0.
        self.world = self.pc = self.hero = self.gs = self.widget = self.zone = None
        self.instance_path = None
        self.world_path = None
        self.initial_pawn_path = None
        self.original_spawn = None
        self.walk_target = None
        self.walk_start = None
        self.walk_distance = 0.
        self.sample_at = self.frame_at = 0.
        self.frame_bytes = 0
        self.pie_start = None
        self.requested_pie = False
        self.last_context_map = None
        self.last_render_world = None
        self.gameplay_pawn = None
        self.gameplay_started = None
        self.callback = None
        self.done = False
        self.busy = False
        self.restore_errors = []
        self.overrides = []
        self.error = None
        self.end_requested = False
        self.capture_hz = max(0., min(8., float(globals().get('MCHUB_CAPTURE_HZ', 6))))
        self.max_frames = max(0, min(1200, int(globals().get('MCHUB_MAX_FRAMES', 900))))
        self.initial_culture = (u.InternationalizationLibrary.get_current_language(),
                                u.InternationalizationLibrary.get_current_locale())
        self.report = dict(passed=False, status='running', project=str(ROOT), phase=self.phase,
            scope='One owned standalone PIE using actual ready-zone walking, Hub RPCs, normal seamless travel and first-perk selection',
            network=False, package_saves=False, gameplay_overrides=False, checks={}, samples=[],
            stages=[], worlds=[], requests=[], screenshots=[], frames=[], cleanup={},
            original_editor_map=self.original_map, source_sha256=hashlib.sha256(
                (ROOT / 'Tools/Unreal/Hub/check_hub_solo.py').read_bytes()).hexdigest(),
            limitations=['Movement is supplied through Pawn.AddMovementInput; physical WASD delivery is not simulated.',
                'Selection and launch use PlayerController request/RPC paths; no mouse-click or network-peer proof.',
                'Unavailable launch rejection is observed through unchanged world/phase and server notice; UWorld.NextURL is not exposed to Python.',
                'Screenshots require subsequent visual review; successful PNG output is not visual approval.',
                'No cook/package, multiplayer, bots, actor fixtures, readiness forcing or director tuning.'])
        OUT.mkdir(parents=True, exist_ok=True)

    def write(self):
        self.report.update(phase=self.phase, wall_seconds=time.monotonic() - self.started)
        (OUT / 'Report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2,
                                                    allow_nan=False), encoding='utf-8')
        if self.report['frames']:
            with (OUT / 'FrameTimes.csv').open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=['file', 'phase', 'world', 'game_seconds',
                    'capture_request_wall_seconds', 'capture_completed_wall_seconds', 'bytes'])
                writer.writeheader()
                writer.writerows({key: entry[key] for key in writer.fieldnames} for entry in self.report['frames'])

    def console(self, command):
        context = self.world if valid(self.world) else self.editor_world
        u.SystemLibrary.execute_console_command(context, command, self.pc if valid(self.pc) else None)

    def stage(self, name):
        self.phase = name
        self.phase_wall = time.monotonic()
        self.phase_game = self.now()
        self.report['stages'].append(dict(name=name, wall_seconds=self.phase_wall - self.started,
                                          world=path(self.world), game_seconds=self.phase_game))
        self.write()
        u.log('MC_HUB_SOLO_STAGE ' + name)

    def now(self):
        return float(u.GameplayStatics.get_time_seconds(self.world)) if valid(self.world) else 0.

    def check(self, key, condition, message):
        require(condition, message)
        self.report['checks'][key] = True

    def start(self):
        require(self.levels.load_level('/Game/Maps/Hub'), 'Could not open saved Hub before PIE')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(not dirty(), 'Loading saved Hub unexpectedly dirtied packages')
        for key in self.levels.get_viewport_config_keys():
            self.levels.editor_set_viewport_realtime(False, key)
            self.overrides.append(key)
        require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(PLAY)), 'Standalone PIE settings rejected')
        self.engine.set_editor_property('FixedFrameRate', 60.)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.console('t.IdleWhenNotForeground 0')
        self.console('net.AllowPIESeamlessTravel 1')
        self.pie_start = u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps({'options': {'bSimulate': False, 'playMode': 'PlayMode_InEditorFloating', 'warmupSeconds': 0.}}))
        require(self.pie_start is not None, 'StartPIE did not return its async result')
        self.requested_pie = True
        require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
        self.stage('await_hub')

    def owned_worlds(self):
        worlds = [world for world in u.EditorLevelLibrary.get_pie_worlds(True) if valid(world)]
        if not self.instance_path:
            return []
        return [world for world in worlds if path(u.GameplayStatics.get_game_instance(world)) == self.instance_path]

    def refresh_world(self):
        worlds = [world for world in u.EditorLevelLibrary.get_pie_worlds(True) if valid(world)]
        if not self.instance_path:
            if not worlds:
                return False
            require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one standalone authority world')
            require(u.GameplayStatics.get_current_level_name(worlds[0], True) == 'Hub', 'Initial PIE did not start on Hub')
            instance = u.GameplayStatics.get_game_instance(worlds[0])
            require(valid(instance), 'Initial PIE has no GameInstance identity')
            self.instance_path = path(instance)
            self.report['owned_game_instance'] = self.instance_path
        owned = [world for world in worlds if path(u.GameplayStatics.get_game_instance(world)) == self.instance_path]
        require(len(owned) == len(worlds), 'Another PIE session replaced or joined the owned check')
        if not owned:
            require(self.phase.startswith('travel_'), 'Owned PIE ended outside a requested travel')
            self.world = self.pc = self.hero = self.gs = self.widget = None
            return False
        # Seamless travel can briefly expose an old and a destination world.
        # Do not act until the Engine's PIE context settles to one world again.
        if len(owned) != 1:
            require(self.phase.startswith('travel_'), 'Multiple owned worlds outside expected travel')
            return False
        world = owned[0]
        require(u.SystemLibrary.is_server(world), 'Owned PIE is no longer standalone authority')
        name = u.GameplayStatics.get_current_level_name(world, True)
        current_path = path(world)
        pc = u.GameplayStatics.get_player_controller(world, 0)
        if not valid(pc):
            return False
        if current_path != self.world_path:
            self.world_path = current_path
            self.report['worlds'].append(dict(world=current_path, map=name,
                game_instance=self.instance_path, at_wall_seconds=time.monotonic() - self.started))
            self.last_render_world = None
        self.world, self.pc = world, pc
        self.hero = pc.get_controlled_pawn()
        self.gs = u.GameplayStatics.get_game_state(world)
        self.widget = owned_root_widget(u.MCHubWidget, pc)
        self.last_context_map = name
        return True

    def capture(self, name, sequence=False):
        require(valid(self.world), 'Cannot capture a missing owned world')
        destination = OUT / ('frames' if sequence else 'screenshots') / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        request = time.monotonic() - self.started
        world_path = path(self.world)
        game_time = self.now()
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(destination)),
                'Specific owned PIE game-window capture failed: ' + str(destination))
        data = destination.read_bytes()
        require(len(data) > 24 and data.startswith(b'\x89PNG\r\n\x1a\n'), 'Capture is not a PNG')
        entry = dict(file=str(destination), phase=self.phase, world=world_path, game_seconds=game_time,
            capture_request_wall_seconds=request, capture_completed_wall_seconds=time.monotonic() - self.started,
            width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'),
            bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.report['frames' if sequence else 'screenshots'].append(entry)
        if sequence:
            self.frame_bytes += len(data)
        return entry

    def actors(self, cls):
        return list(u.GameplayStatics.get_all_actors_of_class(self.world, cls))

    def sample(self):
        row = dict(phase=self.phase, wall_seconds=time.monotonic() - self.started,
                   game_seconds=self.now(), world=path(self.world), pawn=path(self.hero))
        if isinstance(self.hero, u.MCToothCharacter) and valid(self.hero):
            row['position_cm'] = vec(self.hero.get_actor_location())
            row['health'] = float(prop(prop(prop(self.hero, 'status'), 'state'), 'health'))
            eye = prop(self.hero, 'camera').get_world_location()
            row['camera_position_cm'] = vec(eye)
            row['camera_distance_to_pawn_cm'] = math.dist(vec(eye), vec(self.hero.get_actor_location()))
        if isinstance(self.gs, u.MCHubGameState):
            row.update(hub_phase=str(prop(self.gs, 'phase')), day=int(prop(self.gs, 'selected_day')),
                ready=int(self.gs.get_ready_count()), participants=int(self.gs.get_participant_count()),
                all_loaded=bool(self.gs.are_all_loaded()), scene_ready=valid(self.widget) and bool(self.widget.is_scene_ready()),
                notice=str(prop(self.gs, 'notice')))
            if valid(self.widget):
                presentation = owned_selection_presentation(self.pc, self.widget)
                if valid(presentation):
                    row['presentation'] = dict(selected=int(prop(presentation, 'SelectedDay')),
                        active=int(prop(presentation, 'ActiveDay')),
                        openness=[float(prop(presentation, 'Open' + str(day))) for day in range(1, 8)])
        self.report['samples'].append(row)

    def prepare_walk(self, target, stage):
        require(isinstance(self.hero, u.MCToothCharacter), 'No normal Hub character for walking')
        self.walk_target = u.Vector(target.x, target.y, self.hero.get_actor_location().z)
        self.walk_start = self.hero.get_actor_location()
        self.walk_distance = math.hypot(self.walk_target.x - self.walk_start.x, self.walk_target.y - self.walk_start.y)
        self.report.setdefault('walks', []).append(dict(phase=stage, from_cm=vec(self.walk_start),
            target_cm=vec(self.walk_target), initial_distance_cm=self.walk_distance, method='Pawn.AddMovementInput each normal world tick'))
        if self.walk_distance > 1.:
            rotation = u.MathLibrary.find_look_at_rotation(self.walk_start, self.walk_target)
            rotation.pitch = -12.
            self.pc.set_control_rotation(rotation)
        self.stage(stage)

    def walk(self):
        require(time.monotonic() - self.phase_wall < 45., 'Actual walking timed out; route/collision needs review')
        require(valid(self.hero) and path(self.hero) == self.initial_pawn_path, 'Hub pawn changed during normal walking')
        health = float(prop(prop(prop(self.hero, 'status'), 'state'), 'health'))
        require(health > 0., 'Hub pawn died during normal walking')
        current = self.hero.get_actor_location()
        dx, dy = self.walk_target.x - current.x, self.walk_target.y - current.y
        distance = math.hypot(dx, dy)
        if distance <= 65.:
            return True
        self.hero.add_movement_input(u.Vector(dx / distance, dy / distance, 0.), 1., False)
        return False

    def request(self, method, *args):
        self.report['requests'].append(dict(method=method, args=list(args), world=path(self.world),
            wall_seconds=time.monotonic() - self.started, game_seconds=self.now()))
        getattr(self.pc, method)(*args)

    def selector_ready(self, day=None):
        return (self.last_context_map == 'L_HubSelection' and isinstance(self.gs, u.MCHubGameState)
            and prop(self.gs, 'phase') == u.MCHubPhase.SELECTING and valid(self.widget)
            and self.widget.is_scene_ready() and self.gs.are_all_loaded()
            and (day is None or int(prop(self.gs, 'selected_day')) == day))

    def selected_visual(self, day):
        if not self.selector_ready(day):
            return False
        presentation = owned_selection_presentation(self.pc, self.widget)
        return (valid(presentation) and int(prop(presentation, 'SelectedDay')) == day
                and int(prop(presentation, 'ActiveDay')) == day and float(prop(presentation, 'Open' + str(day))) >= .97)

    def click_mouth_row(self, row):
        """Exercise the real button callback, without claiming physical pointer input."""
        targets = []
        for index in range(1, 4):
            name = 'HubMouthHit' + str(index)
            matches = [button for button in u.ObjectIterator(u.MCHubActionButton.static_class())
                       if valid(button) and button.get_name() == name and has_widget_outer(button, self.widget)]
            require(len(matches) == 1, 'Missing unique current mouth click target: ' + name)
            button = matches[0]
            require(button.is_visible() and button.get_is_enabled(), 'Mouth click target is not usable: ' + name)
            # Python's reflected FGeometry copy is zero in this UE binding.
            # Inspect the real CanvasSlot rectangles written by the native
            # geometry/clip calculation, rather than asserting on that copy.
            layout = button.slot
            require(isinstance(layout, u.CanvasPanelSlot) and not layout.get_auto_size(),
                    'Mouth target needs a fixed CanvasSlot: ' + name)
            size, top = layout.get_size(), layout.get_position()
            require(size.x > 200 and size.y > 25, 'Mouth click target has no meaningful arranged rectangle: ' + name)
            rect = [float(top.x), float(top.y), float(top.x + size.x), float(top.y + size.y)]
            require(rect[0] >= -1 and rect[1] >= -1 and rect[2] <= 1601 and rect[3] <= 901,
                    'Mouth click target leaves the authored 1600 x 900 frame: ' + name)
            targets.append((button, rect))
        for previous, current in zip(targets, targets[1:]):
            require(previous[1][3] <= current[1][1] + 1., 'Visible mouth hit regions overlap vertically')
        presentation = owned_selection_presentation(self.pc, self.widget)
        first = int(prop(presentation, 'WindowFirstDay'))
        self.report.setdefault('mouth_clicks', []).append(dict(row=row, expected_day=first + row - 1,
            targets=[dict(button=path(button), canvas_rect=rect) for button, rect in targets],
            geometry_source='Native-calculated CanvasSlot position/size; Python FGeometry reflection is not used',
            method='Actual native UMCHubActionButton.Clicked handler; no physical mouse injection',
            wall_seconds=time.monotonic() - self.started))
        self.check('mouth_click_regions_have_ordered_visible_rectangles', True, '')
        targets[row - 1][0].call_method('Clicked')

    def advance(self):
        age = time.monotonic() - self.phase_wall
        if self.phase == 'await_hub':
            require(age < 30., 'Initial Hub did not become usable')
            if not isinstance(self.gs, u.MCHubGameState) or not isinstance(self.hero, u.MCToothCharacter) or not valid(self.widget):
                return
            if not self.gs.are_all_loaded() or not self.widget.is_scene_ready() or self.now() < 1.3:
                return
            zones = self.actors(u.MCHubReadyZone)
            self.check('one_authored_ready_zone', len(zones) == 1, 'Expected exactly one authored ready zone')
            self.zone = zones[0]
            self.check('hub_uses_separate_game_state', not isinstance(self.gs, u.MCGameState), 'Level GameState leaked into Hub')
            self.check('one_real_participant', self.gs.get_participant_count() == 1, 'Solo Hub participant count is not one')
            self.check('no_authored_preview_pawns_at_hub_start', self.actors(u.MCToothCharacter) == [self.hero],
                       'Authored preview pawns were left as runtime Hub duplicates')
            self.check('spawn_outside_ready_zone', not self.zone.contains_pawn(self.hero) and self.gs.get_ready_count() == 0,
                       'Authored spawn is already inside readiness or readiness did not reset')
            self.initial_pawn_path = path(self.hero)
            self.original_spawn = self.hero.get_actor_location()
            self.capture('01_hub_before_ready.png')
            self.prepare_walk(self.zone.get_actor_location(), 'walk_ready_1')
        elif self.phase in ('walk_ready_1', 'walk_ready_2', 'walk_ready_3'):
            if self.walk() and self.gs.are_all_ready() and self.zone.contains_pawn(self.hero):
                self.stage({'walk_ready_1':'ready_1', 'walk_ready_2':'ready_2', 'walk_ready_3':'ready_3'}[self.phase])
        elif self.phase == 'ready_1' and age >= .8:
            self.check('walking_enters_ready_zone', self.gs.get_ready_count() == 1 and self.gs.are_all_ready(), 'Readiness did not follow actual entry')
            eye = prop(self.hero, 'camera').get_world_location()
            self.check('hub_camera_follows_at_distant_corner', math.dist(vec(eye), vec(self.hero.get_actor_location())) < 1800.,
                       'Hub camera remained clamped to the mouth arena instead of following the player')
            self.capture('02_hub_ready.png')
            center = self.zone.get_actor_location()
            dx, dy = self.original_spawn.x - center.x, self.original_spawn.y - center.y
            distance = math.hypot(dx, dy)
            require(distance > 1., 'Initial spawn cannot define a ready-zone exit direction')
            extent = self.zone.get_component_by_class(u.BoxComponent).get_scaled_box_extent()
            outside_distance = math.hypot(extent.x, extent.y) + 160.
            outside = center + u.Vector(dx / distance * outside_distance, dy / distance * outside_distance, 0.)
            self.prepare_walk(outside, 'walk_out_1')
        elif self.phase == 'walk_out_1':
            if self.walk() and not self.zone.contains_pawn(self.hero) and self.gs.get_ready_count() == 0:
                self.stage('outside_1')
        elif self.phase == 'outside_1' and age >= .6:
            self.check('walking_exit_clears_ready', not self.gs.are_all_ready(), 'Readiness stayed set after exit')
            self.capture('03_hub_exit_clears_ready.png')
            self.prepare_walk(self.zone.get_actor_location(), 'walk_ready_2')
        elif self.phase in ('ready_2', 'ready_3') and age >= .8:
            next_phase = 'travel_selector_1' if self.phase == 'ready_2' else 'travel_selector_2'
            self.check('walking_reentry_restores_ready', self.gs.are_all_ready(), 'Readiness failed on re-entry')
            self.request('request_hub_open_selection')
            self.stage(next_phase)
        elif self.phase in ('travel_selector_1', 'travel_selector_2'):
            require(age < 35., 'Shared selection scene did not load and ACK')
            if self.selector_ready():
                if self.phase == 'travel_selector_1':
                    self.check('normal_hub_to_selector_travel', True, '')
                    self.stage('selector_initial_settle')
                else:
                    self.request('request_hub_select_day', 1)
                    self.stage('select_day1_final')
        elif self.phase == 'selector_initial_settle':
            require(age < 12., 'Initial mouth selection and hit areas did not settle')
            if self.selected_visual(1) and age >= 1.:
                self.capture('04_selector_day1_initial.png')
                self.click_mouth_row(2)
                self.stage('select_day2')
        elif self.phase == 'select_day2':
            require(age < 12., 'Day 2 did not reach the actual cosmetic presentation')
            if self.selected_visual(2) and age >= 1.:
                destinations = [row for row in self.gs.get_destinations() if int(prop(row, 'day')) == 2]
                self.check('day2_unavailable_definition', len(destinations) == 1 and not bool(prop(destinations[0], 'enabled')),
                           'Day 2 is no longer an unavailable fixture; review this authored contract')
                self.check('day2_authoritative_visual_mirror', True, '')
                self.capture('05_selector_day2.png')
                self.request('request_hub_launch')
                self.stage('reject_day2_launch')
        elif self.phase == 'reject_day2_launch':
            require(self.last_context_map == 'L_HubSelection' and isinstance(self.gs, u.MCHubGameState), 'Unavailable day changed the world')
            require(prop(self.gs, 'phase') == u.MCHubPhase.SELECTING, 'Unavailable day initiated travel')
            if age >= 1.:
                self.check('unavailable_launch_rejected_without_scene_change', bool(str(prop(self.gs, 'notice'))), 'Server rejection produced no notice')
                self.click_mouth_row(3)
                self.stage('select_day3')
        elif self.phase == 'select_day3':
            require(age < 12., 'Day 3 did not reach the actual cosmetic presentation')
            if self.selected_visual(3) and age >= 1.:
                self.check('day3_authoritative_visual_mirror', True, '')
                self.capture('06_selector_day3.png')
                self.request('request_hub_return')
                self.stage('travel_hub_return')
        elif self.phase == 'travel_hub_return':
            require(age < 35., 'Returning to Hub did not complete')
            if self.last_context_map == 'Hub' and isinstance(self.gs, u.MCHubGameState) and isinstance(self.hero, u.MCToothCharacter):
                if self.gs.are_all_loaded() and valid(self.widget) and self.widget.is_scene_ready():
                    zones = self.actors(u.MCHubReadyZone)
                    require(len(zones) == 1, 'Returned Hub lacks its single ready zone')
                    self.zone = zones[0]
                    self.check('return_to_hub_resets_ready', self.gs.get_ready_count() == 0 and not self.zone.contains_pawn(self.hero),
                               'Return did not require actual renewed readiness')
                    self.check('no_authored_preview_pawns_after_return', self.actors(u.MCToothCharacter) == [self.hero],
                               'Returning to Hub restored preview pawns as runtime duplicates')
                    self.initial_pawn_path = path(self.hero)
                    self.capture('07_hub_return_ready_reset.png')
                    self.prepare_walk(self.zone.get_actor_location(), 'walk_ready_3')
        elif self.phase == 'select_day1_final':
            require(age < 12., 'Day 1 final selection did not settle')
            if self.selected_visual(1) and age >= 1.:
                self.capture('08_selector_day1_launch.png')
                self.request('request_hub_launch')
                self.stage('travel_gameplay')
        elif self.phase == 'travel_gameplay':
            require(age < 45., 'Normal level did not travel/load/leave its ACK barrier')
            if self.last_context_map == 'L_Mouth' and isinstance(self.gs, u.MCGameState) and isinstance(self.hero, u.MCToothCharacter):
                if not bool(prop(self.gs, 'lobby_waiting')) and self.pc.is_reward_menu_open():
                    choice = owned_root_widget(u.MCPerkChoiceWidget, self.pc)
                    cards = [item for item in u.ObjectIterator(u.MCPerkCardWidget.static_class())
                             if valid(choice) and valid(item) and path(item).startswith(path(choice) + '.') and item.is_visible()]
                    self.check('normal_level_auto_exits_hub_ack_barrier', True, '')
                    self.check('normal_first_perk_has_three_visible_cards', len(cards) == 3, 'Expected three normal first-perk cards')
                    self.report['first_perk_cards'] = [path(card) for card in cards]
                    self.capture('09_normal_first_perk.png')
                    self.request('choose_reward_perk', 0)
                    self.stage('resolve_first_perk')
        elif self.phase == 'resolve_first_perk':
            require(age < 12., 'Normal first-perk request did not resolve')
            if not self.pc.is_reward_menu_open():
                self.gameplay_pawn = path(self.hero)
                self.gameplay_started = self.now()
                self.stage('normal_gameplay')
        elif self.phase == 'normal_gameplay':
            require(self.last_context_map == 'L_Mouth' and path(self.hero) == self.gameplay_pawn, 'Normal gameplay pawn/world changed')
            require(not self.pc.is_reward_menu_open(), 'Another reward menu interrupted four-second observation')
            require(float(prop(prop(prop(self.hero, 'status'), 'state'), 'health')) > 0., 'Normal gameplay pawn is not alive')
            if self.now() - self.gameplay_started >= 4.:
                self.check('normal_gameplay_alive_four_seconds', True, '')
                self.capture('10_normal_gameplay.png')
                self.stage('capture_flush')
        elif self.phase == 'capture_flush' and age >= .3:
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
                elif time.monotonic() - self.phase_wall > 12.:
                    self.error = self.error or 'Owned PIE did not stop within twelve seconds'
                    self.finish()
                return
            require(time.monotonic() - self.started < 240., 'Hub solo exceeded its 240-second wall-time bound')
            if self.phase == 'warming_editor':
                if time.monotonic() - self.started >= 6.:
                    self.start()
                return
            require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
            if not self.refresh_world():
                require(time.monotonic() - self.phase_wall < 45., 'Owned PIE world did not settle during travel')
                return
            current = time.monotonic()
            if current >= self.sample_at:
                self.sample_at = current + .3
                self.sample()
            capture_world_ready = ((self.last_context_map in ('Hub', 'L_HubSelection') and isinstance(self.gs, u.MCHubGameState))
                or (self.last_context_map == 'L_Mouth' and isinstance(self.gs, u.MCGameState)))
            if capture_world_ready and self.capture_hz and current >= self.frame_at and len(self.report['frames']) < self.max_frames and self.frame_bytes < 900 * 1024 * 1024:
                self.frame_at = current + 1. / self.capture_hz
                self.capture('%05d.png' % len(self.report['frames']), sequence=True)
            elif len(self.report['frames']) >= self.max_frames or self.frame_bytes >= 900 * 1024 * 1024:
                self.report['capture_limit_reached'] = True
            self.advance()
        except Exception:
            self.begin_finish(traceback.format_exc())
        finally:
            self.busy = False

    def cancel(self):
        self.begin_finish('Cancelled explicitly')

    def begin_finish(self, error=None):
        if self.done or self.phase == 'ending_pie':
            return
        self.error = self.error or error
        owned = self.owned_worlds()
        worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
        if worlds and not owned:
            self.report['cleanup']['replacement_pie_preserved'] = True
            self.error = self.error or 'Owned PIE was replaced; replacement session was preserved'
            self.finish()
            return
        if owned and self.requested_pie and self.levels.is_in_play_in_editor():
            self.levels.editor_request_end_play()
            self.end_requested = True
            self.report['cleanup']['owned_pie_end_requested'] = True
            self.stage('ending_pie')
        else:
            self.finish()

    def finish(self):
        if self.done:
            return
        self.done = True
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        actions = [lambda key=key: self.levels.editor_set_viewport_realtime(True, key)
                   for key in reversed(self.overrides)] + [
            lambda: require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(self.old_play)), 'PIE settings restore rejected'),
            lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]),
            lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]),
            lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_perf)]
        for action in actions:
            try:
                action()
            except Exception:
                self.restore_errors.append(traceback.format_exc())
        if not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                require(not dirty(), 'Unexpected dirty packages; preserving them instead of changing editor map')
                require(self.levels.load_level(self.original_map), 'Original editor map restore failed')
                self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
                self.report['cleanup']['original_map_restored'] = True
            except Exception:
                self.restore_errors.append(traceback.format_exc())
        for name, value in self.old_cvars.items():
            try:
                u.SystemLibrary.execute_console_command(self.editor_world, name + ' ' + str(value))
            except Exception:
                self.restore_errors.append(traceback.format_exc())
        current_culture = (u.InternationalizationLibrary.get_current_language(), u.InternationalizationLibrary.get_current_locale())
        if current_culture != self.initial_culture:
            self.error = self.error or 'Global culture changed unexpectedly during the probe'
        if self.restore_errors:
            self.error = self.error or 'Temporary settings or editor-state restoration failed'
        self.report.update(passed=not bool(self.error), status='failed' if self.error else 'passed', error=self.error,
            restored_settings=not self.restore_errors, restore_errors=self.restore_errors,
            capture_total_bytes=self.frame_bytes, dirty_after=dirty())
        self.write()
        u.log('MC_HUB_SOLO_' + ('FAIL ' if self.error else 'PASS ') + str(OUT / 'Report.json'))
        # The command-line Python executor observes this on its normal next tick.
        # Never call QuitEditor directly from inside a Slate/render callback.
        u.EditorPythonScripting.set_keep_python_script_alive(False)


def main():
    old = getattr(builtins, KEY, None)
    require(old is None or getattr(old, 'done', True), 'A Hub solo probe is already active')
    probe = HubSolo()
    setattr(builtins, KEY, probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback = u.register_slate_post_tick_callback(probe.tick)


if __name__ == '__main__':
    main()
