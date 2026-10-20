"""Owned normal-start solo observation of the first 3-food Relax portion.

Requires an idle, saved MessControl Editor launched with -MCFirstRelaxOwned
(the existing owned-editor wrapper's -MCHubOwned flag is also accepted).
Starts saved L_Mouth, resolves the ordinary first-perk offer through the normal
PlayerController callback, then observes the naturally served first portion.
No profile/stage/counter edits, food fixtures, teleports, damage, cleaning, F3
commands, network peers, package saves or direct gameplay-helper calls.

Optional globals: MC_FIRST_RELAX_OUTPUT (fresh E: directory),
MC_FIRST_RELAX_CAPTURE_HZ (0..8, default 6), MC_FIRST_RELAX_OBSERVE_SECONDS
(10..20 game seconds, default 10). Captures are real owned PIE-window PNGs;
FrameTimes.csv records wall time for subsequent video assembly. No video is
encoded here. Cancel only this probe: builtins.mc_first_relax_solo.cancel().
"""
import builtins
import csv
import hashlib
import json
import re
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUT = Path(globals().get('MC_FIRST_RELAX_OUTPUT',
    'E:/CodexWork/MessControlFirstRelax_20261020/Solo01')).resolve()
KEY = 'mc_first_relax_solo'
PLAY = dict(playNetMode='PIE_Standalone', playNumberOfClients=1,
    runUnderOneProcess=True, bLaunchSeparateServer=False,
    newWindowWidth=1280, newWindowHeight=720,
    clientFixedFPS=[0], serverFixedFPS=0)


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
    raise RuntimeError('No readable property ' + repr(name) + ' on ' + str(obj)
        + '; attempts: ' + ' | '.join(errors))


def valid(obj):
    try:
        return obj is not None and u.SystemLibrary.is_valid(obj)
    except (TypeError, RuntimeError):
        return False


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def enum_name(value):
    return str(value).split('.')[-1].split(':')[0].strip(' <>').replace('_', '').upper()


def dirty():
    return sorted(package.get_path_name() for package in
        list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def has_outer(obj, root):
    for _ in range(20):
        if not valid(obj):
            return False
        if obj == root:
            return True
        obj = obj.get_outer()
    return False


def owned_root_widget(cls, controller):
    matches = [widget for widget in u.ObjectIterator(cls.static_class())
        if valid(widget) and widget.get_owning_player() == controller
        and widget.is_in_viewport() and widget.is_visible()]
    require(len(matches) <= 1, 'Multiple live owned viewport roots: ' + str(cls))
    return matches[0] if matches else None


def owned_gameplay_hud(controller):
    """The real HUD is composed inside the current Prototype viewport root.

    CreateWidget assigns the GameInstance as the HUD's UObject outer, so its
    panel parent, rather than its own outer, proves attachment to that root.
    Removed travel widgets with the same owning PC are deliberately excluded.
    """
    root = owned_root_widget(u.MCPrototypeWidget, controller)
    matches = []
    for hud in u.ObjectIterator(u.MCGameplayHUD.static_class()):
        if not valid(hud) or hud.get_owning_player() != controller or not hud.is_visible():
            continue
        parent = hud.get_parent()
        attached = valid(root) and valid(parent) and has_outer(parent, root)
        if hud.is_in_viewport() or attached:
            matches.append(hud)
    require(len(matches) <= 1, 'Multiple current owned gameplay HUDs')
    return (matches[0] if matches else None), root


class FirstRelaxSolo:
    def __init__(self):
        command_line = u.SystemLibrary.get_command_line()
        require('-MCFirstRelaxOwned' in command_line or '-MCHubOwned' in command_line,
            'Explicit owned-editor launch flag required')
        require(Path(u.Paths.project_dir()).resolve() == ROOT.resolve(), 'Wrong project checkout')
        require(OUT.drive.upper() == 'E:', 'All outputs must remain on E:')
        require(not OUT.exists() or not any(OUT.iterdir()), 'Preserving prior outputs; choose a fresh directory')
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        require(not self.levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True),
            'An existing PIE session will not be adopted or stopped')
        require(not dirty(), 'Preserve unsaved packages before running this check')
        require(not u.InputLibrary.modifier_keys_state_is_control_down(u.InputLibrary.get_modifier_keys_state()),
            'Ctrl is held; it would launch a spectator instead of the normal pawn')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(valid(self.editor_world), 'Missing original editor world')
        self.original_map = self.editor_world.get_path_name().split('.')[0]
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(PLAY)))
        self.engine = next(obj for obj in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
            if obj.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.old_idle = u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        self.initial_culture = (u.InternationalizationLibrary.get_current_language(),
            u.InternationalizationLibrary.get_current_locale())
        self.started = time.monotonic()
        self.phase = 'warming_editor'
        self.phase_wall = self.started
        self.phase_game = 0.
        self.world = self.pc = self.hero = self.gs = self.sequence = None
        self.instance_path = self.world_path = self.pawn_path = self.director_path = None
        self.pie_start = self.callback = None
        self.requested_pie = self.done = self.busy = False
        self.error = None
        self.restore_errors = []
        self.sample_at = self.frame_at = 0.
        self.frame_bytes = 0
        self.seen_foods = set()
        self.settled_foods = None
        self.last_food_rows = []
        self.first_perk_seen_at = None
        self.hud_counter_seen = False
        self.capture_hz = max(0., min(8., float(globals().get('MC_FIRST_RELAX_CAPTURE_HZ', 6))))
        self.observe_seconds = max(10., min(20., float(globals().get('MC_FIRST_RELAX_OBSERVE_SECONDS', 10))))
        self.report = dict(passed=False, status='running', project=str(ROOT), scope=__doc__,
            network=False, package_saves=False, gameplay_overrides=False, checks={},
            requests=[], stages=[], samples=[], screenshots=[], frames=[], cleanup={},
            original_editor_map=self.original_map, observe_seconds=self.observe_seconds,
            source_sha256=hashlib.sha256((ROOT / 'Tools/Unreal/check_first_relax_solo.py').read_bytes()).hexdigest(),
            limitations=[
                'Only the real first portion is observed. Coffee, second/final portions and final cleaning are not exercised by this probe.',
                'The normal PlayerController.ChooseRewardPerk callback/RPC is invoked; physical key or mouse delivery is not tested.',
                'The normal ApplyCameraOrbitInput/ZoomCamera handlers provide a high view of the naturally spawned food. No player position, camera rig or camera configuration is replaced.',
                'A visible component flag does not prove an item is unobscured in the frame; screenshots and video require visual review.',
                'No multiplayer, cook/package, performance test, complete day run, fixtures or director/profile overrides.'])
        OUT.mkdir(parents=True, exist_ok=True)

    def write(self):
        self.report.update(phase=self.phase, wall_seconds=time.monotonic() - self.started)
        (OUT / 'Report.json').write_text(json.dumps(self.report, ensure_ascii=False,
            indent=2, allow_nan=False), encoding='utf-8')
        if self.report['frames']:
            keys = ['file', 'phase', 'world', 'game_seconds',
                'capture_request_wall_seconds', 'capture_completed_wall_seconds', 'bytes']
            with (OUT / 'FrameTimes.csv').open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=keys)
                writer.writeheader()
                writer.writerows({key: entry[key] for key in keys} for entry in self.report['frames'])

    def now(self):
        return float(u.GameplayStatics.get_time_seconds(self.world)) if valid(self.world) else 0.

    def stage(self, name):
        self.phase, self.phase_wall, self.phase_game = name, time.monotonic(), self.now()
        self.report['stages'].append(dict(name=name, wall_seconds=self.phase_wall - self.started,
            game_seconds=self.phase_game, world=path(self.world)))
        self.write()
        u.log('MC_FIRST_RELAX_SOLO_STAGE ' + name)

    def check(self, key, condition, message):
        require(condition, message)
        self.report['checks'][key] = True

    def console(self, command):
        u.SystemLibrary.execute_console_command(self.world if valid(self.world) else self.editor_world,
            command, self.pc if valid(self.pc) else None)

    def start(self):
        require(self.levels.load_level('/Game/Maps/L_Mouth'), 'Could not load saved L_Mouth before PIE')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        require(not dirty(), 'Loading L_Mouth unexpectedly dirtied packages')
        require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(PLAY)), 'Standalone settings rejected')
        # Preserve the project's normal simulation clock. Capturing PNGs must
        # not stretch a fixed-step gameplay second into many wall seconds.
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.console('t.IdleWhenNotForeground 0')
        self.pie_start = u.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps({'options': {'bSimulate': False, 'playMode': 'PlayMode_InEditorFloating', 'warmupSeconds': 0.}}))
        require(self.pie_start is not None, 'StartPIE returned no async result')
        self.requested_pie = True
        require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
        self.stage('await_first_perk')

    def owned_worlds(self):
        if not self.instance_path:
            return []
        return [world for world in u.EditorLevelLibrary.get_pie_worlds(True)
            if valid(world) and path(u.GameplayStatics.get_game_instance(world)) == self.instance_path]

    def refresh_world(self):
        worlds = [world for world in u.EditorLevelLibrary.get_pie_worlds(True) if valid(world)]
        if not worlds and not self.instance_path:
            return False
        require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),
            'Expected one standalone authority world; no external PIE will be adopted')
        world = worlds[0]
        require(u.GameplayStatics.get_current_level_name(world, True) == 'L_Mouth', 'Unexpected map/travel')
        instance = u.GameplayStatics.get_game_instance(world)
        require(valid(instance), 'PIE has no GameInstance identity')
        if self.instance_path is None:
            self.instance_path, self.world_path = path(instance), path(world)
            self.report['owned_game_instance'] = self.instance_path
            self.report['owned_world'] = self.world_path
        require(path(instance) == self.instance_path and path(world) == self.world_path,
            'Owned world was replaced; replacement must be preserved')
        self.world = world
        self.pc = u.GameplayStatics.get_player_controller(world, 0)
        self.gs = u.GameplayStatics.get_game_state(world)
        if not valid(self.pc) or not isinstance(self.gs, u.MCGameState):
            return False
        self.hero = self.pc.get_controlled_pawn()
        if not isinstance(self.hero, u.MCToothCharacter):
            return False
        self.sequence = prop(self.gs, 'single_day_director')
        if not valid(self.sequence):
            return False
        require(isinstance(self.sequence, u.MCSingleDayDirector), 'Wrong director type')
        if self.pawn_path is None:
            self.pawn_path, self.director_path = path(self.hero), path(self.sequence)
        require(path(self.hero) == self.pawn_path and path(self.sequence) == self.director_path,
            'Normal character/director was replaced during the observation')
        require(not bool(prop(self.gs, 'dev_manual_events')), 'F3/manual event mode is active')
        require(float(prop(prop(prop(self.hero, 'status'), 'state'), 'health')) > 0.,
            'Normal character died during the short first-portion observation')
        return True

    def actors(self, cls):
        return [actor for actor in u.GameplayStatics.get_all_actors_of_class(self.world, cls) if valid(actor)]

    def food_rows(self):
        rows = []
        for food in self.actors(u.MCFoodActor):
            if food.get_owner() != self.sequence or bool(prop(food, 'brush_tool')):
                continue
            if enum_name(prop(prop(food, 'food_data'), 'kind')) != 'FOOD' or bool(prop(food, 'fragment')):
                continue
            if enum_name(prop(food, 'phase')) == 'DISPOSED':
                continue
            rows.append(dict(actor=path(food), item=str(prop(food, 'item_name')),
                batch=int(prop(food, 'batch')), phase=enum_name(prop(food, 'phase')),
                health=float(prop(food, 'health')), spoiled=bool(prop(food, 'spoiled')),
                position_cm=vec(food.get_actor_location()), visual_visible=bool(prop(food, 'visual').is_visible())))
        return sorted(rows, key=lambda row: row['actor'])

    def hud_counter(self):
        hud, root = owned_gameplay_hud(self.pc)
        if not valid(hud):
            return None
        matches = [widget for widget in u.ObjectIterator(u.TextBlock.static_class())
            if valid(widget) and widget.get_name() == 'TaskValue' and has_outer(widget, hud) and widget.is_visible()]
        require(len(matches) <= 1, 'More than one current HUD TaskValue')
        if not matches:
            return None
        text = str(matches[0].get_text())
        return dict(hud=path(hud), viewport_root=path(root), widget=path(matches[0]), text=text,
            numbers=[int(value) for value in re.findall(r'\d+', text)])

    def observe_food(self):
        self.last_food_rows = self.food_rows()
        self.seen_foods.update(row['actor'] for row in self.last_food_rows)
        active_coffee = [path(event) for event in self.actors(u.MCCoffeeFlood) if bool(prop(event, 'active'))]
        require(not active_coffee, 'Coffee started before the untouched first portion was processed')
        require(len(self.seen_foods) <= 3, 'More than three distinct whole foods were issued in portion one')
        return active_coffee

    def sample(self, active_coffee):
        row = dict(phase=self.phase, wall_seconds=time.monotonic() - self.started, game_seconds=self.now(),
            world=path(self.world), pawn=path(self.hero), position_cm=vec(self.hero.get_actor_location()),
            native_stage=enum_name(prop(self.sequence, 'stage')),
            portion_number=int(prop(self.sequence, 'meal_portion_number')),
            portion_count=int(prop(self.sequence, 'meal_portion_count')),
            portion_target=int(prop(self.sequence, 'meal_portion_target')),
            portion_spawned=int(prop(self.sequence, 'meal_portion_spawned')),
            tasks_left=int(prop(self.gs, 'tasks_left')), tasks_total=int(prop(self.gs, 'tasks_total')),
            food=self.last_food_rows, all_observed_issued_food=sorted(self.seen_foods), active_coffee=active_coffee,
            hud_counter=self.hud_counter(), instruction=str(prop(prop(self.gs, 'director_state'), 'instruction')))
        camera = prop(self.hero, 'camera')
        row['camera_position_cm'] = vec(camera.get_world_location())
        self.report['samples'].append(row)
        return row

    def capture(self, name, sequence=False):
        require(valid(self.world) and isinstance(self.gs, u.MCGameState), 'No owned gameplay world to capture')
        destination = OUT / ('frames' if sequence else 'screenshots') / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        request, game_time = time.monotonic() - self.started, self.now()
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(destination)),
            'Owned PIE game-window capture failed: ' + str(destination))
        data = destination.read_bytes()
        require(len(data) > 24 and data.startswith(b'\x89PNG\r\n\x1a\n'), 'Capture is not a PNG')
        entry = dict(file=str(destination), phase=self.phase, world=path(self.world), game_seconds=game_time,
            capture_request_wall_seconds=request, capture_completed_wall_seconds=time.monotonic() - self.started,
            width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'),
            bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.report['frames' if sequence else 'screenshots'].append(entry)
        if sequence:
            self.frame_bytes += len(data)

    def aim_at_food(self):
        points = [row['position_cm'] for row in self.last_food_rows]
        target = u.Vector(*(sum(point[axis] for point in points) / len(points) for axis in range(3)))
        camera = prop(self.hero, 'camera')
        view = camera.get_world_rotation()
        pivot = self.hero.get_actor_location() + u.Vector(0., 0., float(prop(self.hero, 'camera_orbit_height')))
        desired = u.MathLibrary.find_look_at_rotation(pivot, target)
        desired.pitch = -62.
        sensitivity = max(.02, min(1., float(prop(self.hero, 'camera_orbit_sensitivity'))))
        yaw_delta = (float(desired.yaw) - float(view.yaw) + 180.) % 360. - 180.
        delta = u.Vector2D(yaw_delta / sensitivity, (desired.pitch - float(view.pitch)) / sensitivity)
        # These are the same public handlers as mouse orbit and wheel input.
        # Their native collision sweep, easing and 250..1600 cm zoom clamp stay
        # active. SetControlRotation does not drive the mouth's automatic view.
        self.hero.apply_camera_orbit_input(delta)
        self.hero.zoom_camera(-20.)
        self.report['camera_aim'] = dict(method='Normal Hero.ApplyCameraOrbitInput and ZoomCamera handlers',
            food_centroid_cm=vec(target), requested_yaw=float(desired.yaw), requested_pitch=float(desired.pitch),
            orbit_input=[float(delta.x), float(delta.y)], scroll_input=-20.,
            native_zoom_limit_cm=1600, replaced_camera_or_position=False)

    def advance(self):
        age = time.monotonic() - self.phase_wall
        native_stage = enum_name(prop(self.sequence, 'stage'))
        if self.phase == 'await_first_perk':
            require(age < 45., 'Normal first-perk offer did not appear; no tutorial/lobby override was applied')
            if self.pc.is_reward_menu_open() and not bool(prop(self.gs, 'lobby_waiting')):
                choice = owned_root_widget(u.MCPerkChoiceWidget, self.pc)
                cards = [card for card in u.ObjectIterator(u.MCPerkCardWidget.static_class())
                    if valid(choice) and valid(card) and has_outer(card, choice) and card.is_visible()]
                if len(cards) != 3:
                    return
                if self.first_perk_seen_at is None:
                    self.first_perk_seen_at = time.monotonic()
                if time.monotonic() - self.first_perk_seen_at < .6:
                    return
                self.check('normal_first_perk_has_three_visible_cards', True, '')
                self.report['first_perk_cards'] = [path(card) for card in cards]
                self.capture('01_normal_first_perk.png')
                self.report['requests'].append(dict(method='ChooseRewardPerk', choice_index=0,
                    world=path(self.world), wall_seconds=time.monotonic() - self.started))
                self.pc.choose_reward_perk(0)
                self.stage('await_first_portion')
        elif self.phase == 'await_first_portion':
            require(age < 75., 'Natural first portion did not issue exactly three items')
            require(native_stage in ('FIRSTPERK', 'OPENINGPAUSE', 'FIRSTMEAL'),
                'Unexpected native stage before first portion: ' + native_stage)
            if native_stage != 'FIRSTMEAL':
                return
            self.check('first_meal_is_first_portion', int(prop(self.sequence, 'meal_portion_number')) == 1
                and int(prop(self.sequence, 'meal_portion_count')) == 3
                and int(prop(self.sequence, 'meal_portion_target')) == 3, 'Unexpected first-portion contract')
            require(int(prop(self.sequence, 'meal_portion_spawned')) <= 3, 'First-portion issuance exceeded three')
            if int(prop(self.sequence, 'meal_portion_spawned')) == 3 and len(self.last_food_rows) == 3:
                self.check('exactly_three_naturally_issued_whole_food', len(self.seen_foods) == 3,
                    'Unique issued actors do not match the three-food portion')
                self.check('issued_food_share_native_batch', len({row['batch'] for row in self.last_food_rows}) == 1
                    and self.last_food_rows[0]['batch'] > 0, 'First-portion foods have inconsistent native batches')
                self.check('first_perk_resolved_normally', not self.pc.is_reward_menu_open(), 'Perk menu remained open')
                self.settled_foods = set(self.seen_foods)
                self.aim_at_food()
                self.stage('observe_untouched_first_portion')
        elif self.phase == 'observe_untouched_first_portion':
            require(native_stage == 'FIRSTMEAL', 'Untouched first portion incorrectly advanced to ' + native_stage)
            require(int(prop(self.sequence, 'meal_portion_number')) == 1
                and int(prop(self.sequence, 'meal_portion_target')) == 3
                and int(prop(self.sequence, 'meal_portion_spawned')) == 3, 'First-portion counters changed without processing')
            require(self.seen_foods == self.settled_foods and len(self.last_food_rows) == 3,
                'Food duplicated/disappeared while the untouched first portion waited')
            require(int(prop(self.gs, 'tasks_left')) == 3 and int(prop(self.gs, 'tasks_total')) == 3,
                'Untouched first-portion task counter should remain three outstanding foods')
            counter = self.hud_counter()
            if counter and counter['numbers'] == [0, 3]:
                self.hud_counter_seen = True
            if self.now() - self.phase_game >= 1. and not self.report.get('portion_screenshot'):
                self.capture('02_first_portion_three_food.png')
                self.report['portion_screenshot'] = True
            if self.now() - self.phase_game >= self.observe_seconds:
                self.check('hud_displays_zero_of_three', self.hud_counter_seen, 'Actual visible HUD never displayed 0 / 3')
                self.check('no_duplicate_food_while_waiting', True, '')
                self.check('no_coffee_before_processing', True, '')
                self.check('first_portion_waits_for_processing', True, '')
                self.capture('03_first_portion_still_waiting.png')
                self.stage('capture_flush')
        elif self.phase == 'capture_flush' and age >= .25:
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
            require(time.monotonic() - self.started < 180., 'First Relax solo exceeded its 180-second wall bound')
            if self.phase == 'warming_editor':
                if time.monotonic() - self.started >= 3.:
                    self.start()
                return
            require(not prop(self.pie_start, 'Error'), 'StartPIE: ' + str(prop(self.pie_start, 'Error')))
            if not self.refresh_world():
                require(time.monotonic() - self.phase_wall < 45., 'Normal gameplay world/pawn did not become ready')
                return
            # Census every game frame catches short-lived extra actors and any
            # premature coffee. Sampling/capture frequency only bounds evidence.
            active_coffee = self.observe_food()
            current = time.monotonic()
            if current >= self.sample_at:
                self.sample_at = current + .3
                self.sample(active_coffee)
            if self.phase != 'await_first_perk' and self.capture_hz and current >= self.frame_at:
                if len(self.report['frames']) < 240 and self.frame_bytes < 350 * 1024 * 1024:
                    self.frame_at = current + 1. / self.capture_hz
                    self.capture('%05d.png' % len(self.report['frames']), sequence=True)
                else:
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
        worlds = [world for world in u.EditorLevelLibrary.get_pie_worlds(True) if valid(world)]
        owned = self.owned_worlds()
        if worlds and (len(owned) != len(worlds) or not owned):
            self.report['cleanup']['replacement_pie_preserved'] = True
            self.error = self.error or 'Replacement/unidentified PIE session was preserved'
            self.finish()
            return
        if owned and self.requested_pie and self.levels.is_in_play_in_editor():
            self.levels.editor_request_end_play()
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
        actions = [
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
                require(not dirty(), 'Unexpected dirty packages preserved; editor map was not changed')
                require(self.levels.load_level(self.original_map), 'Original editor map restore failed')
                self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
                self.report['cleanup']['original_map_restored'] = True
            except Exception:
                self.restore_errors.append(traceback.format_exc())
        try:
            u.SystemLibrary.execute_console_command(self.editor_world,
                't.IdleWhenNotForeground ' + str(self.old_idle))
        except Exception:
            self.restore_errors.append(traceback.format_exc())
        current_culture = (u.InternationalizationLibrary.get_current_language(), u.InternationalizationLibrary.get_current_locale())
        if current_culture != self.initial_culture:
            self.error = self.error or 'Global culture unexpectedly changed'
        if self.restore_errors:
            self.error = self.error or 'Settings/editor-state restoration failed'
        self.report.update(passed=not bool(self.error), status='failed' if self.error else 'passed',
            error=self.error, restored_settings=not self.restore_errors, restore_errors=self.restore_errors,
            capture_total_bytes=self.frame_bytes, dirty_after=dirty())
        self.write()
        u.log('MC_FIRST_RELAX_SOLO_' + ('FAIL ' if self.error else 'PASS ') + str(OUT / 'Report.json'))
        # Let the owned command-line executor exit normally after this callback.
        u.EditorPythonScripting.set_keep_python_script_alive(False)


def main():
    old = getattr(builtins, KEY, None)
    require(old is None or getattr(old, 'done', True), 'Another First Relax solo probe is active')
    probe = FirstRelaxSolo()
    setattr(builtins, KEY, probe)
    u.EditorPythonScripting.set_keep_python_script_alive(True)
    probe.write()
    probe.callback = u.register_slate_post_tick_callback(probe.tick)


if __name__ == '__main__':
    main()
