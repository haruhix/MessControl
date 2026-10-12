"""Short, owned solo PIE check of the approved wave/throwable-chili behavior.

Run this file in the idle MessControl Editor with L_Mouth already open. It never
saves assets, changes maps, or touches an existing PIE session. The real first
perk is selected through the player's menu; F3 SpicyPepper creates the real food.
The normal player picks up and throws that pepper shortly before its timer ends.

Results: Artifacts/ApprovedCombatSolo_<unix-seconds>/Combat.json and genuine
30-fps SceneCapture PNG sequences (StandingWave, JumpWave, ChiliThrow).
The clips can be encoded externally with ffmpeg -framerate 30 -i %04d.png.
Optional globals: MC_APPROVED_COMBAT_OUTPUT, MC_APPROVED_COMBAT_CAPTURE=False,
MC_APPROVED_COMBAT_SIZE=(768,576), MC_APPROVED_COMBAT_RUN=False (import only).
Cancel through builtins.mc_approved_combat_solo.finish('cancelled').
"""
import builtins
import json
import time
import traceback
from pathlib import Path

import unreal as u

KEY = 'mc_approved_combat_solo'
FPS = 60
VIDEO_FPS = 30
PLAY_SETTINGS = dict(playNetMode='PIE_Standalone', playNumberOfClients=1,
                     runUnderOneProcess=True, bLaunchSeparateServer=False,
                     newWindowWidth=1280, newWindowHeight=720,
                     clientFixedFPS=[FPS], serverFixedFPS=0)


def prop(obj, name, default=None):
    native = ''.join(part[:1].upper()+part[1:] for part in name.split('_'))
    for key in (name, native):
        try:
            return obj.get_editor_property(key)
        except Exception:
            pass
    return getattr(obj, name, default)


def set_prop(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception:
        native = ''.join(part[:1].upper()+part[1:] for part in name.split('_'))
        obj.set_editor_property(native, value)


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


class ApprovedCombatSolo:
    def __init__(self):
        self.callback = self.world = self.hero = self.pc = self.gs = None
        self.capture = self.target = self.component = self.pepper = self.wave = None
        self.engine = self.perf = self.play = None
        self.pie_start = None
        self.old_play = self.old_engine = self.old_perf = None
        self.created = []
        self.done = self.owns_pie = False
        self.phase = 'waiting_pie'
        self.started = time.monotonic()
        self.phase_wall_at = self.started
        self.last_time = None
        self.video_clock = 0.
        self.clip = None
        self.frame = 0
        self.walking_since = None
        self.capture_enabled = bool(globals().get('MC_APPROVED_COMBAT_CAPTURE', True))
        self.size = tuple(globals().get('MC_APPROVED_COMBAT_SIZE', (768, 576)))
        self.output = Path(globals().get('MC_APPROVED_COMBAT_OUTPUT',
            Path(u.Paths.project_dir(), 'Artifacts', 'ApprovedCombatSolo_'+str(int(time.time()))))).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.report = dict(status='running', scope='Real solo PIE native gameplay, no network or broad automation tests',
            game_fps=FPS, video_fps=VIDEO_FPS, checks={}, samples=[], clips={}, restoration=[],
            capture='Actual SceneCapture2D: tongue, player, real chili and native hazard/effect actors; mouth walls hidden for visibility',
            fixture_changes='Player/pepper moved to a supported central tongue position; no synthetic detonation or jump height',
            condition_isolation='Wait for actual Standing recovery, then continuous Walking for 0.8s before each fixture; never force body state',
            assets_saved=False)
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        if self.levels.is_in_play_in_editor() or u.EditorLevelLibrary.get_pie_worlds(True):
            raise RuntimeError('Stop existing PIE first; this script owns and restores only its own solo session')
        self.editor_world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
        if '/Game/Maps/L_Mouth' not in self.editor_world.get_path_name():
            raise RuntimeError('Open L_Mouth first; the check does not replace the open map or unsaved edits')
        self.play = u.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
        self.old_play = json.loads(u.ToolsetLibrary.get_object_properties(self.play, list(PLAY_SETTINGS)))
        self.engine = next(o for o in u.ObjectIterator(u.load_class(None, '/Script/Engine.Engine'))
                           if o.get_path_name().startswith('/Engine/Transient.'))
        self.old_engine = (prop(self.engine, 'bUseFixedFrameRate'), prop(self.engine, 'FixedFrameRate'))
        self.perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
        self.old_perf = prop(self.perf, 'bThrottleCPUWhenNotForeground')
        self.library = u.get_default_object(u.GameplayStatics)
        self.write()

    def write(self):
        (self.output/'Combat.json').write_text(json.dumps(self.report, ensure_ascii=False,
            indent=2, allow_nan=False), encoding='utf-8')

    def start(self):
        if not u.ToolsetLibrary.set_object_properties(self.play, json.dumps(PLAY_SETTINGS)):
            raise RuntimeError('Could not apply solo PIE settings')
        self.engine.set_editor_property('FixedFrameRate', float(FPS))
        self.engine.set_editor_property('bUseFixedFrameRate', True)
        self.perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
        self.callback = u.register_slate_post_tick_callback(self.tick)
        self.owns_pie = True
        # Explicit engine request opens a floating game viewport. The subsystem
        # EditorRequestBeginPlay always attaches to the active editor viewport.
        self.pie_start = u.ToolsetRegistry.execute_tool(
            'EditorToolset.EditorAppToolset', 'StartPIE',
            json.dumps({'Options': {'bSimulate': False, 'PlayMode': 1, 'WarmupSeconds': 0.}}))
        if self.pie_start is None:
            raise RuntimeError('StartPIE returned no async result')
        if self.pie_start.get_editor_property('Error'):
            raise RuntimeError('StartPIE: '+str(self.pie_start.get_editor_property('Error')))
        u.log('MC_APPROVED_COMBAT_SOLO_STARTED '+str(self.output))

    def set_phase(self, phase):
        self.phase = phase
        self.phase_at = u.GameplayStatics.get_time_seconds(self.world)
        self.phase_wall_at = time.monotonic()
        self.walking_since = None
        self.write()

    def spawn(self, cls, pose, properties=None):
        actor = self.library.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, cls, pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        if not actor:
            raise RuntimeError('Deferred spawn failed for '+str(cls))
        self.created.append(actor)
        for name, value in (properties or {}).items():
            set_prop(actor, name, value)
        self.library.call_method('FinishSpawningActor', args=(actor, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        return actor

    def floor_at(self, point):
        ignored = [self.hero]
        for cls in (u.MCFoodActor, u.MCArenaTooth, u.MCThroat):
            ignored.extend(u.GameplayStatics.get_all_actors_of_class(self.world, cls))
        result = u.SystemLibrary.line_trace_single(self.world, point+u.Vector(0,0,1500),
            point-u.Vector(0,0,1200), u.TraceTypeQuery.TRACE_TYPE_QUERY1,
            True, ignored, u.DrawDebugTrace.NONE, True)
        # Current engine Python returns HitResult or None; accept tuple wrappers
        # as well without guessing an impact when the supporting trace misses.
        if isinstance(result, tuple):
            result = next((item for item in result if hasattr(item, 'to_tuple')), None)
        # UE 5.8's native BreakHitResult output is the reliable Python surface:
        # BlockingHit at 0, ImpactPoint at 5 (see audit_tongue_surface.py).
        fields = result.to_tuple() if result is not None else None
        if not fields or not fields[0]:
            raise RuntimeError('No supporting floor beneath '+str(xyz(point)))
        return fields[5]

    def health(self):
        return float(prop(prop(self.hero, 'status'), 'state').get_editor_property('health'))

    def body_state(self):
        physics = prop(self.hero, 'tooth_physics')
        if not valid(physics):
            raise RuntimeError('Player has no live ToothPhysics component')
        return physics.call_method('GetBodyState')

    def standing(self):
        return self.body_state() == u.MCBodyState.STANDING

    def settled_walking(self, now):
        ready = self.standing() and prop(self.move, 'movement_mode') == u.MovementMode.MOVE_WALKING
        if not ready:
            self.walking_since = None
            return False
        if self.walking_since is None:
            self.walking_since = now
        return now-self.walking_since >= .8

    def place_player(self):
        if not self.standing():
            raise RuntimeError('Reposition attempted before actual Standing recovery')
        self.hero.set_primary_input_held(False)
        self.hero.set_handle_input_held(False)
        self.hero.set_jump_input_held(False)
        self.move.stop_movement_immediately()
        self.hero.set_actor_location_and_rotation(self.base+u.Vector(0,0,self.half+3),
            u.Rotator(0,0,0), False, True)
        self.move.set_movement_mode(u.MovementMode.MOVE_FALLING)

    def new_wave(self, jump):
        if not self.standing() or prop(self.move, 'movement_mode') != u.MovementMode.MOVE_WALKING:
            raise RuntimeError('Wave fixture requires an actually Standing, Walking player')
        self.before_health = self.health()
        self.jump_called = False
        self.jump_peak = 0.
        self.wave = self.spawn(u.MCHazardWave, u.Transform(location=self.base+u.Vector(-260,0,5)),
            dict(max_radius=550., damage=12., warning_seconds=.35, travel_seconds=.8,
                 clear_height=12., b_spicy=False))
        self.wave_at = u.GameplayStatics.get_time_seconds(self.world)
        self.report.setdefault('condition_start_states', {})['JumpWave' if jump else 'StandingWave'] = dict(
            game_time=self.wave_at, body_state=str(self.body_state()),
            movement_mode=str(prop(self.move, 'movement_mode')), health=self.before_health)
        self.start_clip('JumpWave' if jump else 'StandingWave')
        self.set_phase('jump_wave' if jump else 'standing_wave')

    def start_clip(self, name):
        self.clip, self.frame, self.video_clock = name, 0, 0.
        if self.capture_enabled:
            directory = self.output/name
            directory.mkdir(exist_ok=True)
            if any(directory.glob('*.png')):
                raise RuntimeError('Refusing to overwrite existing frames in '+str(directory))
            self.report['clips'][name] = dict(directory=str(directory), fps=VIDEO_FPS, frame_count=0,
                frames=[], genuine_engine_frames=True)

    def setup_capture(self):
        if not self.capture_enabled:
            return
        self.capture = self.spawn(u.SceneCapture2D, u.Transform())
        self.component = prop(self.capture, 'capture_component2d')
        self.target = u.RenderingLibrary.create_render_target2d(self.world, self.size[0], self.size[1],
            u.TextureRenderTargetFormat.RTF_RGBA8, u.LinearColor(.025,.03,.045,1), False, False)
        for name, value in dict(texture_target=self.target, capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
            capture_every_frame=False, capture_on_movement=False, always_persist_rendering_state=True,
            projection_type=u.CameraProjectionMode.ORTHOGRAPHIC, ortho_width=950.,
            primitive_render_mode=u.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():
            self.component.set_editor_property(name, value)
        self.component.set_editor_property('show_flag_settings', [u.EngineShowFlagsSetting(show_flag_name=name, enabled=False)
            for name in ('MotionBlur', 'Fog', 'VolumetricFog', 'Atmosphere')])

    def capture_frame(self, now, dt):
        if not self.capture_enabled or not self.clip:
            return
        self.video_clock += dt
        if self.video_clock+1.e-5 < 1./VIDEO_FPS:
            return
        self.video_clock -= 1./VIDEO_FPS
        aim = self.base+u.Vector(40,0,120)
        eye = aim+u.Vector(-430,-650,300)
        self.capture.set_actor_location_and_rotation(eye, u.MathLibrary.find_look_at_rotation(eye, aim), False, True)
        self.component.clear_show_only_components()
        for cls in (u.MCTongue, u.MCToothCharacter, u.MCFoodActor, u.MCHazardWave, u.MCReactionVFX, u.MCFirePatch):
            for actor in u.GameplayStatics.get_all_actors_of_class(self.world, cls):
                self.component.show_only_actor_components(actor)
        self.component.capture_scene()
        self.frame += 1
        filename = '%04d.png' % self.frame
        directory = self.output/self.clip
        u.RenderingLibrary.export_render_target(self.world, self.target, str(directory), filename)
        if not (directory/filename).is_file():
            raise RuntimeError('Missing genuine rendered frame '+str(directory/filename))
        self.report['clips'][self.clip]['frame_count'] = self.frame
        self.report['clips'][self.clip]['frames'].append(dict(file=str(directory/filename), time=now))
        if self.frame == 1 or self.frame % VIDEO_FPS == 0:
            self.write()

    def tick(self, _delta):
        try:
            if self.pie_start is not None and self.pie_start.get_editor_property('Error'):
                raise RuntimeError('StartPIE: '+str(self.pie_start.get_editor_property('Error')))
            if time.monotonic()-self.started > 150:
                raise RuntimeError('Solo behavior check timed out')
            worlds = u.EditorLevelLibrary.get_pie_worlds(False)
            if not worlds:
                if self.world:
                    raise RuntimeError('Owned PIE ended before the check completed')
                return
            if len(worlds) != 1 or not u.SystemLibrary.is_standalone(worlds[0]):
                raise RuntimeError('Expected exactly one standalone PIE world')
            self.world = worlds[0]
            now = u.GameplayStatics.get_time_seconds(self.world)
            if self.last_time is not None and now <= self.last_time:
                return
            dt = 1./FPS if self.last_time is None else now-self.last_time
            self.last_time = now
            if self.phase == 'waiting_pie':
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                self.hero = self.pc.get_controlled_pawn() if self.pc else None
                self.gs = u.GameplayStatics.get_game_state(self.world)
                if not self.hero or not self.gs or not isinstance(self.hero, u.MCToothCharacter):
                    return
                if prop(self.gs, 'b_tutorial_active', False) or prop(self.gs, 'b_lobby_waiting', False):
                    raise RuntimeError('Use the normal map with tutorial/lobby disabled for this solo check')
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.report['first_perk_selected_through_real_menu'] = True
                    return
                director = prop(self.gs, 'single_day_director')
                if valid(director) and any(token in str(prop(director, 'stage')).upper() for token in ('TRAINING', 'FIRST_PERK', 'FIRSTPERK')):
                    return
                if valid(director):
                    # Manual-event state does not suspend this owner's opening
                    # pause. Pause only our PIE director before wave fixtures.
                    director.set_actor_tick_enabled(False)
                    self.report['solo_director_paused_for_isolation'] = director.get_path_name()
                self.gs.set_editor_property('bDevManualEvents', True)
                self.gs.set_editor_property('PhaseEndsAt', 0.)
                self.move = prop(self.hero, 'character_movement')
                self.half = prop(self.hero, 'capsule_component').get_scaled_capsule_half_height()
                self.base = self.floor_at(u.Vector(-100,0,0))
                self.report.update(map=self.world.get_path_name(), player=self.hero.get_path_name(), supported_floor_cm=xyz(self.base))
                prop(self.hero, 'mesh').set_editor_property('visibility_based_anim_tick_option',
                    u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
                self.setup_capture()
                self.set_phase('wait_recover_standing')
                return
            age = now-self.phase_at
            sample = dict(phase=self.phase, game_time=now, health=self.health(), player_cm=xyz(self.hero.get_actor_location()),
                body_state=str(self.body_state()), movement_mode=str(prop(self.move, 'movement_mode')),
                wave_hit_count=int(prop(self.wave, 'hit_count', 0)) if valid(self.wave) else None)
            if valid(self.pepper):
                sample.update(pepper_cm=xyz(self.pepper.get_actor_location()), pepper_phase=str(prop(self.pepper, 'phase')),
                    fuse_ends_at=float(prop(self.pepper, 'fuse_ends_at', 0)))
            self.report['samples'].append(sample)
            self.capture_frame(now, dt)
            if self.phase in ('wait_recover_standing', 'wait_recover_jump', 'wait_recover_chili'):
                if self.health() <= 0.:
                    raise RuntimeError('Player died while waiting for natural recovery')
                if age > 20.:
                    raise RuntimeError('Player did not naturally recover to Standing within 20 game seconds')
                if self.standing():
                    self.report.setdefault('natural_recovery_waits', []).append(dict(
                        phase=self.phase, waited_game_seconds=age, completed_game_time=now,
                        body_state=str(self.body_state()), movement_mode=str(prop(self.move, 'movement_mode'))))
                    destination = {'wait_recover_standing': 'settle_standing',
                                   'wait_recover_jump': 'settle_jump',
                                   'wait_recover_chili': 'settle_chili'}[self.phase]
                    self.place_player()
                    self.set_phase(destination)
            elif self.phase in ('settle_standing', 'settle_jump', 'settle_chili') and age > 10.:
                raise RuntimeError('Player did not settle in actual Standing/Walking before fixture')
            elif self.phase == 'settle_standing' and self.settled_walking(now):
                self.new_wave(False)
            elif self.phase == 'standing_wave' and age >= 1.45:
                delta = self.before_health-self.health()
                self.report['checks']['standing_pain_wave'] = dict(passed=abs(delta-12.)<.01,
                    health_before=self.before_health, health_after=self.health(), damage=delta)
                self.clip = None
                self.set_phase('wait_recover_jump')
            elif self.phase == 'settle_jump' and self.settled_walking(now):
                self.new_wave(True)
            elif self.phase == 'jump_wave':
                if age >= .48 and not self.jump_called:
                    if not self.standing() or prop(self.move, 'movement_mode') != u.MovementMode.MOVE_WALKING:
                        raise RuntimeError('Jump request contaminated by a non-standing/non-walking player')
                    self.hero.set_jump_input_held(True)
                    self.jump_called = True
                    self.report['jump_input_game_time'] = now
                if age >= .68:
                    self.hero.set_jump_input_held(False)
                self.jump_peak = max(self.jump_peak, float(self.hero.get_actor_location().z)-self.half-float(self.base.z))
                if age >= 1.45:
                    delta = self.before_health-self.health()
                    self.report['checks']['jump_pain_wave'] = dict(passed=abs(delta)<.01 and self.jump_peak>12,
                        health_before=self.before_health, health_after=self.health(), damage=delta, peak_sole_clearance_cm=self.jump_peak)
                    self.clip = None
                    self.set_phase('wait_recover_chili')
            elif self.phase == 'settle_chili' and self.settled_walking(now):
                self.report.setdefault('condition_start_states', {})['ChiliPickup'] = dict(
                    game_time=now, body_state=str(self.body_state()),
                    movement_mode=str(prop(self.move, 'movement_mode')), health=self.health())
                self.before_food = {actor.get_path_name() for actor in u.GameplayStatics.get_all_actors_of_class(self.world, u.MCFoodActor)}
                self.pc.request_dev_action(u.MCDevAction.SPICY_PEPPER, -1)
                self.set_phase('find_pepper')
            elif self.phase == 'find_pepper':
                self.pepper = next((actor for actor in u.GameplayStatics.get_all_actors_of_class(self.world, u.MCFoodActor)
                    if actor.get_path_name() not in self.before_food and str(prop(actor, 'item_name')) == 'SpicyPepper'), None)
                if self.pepper:
                    self.created.append(self.pepper)
                    self.pepper.set_actor_location_and_rotation(self.base+u.Vector(78,0,45), u.Rotator(0,0,0), False, True)
                    body = prop(self.pepper, 'body')
                    body.set_physics_linear_velocity(u.Vector(0,0,0), False, 'None')
                    self.hero.get_editor_property('inventory').server_select(u.MCToolSlot.BRUSH)
                    self.hero.set_primary_input_held(True)
                    self.set_phase('carry_pepper')
                elif age > 3:
                    raise RuntimeError('F3 SpicyPepper did not create a real pepper')
            elif self.phase == 'carry_pepper':
                held = prop(self.hero, 'held_food') == self.pepper
                deadline = float(prop(self.pepper, 'fuse_ends_at', 0))
                if not held and age > 2.5:
                    raise RuntimeError('The real primary-input pickup did not acquire chili')
                if held and deadline > 0:
                    self.report['pickup'] = dict(held=True, phase=str(prop(self.pepper, 'phase')), fuse_ends_at=deadline)
                    self.deadline = deadline
                    self.thrown = False
                    self.set_phase('chili_timer')
            elif self.phase == 'chili_timer':
                remaining = self.deadline-now
                if remaining <= 2.8 and self.clip is None:
                    self.start_clip('ChiliThrow')
                if remaining <= .24 and not self.thrown:
                    if prop(self.hero, 'held_food') != self.pepper:
                        raise RuntimeError('Chili was dropped before the late timer throw')
                    self.hero.throw_item()
                    self.hero.set_primary_input_held(False)
                    self.thrown = True
                    self.report['throw'] = dict(game_time=now, remaining_seconds=remaining,
                        fuse_ends_at_before=self.deadline, fuse_ends_at_after=float(prop(self.pepper, 'fuse_ends_at', 0)))
                # EMCReactionEffect is not a Blueprint/Python-readable enum.
                # Observe the native burst born at the real deadline instead:
                # Detonate() spawns its zero-damage cosmetic AMCReactionVFX at
                # the pepper's actual position before disposing the food. This
                # identifies the burst from source + public runtime evidence,
                # rather than silently reading a protected reflected property.
                effects = []
                pepper_point = self.pepper.get_actor_location() if valid(self.pepper) else None
                if pepper_point is not None and remaining <= .1:
                    for actor in u.GameplayStatics.get_all_actors_of_class(self.world, u.MCReactionVFX):
                        creation_age = float(actor.get_game_time_since_creation())
                        created_at = now-creation_age
                        applied = float(actor.get_editor_property('AppliedDamage'))
                        point = actor.get_actor_location()
                        distance = (point-pepper_point).length()
                        if abs(created_at-self.deadline) <= .12 and applied <= 0. and distance <= 15.:
                            effects.append((actor, created_at, creation_age, applied, distance))
                if effects and 'detonation' not in self.report:
                    observed, created_at, creation_age, applied, distance = min(effects, key=lambda item: item[4])
                    point = observed.get_actor_location()
                    floor = self.floor_at(point)
                    disposed = not valid(self.pepper) or 'DISPOSED' in str(prop(self.pepper, 'phase')).upper()
                    self.report['detonation'] = dict(game_time=now, point_cm=xyz(point),
                        clearance_above_floor_cm=float(point.z-floor.z), disposed=disposed,
                        actual_effect_actor=observed.get_path_name(), created_at_game_time=created_at,
                        effect_age_seconds=creation_age, effect_applied_damage=applied,
                        distance_to_actual_pepper_cm=distance,
                        identification='Source-derived ChiliExplosion identity: native AMCReactionVFX born at fuse deadline, zero cosmetic damage, same disposed-pepper position',
                        source='Source/MessControl/Private/MCSpicyFood.cpp: AMCFoodActor::Detonate')
                    self.report['checks']['chili_in_air'] = dict(passed=self.thrown and disposed and point.z-floor.z>20,
                        uses_real_F3_item=True, uses_real_pickup_and_throw=True, expired_fuse=self.deadline,
                        observed_game_time=now, clearance_above_floor_cm=float(point.z-floor.z))
                    self.report['checks']['chili_throw_keeps_deadline'] = dict(passed=abs(
                        self.report['throw']['fuse_ends_at_after']-self.deadline)<.001)
                if remaining < -1.1:
                    if 'detonation' not in self.report:
                        raise RuntimeError('No observable native burst born at the fuse deadline and disposed-pepper position was found')
                    self.finish()
        except Exception as error:
            self.report['traceback'] = traceback.format_exc()
            self.finish(str(error))

    def finish(self, error=None):
        if getattr(self, 'done', False):
            return
        self.done = True
        errors = []
        def restore(label, operation):
            try:
                if operation() is False:
                    raise RuntimeError('Operation reported failure')
                self.report['restoration'].append(label)
            except Exception as exception:
                errors.append(label+': '+str(exception))
        if self.callback is not None:
            restore('unregister callback', lambda: u.unregister_slate_post_tick_callback(self.callback))
            self.callback = None
        if valid(self.component):
            restore('detach capture target', lambda: self.component.set_editor_property('texture_target', None))
        if self.target:
            restore('release render target', lambda: u.RenderingLibrary.release_render_target2d(self.target))
        if self.owns_pie and self.levels.is_in_play_in_editor():
            restore('end only owned solo PIE', self.levels.editor_request_end_play)
        if self.old_play is not None:
            restore('PIE settings', lambda: u.ToolsetLibrary.set_object_properties(self.play, json.dumps(self.old_play)))
        if self.old_engine is not None:
            restore('fixed-frame switch', lambda: self.engine.set_editor_property('bUseFixedFrameRate', self.old_engine[0]))
            restore('fixed frame rate', lambda: self.engine.set_editor_property('FixedFrameRate', self.old_engine[1]))
        if self.old_perf is not None:
            restore('foreground throttle', lambda: self.perf.set_editor_property('bThrottleCPUWhenNotForeground', self.old_perf))
        passed = bool(self.report['checks']) and all(check.get('passed', False) for check in self.report['checks'].values())
        self.report.update(status='complete' if passed and not error and not errors else 'failed',
            passed=passed and not error and not errors, error=error, restoration_errors=errors,
            wall_seconds=time.monotonic()-self.started)
        self.write()
        u.log('MC_APPROVED_COMBAT_SOLO '+self.report['status']+' '+str(error or self.output))


def start_check():
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('The previous combat check is still active')
    check = ApprovedCombatSolo.__new__(ApprovedCombatSolo)
    setattr(builtins, KEY, check)
    try:
        check.__init__()
        check.start()
    except Exception as error:
        if hasattr(check, 'report'):
            check.finish(str(error))
        raise
    print('Approved solo combat check started: '+str(check.output))
    return check


if globals().get('MC_APPROVED_COMBAT_RUN', True):
    start_check()
