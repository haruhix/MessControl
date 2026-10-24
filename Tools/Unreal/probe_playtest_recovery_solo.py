"""One owned normal (no -MCDevUI) standalone L_Mouth PIE: F3 and recovery.

Offline-authored probe; execute only after the requested native build, in idle
clean L_Mouth. Imports lifecycle/restore helpers from probe_clean_respawn_solo.
Temporary PIE fixtures only: suspend scheduler/tongue actor ticks, configure one
fresh fragment and one partly wiped ordinary stain beyond the tongue. Native
actor ticks must relocate both. Never writes expected recovered state or saves
packages. Recovery debt is observed from real phase/care/masks and no score or
throat intake; private native CountDirt/GameplaySpawnFootprint are not invented.
Screenshots are exact PIE SWindow captures, including actual HUD/window chrome.
Cancel: builtins.mc_playtest_recovery_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import hashlib
import json
import math
import os
import runpy
import time
import traceback
import uuid
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_clean_respawn_solo.py'), run_name='recovery_support')
Base, prop, valid, require, PLAY = (support[n] for n in ('CleanRespawnSolo', 'prop', 'valid', 'require', 'PLAY'))
KEY = 'mc_playtest_recovery_solo'
OUTPUT = Path('E:/CodexWork/RespawnVideoSoftlock_20261024/SoloRecovery')
TEMP = Path('E:/CodexTemp/RespawnVideoSoftlock_20261024/SoloRecovery')


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


class PlaytestRecoverySolo(Base):
    def __init__(self):
        self.done = self.requested_pie = False
        self.callback = self.pie_start = self.world = self.pc = None
        self.viewport_overrides, self.fixtures, self.paused_actors = [], [], []
        self.phase = 'waiting_pie'
        self.phase_at = self.started = time.monotonic()
        self.ready_since = None
        self.choice_at = -100.
        self.editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        require(not self.levels.is_in_play_in_editor() and not list(u.EditorLevelLibrary.get_pie_worlds(True)),
                'Preexisting PIE is active and remains untouched')
        self.editor_world = self.editor.get_editor_world()
        self.editor_path = self.editor_world.get_path_name()
        require(self.editor_path == '/Game/Maps/L_Mouth.L_Mouth', 'Open clean L_Mouth before the probe')
        require('-mcdevui' not in u.SystemLibrary.get_command_line().lower(), 'Normal Editor without -MCDevUI required')
        require(u.SystemLibrary.get_console_variable_int_value('MC.DevUI') == 0, 'MC.DevUI must already be zero')
        dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        require(not dirty, 'Unrelated dirty packages must be preserved; no saves are performed')
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
        TEMP.mkdir(parents=True, exist_ok=True)
        self.old_temp = {key: os.environ.get(key) for key in ('TEMP', 'TMP')}
        os.environ.update(TEMP=str(TEMP), TMP=str(TEMP))
        self.report = dict(status='running', passed=False, screenshots=[], assertions=[], samples=[], boundaries=[],
            phase=self.phase, previous_play_settings=self.old_play, editor_world=self.editor_path,
            network_tested=False, microphone_used=False, package_save_requested=False,
            fixtures=['temporary actor tick suspension of schedulers/tongue',
                      'reflected row/mesh/fragment initialization before FinishSpawningActor',
                      'temporary stain care/mask initialization, never expected recovered position/state'],
            limitations=['F3 calls the exported ToggleDevPanel UFUNCTION; this does not synthesize a physical keyboard event.',
                         'Native non-reflected placement/count functions are not called. Actual tissue traces and the native gate contract are independently observed.',
                         'No multiplayer or first-launch/prewarm certification is implied.'])

    def check(self, label, ok, **evidence):
        self.report['assertions'].append(dict(label=label, passed=bool(ok), **evidence))
        self.write()
        require(ok, label)

    def widgets(self, native_class, require_visible=True):
        cls = u.load_class(None, '/Script/MessControl.'+native_class)
        matches, inventory = [], []
        for widget in u.ObjectIterator(cls):
            if not valid(widget):
                continue
            owner = widget.get_owning_player()
            parent = widget.get_parent()
            viewport = bool(widget.is_in_viewport())
            visible = bool(widget.is_visible())
            # Exact owner identity also scopes the widget to our verified PIE
            # world. GameplayHUD is a child of PrototypeWidget's CanvasPanel
            # (MCPrototypeWidget.cpp:62-68), so it is not added to viewport itself.
            attached = viewport or valid(parent)
            inventory.append(dict(path=widget.get_path_name(),
                owner=owner.get_path_name() if valid(owner) else None,
                owned_by_probe_controller=owner == self.pc,
                owner_world=self.world.get_path_name() if owner == self.pc else None,
                in_viewport=viewport, visible=visible,
                visibility=str(widget.get_visibility()),
                parent=parent.get_path_name() if valid(parent) else None))
            if owner == self.pc and attached and (visible or not require_visible):
                matches.append(widget)
        self.report.setdefault('widget_inventory', {})[native_class] = inventory
        return matches

    def panel(self):
        # A collapsed top-level panel remains attached; keep it for close checks.
        panels = self.widgets('MCDevPanelWidget', require_visible=False)
        return panels[0] if panels else None

    def ui_contract(self):
        self.check('Global developer UI stays off', u.SystemLibrary.get_console_variable_int_value('MC.DevUI') == 0)
        hud = self.widgets('MCGameplayHUD')
        self.write()  # Preserve actual hierarchy evidence if the assertion fails.
        require(len(hud) == 1, 'Expected one live gameplay HUD for this local controller')
        prefix = hud[0].get_path_name()+'.'
        buttons = [b for b in u.ObjectIterator(u.Button) if valid(b) and b.get_name() == 'DirectorLogButton'
                   and b.get_path_name().startswith(prefix)]
        require(len(buttons) == 1, 'DirectorLogButton not found by actual widget-tree object name')
        self.check('Director log HUD button remains Collapsed', buttons[0].get_visibility() == u.SlateVisibility.COLLAPSED,
                   object=buttons[0].get_path_name(), visibility=str(buttons[0].get_visibility()))

    def toggle(self):
        # Source declaration is UFUNCTION(BlueprintCallable). The runtime call
        # fails explicitly if this built Editor does not actually export it.
        self.pc.call_method('ToggleDevPanel', args=())

    def screenshot(self, name):
        target = self.output/name
        require(u.MCGameplayUIEditorLibrary.capture_pie_game_window(self.world, str(target)), 'Exact PIE SWindow capture failed')
        data = target.read_bytes()
        require(data[:8] == b'\x89PNG\r\n\x1a\n' and len(data) >= 24, 'Capture did not produce a PNG')
        self.report['screenshots'].append(dict(file=str(target), sha256=hashlib.sha256(data).hexdigest(),
            width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'),
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)), world=self.world.get_path_name(),
            method='MCGameplayUIEditorLibrary.capture_pie_game_window: exact PIE SWindow, no image compositing'))

    def actors(self, cls):
        return [a for a in u.GameplayStatics.get_all_actors_of_class(self.world, cls) if valid(a)]

    def floor(self, point):
        trace = self.surface.line_trace_component(point+u.Vector(0, 0, 6000), point-u.Vector(0, 0, 6000), True, False, False)
        if trace is None or trace is False:
            return None
        require(isinstance(trace, (tuple, list)) and len(trace) == 4
                and isinstance(trace[0], u.Vector) and isinstance(trace[1], u.Vector), 'Unexpected native component trace output')
        return trace[0], trace[1]

    def footprint(self, point, radius, dirt=False):
        # Independently use the source's current work bands plus actual tissue
        # traces, not a guessed world-Z plane or a non-exported native method.
        near = float(prop(self.tongue, 'gameplay_spawn_near_depth'))
        far = float(prop(self.tongue, 'gameplay_spawn_far_depth'))
        min_x, max_x = self.center.x-self.extent.x, self.center.x+self.extent.x
        lo, hi = max_x-2*self.extent.x*far, max_x-2*self.extent.x*near
        side = self.extent.y*(.72 if dirt else 1.)
        margin = radius+(65. if dirt else 0.)
        if point.x-margin < lo or point.x+margin > hi or abs(point.y-self.center.y)+margin > side:
            return False
        ground = self.floor(point)
        if ground is None or ground[1].z < .65:
            return False
        for i in range(8):
            angle = i*math.pi/4
            rim = self.floor(ground[0]+u.Vector(math.cos(angle)*margin, math.sin(angle)*margin, 0))
            if rim is None or rim[1].z < .65 or abs(rim[0].z-ground[0].z) > margin*.8+20:
                return False
        return True

    def defer(self, cls, position):
        pose = u.Transform(location=position)
        actor = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, cls, pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        require(valid(actor), 'Deferred fixture spawn failed')
        self.fixtures.append(actor)
        return actor, pose

    def finish_spawn(self, actor, pose):
        self.statics.call_method('FinishSpawningActor', args=(actor, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))

    def setup_recovery(self):
        self.statics = u.get_default_object(u.GameplayStatics)
        tongues = self.actors(u.MCTongue)
        require(len(tongues) == 1, 'This bounded probe expects one actual arena tongue')
        self.tongue = tongues[0]
        self.surface = prop(self.tongue, 'surface')
        self.center, self.extent, _radius = u.SystemLibrary.get_component_bounds(self.surface)
        for class_name in ('MCSingleDayDirector', 'MCDayDirector', 'MCGameDirector', 'MCTutorialDirector', 'MCTongue'):
            for actor in self.actors(u.load_class(None, '/Script/MessControl.'+class_name)):
                enabled = actor.is_actor_tick_enabled()
                self.paused_actors.append((actor, enabled))
                actor.set_actor_tick_enabled(False)
        self.outside = u.Vector(self.center.x+self.extent.x+20000, self.center.y+self.extent.y+20000,
                                self.center.z-self.extent.z-600)
        self.food, pose = self.defer(u.MCFoodActor, self.outside)
        row = prop(self.food, 'food_data')
        for name, value in dict(kind=u.MCFoodKind.FOOD, scale=u.Vector(.25, .25, .25),
                                fragment_scale=u.Vector(.25, .25, .25), spoil_seconds=600., health=25., mass=1.).items():
            row.set_editor_property(name, value)
        self.food.set_editor_property('food_data', row)
        self.food.set_editor_property('item_name', 'SoloRecoveryFixture')
        self.food.set_editor_property('item_mesh', u.load_asset('/Game/Stylized_Vegetables/Meshes/SM_Broccoli'))
        self.food.set_editor_property('fragment', True)
        self.food.set_editor_property('phase', u.MCFoodPhase.FREE)
        self.food.set_editor_property('health', 25.)
        self.finish_spawn(self.food, pose)
        self.patch, pose = self.defer(u.MCMouthSurface, self.outside+u.Vector(100, 0, 100))
        self.patch.set_editor_property('randomize_liquid_size', False)
        self.patch.set_editor_property('liquid_half_size', 80.)
        self.finish_spawn(self.patch, pose)
        # These are disclosed initial conditions of the temporary fixture only.
        # No private UPROPERTY access or native non-reflected ApplyCoffee call.
        state = prop(prop(self.patch, 'status'), 'state')
        state.set_editor_property('coffee_left', max(1, int(prop(state, 'coffee_total'))))
        prop(self.patch, 'status').set_editor_property('state', state)
        self.mask = [int(v) for v in prop(self.patch, 'wipe_mask')]
        require(len(self.mask) == 4096, 'Expected initialized 64x64 ordinary puddle mask')
        for y in range(29, 35):
            for x in range(29, 35):
                self.mask[y*64+x] = 0
        self.patch.set_editor_property('wipe_mask', self.mask)
        self.contacts = int(prop(state, 'coffee_left'))
        self.seed = int(prop(self.patch, 'liquid_seed'))
        self.player_state = prop(self.pc, 'player_state')
        self.points = int(prop(self.player_state, 'points'))
        self.throats = [(t, int(prop(t, 'food_swallowed'))) for t in self.actors(u.MCThroat)]
        self.report['fixture_before'] = self.snapshot()
        self.check('Configured fragment really begins outside all tongue support', self.floor(self.food.get_actor_location()) is None)
        self.check('Partly wiped ordinary stain really begins outside all tongue support', self.floor(self.patch.get_actor_location()) is None)
        self.check('Stain fixture keeps a partly erased dirty mask', 0 < self.mask.count(0) < len(self.mask) and self.contacts > 0)
        self.screenshot('02_before_recovery_actual_window.png')
        self.stage('recovery_wait')

    def snapshot(self):
        return dict(game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            food=dict(path=self.food.get_path_name(), position=vec(self.food.get_actor_location()), phase=str(prop(self.food, 'phase'))),
            stain=dict(path=self.patch.get_path_name(), position=vec(self.patch.get_actor_location()),
                       coffee_left=int(prop(prop(prop(self.patch, 'status'), 'state'), 'coffee_left')),
                       wipe_sum=sum(int(v) for v in prop(self.patch, 'wipe_mask'))))

    def verify_recovery(self):
        require(valid(self.food) and valid(self.patch), 'A recovery fixture was destroyed instead of returned')
        _origin, extent, _radius = u.SystemLibrary.get_component_bounds(prop(self.food, 'body'))
        food_radius = max(35., math.hypot(extent.x, extent.y)+15.)
        food_ground = self.floor(self.food.get_actor_location())
        stain_ground = self.floor(self.patch.get_actor_location())
        food_ok = (self.footprint(self.food.get_actor_location(), food_radius) and food_ground is not None
                   and abs(self.food.get_actor_location().z-food_ground[0].z) < extent.z+100.)
        stain_ok = (self.footprint(self.patch.get_actor_location(), float(prop(self.patch, 'liquid_half_size'))*1.415, True)
                    and stain_ground is not None and abs(self.patch.get_actor_location().z-stain_ground[0].z) < 35.)
        self.report['samples'].append(self.snapshot())
        if not food_ok or not stain_ok:
            require(time.monotonic()-self.phase_at < 18., 'Native recovery did not find a supported location within 18 seconds')
            return False
        self.check('Fragment returns on native ticks without being disposed', prop(self.food, 'phase') != u.MCFoodPhase.DISPOSED,
                   position=vec(self.food.get_actor_location()), tissue_and_band_verified=True)
        self.check('Ordinary stain returns on native ticks and retains care debt',
                   int(prop(prop(prop(self.patch, 'status'), 'state'), 'coffee_left')) == self.contacts,
                   position=vec(self.patch.get_actor_location()), tissue_and_work_area_verified=True)
        self.check('All existing wiped bytes survive relocation', [int(v) for v in prop(self.patch, 'wipe_mask')] == self.mask)
        self.check('The ordinary puddle outline seed survives relocation', int(prop(self.patch, 'liquid_seed')) == self.seed)
        self.check('Recovery awards no player score', int(prop(self.player_state, 'points')) == self.points)
        self.check('Recovery counts no throat delivery', all(int(prop(t, 'food_swallowed')) == before for t, before in self.throats))
        self.report['fixture_after'] = self.snapshot()
        self.report['boundaries'].append('Ordinary stain Batch/Tongue are native-only UPROPERTY fields; not read or forced. Debt is observed via unchanged dirty care and wipe mask. Full native gate counting is covered by the C++ regressions.')
        self.ui_contract()
        self.screenshot('03_native_recovery_actual_window.png')
        return True

    def tick(self, _dt):
        if self.done:
            return
        try:
            wall = time.monotonic()
            require(wall-self.started < 150., 'Owned solo recovery probe exceeded 150 seconds')
            require(self.pie_start is None or not prop(self.pie_start, 'Error'), 'Async StartPIE failed')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]), 'Expected exactly one standalone authority world')
                self.world = worlds[0]
                require('L_Mouth' in self.world.get_path_name(), 'PIE did not start L_Mouth')
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not valid(self.pc):
                    self.world = None
                    return
                require(len(self.actors(u.PlayerController)) == 1, 'Additional players are outside this solo probe')
                self.report['world'] = self.world.get_path_name()
                self.stage('startup_ready')
            require(valid(self.world) and self.world in worlds, 'Owned PIE ended or was replaced')
            if self.phase == 'startup_ready':
                if self.pc.is_reward_menu_open():
                    if wall-self.choice_at > .4:
                        self.pc.choose_reward_perk(0)
                        self.choice_at = wall
                    return
                if not valid(self.pc.get_controlled_pawn()) or not self.ready():
                    self.ready_since = None
                    return
                self.ready_since = self.ready_since or wall
                if wall-self.ready_since < .5:
                    return
                self.ui_contract()
                self.toggle()
                self.stage('f3_open_settle')
            elif self.phase == 'f3_open_settle' and wall-self.phase_at > 1.5:
                panel = self.panel()
                self.check('Exported F3 handler opens the host panel and it survives Tick',
                           panel is not None and panel.is_visible(), duration_seconds=wall-self.phase_at)
                prefix = panel.get_path_name()+'.'
                host_text = [str(w.get_text()) for w in u.ObjectIterator(u.TextBlock)
                             if valid(w) and w.get_path_name().startswith(prefix)
                             and str(w.get_text()).startswith('ХОСТ')]
                self.check('Native F3 panel identifies this standalone authority as host', bool(host_text), native_status=host_text)
                self.ui_contract()
                self.screenshot('01_f3_open_normal_hud.png')
                self.toggle()
                self.stage('f3_closed_settle')
            elif self.phase == 'f3_closed_settle' and wall-self.phase_at > .5:
                panel = self.panel()
                self.check('Exported F3 handler closes the panel', panel is not None and panel.get_visibility() == u.SlateVisibility.COLLAPSED)
                self.ui_contract()
                self.setup_recovery()
            elif self.phase == 'recovery_wait':
                if self.verify_recovery():
                    self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        cleanup_errors = []
        for actor in reversed(self.fixtures):
            try:
                if valid(actor):
                    actor.destroy_actor()
            except Exception:
                cleanup_errors.append(traceback.format_exc())
        for actor, enabled in self.paused_actors:
            try:
                if valid(actor):
                    actor.set_actor_tick_enabled(enabled)
            except Exception:
                cleanup_errors.append(traceback.format_exc())
        for key, previous in self.old_temp.items():
            if previous is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = previous
        self.report['fixture_cleanup_errors'] = cleanup_errors
        self.report['temporary_actor_count'] = len(self.fixtures)
        self.report['paused_actor_count'] = len(self.paused_actors)
        if cleanup_errors:
            error = (error or '')+'\nFixture cleanup: '+str(cleanup_errors)
        super().finish(error)
        u.log('MC_PLAYTEST_RECOVERY_'+self.report['status'].upper()+' '+str(self.output/'Report.json'))


def main():
    previous = getattr(builtins, KEY, None)
    require(previous is None or getattr(previous, 'done', True), 'A recovery solo probe is already running')
    probe = PlaytestRecoverySolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
    return probe


if __name__ == '__main__':
    main()
