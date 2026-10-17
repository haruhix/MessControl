"""Prepare/capture current NutBoss native proxies in one existing solo PIE lab.

Import or direct file execution is read-only unless MC_NUT_DYNAMICS_RUN=True.
Options: MC_NUT_DYNAMICS_OUTPUT, FPS (30/60, default60), MODES (Native,
StylizedDynamics, optional KawaiiPhysics), SCENARIOS (names below), TIMEOUT
(default/maximum900s; shorter overrides allowed), TAIL_SECONDS (.3),
BACKDROP (True), FILL_INTENSITY (8). A complete eight-scenario/three-mode
60fps run can export around 5000 actual PNGs and take several minutes.
WizardTeleport is optional; the default remains the original eight scenarios.
Use these names with the MC_NUT_DYNAMICS_ prefix. No authored asset is saved,
no animation sequence is substituted, and every PNG is an actual engine frame.
"""
import builtins
import hashlib
import json
import math
import re
import time
from pathlib import Path

import unreal as u


SCENARIOS = (
    ('WizardCast', 'MAGE_FIREBALL', 'telegraph', 9, 8., 'Маг: огненный шар'),
    ('WizardMelee', 'MAGE_MELEE', 'telegraph', 8, 8., 'Маг: ближний удар'),
    ('WizardRain', 'MAGE_NUT_RAIN', 'telegraph', 11, 12., 'Маг: ореховый дождь'),
    ('WizardSummon', 'MAGE_SUMMON', 'telegraph', 10, 8., 'Маг: призыв'),
    ('WizardHit', 'MAGE_HURT', 'hit', 13, 3., 'Маг: реакция на удар'),
    ('TankMelee', 'TANK_MELEE', 'telegraph', 0, 8., 'Танк: ближний удар'),
    ('TankRoll', 'TANK_ROLL', 'telegraph', 3, 14., 'Танк: крутилка'),
    ('TankShieldBreak', 'TANK_SHIELD_BREAK_OVERFLOW', 'shield', 6, 3., 'Танк: разрушение щита'),
    ('WizardTeleport', 'MAGE_TELEPORT', 'telegraph', 12, 8., 'Маг: телепорт'),
)
DEFAULT_SCENARIO_NAMES = tuple(row[0] for row in SCENARIOS if row[0] != 'WizardTeleport')
MODE_LABELS = {'Native': 'Native · текущая игра',
               'StylizedDynamics': 'StylizedDynamics · текущие ограничения',
               'KawaiiPhysics': 'KawaiiPhysics · текущие ограничения'}
MODE_ENUMS = {'Native': 'NATIVE', 'StylizedDynamics': 'STYLIZED_DYNAMICS',
              'KawaiiPhysics': 'KAWAII_PHYSICS'}
BONE_FIELDS = ('torso_bone', 'head_bone', 'left_arm_bone', 'right_arm_bone',
               'left_forearm_bone', 'right_forearm_bone', 'left_hand_bone', 'right_hand_bone')
ASSET_FIELDS = ('tank_skeletal_mesh', 'tank_unshielded_skeletal_mesh', 'tank_ball_mesh',
                'tank_idle_animation', 'tank_walk_animation', 'tank_unshielded_idle_animation',
                'tank_unshielded_walk_animation', 'tank_walk_left_animation', 'tank_walk_right_animation',
                'tank_melee_animation', 'tank_hit_animation', 'tank_shield_break_animation',
                'tank_jump_animation', 'tank_transform_animation', 'tank_charge_tell_animation',
                'tank_charge_loop_animation', 'tank_charge_recovery_animation', 'mage_skeletal_mesh',
                'mage_idle_animation', 'mage_walk_animation', 'mage_cast_animation',
                'mage_melee_animation', 'mage_heavy_cast_animation', 'mage_summon_animation',
                'mage_rain_animation', 'mage_hit_animation', 'mage_death_animation')


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


def quat(value):
    return [float(value.x), float(value.y), float(value.z), float(value.w)]


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def object_path(value):
    if value is None:
        return None
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    matches = re.findall(r'/Game/[^\s\'\"<>]+', str(value))
    return matches[0].rstrip(')') if matches else None


def inspect_runtime():
    """Read-only preflight: no mode changes, ticking, fixture resets or spawns."""
    worlds = u.EditorLevelLibrary.get_pie_worlds(False)
    report = dict(worlds=[world.get_path_name() for world in worlds], classes={}, stations=[])
    for name in ('MCVFXLab', 'MCVFXLabBossStation', 'MCNutBoss',
                 'MCNutBossAnimInstance', 'MCAnimationComparisonSubsystem'):
        cls = u.load_class(None, '/Script/MessControl.' + name)
        report['classes'][name] = cls.get_path_name() if cls else None
    if len(worlds) != 1:
        return report
    world = worlds[0]
    library = u.get_default_object(u.load_class(None, '/Script/Engine.SubsystemBlueprintLibrary'))
    comparison = library.call_method('GetWorldSubsystem', args=(world, u.MCAnimationComparisonSubsystem))
    report['mode'] = str(comparison.call_method('GetMode'))
    report['availability'] = {name: bool(comparison.call_method('IsModeAvailable', args=(
        getattr(u.MCSecondaryAnimationMode, symbol),))) for name, symbol in MODE_ENUMS.items()}
    labs = u.GameplayStatics.get_all_actors_of_class(world, u.MCVFXLab)
    if len(labs) != 1:
        raise RuntimeError('Expected exactly one MCVFXLab')
    for index, row in enumerate(prop(labs[0], 'results', [])):
        station = prop(row, 'station')
        if not isinstance(station, u.MCVFXLabBossStation):
            continue
        profile = prop(station, 'nut_profile')
        settings = prop(prop(profile, 'settings'), 'boss')
        report['stations'].append(dict(index=index, name=str(prop(row, 'name')),
            kind=str(prop(station, 'kind')), profile=object_path(profile),
            assets={field: object_path(prop(settings, field)) for field in ASSET_FIELDS}))
    return report


class NutDynamicsCapture:
    def __init__(self):
        self.callback = self.capture = self.fill = self.wall = self.review_floor = self.target = self.component = None
        self.review_material = None
        self.engine = self.perf = self.comparison = self.lab = self.world = self.pc = self.pawn = None
        self.old_engine = self.old_perf = self.old_mode = self.old_viewer = self.old_lab = None
        self.old_station_index = self.old_dilation = self.old_max_fps = None
        self.station = self.boss = None
        # Destroyed Unreal wrappers must never remain dictionary keys: hashing
        # one during teardown can raise before any restoration is attempted.
        self.mesh_restore, self.station_restore = [], []
        self.mesh_paths, self.station_paths = set(), set()
        self.done, self.started = False, time.monotonic()
        self.output = Path(globals().get('MC_NUT_DYNAMICS_OUTPUT',
            Path(u.Paths.project_dir(), 'Artifacts/NutDynamicsNightly'))).resolve()
        self.fps = int(globals().get('MC_NUT_DYNAMICS_FPS', 60))
        if self.fps not in (30, 60):
            raise ValueError('Capture FPS must be 30 or 60')
        self.timeout = min(900., float(globals().get('MC_NUT_DYNAMICS_TIMEOUT', 900.)))
        self.tail = max(.1, float(globals().get('MC_NUT_DYNAMICS_TAIL_SECONDS', .3)))
        self.mode_names = list(globals().get('MC_NUT_DYNAMICS_MODES', ['Native', 'StylizedDynamics']))
        if (not self.mode_names or self.mode_names[0] != 'Native'
                or len(set(self.mode_names)) != len(self.mode_names)
                or any(name not in MODE_ENUMS for name in self.mode_names)):
            raise ValueError('MODES must begin with Native, with unique supported mode names')
        if bool(globals().get('MC_NUT_DYNAMICS_ALLOW_HIGH', False)):
            raise ValueError('Current solver has no reflected amplification API; compiled tuning is retained')
        selected = list(globals().get('MC_NUT_DYNAMICS_SCENARIOS', DEFAULT_SCENARIO_NAMES))
        lookup = {row[0]: row for row in SCENARIOS}
        if not selected or len(set(selected)) != len(selected) or any(name not in lookup for name in selected):
            raise ValueError('SCENARIOS must select unique existing scenario names')
        self.scenarios = [lookup[name] for name in selected]
        self.preflight = inspect_runtime()
        if len(self.preflight['worlds']) != 1 or 'L_VFXLab' not in self.preflight['worlds'][0]:
            raise RuntimeError('Requires one already running solo PIE L_VFXLab')
        if any(not self.preflight['classes'][name] for name in self.preflight['classes']):
            raise RuntimeError('Current native runtime classes are unavailable')
        if any(not self.preflight['availability'].get(name) for name in self.mode_names):
            raise RuntimeError('Selected actual plugin mode is unavailable')
        self.output.mkdir(parents=True, exist_ok=True)
        if any(self.output.glob('**/frame_*.png')) or (self.output / 'Capture.json').exists():
            raise RuntimeError('Refusing to overwrite an existing capture directory')
        self.report = dict(status='running', capture='Actual solo PIE SceneCapture2D engine frames',
            preflight=self.preflight, game_fps=self.fps, video_fps=self.fps, size=[640, 480],
            wall_timeout_seconds=self.timeout,
            tuning='Current compiled game tuning; no amplification, no squash/stretch',
            bounds=dict(body_degrees=4, head_degrees=10, translation_cm=2, affected_roles=['Torso', 'Head']),
            diagnostic_frame_lag=1, scenarios=[], frames=[], restoration=[], violations=[],
            limitations=['No NutBoss Walk lab kind/reflected preview API; Walk is not captured.',
                        'Idle/walk bases use absolute server time; reset does not align their loop phase.',
                        'Subsystem diagnostics are recorded before evaluation, one evaluation behind the rendered frame.',
                        'Roll follows a live moving target; independent replays may take different world-space paths despite matching action phases.',
                        'Roll uses a static TankBall during execution; skeletal secondary motion can affect warrior transformation/return but cannot alter ball rotation.',
                        'Each shot ends after actual station completion plus the stated idle tail.'],
            camera='Same orthographic 3/4 view per scenario; immediate boss follow without smoothing',
            lighting='Existing lab plus identical transient movable frontal fill; optional neutral review wall/floor',
            render_floor='Neutral transient floor using original support mesh and transform when BACKDROP=True; original station floor otherwise')
        self.manifest = dict(status='running', fps=self.fps, size=[640, 480],
            provider='Unreal runtime: ' + ' / '.join(self.mode_names), sources=[],
            cases=[dict(name=name, label=MODE_LABELS[name], directory=name) for name in self.mode_names],
            clips=[], frames=[], scope='Current saved UE gameplay proxies; source_frame denotes captured engine tick')
        self.world = u.EditorLevelLibrary.get_pie_worlds(False)[0]
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        self.pawn = self.pc.get_controlled_pawn()
        self.lab = u.GameplayStatics.get_all_actors_of_class(self.world, u.MCVFXLab)[0]
        self.old_lab = (bool(prop(self.lab, 'bRunning')), self.lab.is_actor_tick_enabled())
        self.old_station_index = prop(self.pc, 'station_index')
        if self.pawn:
            self.old_viewer = (self.pawn.get_actor_location(), self.pawn.get_actor_rotation(), self.pc.get_control_rotation())
        library = u.get_default_object(u.load_class(None, '/Script/Engine.SubsystemBlueprintLibrary'))
        self.comparison = library.call_method('GetWorldSubsystem', args=(self.world, u.MCAnimationComparisonSubsystem))
        self.old_mode = self.comparison.call_method('GetMode')
        self.library = u.get_default_object(u.GameplayStatics)
        self.stations = {}
        rows = prop(self.lab, 'results', [])
        for scenario in self.scenarios:
            expected = getattr(u.MCVFXLabBossKind, scenario[1])
            matches = [(index, prop(row, 'station')) for index, row in enumerate(rows)
                       if prop(prop(row, 'station'), 'kind') == expected]
            if len(matches) != 1:
                raise RuntimeError('Missing/ambiguous actual station kind: ' + scenario[1])
            self.stations[scenario[0]] = matches[0]
        self.collect_sources()
        self.write()
        # All changes below are transient world/settings changes, after preflight.
        self.lab.call_method('SetRunning', args=(False,))
        self.lab.set_actor_tick_enabled(False)
        self.old_dilation = float(u.GameplayStatics.get_global_time_dilation(self.world))
        u.GameplayStatics.set_global_time_dilation(self.world, 1.)
        self.engine = next(obj for obj in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if obj.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate'))
        self.engine.set_editor_property('FixedFrameRate', float(self.fps))
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.old_max_fps = float(u.SystemLibrary.get_console_variable_float_value('t.MaxFPS'))
        u.SystemLibrary.execute_console_command(self.world, 't.MaxFPS 0')
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.setup_capture()
        self.run_index, self.last_time = -1, None
        self.plan = [(scenario, mode) for scenario in self.scenarios for mode in self.mode_names]
        self.native_counts = {}
        self.advance()
        self.callback = u.register_slate_post_tick_callback(self.tick)

    def collect_sources(self):
        assets = set()
        for _, station in self.stations.values():
            profile = prop(station, 'nut_profile')
            assets.add(object_path(profile))
            settings = prop(prop(profile, 'settings'), 'boss')
            assets.update(object_path(prop(settings, field)) for field in ASSET_FIELDS)
            floor = prop(station, 'floor')
            assets.add(object_path(prop(floor, 'source_mesh')))
        root = Path(u.Paths.project_dir()).resolve()
        for asset in sorted(value for value in assets if value and value.startswith('/Game/')):
            package = asset.split('.')[0]
            path = root / 'Content' / (package[len('/Game/'):] + '.uasset')
            if not path.is_file():
                raise RuntimeError('Saved package provenance missing: ' + str(path))
            self.manifest['sources'].append(dict(path=str(path), sha256=sha256(path), asset=asset))

    def spawn(self, cls, transform):
        actor = self.library.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, cls, transform,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        if not actor:
            raise RuntimeError('Transient review actor spawn failed')
        self.library.call_method('FinishSpawningActor', args=(actor, transform, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        return actor

    def setup_capture(self):
        self.capture = self.spawn(u.SceneCapture2D, u.Transform())
        self.component = prop(self.capture, 'capture_component2d')
        self.target = u.RenderingLibrary.create_render_target2d(self.world, 640, 480,
            u.TextureRenderTargetFormat.RTF_RGBA8, u.LinearColor(.18, .22, .28, 1), False, False)
        for name, value in dict(texture_target=self.target, capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
            capture_every_frame=False, capture_on_movement=False, always_persist_rendering_state=True,
            projection_type=u.CameraProjectionMode.ORTHOGRAPHIC, ortho_width=1280.,
            primitive_render_mode=u.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():
            self.component.set_editor_property(name, value)
        self.component.set_editor_property('show_flag_settings', [u.EngineShowFlagsSetting(show_flag_name=name, enabled=False)
            for name in ('MotionBlur', 'Fog', 'VolumetricFog', 'Atmosphere')])
        self.fill = self.spawn(u.DirectionalLight, u.Transform())
        light = prop(self.fill, 'light_component')
        light.set_editor_property('mobility', u.ComponentMobility.MOVABLE)
        light.set_editor_property('intensity', float(globals().get('MC_NUT_DYNAMICS_FILL_INTENSITY', 8.)))
        light.set_editor_property('cast_shadows', False)
        if bool(globals().get('MC_NUT_DYNAMICS_BACKDROP', True)):
            self.wall = self.spawn(u.StaticMeshActor, u.Transform())
            mesh = prop(self.wall, 'static_mesh_component')
            mesh.set_editor_property('mobility', u.ComponentMobility.MOVABLE)
            mesh.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
            mesh.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
            mesh.set_editor_property('cast_shadow', False)
            material = mesh.create_dynamic_material_instance(0, u.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
            material.set_vector_parameter_value('Color', u.LinearColor(.18, .22, .28, 1))
            material.set_scalar_parameter_value('Roughness', 1.)
            self.review_material = material
            mesh.set_material(0, material)
            self.wall.set_actor_scale3d(u.Vector(.1, 35, 35))
            self.review_floor = self.spawn(u.StaticMeshActor, u.Transform())
            review_mesh = prop(self.review_floor, 'static_mesh_component')
            review_mesh.set_editor_property('mobility', u.ComponentMobility.MOVABLE)
            review_mesh.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
            review_mesh.set_editor_property('cast_shadow', False)
            review_mesh.set_material(0, material)

    def advance(self):
        if self.station and u.SystemLibrary.is_valid(self.station):
            self.station.call_method('SetRunning', args=(False,))
        self.run_index += 1
        if self.run_index >= len(self.plan):
            self.finish()
            return
        self.scenario, self.mode_name = self.plan[self.run_index]
        name = self.scenario[0]
        index, self.station = self.stations[name]
        station_path = self.station.get_path_name()
        if station_path not in self.station_paths:
            self.station_paths.add(station_path)
            self.station_restore.append((self.station, prop(self.station, 'cycle_seconds'), station_path))
        self.station.set_editor_property('cycle_seconds', 120.)
        mode = getattr(u.MCSecondaryAnimationMode, MODE_ENUMS[self.mode_name])
        self.comparison.call_method('SetMode', args=(mode,))
        if self.comparison.call_method('GetMode') != mode:
            raise RuntimeError('Requested actual mode did not become active')
        self.pc.call_method('ServerSelectStation', args=(index,))
        self.station.call_method('SetRunning', args=(True,))
        self.station.call_method('Reset')
        self.directory = self.output / self.mode_name / name
        self.directory.mkdir(parents=True, exist_ok=True)
        self.frame, self.recording, self.completed_at = 0, False, None
        self.boss = None
        self.wait_started = time.monotonic()
        self.current = dict(name=name, mode=self.mode_name, station=index,
            kind=str(prop(self.station, 'kind')), profile=object_path(prop(self.station, 'nut_profile')),
            frames=0, trigger=self.scenario[2], metrics_max=dict(angle_degrees=0., translation_cm=0., active_bones=0),
            passed=0, failed=0, states=[], cues=[], same_fixture=True)
        self.report['scenarios'].append(self.current)
        self.write()

    def owns(self, actor):
        visited = set()
        while actor and actor not in visited:
            if actor == self.station:
                return True
            visited.add(actor)
            actor = actor.get_owner()
        return False

    def fixture(self):
        actors = [actor for actor in u.GameplayStatics.get_all_actors_of_class(self.world, u.Actor)
                  if actor != self.station and self.owns(actor)]
        boss = prop(self.station, 'nut_boss')
        if not boss or not u.SystemLibrary.is_valid(boss):
            boss = next((actor for actor in actors if isinstance(actor, u.MCNutBoss)), None)
        return boss, actors

    def camera_and_visibility(self, actors):
        tank = self.scenario[0].startswith('Tank')
        aim = self.boss.get_actor_location() + u.Vector(70, 0, 120 if tank else 40)
        eye = aim + u.Vector(1100, -900, 540)
        rotation = u.MathLibrary.find_look_at_rotation(eye, aim)
        self.capture.set_actor_location_and_rotation(eye, rotation, False, True)
        self.fill.set_actor_location_and_rotation(eye, rotation, False, True)
        self.component.set_editor_property('ortho_width', 1380. if tank else 1260.)
        self.component.clear_show_only_components()
        floor = prop(self.station, 'floor')
        if floor:
            if self.review_floor:
                source_mesh = prop(floor, 'source_mesh')
                if not source_mesh:
                    raise RuntimeError('Neutral floor requires the actual support source mesh')
                review_mesh = prop(self.review_floor, 'static_mesh_component')
                if prop(review_mesh, 'static_mesh') != source_mesh:
                    review_mesh.set_static_mesh(source_mesh)
                review_mesh.set_material(0, self.review_material)
                self.review_floor.set_actor_scale3d(floor.get_actor_scale3d())
                self.review_floor.set_actor_location_and_rotation(
                    floor.get_actor_location(), floor.get_actor_rotation(), False, True)
                self.component.show_only_actor_components(self.review_floor)
                self.current['review_floor'] = dict(source_mesh=object_path(source_mesh),
                    position_cm=xyz(floor.get_actor_location()), scale=xyz(floor.get_actor_scale3d()),
                    material='Transient neutral-gray material; render-only, no collision')
            else:
                self.component.show_only_actor_components(floor)
        for actor in actors:
            self.component.show_only_actor_components(actor)
            for mesh in actor.get_components_by_class(u.SkeletalMeshComponent):
                mesh_path = mesh.get_path_name()
                if mesh_path not in self.mesh_paths:
                    self.mesh_paths.add(mesh_path)
                    self.mesh_restore.append((mesh, prop(mesh, 'visibility_based_anim_tick_option'), mesh_path))
                mesh.set_editor_property('visibility_based_anim_tick_option',
                    u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        if self.wall:
            away = aim - eye
            length = math.sqrt(away.x*away.x + away.y*away.y + away.z*away.z)
            self.wall.set_actor_location_and_rotation(aim + away * (1200./length), rotation, False, True)
            self.component.show_only_actor_components(self.wall)
        self.current.update(camera_aim_cm=xyz(aim), ortho_width_cm=1380. if tank else 1260.)

    def trigger(self, now):
        kind = self.scenario[2]
        if kind == 'telegraph':
            if 'TELEGRAPH' not in str(prop(self.boss, 'state')).upper():
                return None
            return float(prop(self.boss, 'state_started_at', now))
        stamp = float(prop(self.boss, 'visual_hit_at' if kind == 'hit' else 'shield_broken_at', -100))
        return stamp if 0 <= now - stamp <= .1 else None

    def sample(self, now, actors):
        model = prop(self.boss, 'tank_model' if self.scenario[0].startswith('Tank') else 'mage_model')
        settings = prop(self.boss, 'boss_settings')
        bones = {}
        for field in BONE_FIELDS:
            name = str(prop(settings, field, ''))
            if model.get_bone_index(name) < 0:
                name = name.replace('.', '_')
            if model.get_bone_index(name) < 0:
                continue
            transform = model.get_socket_transform(name, u.RelativeTransformSpace.RTS_COMPONENT)
            values = dict(position_cm=xyz(transform.translation), rotation_xyzw=quat(transform.rotation), scale=xyz(transform.scale3d))
            if any(not math.isfinite(value) for group in values.values() for value in group):
                raise RuntimeError('Nonfinite real bone transform: ' + name)
            bones[name] = values
        metrics = []
        for value in self.comparison.call_method('GetSamples'):
            if prop(value, 'MeshName') != model.get_path_name():
                continue
            metric = dict(mesh=prop(value, 'MeshName'), mode=str(prop(value, 'Mode')),
                active_bones=int(prop(value, 'ActiveBones', 0)), evaluations=int(prop(value, 'Evaluations', 0)),
                angle_degrees=float(prop(value, 'MaxAngleDegrees', 0)), translation_cm=float(prop(value, 'MaxTranslation', 0)))
            metrics.append(metric)
            maxima = self.current['metrics_max']
            for key in ('active_bones', 'angle_degrees', 'translation_cm'):
                maxima[key] = max(maxima[key], metric[key])
            if metric['active_bones'] > 2 or metric['angle_degrees'] > 10.01 or metric['translation_cm'] > 2.01:
                self.report['violations'].append(dict(scenario=self.scenario[0], mode=self.mode_name, index=self.frame, metric=metric))
        state = str(prop(self.boss, 'state'))
        cues = sorted({str(prop(prop(actor, 'cue'), 'type')) for actor in actors if isinstance(actor, u.MCNutCombatEffect)})
        if state not in self.current['states']:
            self.current['states'].append(state)
        self.current['cues'] = sorted(set(self.current['cues']) | set(cues))
        mesh = prop(model, 'skeletal_mesh_asset')
        return dict(case=self.mode_name, clip=self.scenario[0], index=self.frame, source_frame=self.frame,
            game_time=now, game_dt=float(u.GameplayStatics.get_world_delta_seconds(self.world)),
            trigger_relative_seconds=now-self.trigger_at, state=state, attack=str(prop(self.boss, 'attack')),
            boss=self.boss.get_path_name(), model=model.get_path_name(), mesh=object_path(mesh),
            anim_instance=object_path(model.get_anim_instance()), position_cm=xyz(self.boss.get_actor_location()),
            resolve_at=float(prop(self.boss, 'resolve_at', 0)), attack_end_at=float(prop(self.boss, 'attack_end_at', 0)),
            shield_health=float(prop(self.boss, 'shield_health', 0)), bones_component=bones,
            metrics=metrics, cues=cues)

    def tick(self, unused_slate_dt):
        try:
            if time.monotonic()-self.started > self.timeout:
                raise RuntimeError('Capture exceeded its %.0fs wall timeout' % self.timeout)
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_time is not None and now <= self.last_time + .000001:
                return
            step = now-self.last_time if self.last_time is not None else None
            self.last_time = now
            boss, actors = self.fixture()
            if not boss:
                if time.monotonic()-self.wait_started > 20:
                    raise RuntimeError('Selected real station did not spawn its NutBoss')
                return
            if self.recording and boss != self.boss:
                raise RuntimeError('Station replaced its boss during a shot')
            self.boss = boss
            self.camera_and_visibility(actors)
            if not self.recording:
                stamp = self.trigger(now)
                if stamp is None:
                    if time.monotonic()-self.wait_started > 25:
                        raise RuntimeError('Expected real action trigger never arrived')
                    return
                self.trigger_at, self.recording = stamp, True
                self.current.update(boss=boss.get_path_name(), trigger_at=stamp,
                    recording_start_game_time=now, first_trigger_offset_seconds=now-stamp)
            if step is not None and abs(step-1/self.fps) > .001:
                raise RuntimeError('Nonconsecutive real gameplay frame: %.6f; no synthetic frames allowed' % step)
            if abs(float(u.GameplayStatics.get_world_delta_seconds(self.world))-1/self.fps) > .001:
                raise RuntimeError('Actual world delta differs from the requested native capture rate')
            sample = self.sample(now, actors)
            self.component.capture_scene()
            filename = 'frame_%04d.png' % self.frame
            u.RenderingLibrary.export_render_target(self.world, self.target, str(self.directory), filename)
            path = self.directory / filename
            if not path.is_file():
                raise RuntimeError('SceneCapture did not export its actual PNG')
            digest = sha256(path)
            self.report['frames'].append(sample)
            self.manifest['frames'].append(dict(case=self.mode_name, clip=self.scenario[0], index=self.frame,
                source_frame=self.frame, path=str(path), sha256=digest, game_time=now,
                trigger_relative_seconds=now-self.trigger_at))
            self.frame += 1
            self.current.update(frames=self.frame, passed=int(prop(self.station, 'passed', 0)),
                failed=int(prop(self.station, 'failed', 0)), executions=int(prop(self.station, 'executions', 0)),
                recoveries=int(prop(self.station, 'recoveries', 0)), status=str(prop(self.station, 'status')))
            if self.current['passed'] + self.current['failed'] > 0 and self.completed_at is None:
                self.completed_at = now
            name = self.scenario[0]
            if self.mode_name == 'Native':
                finished = self.completed_at is not None and now-self.completed_at >= self.tail - .000001
            else:
                finished = self.frame >= self.native_counts[name]
            if now-self.trigger_at > self.scenario[4]:
                raise RuntimeError('Native action failed to complete before its scenario limit')
            if finished:
                if self.current['passed'] + self.current['failed'] == 0:
                    raise RuntimeError('Comparison did not reach its real callback/recovery at the native shot length')
                self.current['observed_complete_cycle'] = True
                if self.mode_name == 'Native':
                    self.native_counts[name] = self.frame
                    self.manifest['clips'].append(dict(name=name, label=self.scenario[5], frame_count=self.frame,
                        frame_start=0, frame_end=self.frame-1, timeline_seconds=(self.frame-1)/self.fps,
                        source_kind='Actual current UE gameplay engine ticks', full_station_cycle=True,
                        idle_tail_seconds=self.tail))
                self.advance()
            elif self.frame == 1 or self.frame % self.fps == 0:
                self.write()
        except Exception as error:
            self.finish(str(error))

    def write(self):
        for name, value in (('Capture.json', self.report), ('RenderManifest.json', self.manifest)):
            path = self.output / name
            temporary = path.with_suffix(path.suffix + '.tmp')
            temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf8')
            temporary.replace(path)

    def finish(self, error=None):
        if getattr(self, 'done', False):
            return
        self.done = True
        failures, skipped = [], []
        def restore(label, operation):
            try:
                operation()
                if hasattr(self, 'report'):
                    self.report['restoration'].append(label)
            except Exception as exception:
                failures.append(label + ': ' + str(exception))
        def valid(value, label):
            if value is None:
                return False
            try:
                result = bool(u.SystemLibrary.is_valid(value))
                if not result:
                    skipped.append(label + ': object was already destroyed')
                return result
            except Exception as exception:
                # Dead wrappers can throw while marshaling an UObject argument.
                # Their temporary mesh option no longer needs restoration.
                skipped.append(label + ': stale wrapper: ' + str(exception))
                return False
        if self.callback is not None:
            restore('unregister capture callback', lambda: u.unregister_slate_post_tick_callback(self.callback))
            self.callback = None
        for mesh, old, path in self.mesh_restore:
            if valid(mesh, 'mesh tick ' + path):
                restore('mesh tick ' + path, lambda mesh=mesh, old=old:
                    mesh.set_editor_property('visibility_based_anim_tick_option', old))
        for station, old, path in self.station_restore:
            if valid(station, 'station cycle ' + path):
                restore('station cycle ' + path, lambda station=station, old=old:
                    station.set_editor_property('cycle_seconds', old))
        if valid(self.station, 'final station'):
            restore('clear final transient fixture', lambda: self.station.call_method('SetRunning', args=(False,)))
        if self.comparison is not None and self.old_mode is not None:
            restore('secondary mode', lambda: self.comparison.call_method('SetMode', args=(self.old_mode,)))
        if self.target is not None:
            restore('detach capture target', lambda: self.component.set_editor_property('texture_target', None))
            restore('release capture target', lambda: u.RenderingLibrary.release_render_target2d(self.target))
        for actor in (self.capture, self.fill, self.wall, self.review_floor):
            if valid(actor, 'transient review actor'):
                restore('destroy transient review actor', actor.destroy_actor)
        if self.old_engine is not None:
            restore('fixed-frame switch', lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]))
            restore('fixed frame rate', lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]))
        if self.old_max_fps is not None:
            restore('FPS cap', lambda: u.SystemLibrary.execute_console_command(self.world, 't.MaxFPS %.9g' % self.old_max_fps))
        if self.old_dilation is not None:
            restore('world time dilation', lambda: u.GameplayStatics.set_global_time_dilation(self.world, self.old_dilation))
        if self.old_perf is not None:
            restore('editor foreground throttle', lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_perf))
        if self.pc is not None and self.old_station_index is not None:
            restore('viewer station selection', lambda: self.pc.call_method('ServerSelectStation', args=(self.old_station_index,)))
        if self.old_viewer is not None and valid(self.pawn, 'viewer pawn'):
            restore('viewer position', lambda: self.pawn.set_actor_location_and_rotation(self.old_viewer[0], self.old_viewer[1], False, True))
            restore('viewer rotation', lambda: self.pc.set_control_rotation(self.old_viewer[2]))
        if self.old_lab is not None:
            restore('lab running state', lambda: self.lab.call_method('SetRunning', args=(self.old_lab[0],)))
            restore('lab ticking', lambda: self.lab.set_actor_tick_enabled(self.old_lab[1]))
        if hasattr(self, 'report'):
            for source in self.manifest['sources']:
                try:
                    if sha256(Path(source['path'])) != source['sha256']:
                        failures.append('Saved source package changed: ' + source['path'])
                except Exception as exception:
                    failures.append('Saved source package check failed: ' + source['path'] + ': ' + str(exception))
            status = 'failed' if error or failures else 'complete'
            self.report.update(status=status, error=error, restore_errors=failures, restore_skipped=skipped,
                wall_seconds=time.monotonic()-self.started, package_sources_unchanged=not any(
                    'source package' in failure for failure in failures),
                gameplay_failed_runs=sum(1 for scenario in self.report['scenarios'] if scenario.get('failed', 0)),
                captured_png_count=len(self.manifest['frames']))
            self.manifest.update(status=status, error=error, restore_errors=failures,
                rendered_png_count=len(self.manifest['frames']), source_unchanged=self.report['package_sources_unchanged'])
            self.write()
            u.log('MC_NUT_DYNAMICS_CAPTURE ' + status + ' ' + str(error or self.output))
        elif error:
            u.log_error('MC_NUT_DYNAMICS_CAPTURE preflight failed: ' + error)


def start_capture():
    capture = NutDynamicsCapture.__new__(NutDynamicsCapture)
    builtins.mc_nut_dynamics_capture = capture
    try:
        capture.__init__()
    except Exception as error:
        capture.finish(str(error))
        raise
    print('Nut dynamics capture started: ' + str(capture.output))
    return capture


if globals().get('MC_NUT_DYNAMICS_RUN', False):
    start_capture()
elif globals().get('MC_NUT_DYNAMICS_INSPECT', False):
    print('MC_NUT_DYNAMICS_PREFLIGHT ' + json.dumps(inspect_runtime(), ensure_ascii=False))
