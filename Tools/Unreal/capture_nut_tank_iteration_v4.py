"""Capture real, consecutive 60 Hz PIE frames; compatible with old/new Tank code.

Run in an already playing L_VFXLab through remote_python.py. Optional globals:
MC_TANK_ITERATION_OUTPUT, MC_TANK_ITERATION_VERSION (Before/After), and
MC_TANK_ITERATION_CASES [('Roll', 3, 480), ('Charge', 1, 300), ('ShieldBreak', 6, 180)].
No editor launch, package save, fabricated poses, or interpolated video frames.
"""
import builtins
import json
import math
import time
from pathlib import Path
import unreal as u


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


def quat_values(value):
    return [float(value.x), float(value.y), float(value.z), float(value.w)]


def quat_delta(current, previous):
    # Hamilton product current * conjugate(previous), hemisphere normalized.
    x, y, z, w = current
    a, b, c, d = (-previous[0], -previous[1], -previous[2], previous[3])
    delta = [w*a+x*d+y*c-z*b, w*b-x*c+y*d+z*a,
             w*c+x*b-y*a+z*d, w*d-x*a-y*b-z*c]
    return [-v for v in delta] if delta[3] < 0 else delta


class TankIterationCapture:
    def __init__(self):
        self.callback = self.capture = self.fill = self.target = self.component = None
        self.engine = self.perf = self.comparison = None
        self.old_engine = self.old_perf = self.old_mode = None
        self.old_viewer = self.pawn = None
        self.mesh_restore, self.station_restore = {}, {}
        self.done = False
        self.started = time.monotonic()
        self.output = Path(globals().get('MC_TANK_ITERATION_OUTPUT',
            Path(u.Paths.project_dir(), 'Artifacts/TankCartoonV4_20261010',
                 str(globals().get('MC_TANK_ITERATION_VERSION', 'After'))))).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.report = dict(status='running', capture='Actual PIE SceneCapture2D',
            version=str(globals().get('MC_TANK_ITERATION_VERSION', 'After')),
            game_fps=60, video_fps=60, animation_mode='Native',
            camera='World-fixed yaw, immediate follow of actual boss position; no camera lag',
            lighting='Existing lab lighting plus one transient movable frontal fill at intensity 8',
            capture_scope='Selected station floor and its real owned fixtures/effects only',
            scenarios=[], frames=[], restoration=[])
        self.world = u.EditorLevelLibrary.get_pie_worlds(False)[0]
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
        self.engine.set_editor_property('FixedFrameRate', 60.)
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.library = u.get_default_object(u.GameplayStatics)
        self.capture = self.spawn(u.SceneCapture2D, u.Transform())
        self.component = prop(self.capture, 'capture_component2d')
        self.target = u.RenderingLibrary.create_render_target2d(self.world, 960, 720,
            u.TextureRenderTargetFormat.RTF_RGBA8, u.LinearColor(.025, .03, .045, 1), False, False)
        for name, value in dict(texture_target=self.target, capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
            capture_every_frame=False, capture_on_movement=False, always_persist_rendering_state=True,
            projection_type=u.CameraProjectionMode.ORTHOGRAPHIC, ortho_width=1200.,
            primitive_render_mode=u.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():
            self.component.set_editor_property(name, value)
        self.component.set_editor_property('show_flag_settings', [u.EngineShowFlagsSetting(show_flag_name=name, enabled=False)
            for name in ('MotionBlur', 'Fog', 'VolumetricFog', 'Atmosphere')])
        self.fill = self.spawn(u.DirectionalLight, u.Transform())
        light = prop(self.fill, 'light_component')
        light.set_editor_property('mobility', u.ComponentMobility.MOVABLE)
        light.set_editor_property('intensity', 8.)
        light.set_editor_property('cast_shadows', False)
        self.cases = list(globals().get('MC_TANK_ITERATION_CASES',
                          [('Roll', 3, 480), ('Charge', 1, 300), ('ShieldBreak', 6, 180)]))
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
        if self.station not in self.station_restore:
            self.station_restore[self.station] = prop(self.station, 'cycle_seconds')
        # FinishCycle keeps this SAME boss alive; avoid its next cleanup/reset during recording.
        self.station.set_editor_property('cycle_seconds', 120.)
        self.lab.call_method('ResetStation', args=(index,))
        self.frame, self.recording = 0, False
        self.boss = self.previous_sample = None
        self.wait_started = time.monotonic()
        self.report['scenarios'].append(dict(name=name, station=index, kind=str(prop(self.station, 'kind')),
            expected_frames=frames, frames=0, same_fixture_retained_after_pass=True))

    def owns(self, actor):
        visited = set()
        while actor and actor not in visited:
            if actor == self.station:
                return True
            visited.add(actor)
            actor = actor.get_owner()
        return False

    def get_boss(self):
        boss = prop(self.station, 'nut_boss')
        if boss and u.SystemLibrary.is_valid(boss):
            return boss
        return next((a for a in u.GameplayStatics.get_all_actors_of_class(self.world, u.MCNutBoss)
                     if self.owns(a)), None)

    def sample(self, game_time):
        boss = self.boss
        ball, model, shield = (prop(boss, name) for name in ('tank_ball', 'tank_model', 'shield'))
        location = xyz(boss.get_actor_location())
        forward = xyz(prop(boss, 'attack_forward', boss.get_actor_forward_vector()))
        sample = dict(scenario=self.cases[self.index][0], frame=self.frame + 1, game_time=game_time,
            game_dt=float(u.GameplayStatics.get_world_delta_seconds(self.world)), boss=boss.get_path_name(),
            state=str(prop(boss, 'state')), attack=str(prop(boss, 'attack')), position_cm=location,
            actor_yaw_deg=float(boss.get_actor_rotation().yaw), attack_forward=forward,
            shield_hp=float(prop(boss, 'shield_health', -1)), shield_broken_at=prop(boss, 'shield_broken_at'),
            shield_component_visible=bool(shield.is_visible()) if shield else None,
            tank_mesh=str(prop(model, 'skeletal_mesh_asset').get_path_name()) if model and prop(model, 'skeletal_mesh_asset') else None,
            ball_visible=bool(ball.is_visible()) if ball else None)
        if model:
            sample['material_sections_shown_lod0'] = [bool(model.call_method('IsMaterialSectionShown', args=(i, 0)))
                                                    for i in range(model.get_num_materials())]
        if ball:
            sample['ball_world_quat_xyzw'] = quat_values(ball.call_method('K2_GetComponentToWorld').rotation)
            sample['ball_relative_quat_xyzw'] = quat_values(ball.get_relative_transform().rotation)
            rotator = ball.get_relative_transform().rotation.rotator()
            sample['ball_relative_pitch_yaw_roll_deg'] = [float(rotator.pitch), float(rotator.yaw), float(rotator.roll)]
        previous = self.previous_sample
        if previous:
            displacement = [location[i]-previous['position_cm'][i] for i in range(3)]
            length = math.hypot(displacement[0], displacement[1])
            sample['travel_cm'] = length
            sample['forward_travel_cm'] = sum(displacement[i]*forward[i] for i in range(2))
            if ball and sample['ball_visible'] and previous.get('ball_visible') and length > .001:
                delta = quat_delta(sample['ball_world_quat_xyzw'], previous['ball_world_quat_xyzw'])
                axis = [-displacement[1]/length, displacement[0]/length, 0.]
                projected = sum(delta[i]*axis[i] for i in range(3))
                sample['world_roll_about_up_cross_motion_deg'] = math.degrees(2*math.atan2(projected, delta[3]))
                relative_delta = quat_delta(sample['ball_relative_quat_xyzw'], previous['ball_relative_quat_xyzw'])
                # UE FRotator positive Pitch is the negative right-handed quaternion Y rotation.
                sample['signed_relative_pitch_delta_deg'] = -math.degrees(2*math.atan2(relative_delta[1], relative_delta[3]))
                prior_motion = previous.get('motion_direction_xy')
                sample['motion_direction_xy'] = [displacement[0]/length, displacement[1]/length]
                if prior_motion:
                    dot = max(-1., min(1., sum(a*b for a, b in zip(prior_motion, sample['motion_direction_xy']))))
                    sample['turn_angle_deg'] = math.degrees(math.acos(dot))
        self.previous_sample = sample
        return sample

    def tick(self, slate_dt):
        try:
            if time.monotonic()-self.started > 600:
                raise RuntimeError('Capture timeout')
            game_time = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and game_time <= self.last_game_time + .000001:
                return  # Extra Slate callback is not another gameplay frame.
            game_step = game_time-self.last_game_time if self.last_game_time is not None else None
            self.last_game_time = game_time
            boss = self.get_boss()
            if not boss:
                if time.monotonic()-self.wait_started > 30:
                    raise RuntimeError('Station did not spawn its real boss')
                return
            if self.recording and boss != self.boss:
                raise RuntimeError('Station reset/replaced boss while recording')
            self.boss = boss
            self.component.clear_show_only_components()
            self.component.show_only_actor_components(prop(self.station, 'floor'))
            for cls in (u.MCNutBoss, u.MCToothCharacter, u.MCNutCombatEffect):
                for actor in u.GameplayStatics.get_all_actors_of_class(self.world, cls):
                    if not self.owns(actor):
                        continue
                    self.component.show_only_actor_components(actor)
                    for mesh in actor.get_components_by_class(u.SkeletalMeshComponent):
                        if mesh not in self.mesh_restore:
                            self.mesh_restore[mesh] = prop(mesh, 'visibility_based_anim_tick_option')
                        mesh.set_editor_property('visibility_based_anim_tick_option',
                                                u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            # Follow translation immediately, retaining identical world yaw/pitch for all runs.
            aim = boss.get_actor_location() + u.Vector(0, 0, 30)
            eye = aim + u.Vector(1100, -900, 440)
            rotation = u.MathLibrary.find_look_at_rotation(eye, aim)
            self.capture.set_actor_location_and_rotation(eye, rotation, False, True)
            self.fill.set_actor_location_and_rotation(eye, rotation, False, True)
            if not self.recording:
                name = self.cases[self.index][0]
                state = str(prop(boss, 'state')).upper()
                if name == 'ShieldBreak':
                    settings = prop(boss, 'boss_settings')
                    entrance_seconds = float(prop(settings, 'entrance_seconds', 1.3))
                    remaining = entrance_seconds-(game_time-float(prop(boss, 'state_started_at', game_time)))
                    ready = ('FALLING' in state and remaining <= .20) or ('IDLE' in state and prop(boss, 'shield_health', 0) > 0)
                else:
                    ready = 'TELEGRAPH' in state
                if not ready:
                    return
                self.recording = True
                self.report['scenarios'][-1]['boss'] = boss.get_path_name()
                self.report['scenarios'][-1]['recording_start_game_time'] = game_time
            elif abs(game_step-1/60) > .001:
                raise RuntimeError('Nonconsecutive 60 Hz game step: %.6f; refusing synthetic/duplicate frames' % game_step)
            sample = self.sample(game_time)
            self.component.capture_scene()
            filename = '%04d.png' % (self.frame + 1)
            u.RenderingLibrary.export_render_target(self.world, self.target, str(self.directory), filename)
            if not (self.directory / filename).is_file():
                raise RuntimeError('Missing captured frame: ' + filename)
            self.report['frames'].append(sample)
            self.frame += 1
            scenario = self.report['scenarios'][-1]
            scenario.update(frames=self.frame, passed=int(prop(self.station, 'passed', 0)),
                failed=int(prop(self.station, 'failed', 0)), executions=int(prop(self.station, 'executions', 0)),
                recoveries=int(prop(self.station, 'recoveries', 0)), status=str(prop(self.station, 'status')))
            if sample.get('turn_angle_deg', 0) > 35 and 'turn_snapshot' not in scenario:
                scenario['turn_snapshot'] = str(self.directory / filename)
            if self.frame == 1 or self.frame % 60 == 0:
                self.write()
            if self.frame >= self.cases[self.index][2]:
                samples = [item for item in self.report['frames'] if item['scenario'] == self.cases[self.index][0]]
                scenario['metrics'] = dict(travel_cm=sum(item.get('travel_cm', 0) for item in samples),
                    signed_relative_pitch_deg=sum(item.get('signed_relative_pitch_delta_deg', 0) for item in samples),
                    signed_world_roll_about_motion_deg=sum(item.get('world_roll_about_up_cross_motion_deg', 0) for item in samples),
                    direction_changes=sum(item.get('turn_angle_deg', 0) > 35 for item in samples),
                    observed_states=sorted(set(item['state'] for item in samples)),
                    shield_hp_before=samples[0]['shield_hp'], shield_hp_after=samples[-1]['shield_hp'],
                    tank_mesh_before=samples[0]['tank_mesh'], tank_mesh_after=samples[-1]['tank_mesh'],
                    mesh_swapped=samples[0]['tank_mesh'] != samples[-1]['tank_mesh'],
                    shield_component_visible_after=samples[-1]['shield_component_visible'],
                    material_sections_shown_after=samples[-1].get('material_sections_shown_lod0'))
                self.write()
                self.advance()
        except Exception as error:
            self.finish(str(error))

    def write(self):
        (self.output / 'Capture.json').write_text(json.dumps(self.report, indent=2), encoding='utf8')

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
                restore_errors.append(label + ': ' + str(exception))
        if self.comparison and self.old_mode is not None:
            restore('secondary animation mode', lambda: self.comparison.call_method('SetMode', args=(self.old_mode,)))
        for mesh, option in self.mesh_restore.items():
            if u.SystemLibrary.is_valid(mesh):
                restore('animation tick ' + mesh.get_path_name(), lambda mesh=mesh, option=option:
                        mesh.set_editor_property('visibility_based_anim_tick_option', option))
        for station, cycle in self.station_restore.items():
            if u.SystemLibrary.is_valid(station):
                restore('station cycle duration ' + station.get_name(), lambda station=station, cycle=cycle:
                        station.set_editor_property('cycle_seconds', cycle))
        if self.target:
            restore('detach render target', lambda: self.component.set_editor_property('texture_target', None))
            restore('release render target', lambda: u.RenderingLibrary.release_render_target2d(self.target))
        for actor in (self.capture, self.fill):
            if actor and u.SystemLibrary.is_valid(actor):
                restore('destroy temporary ' + actor.get_name(), actor.destroy_actor)
        if self.old_engine:
            restore('fixed-frame switch', lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]))
            restore('fixed frame rate', lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]))
        if self.old_perf is not None:
            restore('editor foreground throttle', lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_perf))
        if self.old_viewer and u.SystemLibrary.is_valid(self.pawn):
            restore('viewer position', lambda: self.pawn.set_actor_location_and_rotation(self.old_viewer[0], self.old_viewer[1], False, True))
            restore('viewer rotation', lambda: self.pc.set_control_rotation(self.old_viewer[2]))
        self.report.update(status='failed' if error or restore_errors else 'complete', error=error,
            restore_errors=restore_errors, wall_seconds=time.monotonic()-self.started,
            retained_fixture_note='No station reset during footage. Last cycle retains its current boss until normal station activation/reset.')
        self.write()
        u.log('MC_TANK_ITERATION_CAPTURE ' + self.report['status'] + ' ' + str(error or self.output))


capture = TankIterationCapture.__new__(TankIterationCapture)
builtins.mc_tank_iteration_capture = capture
try:
    capture.__init__()
except Exception as error:
    capture.finish(str(error))
    raise
print('Tank iteration capture started: ' + str(capture.output))
