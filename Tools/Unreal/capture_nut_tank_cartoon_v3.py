"""Record the actual Tank attack, shield hit and shield break in the PIE lab.

No packages save. Native mode and temporary capture/timing settings restore
afterward. Capture.json includes the existing station's gameplay counters.
"""
import builtins
import json
import time
from pathlib import Path
import unreal as u


class TankCapture:
    def __init__(self):
        self.callback = self.capture = self.fill = self.target = self.engine = self.perf = None
        self.old_engine = self.old_perf = self.comparison = None
        self.output = Path(globals().get('MC_TANK_CAPTURE_OUTPUT',
                           Path(u.Paths.project_dir(), 'Artifacts/TankCartoonV3_20261010_60fps_Fixed'))).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.report = dict(status='running', capture='Actual PIE SceneCapture2D', map='/Game/Tests/L_VFXLab',
                           game_fps=60, video_fps=60, animation_mode='Native', scenarios=[], frames=[],
                           lighting='Lab lighting plus a transient frontal fill shared by all three recordings')
        self.started = time.monotonic()
        self.world = u.EditorLevelLibrary.get_pie_worlds(False)[0]
        self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
        self.lab = u.GameplayStatics.get_all_actors_of_class(self.world, u.MCVFXLab)[0]
        library = u.get_default_object(u.load_class(None, '/Script/Engine.SubsystemBlueprintLibrary'))
        self.comparison = library.call_method('GetWorldSubsystem', args=(self.world, u.MCAnimationComparisonSubsystem))
        self.old_mode = self.comparison.call_method('GetMode')
        self.comparison.call_method('SetMode', args=(u.MCSecondaryAnimationMode.NATIVE,))
        self.engine = next(o for o in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (self.engine.get_editor_property('bUseFixedFrameRate'), self.engine.get_editor_property('FixedFrameRate'))
        self.engine.set_editor_property('FixedFrameRate', 60.)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = self.perf.get_editor_property('bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.library = u.get_default_object(u.GameplayStatics)
        self.capture = self.spawn(u.SceneCapture2D, u.Transform())
        self.component = self.capture.get_editor_property('capture_component2d')
        self.target = u.RenderingLibrary.create_render_target2d(self.world, 960, 720, u.TextureRenderTargetFormat.RTF_RGBA8,
                                                              u.LinearColor(.025, .03, .045, 1), False, False)
        for key, value in dict(texture_target=self.target, capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
                              capture_every_frame=False, capture_on_movement=False, always_persist_rendering_state=True,
                              projection_type=u.CameraProjectionMode.ORTHOGRAPHIC, ortho_width=1060.,
                              primitive_render_mode=u.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():
            self.component.set_editor_property(key, value)
        self.component.set_editor_property('show_flag_settings', [u.EngineShowFlagsSetting(show_flag_name=name, enabled=False)
                                          for name in ('MotionBlur', 'Fog', 'VolumetricFog', 'Atmosphere')])
        self.fill = self.spawn(u.DirectionalLight, u.Transform())
        light = self.fill.get_editor_property('light_component')
        light.set_editor_property('mobility', u.ComponentMobility.MOVABLE)
        light.set_editor_property('intensity', 8.)
        light.set_editor_property('cast_shadows', False)
        self.cases = [('Melee', 0, 360), ('ShieldHit', 5, 180), ('ShieldBreak', 6, 180)]
        self.index = -1
        self.advance()
        self.callback = u.register_slate_post_tick_callback(self.tick)

    def spawn(self, cls, pose):
        actor = self.library.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, cls, pose,
                 u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.library.call_method('FinishSpawningActor', args=(actor, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        return actor

    def advance(self):
        self.index += 1
        if self.index == len(self.cases):
            self.finish()
            return
        name, index, _ = self.cases[self.index]
        self.directory = self.output / name
        self.directory.mkdir(exist_ok=True)
        if any(self.directory.glob('*.png')):
            raise RuntimeError('Frames already exist: ' + str(self.directory))
        self.pc.call_method('ServerSelectStation', args=(index,))
        self.station = self.lab.get_editor_property('results')[index].get_editor_property('station')
        self.lab.call_method('ResetStation', args=(index,))
        self.base = self.station.get_actor_location()
        eye = self.base + u.Vector(1100, -900, 620)
        aim = self.base + u.Vector(110, 0, 180)
        rot = u.MathLibrary.find_look_at_rotation(eye, aim)
        self.capture.set_actor_location_and_rotation(eye, rot, False, True)
        self.fill.set_actor_location_and_rotation(eye, rot, False, True)
        self.frame = self.wait = 0
        self.boss = None
        self.report['scenarios'].append(dict(name=name, station=index))

    def tick(self, dt):
        try:
            if time.monotonic() - self.started > 240:
                raise RuntimeError('Capture timeout')
            self.wait += 1
            if self.wait < 4:
                return
            self.component.clear_show_only_components()
            self.component.show_only_actor_components(self.station.get_editor_property('floor'))
            actors = u.GameplayStatics.get_all_actors_of_class(self.world, u.MCNutBoss)
            bosses = [a for a in actors if (a.get_actor_location() - self.base).length() < 1300]
            for boss in bosses:
                self.component.show_only_actor_components(boss)
                for mesh in boss.get_components_by_class(u.SkeletalMeshComponent):
                    mesh.set_editor_property('visibility_based_anim_tick_option', u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            self.boss = bosses[0] if bosses else self.boss
            for cls in (u.MCToothCharacter, u.MCNutCombatEffect):
                for actor in u.GameplayStatics.get_all_actors_of_class(self.world, cls):
                    if (actor.get_actor_location() - self.base).length() < 1300:
                        self.component.show_only_actor_components(actor)
            self.component.capture_scene()
            filename = '%04d.png' % (self.frame + 1)
            u.RenderingLibrary.export_render_target(self.world, self.target, str(self.directory), filename)
            if not (self.directory / filename).is_file():
                raise RuntimeError('Missing captured frame: ' + filename)
            scenario = self.report['scenarios'][-1]
            scenario.update(passed=int(self.station.get_editor_property('passed')), failed=int(self.station.get_editor_property('failed')),
                            executions=int(self.station.get_editor_property('executions')), status=str(self.station.get_editor_property('status')))
            pose = {}
            if self.boss and u.SystemLibrary.is_valid(self.boss):
                for mesh in self.boss.get_components_by_class(u.SkeletalMeshComponent):
                    if mesh.get_name() == 'TankModel':
                        for bone in ('root_x', 'head_x', 'hand_r'):
                            p = mesh.get_socket_location(bone)
                            pose[bone] = [float(p.x), float(p.y), float(p.z)]
            self.report['frames'].append(dict(scenario=self.cases[self.index][0], frame=self.frame + 1,
                game_time=u.GameplayStatics.get_time_seconds(self.world), game_dt=u.GameplayStatics.get_world_delta_seconds(self.world),
                boss=self.boss.get_name() if self.boss and u.SystemLibrary.is_valid(self.boss) else None, pose_cm=pose))
            self.frame += 1
            if self.frame == 1:
                self.write()
            if self.frame >= self.cases[self.index][2]:
                self.advance()
        except Exception as error:
            self.finish(str(error))

    def write(self):
        (self.output / 'Capture.json').write_text(json.dumps(self.report, indent=2), encoding='utf8')

    def finish(self, error=None):
        if self.callback is not None:
            u.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        if self.comparison:
            self.comparison.call_method('SetMode', args=(self.old_mode,))
        if self.target:
            self.component.set_editor_property('texture_target', None)
            u.RenderingLibrary.release_render_target2d(self.target)
            self.target = None
        for actor in (self.capture, self.fill):
            if actor:
                actor.destroy_actor()
        self.capture = self.fill = None
        if self.old_engine:
            self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0])
            self.engine.set_editor_property('FixedFrameRate', self.old_engine[1])
        if self.old_perf is not None:
            self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_perf)
        self.report.update(status='failed' if error else 'complete', error=error, wall_seconds=time.monotonic() - self.started)
        self.write()
        u.log('MC_TANK_CARTOON_CAPTURE ' + self.report['status'] + ' ' + str(error or self.output))


capture = TankCapture.__new__(TankCapture)
builtins.mc_tank_cartoon_capture = capture
try:
    capture.__init__()
except Exception as error:
    capture.finish(str(error))
    raise
print('Tank capture started: ' + str(capture.output))
