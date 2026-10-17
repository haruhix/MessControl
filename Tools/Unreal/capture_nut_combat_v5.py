"""Record Tank melee and four Wizard casts in an already running solo PIE lab.

Actual 30 Hz engine frames, default 768x576; no package saves or new gameplay.
Optional globals: MC_NUT_COMBAT_OUTPUT, MC_NUT_COMBAT_SIZE=(960,720),
MC_NUT_COMBAT_CASES=[(name, station_index, frames)]. Rain defaults to 7 seconds
to include its real 1.2s tell + 4s active spell + 1.4s recovery.
"""
import builtins
import json
import time
from pathlib import Path
import unreal as u

FPS = 30


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


class NutCombatCapture:
    def __init__(self):
        self.callback = self.capture = self.fill = self.target = self.component = None
        self.engine = self.perf = self.comparison = None
        self.old_engine = self.old_perf = self.old_mode = None
        self.old_viewer = self.pawn = None
        self.mesh_restore, self.station_restore, self.profile_restore = {}, {}, {}
        self.done = False
        self.started = time.monotonic()
        self.output = Path(globals().get('MC_NUT_COMBAT_OUTPUT',
            Path(u.Paths.project_dir(), 'Artifacts/NutCombatV5_20261010'))).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.size = tuple(globals().get('MC_NUT_COMBAT_SIZE', (768, 576)))
        self.report = dict(status='running', capture='Actual solo PIE SceneCapture2D',
            game_fps=FPS, video_fps=FPS, animation_mode='Native', size=list(self.size),
            camera='Fixed world yaw, close immediate boss follow; real teleport moves the camera with its boss',
            lighting='Existing lab lighting plus transient movable frontal fill, intensity 8',
            capture_scope='Selected station floor and actual owner-chain fixtures/effects only',
            scenarios=[], frames=[], restoration=[])
        worlds = u.EditorLevelLibrary.get_pie_worlds(False)
        if len(worlds) != 1:
            raise RuntimeError('Capture requires one already running solo PIE world')
        self.world = worlds[0]
        self.report['map'] = self.world.get_path_name()
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        self.pawn = self.pc.get_controlled_pawn()
        self.old_viewer = (self.pawn.get_actor_location(), self.pawn.get_actor_rotation(),
                           self.pc.get_control_rotation()) if self.pawn else None
        self.lab = u.GameplayStatics.get_all_actors_of_class(self.world, u.MCVFXLab)[0]
        subsystem_library = u.get_default_object(u.load_class(None, '/Script/Engine.SubsystemBlueprintLibrary'))
        self.comparison = subsystem_library.call_method('GetWorldSubsystem', args=(self.world, u.MCAnimationComparisonSubsystem))
        self.old_mode = self.comparison.call_method('GetMode')
        self.comparison.call_method('SetMode', args=(u.MCSecondaryAnimationMode.NATIVE,))
        self.engine = next(o for o in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate'))
        self.engine.set_editor_property('FixedFrameRate', float(FPS))
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.library = u.get_default_object(u.GameplayStatics)
        self.capture = self.spawn(u.SceneCapture2D, u.Transform())
        self.component = prop(self.capture, 'capture_component2d')
        self.target = u.RenderingLibrary.create_render_target2d(self.world, self.size[0], self.size[1],
            u.TextureRenderTargetFormat.RTF_RGBA8, u.LinearColor(.025, .03, .045, 1), False, False)
        for name, value in dict(texture_target=self.target, capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
            capture_every_frame=False, capture_on_movement=False, always_persist_rendering_state=True,
            projection_type=u.CameraProjectionMode.ORTHOGRAPHIC, ortho_width=1400.,
            primitive_render_mode=u.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():
            self.component.set_editor_property(name, value)
        self.component.set_editor_property('show_flag_settings', [u.EngineShowFlagsSetting(show_flag_name=name, enabled=False)
            for name in ('MotionBlur', 'Fog', 'VolumetricFog', 'Atmosphere')])
        self.fill = self.spawn(u.DirectionalLight, u.Transform())
        light = prop(self.fill, 'light_component')
        light.set_editor_property('mobility', u.ComponentMobility.MOVABLE)
        light.set_editor_property('intensity', 8.)
        light.set_editor_property('cast_shadows', False)
        self.cases = list(globals().get('MC_NUT_COMBAT_CASES', [('TankMelee', 0, 126),
            ('WizardFireball', 9, 126), ('WizardSummon', 10, 126),
            ('WizardRain', 11, 210), ('WizardTeleport', 12, 120)]))
        self.index, self.last_game_time = -1, None
        self.advance()
        self.callback = u.register_slate_post_tick_callback(self.tick)

    def spawn(self, cls, pose):
        actor = self.library.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, cls, pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.library.call_method('FinishSpawningActor', args=(actor, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        return actor

    def advance(self):
        self.index += 1
        if self.index >= len(self.cases):
            self.finish()
            return
        name, index, frames = self.cases[self.index]
        self.directory = self.output / name
        self.directory.mkdir(exist_ok=True)
        if any(self.directory.glob('*.png')):
            raise RuntimeError('Refusing to overwrite captured frames: ' + str(self.directory))
        self.pc.call_method('ServerSelectStation', args=(index,))
        self.station = prop(self.lab, 'results')[index].get_editor_property('station')
        override_profile = globals().get('MC_NUT_COMBAT_NUT_PROFILE')
        if override_profile:
            # EditorAssetLibrary refuses asset access during PIE; runtime
            # loading is required for this transient station override.
            profile = u.load_asset(override_profile)
            if not profile:
                raise RuntimeError('Missing review NutProfile: '+str(override_profile))
            if self.station not in self.profile_restore:
                self.profile_restore[self.station] = prop(self.station, 'nut_profile')
            self.station.set_editor_property('nut_profile', profile)
            self.report['review_profile'] = profile.get_path_name()
        if self.station not in self.station_restore:
            self.station_restore[self.station] = prop(self.station, 'cycle_seconds')
        # Preserve this same boss through the complete shot; its next cycle must not respawn it.
        self.station.set_editor_property('cycle_seconds', 120.)
        self.lab.call_method('ResetStation', args=(index,))
        self.frame, self.recording = 0, False
        self.boss = None
        self.wait_started = time.monotonic()
        self.report['scenarios'].append(dict(name=name, station=index, kind=str(prop(self.station, 'kind')),
            expected_frames=frames, frames=0, no_fixture_respawn=True))
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

    def frame_camera(self):
        start, target = prop(self.boss, 'locked_start'), prop(self.boss, 'locked_target')
        # Prioritize readable poses/hands: 400cm Mage is approximately 270px tall
        # at 768x576. Never zoom out to fit the entire Rain footprint or teleport.
        tank = self.cases[self.index][0] == 'TankMelee'
        aim = self.boss.get_actor_location() + u.Vector(80, 0, 130 if tank else 30)
        width = 1280. if tank else 1060.
        eye = aim + u.Vector(1100, -900, 540)
        rotation = u.MathLibrary.find_look_at_rotation(eye, aim)
        self.capture.set_actor_location_and_rotation(eye, rotation, False, True)
        self.fill.set_actor_location_and_rotation(eye, rotation, False, True)
        self.component.set_editor_property('ortho_width', width)
        self.report['scenarios'][-1].update(camera_aim_cm=xyz(aim), ortho_width_cm=width,
            locked_start_cm=xyz(start), locked_target_cm=xyz(target))

    def sample(self, game_time, actors):
        boss = self.boss
        model = prop(boss, 'tank_model' if self.cases[self.index][0] == 'TankMelee' else 'mage_model')
        mesh_asset = prop(model, 'skeletal_mesh_asset')
        bones = {}
        for bone in ('root_x', 'head_x', 'hand_l', 'hand_r', 'foot_l', 'foot_r'):
            if model and model.does_socket_exist(bone):
                bones[bone] = xyz(model.get_socket_location(bone))
        cues = [str(prop(prop(actor, 'cue'), 'type')) for actor in actors if isinstance(actor, u.MCNutCombatEffect)]
        return dict(scenario=self.cases[self.index][0], frame=self.frame+1, game_time=game_time,
            game_dt=float(u.GameplayStatics.get_world_delta_seconds(self.world)), boss=boss.get_path_name(),
            state=str(prop(boss, 'state')), attack=str(prop(boss, 'attack')),
            position_cm=xyz(boss.get_actor_location()), resolve_at=float(prop(boss, 'resolve_at', 0)),
            attack_end_at=float(prop(boss, 'attack_end_at', 0)), mesh=mesh_asset.get_path_name() if mesh_asset else None,
            pose_bones_cm=bones, active_cues=cues, owned_actor_count=len(actors))

    def tick(self, slate_dt):
        try:
            if time.monotonic()-self.started > 480:
                raise RuntimeError('Capture timeout')
            game_time = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and game_time <= self.last_game_time+.000001:
                return
            game_step = game_time-self.last_game_time if self.last_game_time is not None else None
            self.last_game_time = game_time
            boss, actors = self.fixture()
            if not boss:
                if time.monotonic()-self.wait_started > 25:
                    raise RuntimeError('Station did not spawn its real boss')
                return
            if self.recording and boss != self.boss:
                raise RuntimeError('Station replaced boss mid-shot')
            self.boss = boss
            self.component.clear_show_only_components()
            self.component.show_only_actor_components(prop(self.station, 'floor'))
            for actor in actors:
                self.component.show_only_actor_components(actor)
                for mesh in actor.get_components_by_class(u.SkeletalMeshComponent):
                    if mesh not in self.mesh_restore:
                        self.mesh_restore[mesh] = prop(mesh, 'visibility_based_anim_tick_option')
                    mesh.set_editor_property('visibility_based_anim_tick_option',
                                            u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            if not self.recording:
                if 'TELEGRAPH' not in str(prop(boss, 'state')).upper():
                    if time.monotonic()-self.wait_started > 25:
                        raise RuntimeError('Real attack never entered Telegraph')
                    return
                self.frame_camera()
                self.recording = True
                self.report['scenarios'][-1].update(boss=boss.get_path_name(), recording_start_game_time=game_time,
                    windup_remaining_seconds=float(prop(boss, 'resolve_at', game_time))-game_time)
            elif abs(game_step-1/FPS) > .001:
                raise RuntimeError('Nonconsecutive 30 Hz gameplay frame: %.6f' % game_step)
            self.frame_camera()
            sample = self.sample(game_time, actors)
            self.component.capture_scene()
            filename = '%04d.png' % (self.frame+1)
            u.RenderingLibrary.export_render_target(self.world, self.target, str(self.directory), filename)
            if not (self.directory/filename).is_file():
                raise RuntimeError('Missing captured PNG: ' + filename)
            self.report['frames'].append(sample)
            self.frame += 1
            scenario = self.report['scenarios'][-1]
            scenario.update(frames=self.frame, passed=int(prop(self.station, 'passed', 0)),
                failed=int(prop(self.station, 'failed', 0)), executions=int(prop(self.station, 'executions', 0)),
                recoveries=int(prop(self.station, 'recoveries', 0)), status=str(prop(self.station, 'status')))
            if self.frame == 1 or self.frame % FPS == 0:
                self.write()
            if self.frame >= self.cases[self.index][2]:
                samples = [item for item in self.report['frames'] if item['scenario'] == self.cases[self.index][0]]
                scenario['observed_states'] = sorted({item['state'] for item in samples})
                scenario['observed_cues'] = sorted({cue for item in samples for cue in item['active_cues']})
                self.advance()
        except Exception as error:
            self.finish(str(error))

    def write(self):
        (self.output/'Capture.json').write_text(json.dumps(self.report, indent=2), encoding='utf8')

    def finish(self, error=None):
        if getattr(self, 'done', False):
            return
        self.done = True
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        restore_errors = []
        def restore(label, operation):
            try:
                operation()
                self.report['restoration'].append(label)
            except Exception as exception:
                restore_errors.append(label+': '+str(exception))
        if self.comparison and self.old_mode is not None:
            restore('secondary animation mode', lambda: self.comparison.call_method('SetMode', args=(self.old_mode,)))
        for mesh, option in self.mesh_restore.items():
            if u.SystemLibrary.is_valid(mesh):
                restore('animation tick '+mesh.get_path_name(), lambda mesh=mesh, option=option:
                        mesh.set_editor_property('visibility_based_anim_tick_option', option))
        for station, cycle in self.station_restore.items():
            if u.SystemLibrary.is_valid(station):
                restore('station cycle '+station.get_name(), lambda station=station, cycle=cycle:
                        station.set_editor_property('cycle_seconds', cycle))
        for station, profile in self.profile_restore.items():
            if u.SystemLibrary.is_valid(station):
                restore('station profile '+station.get_name(), lambda station=station, profile=profile:
                        station.set_editor_property('nut_profile', profile))
        if self.target:
            restore('detach render target', lambda: self.component.set_editor_property('texture_target', None))
            restore('release render target', lambda: u.RenderingLibrary.release_render_target2d(self.target))
        for actor in (self.capture, self.fill):
            if actor and u.SystemLibrary.is_valid(actor):
                restore('destroy temporary '+actor.get_name(), actor.destroy_actor)
        if self.old_engine:
            restore('fixed-frame switch', lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]))
            restore('fixed frame rate', lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]))
        if self.old_perf is not None:
            restore('foreground throttle', lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_perf))
        if self.old_viewer and u.SystemLibrary.is_valid(self.pawn):
            restore('viewer position', lambda: self.pawn.set_actor_location_and_rotation(self.old_viewer[0], self.old_viewer[1], False, True))
            restore('viewer rotation', lambda: self.pc.set_control_rotation(self.old_viewer[2]))
        self.report.update(status='failed' if error or restore_errors else 'complete', error=error,
            restore_errors=restore_errors, wall_seconds=time.monotonic()-self.started)
        self.write()
        u.log('MC_NUT_COMBAT_CAPTURE '+self.report['status']+' '+str(error or self.output))


capture = NutCombatCapture.__new__(NutCombatCapture)
builtins.mc_nut_combat_capture = capture
try:
    capture.__init__()
except Exception as error:
    capture.finish(str(error))
    raise
print('Nut combat capture started: '+str(capture.output))
