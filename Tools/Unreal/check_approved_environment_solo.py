"""Short, owned solo PIE check of approved environment mechanics on L_Mouth.

Run in the idle Editor after LiveCoding. Uses the real F3 IceEvent and
HugeTongueStain paths plus held primary input; never calls brush gameplay helpers
directly. Spoiled food is a transient fixture whose normal tick drives smell and
the player's expression. All mutations are confined to this probe's PIE world.

Writes Saved/Checks/ApprovedEnvironmentSolo/Report.json and SHOWUI screenshots.
No map/package saves, network peers or native automation suite. Stops only its
own PIE, unregisters its callback and restores play/frame/foreground settings.
Optional globals: MC_ENV_OUTPUT (directory), MC_ENV_CAPTURE_HZ (default 6;
set 0 to capture key stills only).
Cancel: builtins.mc_approved_environment_solo.finish('cancelled').
"""
import builtins
import json
import math
import time
import traceback
from pathlib import Path

import unreal as u

KEY = 'mc_approved_environment_solo'
PLAY = dict(playNetMode='PIE_Standalone', playNumberOfClients=1,
            runUnderOneProcess=True, bLaunchSeparateServer=False,
            newWindowWidth=1280, newWindowHeight=720,
            clientFixedFPS=[60], serverFixedFPS=0)


def prop(obj, name):
    native = ''.join(part[:1].upper()+part[1:] for part in name.split('_'))
    error = None
    for key in (name, native, 'b'+native):
        try:
            return obj.get_editor_property(key)
        except Exception as caught:
            error = caught
    raise error


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def squared_xy(a, b):
    return (a.x-b.x)**2+(a.y-b.y)**2


def object_path(obj):
    return obj.get_path_name() if obj is not None and u.SystemLibrary.is_valid(obj) else None


class ApprovedEnvironmentSolo:
    def __init__(self):
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        if self.levels.is_in_play_in_editor() or u.EditorLevelLibrary.get_pie_worlds(True):
            raise RuntimeError('An existing PIE is active; it was left untouched')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        if '/Game/Maps/L_Mouth' not in self.editor_world.get_path_name():
            raise RuntimeError('Open L_Mouth first; the probe does not change the editor map')
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(PLAY)))
        self.engine = next(obj for obj in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if obj.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.old_idle = u.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
        default = Path(u.Paths.project_saved_dir())/'Checks/ApprovedEnvironmentSolo'
        self.output = Path(globals().get('MC_ENV_OUTPUT', default)).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.capture_hz = max(0., min(12., float(globals().get('MC_ENV_CAPTURE_HZ', 6))))
        self.callback = None
        self.pie_start = None
        self.requested_pie = False
        self.world = self.pc = self.hero = self.tongue = self.ice = self.food = self.huge = self.camera = None
        self.statics = u.get_default_object(u.GameplayStatics)
        self.started = time.monotonic()
        self.done = False
        self.phase = 'waiting_pie'
        self.phase_at = self.sample_at = self.frame_at = 0.
        self.last_game_time = None
        self.ice_requested = False
        self.primary = False
        self.cold_position = None
        self.growth_radius = 0.
        self.release_radius = 0.
        self.max_stink = self.max_gesture = 0.
        self.huge_before = None
        self.report = dict(status='running', passed=False,
                           scope='One owned standalone PIE on L_Mouth; real F3 and primary-input paths',
                           previous_play_settings=self.old_play, samples=[], checks={}, screenshots=[], frames=[],
                           fixture_tuning={'FreezeSeconds': 60., 'IcicleWarningSeconds': 120.,
                                           'bUseAnimatedBossAttacks': False},
                           tuning_reason='Isolate brushing from unrelated attack impacts during this short check')

    @staticmethod
    def require(ok, message):
        if not ok:
            raise RuntimeError(message)

    def write(self):
        (self.output/'Report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2,
                                                       allow_nan=False), encoding='utf8')
        (self.output/'README.md').write_text(
            '# Approved environment solo check\n\n'
            'One temporary standalone PIE on L_Mouth, in a 1280×720 floating game window. '
            'The first-perk UI resolves before the existing F3 IceEvent path runs. '
            'Actual held primary input drives floor brushing and the huge-stain wipe.\n\n'
            'Frost fixture tuning, confined to PIE: FreezeSeconds=60, IcicleWarningSeconds=120, '
            'bUseAnimatedBossAttacks=false. This isolates the short brushing check from unrelated '
            'attack impacts. It is not a difficulty/balance playthrough. Patch radii are derived '
            'from the actual replicated Cleaned and LastBrushedAt values; player warmth is observed '
            'in the event player state.\n\n'
            'The smell check uses one stationary spoiled food fixture with its normal actor tick '
            'enabled. Native smell VFX and the player expression/gesture are observed. '
            'The huge stain is spawned by the existing F3 action, then cleaned using held input.\n\n'
            'Frames are genuine Shot SHOWUI engine screenshots, recorded at requested '
            +str(self.capture_hz)+' Hz. Report.json stores each absolute file and game timestamp '
            'for video composition. Temporary review cameras improve visibility; UI remains visible. '
            'Key stills are in screenshots/.\n\n'
            'No content packages are saved. Only this owned PIE is stopped; play-window, frame-rate '
            'and foreground settings are restored. No network or broad automation suite is run.\n',
            encoding='utf8')

    def console(self, command):
        u.SystemLibrary.execute_console_command(self.world or self.editor_world, command, self.pc)

    def screenshot(self, name, sequence=False):
        target = self.output/('frames' if sequence else 'screenshots')/name
        target.parent.mkdir(parents=True, exist_ok=True)
        self.console('Shot SHOWUI filename="'+str(target).replace('\\', '/')+'" -nosuffix')
        entry = dict(file=str(target), phase=self.phase,
                     time=float(u.GameplayStatics.get_time_seconds(self.world)))
        self.report['frames' if sequence else 'screenshots'].append(entry)

    def frame_camera(self, location, target):
        rotation = u.MathLibrary.find_look_at_rotation(location, target)
        if self.camera is None:
            pose = u.Transform(location=location, rotation=rotation)
            self.camera = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(
                self.world, u.CameraActor.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
                None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
            self.require(self.camera is not None, 'Could not create temporary review camera')
            self.statics.call_method('FinishSpawningActor', args=(self.camera, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        else:
            self.camera.set_actor_location_and_rotation(location, rotation, False, True)
        self.pc.set_view_target_with_blend(self.camera, 0.)

    def stage(self, name):
        self.phase = name
        self.phase_at = float(u.GameplayStatics.get_time_seconds(self.world))
        self.report['last_phase'] = name
        self.write()
        u.log('MC_APPROVED_ENV_STAGE '+name)

    def start(self):
        self.require(u.ToolsetLibrary.set_object_properties(self.play, json.dumps(PLAY)),
                     'Could not apply standalone PIE settings')
        self.engine.set_editor_property('FixedFrameRate', 60.)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.console('t.IdleWhenNotForeground 0')
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.requested_pie = True
        # Explicit engine request creates a floating game window; the subsystem
        # EditorRequestBeginPlay API always attaches to an editor viewport.
        self.pie_start = u.ToolsetRegistry.execute_tool(
            'EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps({'Options': {'bSimulate': False, 'PlayMode': 1, 'WarmupSeconds': 0.}}))
        self.require(self.pie_start is not None, 'StartPIE returned no async result')
        self.require(not prop(self.pie_start, 'Error'), str(prop(self.pie_start, 'Error')))
        self.write()
        u.log('MC_APPROVED_ENV_START '+str(self.output))

    def actors(self, cls):
        return list(u.GameplayStatics.get_all_actors_of_class(self.world, cls))

    def primary_input(self, active):
        self.hero.call_method('SetPrimaryInputHeld', args=(bool(active),))
        self.primary = bool(active)

    def floor(self, point):
        result = prop(self.tongue, 'surface').line_trace_component(
            point+u.Vector(0, 0, 2500), point-u.Vector(0, 0, 2500), True, False, False)
        if result is None or result is False:
            return None
        # PrimitiveComponent.h K2_LineTraceComponent has four output parameters;
        # PyGenUtil::PackReturnValues omits its success bool and packs the outs.
        # Prefer the named FHitResult field, also accepting a direct hit wrapper.
        values = list(result) if isinstance(result, (tuple, list)) else [result]
        for value in reversed(values):
            try:
                impact = value.get_editor_property('impact_point')
                if isinstance(impact, u.Vector):
                    return impact
            except Exception:
                pass
        # The verified K2 tuple's first out is FVector HitLocation. Never treat
        # an unknown result's first entry as a point without checking its type.
        if values and isinstance(values[0], u.Vector):
            return values[0]
        raise RuntimeError('Unexpected LineTraceComponent output '+repr(type(result)))

    def move_to(self, floor, yaw=0.):
        self.hero.get_movement_component().stop_movement_immediately()
        height = self.hero.capsule_component.get_scaled_capsule_half_height()
        location = floor+u.Vector(0, 0, height+2.)
        self.hero.set_actor_location_and_rotation(location, u.Rotator(yaw=yaw), False, True)
        self.hero.get_movement_component().set_movement_mode(u.MovementMode.MOVE_WALKING)
        return location

    def cold_floor(self):
        origin, extent = self.tongue.get_actor_bounds(False)
        transform = self.tongue.get_actor_transform()
        candy = transform.transform_location(prop(self.ice, 'candy_anchor'))
        safe = transform.transform_location(prop(self.ice, 'safe_anchor'))
        # The boss's frost radius only increases the already tuned freeze rate;
        # it is not a solid obstacle. Keep a physical approach margin instead of
        # pushing this check to the tongue's delivery/end caps.
        cold_gap = 420.
        safe_gap = float(prop(self.ice, 'circle_radius'))+230.
        zones = []
        for zone in self.actors(u.MCFoodDisposal):
            volume = prop(zone, 'volume')
            zones.append((zone, volume.get_world_transform(), volume.get_unscaled_box_extent()))
        candidates = [u.Vector(x, y, origin.z)
                      for x in (500., 650., 350., -450., -650.) for y in (0., 250., -250., 400., -400.)]
        candidates += [origin+u.Vector(extent.x*x, extent.y*y, 0)
                       for x in (.25, -.25, .35, -.35, .45, -.45) for y in (0., .2, -.2)]
        for candidate in candidates:
            floor = self.floor(candidate)
            if not floor or squared_xy(floor, candy)<cold_gap**2 or squared_xy(floor, safe)<safe_gap**2:
                continue
            near_delivery = False
            for zone, geometry, half in zones:
                local = geometry.inverse_transform_location(floor)
                scale = geometry.scale3d
                margin_x, margin_y = 180./max(.01, abs(scale.x)), 180./max(.01, abs(scale.y))
                if abs(local.x) <= half.x+margin_x and abs(local.y) <= half.y+margin_y:
                    near_delivery = True
                    break
            if near_delivery:
                continue
            # Verify a usable patch footprint rather than accepting one hit at
            # an edge or a delivery guide crossing the deformed tongue.
            supports = [self.floor(floor+offset) for offset in
                        (u.Vector(150, 0, 0), u.Vector(-150, 0, 0),
                         u.Vector(0, 150, 0), u.Vector(0, -150, 0))]
            if any(point is None or abs(point.z-floor.z)>45. for point in supports):
                continue
            self.report['cold_fixture_location'] = dict(
                floor_cm=vec(floor), candy_floor_cm=vec(candy), safe_floor_cm=vec(safe),
                candy_distance_cm=math.sqrt(squared_xy(floor, candy)),
                safe_distance_cm=math.sqrt(squared_xy(floor, safe)),
                excluded_delivery_actors=[object_path(zone) for zone, _, _ in zones],
                selection='Interior supported footprint, outside expanded delivery volumes and initial warm circle',
                physical_candy_clearance_cm=cold_gap)
            return floor
        raise RuntimeError('No clear cold tongue point outside the initial warm circle/boss found')

    def patch_radius(self, patch, now):
        clean = max(0., float(prop(patch, 'cleaned'))-
                    max(0., now-float(prop(patch, 'last_brushed_at')))/max(2., float(prop(self.ice, 'brush_refreeze_seconds'))))
        return max(50., float(prop(self.ice, 'brush_warm_radius')))*math.sqrt(clean)

    def sample(self, now):
        physics = prop(self.hero, 'tooth_physics')
        movement = self.hero.get_movement_component()
        contact = prop(self.hero, 'brush_contact')
        status = prop(prop(self.hero, 'status'), 'state')
        game = u.GameplayStatics.get_game_state(self.world)
        value = dict(game_seconds=now, phase=self.phase, hero=vec(self.hero.get_actor_location()),
                     brushing=bool(prop(self.hero, 'brushing')), handling=bool(prop(self.hero, 'handling')),
                     requested_primary=self.primary, body_state=str(physics.call_method('GetBodyState')),
                     movement_mode=str(prop(movement, 'movement_mode')), velocity_cm_s=vec(self.hero.get_velocity()),
                     health=float(prop(status, 'health')), selected_tool=str(prop(prop(self.hero, 'inventory'), 'selected')),
                     held_food=object_path(prop(self.hero, 'held_food')), care_target=object_path(prop(self.hero, 'care_target')),
                     contact_progress=float(prop(self.hero, 'contact_progress')),
                     reward_interaction=object_path(prop(self.hero, 'reward_interaction')),
                     reward_menu_open=bool(self.pc.is_reward_menu_open()),
                     look_input_ignored=bool(self.pc.is_look_input_ignored()),
                     move_input_ignored=bool(self.pc.is_move_input_ignored()),
                     self_care=bool(prop(self.hero, 'self_care')), in_coffee=bool(prop(self.hero, 'in_coffee')),
                     frozen_legs=bool(self.hero.has_frozen_legs()),
                     freezing_to_death=bool(self.hero.is_freezing_to_death()),
                     mimic_captured=bool(self.hero.is_mimic_captured()),
                     swallowed_by=object_path(prop(self.hero, 'swallowed_by')),
                     game_phase=str(prop(game, 'phase')))
        if self.ice and self.phase in ('arrival', 'cold_baseline', 'brush_grow', 'refreeze'):
            floor = self.floor(self.hero.get_actor_location())
            if floor is not None:
                feet = self.hero.get_actor_location()-u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height())
                value['feet_floor_clearance_cm'] = float(feet.z-floor.z)
                value['floor_underfoot_cm'] = vec(floor)
            patches = prop(self.ice, 'brushed_warm_patches')
            value['patches'] = [dict(cleaned=float(prop(p, 'cleaned')),
                                     last_brushed_at=float(prop(p, 'last_brushed_at')),
                                     radius_cm=self.patch_radius(p, now)) for p in patches]
            player = next((p for p in prop(self.ice, 'players') if prop(p, 'hero') == self.hero), None)
            if player:
                value['freeze_amount'] = float(prop(player, 'amount'))
                value['safe'] = bool(prop(player, 'safe'))
        if self.phase == 'stink':
            expression = prop(self.hero, 'expression')
            self.max_stink = max(self.max_stink, float(prop(expression, 'stink_reaction')))
            self.max_gesture = max(self.max_gesture, float(prop(expression, 'stink_gesture_alpha')))
            value['stink_reaction'] = float(prop(expression, 'stink_reaction'))
            value['stink_gesture_alpha'] = float(prop(expression, 'stink_gesture_alpha'))
        self.report['samples'].append(value)

    def begin_stink(self):
        self.pc.call_method('RequestDevAction', args=(u.MCDevAction.STOP_ICE_EVENT, -1))
        self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
        floor = self.floor(self.hero.get_actor_location()+u.Vector(110, 90, 0))
        self.require(floor is not None, 'No floor for the small spoiled-food fixture')
        pose = u.Transform(location=floor+u.Vector(0, 0, 45))
        self.food = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(
            self.world, u.MCFoodActor.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(self.food is not None, 'Could not spawn spoiled food fixture')
        row = prop(self.food, 'food_data')
        row.set_editor_property('kind', u.MCFoodKind.FOOD)
        row.set_editor_property('scale', u.Vector(.6, .6, .6))
        row.set_editor_property('spoil_seconds', 300.)
        self.food.set_editor_property('food_data', row)
        self.food.set_editor_property('item_name', 'ApprovedStinkFixture')
        self.food.set_editor_property('item_mesh', u.load_asset('/Game/Stylized_Vegetables/Meshes/SM_Broccoli'))
        self.food.set_editor_property('spoiled', True)
        self.food.set_editor_property('batch', 190001)
        self.statics.call_method('FinishSpawningActor', args=(self.food, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        # Isolate this visual/social check from rolling physics; keep normal actor tick active.
        prop(self.food, 'body').set_simulate_physics(False)
        hero = self.hero.get_actor_location()
        self.frame_camera(hero+u.Vector(340, -240, 180), hero+u.Vector(0, 0, 25))
        self.stage('stink')

    def begin_huge(self):
        self.food.destroy_actor()
        self.food = None
        before = set(self.actors(u.MCMouthSurface))
        self.pc.call_method('RequestDevAction', args=(u.MCDevAction.HUGE_TONGUE_STAIN, -1))
        created = [a for a in self.actors(u.MCMouthSurface) if a not in before
                   and a.actor_has_tag('MCExperimentalHugeStain')]
        self.require(len(created) == 1, 'F3 did not create exactly one huge stain')
        self.huge = created[0]
        half_size = float(prop(self.huge, 'liquid_half_size'))
        self.require(half_size >= 600., 'Huge stain is smaller than the experimental 600cm half-size')
        self.report['checks']['huge_single_f3_spawn'] = True
        self.report['huge'] = dict(actor=self.huge.get_path_name(), half_size_cm=half_size)
        center = self.huge.get_actor_location()
        # Face toward the stain's inner region with normal held brush input.
        floor = self.floor(center-u.Vector(half_size*.25, 0, 0))
        self.require(floor is not None, 'No tongue below huge-stain brushing approach')
        self.move_to(floor)
        self.huge_before = list(prop(self.huge, 'wipe_mask'))
        self.frame_camera(center+u.Vector(half_size*.1, -half_size*2., half_size*2.1), center)
        self.primary_input(True)
        self.stage('huge_brush')

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 100., 'Short probe exceeded its wall-time limit')
            if self.pie_start is not None:
                self.require(not prop(self.pie_start, 'Error'), 'StartPIE: '+str(prop(self.pie_start, 'Error')))
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_server(worlds[0]),
                             'Expected exactly one standalone authority PIE world')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if self.pc is None or self.pc.get_controlled_pawn() is None:
                    self.world = None
                    return
                self.report['world'] = self.world.get_path_name()
                self.stage('first_perk')
                return
            self.require(self.world in worlds, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'Solo controller has no tooth character')
            if self.phase not in ('first_perk', 'perk_resolution') and self.pc.is_reward_menu_open():
                self.report.setdefault('reward_ui_interruptions', []).append(dict(
                    phase=self.phase, game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
                    brushing_before_choice=bool(prop(self.hero, 'brushing')),
                    requested_primary=self.primary,
                    input_policy='Menu is resolved through ChooseRewardPerk; primary is not silently reasserted'))
                self.pc.choose_reward_perk(0)
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.phase_at
            if now >= self.sample_at:
                self.sample_at = now+.1
                if self.phase != 'arrival' or self.ice:
                    self.sample(now)
            if self.capture_hz and now >= self.frame_at and self.phase not in ('waiting_pie', 'first_perk', 'perk_resolution', 'arrival'):
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase == 'first_perk':
                self.require(elapsed<25., 'First-perk UI did not become ready on normal L_Mouth')
                if now>=1. and self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.report['checks']['normal_first_perk_ready'] = True
                    self.stage('perk_resolution')
            elif self.phase == 'perk_resolution':
                self.require(elapsed<10., 'First-perk choice did not resolve')
                if not self.pc.is_reward_menu_open():
                    self.report['checks']['normal_first_perk_resolved'] = True
                    self.pc.call_method('RequestDevAction', args=(u.MCDevAction.ICE_EVENT, -1))
                    self.ice_requested = True
                    self.stage('arrival')
            elif self.phase == 'arrival':
                if not self.ice:
                    events = self.actors(u.MCIceEvent)
                    self.require(events, 'F3 IceEvent did not produce an event')
                    self.ice = events[0]
                    self.tongue = prop(self.ice, 'tongue')
                    self.require(self.tongue is not None, 'Frost event has no tongue')
                    self.ice.set_editor_property('freeze_seconds', 60.)
                    self.ice.set_editor_property('icicle_warning_seconds', 120.)
                    self.ice.set_editor_property('use_animated_boss_attacks', False)
                    self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
                    self.cold_position = self.move_to(self.cold_floor())
                    self.frame_camera(self.cold_position+u.Vector(420, 450, 380), self.cold_position-u.Vector(0, 0, 40))
                if prop(self.ice, 'stage') == u.MCIceEventStage.ACTIVE:
                    self.stage('cold_baseline')
            elif self.phase == 'cold_baseline' and elapsed >= .7:
                samples = [s for s in self.report['samples'] if s['phase'] == 'cold_baseline']
                self.require(samples and any(s.get('safe') is False for s in samples),
                             'Selected underfoot point was already a warm zone')
                self.screenshot('01_frost_before_brushing.png')
                self.primary_input(True)
                self.stage('brush_grow')
            elif self.phase == 'brush_grow':
                patches = prop(self.ice, 'brushed_warm_patches')
                radius = max((self.patch_radius(p, now) for p in patches), default=0.)
                self.growth_radius = max(self.growth_radius, radius)
                if elapsed >= 3.2:
                    self.primary_input(False)
                    self.require(self.growth_radius > 130., 'Held brush did not grow a warming patch')
                    samples = [s for s in self.report['samples'] if s['phase'] == 'brush_grow']
                    self.require(any(s.get('safe') is True for s in samples), 'Brushed patch did not make the player warm/safe')
                    self.release_radius = radius
                    self.report['checks']['held_brush_grows_warm_patch'] = True
                    self.report['checks']['brushed_patch_warms_player'] = True
                    self.screenshot('02_frost_cleared_warm_patch.png')
                    self.stage('refreeze')
            elif self.phase == 'refreeze' and elapsed >= 6.:
                patches = prop(self.ice, 'brushed_warm_patches')
                radius = max((self.patch_radius(p, now) for p in patches), default=0.)
                self.require(radius < self.release_radius-20., 'Released warming patch did not refreeze/shrink')
                self.report['checks']['released_patch_refreezes'] = True
                self.report['warming_patch'] = dict(max_radius_cm=self.growth_radius,
                                                  release_radius_cm=self.release_radius,
                                                  radius_after_six_seconds_cm=radius)
                self.screenshot('03_frost_refreezing.png')
                self.begin_stink()
            elif self.phase == 'stink':
                if .6 <= elapsed < .8 and not self.report.get('stink_shot'):
                    self.screenshot('04_spoiled_food_face_and_gesture.png')
                    self.report['stink_shot'] = True
                if elapsed >= 2.:
                    # Effect's native enum is not readable through Python. The
                    # UpdateSmell source owns and attaches its persistent native
                    # AMCReactionVFX to this food; observe those public facts.
                    smell = [a for a in self.actors(u.MCReactionVFX)
                             if a.get_owner() == self.food and a.get_attach_parent_actor() == self.food
                             and float(a.get_editor_property('AppliedDamage')) <= 0.
                             and float(a.get_life_span()) <= 0.]
                    self.require(smell, 'Spoiled food did not create its native smell VFX')
                    self.require(self.max_stink > .5, 'Nearby spoiled food did not drive the face reaction')
                    self.require(self.max_gesture > .3, 'Nearby spoiled food did not drive the nose-wave gesture')
                    self.report['checks']['spoiled_food_native_smell'] = True
                    self.report['checks']['nearby_stink_expression'] = True
                    self.report['checks']['nearby_stink_gesture'] = True
                    self.report['stink'] = dict(max_face_reaction=self.max_stink, max_gesture_alpha=self.max_gesture,
                                               smell_actors=[a.get_path_name() for a in smell],
                                               identification='Source-derived SpoiledSmell identity: persistent zero-damage native VFX owned by and attached to the spoiled food',
                                               source='Source/MessControl/Private/MCFoodEnvironment.cpp: AMCFoodActor::UpdateSmell')
                    self.begin_huge()
            elif self.phase == 'huge_brush':
                if .1 <= elapsed < .3 and not self.report.get('huge_shot'):
                    self.screenshot('05_one_huge_tongue_stain.png')
                    self.report['huge_shot'] = True
                current = list(prop(self.huge, 'wipe_mask'))
                changed = sum(b < a for a, b in zip(self.huge_before, current))
                if elapsed >= 1. and not self.report.get('huge_closeup'):
                    hero = self.hero.get_actor_location()
                    self.frame_camera(hero+u.Vector(360, 300, 300), hero-u.Vector(0, 0, 40))
                    self.report['huge_closeup'] = True
                if elapsed >= 2.:
                    self.primary_input(False)
                    self.require(changed > 0, 'Held brush did not wipe any huge-stain mask samples')
                    self.report['checks']['huge_stain_real_brushing'] = True
                    self.report['huge']['wiped_samples'] = changed
                    self.screenshot('06_huge_stain_brushed_area.png')
                    self.stage('capture_flush')
            elif self.phase == 'capture_flush' and elapsed >= .5:
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def owned_pie_is_active(self, worlds=None):
        """Destroyed UObject wrappers can raise even during list membership."""
        try:
            if self.world is None or not u.SystemLibrary.is_valid(self.world):
                return False
            owned_path = self.world.get_path_name()
            candidates = list(u.EditorLevelLibrary.get_pie_worlds(True)) if worlds is None else worlds
            for candidate in candidates:
                if u.SystemLibrary.is_valid(candidate) and candidate.get_path_name() == owned_path:
                    return True
        except (TypeError,RuntimeError):
            return False
        return False

    def finish(self, error=None):
        if self.done:
            return
        self.done = True
        restore_errors = []
        if self.callback is not None:
            try:
                u.unregister_slate_post_tick_callback(self.callback)
            except Exception:
                restore_errors.append(traceback.format_exc())
            self.callback = None
        try:
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
        except Exception:
            worlds = []
            restore_errors.append(traceback.format_exc())
        owned_active = self.owned_pie_is_active(worlds)
        interrupted = self.world is not None and not owned_active
        if interrupted:
            self.report['interruption'] = dict(reason='Owned PIE was stopped or replaced externally',
                replacement_pie_preserved=bool(worlds),last_phase=self.phase)
            error = error or 'Owned PIE ended before the check completed'
        if owned_active:
            try:
                if self.hero is not None and u.SystemLibrary.is_valid(self.hero):
                    self.primary_input(False)
            except Exception:
                restore_errors.append(traceback.format_exc())
        try:
            if self.requested_pie and owned_active and self.levels.is_in_play_in_editor():
                self.levels.editor_request_end_play()
                self.report['owned_pie_end_requested'] = True
        except Exception:
            restore_errors.append(traceback.format_exc())
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
                restore_errors.append(traceback.format_exc())
        if restore_errors:
            self.report['restore_errors'] = restore_errors
            error = error or 'Setting restoration failed'
        self.report.update(status='interrupted' if interrupted else 'failed' if error else 'passed', passed=not bool(error), error=error,
                           restored_settings=not restore_errors, wall_seconds=time.monotonic()-self.started)
        self.write()
        u.log('MC_APPROVED_ENV_'+('FAIL ' if error else 'PASS ')+str(self.output/'Report.json'))


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('Environment probe already active; finish it before starting another')
    probe = ApprovedEnvironmentSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
