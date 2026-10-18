"""Feedback 9/10, optionally 6: owned solo PIE, real respawn and E/Q delivery.

Run in idle saved L_Mouth after the approved Editor build. The spawn fixture is
one temporary nonlocal PlayerController using native RestartPlayerAtTransform
from an intentionally floor-level transform; no network peer is created. Intake
uses four small food fixtures, ordinary E and client Q/held Q. Native geometry,
gravity, capture, curl, VFX and scoring decide results. The scheduler is paused
only in this owned PIE after resolving the first normal perk.

Optional globals: MC_FEEDBACK_CASES=('spawn','intake','rafts'),
MC_FEEDBACK_CAPTURE_HZ=12. Cancel with
builtins.mc_feedback_spawn_intake_solo.finish('Stopped by operator').
No source/package authoring, build, network or commit is performed here.
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import time
import traceback
import uuid
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
assert ROOT.drive.upper() == 'E:', 'Keep all task output on E:'
OUTPUT = ROOT/'Saved/Feedback5_11_20261018/SpawnIntake'
RUN = OUTPUT/'Runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
CASES = tuple(globals().get('MC_FEEDBACK_CASES', ('spawn', 'intake', 'rafts')))
assert CASES and all(case in ('spawn', 'intake', 'rafts') for case in CASES)
manual = runpy.run_path(str(ROOT/'Tools/Unreal/check_manual_intake_solo.py'), run_name='feedback_manual_support',
    init_globals={'MC_MANUAL_INTAKE_FOCUS': 'throws'})
base = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'), run_name='feedback_base_support',
    init_globals={'MC_ENV_OUTPUT': RUN, 'MC_ENV_CAPTURE_HZ': globals().get('MC_FEEDBACK_CAPTURE_HZ', 12)})
Parent, Base = manual['ManualIntakeSolo'], base['ApprovedEnvironmentSolo']
prop, vec, valid, path = (manual[key] for key in ('prop', 'vec', 'valid', 'path'))
wave_support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_wave_observer_support.py'),
    run_name='feedback_spawn_intake_wave_support')
KEY = 'mc_feedback_spawn_intake_solo'


def dot(a, b):
    return a.x*b.x+a.y*b.y+a.z*b.z


class FeedbackSpawnIntakeSolo(Parent):
    def __init__(self):
        # Reuse capture/settings cleanup and verified HitResult/camera helpers,
        # without the old probe's superseded no-vortex/unthrown-food assertions.
        Base.__init__(self)
        self.require(callable(getattr(u.MCGameplayUIEditorLibrary, 'read_pie_throat_vortex', None)),
            'Approved Editor build is missing the read-only ReadPIEThroatVortex telemetry helper')
        self.throat = self.collection = None
        self.fixtures = []
        self.throw_held = self.replay_held = False
        self.clip = None
        self.clip_until = 0.
        self.focus = 'throws'
        self.spawn_controller = self.spawn_pawn = self.spawn_state = None
        self.spawn_rows = []
        self.spawn_cleanup_done = False
        self.spawn_cleanup_requested_at = None
        self.trajectory_rows = {}
        self.vortex_paths = set()
        self.peak_queue = self.peak_vortex_count = 0
        self.cat_event = None
        self.panel_open = False
        self.wave_observer_held = False
        self.startup_world_wall = self.startup_world_game = None
        self.pending_cases = list(CASES)
        self.report.update(scope=__doc__, requested_cases=list(CASES), clips={}, source_sha256={},
            run_directory=str(RUN), fixture_tuning={}, mode='feedback_spawn_intake',
            tuning_reason='Disclosed floor-level spawn, four small food fragments and observer positions/camera; native capsule placement, Q launch, intake capture, trajectories and scoring remain active. Scheduled Director ticks pause after the ordinary first perk.',
            limitations=[
                'Only one standalone PIE. The temporary nonlocal controller is an in-process spawn fixture, not a network peer or a latency simulation.',
                'RestartPlayerAtTransform receives a disclosed floor-level transform. Native capsule placement/collision and subsequent movement remain active; no health, reserve, movement mode or actor-position correction is applied to its spawned pawn.',
                'Small broccoli fragments, the main player approach positions and review CameraActor are disclosed PIE fixtures. Collection, client Q/held Q, projectile physics, throat attraction, individual curl, vortex and scoring stay native.',
                'The unrelated scheduler is paused in the owned PIE after its ordinary first perk. No gameplay progress counter is assigned to pass a check.',
                'The optional raft check opens the real F3 panel and starts native CataclysmRandom. It asserts the selected jelly branch and absence of raft actors; this is not an entire cataclysm completion playthrough.',
                'For that branch check, a disclosed supported tooth-side observer position and actual held RMB brace survive the opening wave. The brace releases before the jelly clip; no health, phase, movement mode or native force is assigned to survive it.',
                'Slate captures bind directly to the owned floating PIE window. Reports include source timestamps and reject unsupported frame sizes/camera mismatches.'
            ])
        for item in ('Tools/Unreal/probe_feedback_spawn_intake_solo.py', 'Tools/Unreal/probe_wave_observer_support.py',
                     'Source/MessControlEditor/Public/MCGameplayUIEditorLibrary.h',
                     'Source/MessControlEditor/Private/MCGameplayUIEditorLibrary.cpp',
                     'Source/MessControl/Private/MCGameMode.cpp',
                     'Source/MessControl/Private/MCThroat.cpp', 'Source/MessControl/Private/MCFoodCollectionComponent.cpp'):
            self.report['source_sha256'][item] = hashlib.sha256((ROOT/item).read_bytes()).hexdigest()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report.json').write_text(data, encoding='utf-8')

    def spawn(self, cls, pose):
        actor = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, cls, pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(actor), 'Deferred fixture spawn failed: '+str(cls))
        self.statics.call_method('FinishSpawningActor', args=(actor, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        return actor

    def reserve_snapshot(self):
        gs = u.GameplayStatics.get_game_state(self.world)
        return sorted([dict(path=path(tooth), tooth_id=int(prop(prop(tooth, 'state'), 'tooth_id')),
            available=bool(tooth.is_available()), consumed=bool(prop(prop(tooth, 'state'), 'consumed')))
            for tooth in prop(gs, 'arena_teeth') if valid(tooth)], key=lambda row: row['tooth_id'])

    def place_observer(self, foot, yaw, reason):
        # The shared wave helper requires a position-only fixture: unlike the
        # input approach's move_to(), this does not stop velocity or set CMC mode.
        position = foot+u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height()+3.)
        self.hero.set_actor_location_and_rotation(position, u.Rotator(yaw=yaw), False, True)
        self.pc.set_control_rotation(u.Rotator(yaw=yaw))
        self.report.setdefault('observer_position_fixtures', []).append(dict(
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)), position_cm=vec(position),
            yaw_degrees=yaw, reason=reason, health_or_movement_or_phase_written=False))

    def startup_snapshot(self):
        mode = u.GameplayStatics.get_game_mode(self.world)
        row = dict(world=path(self.world), mode=path(mode),
            mode_class=path(mode.get_class()) if valid(mode) else None, controllers=[])
        try:
            row['options_string'] = str(prop(mode, 'options_string')) if valid(mode) else None
        except Exception as error:
            row['options_read_error'] = str(error)
        for controller in self.actors(u.PlayerController):
            pawn = controller.get_controlled_pawn()
            row['controllers'].append(dict(path=path(controller), controller_class=path(controller.get_class()),
                is_mc_player_controller=isinstance(controller, u.MCPlayerController), pawn=path(pawn),
                pawn_class=path(pawn.get_class()) if valid(pawn) else None))
        row.update(elapsed_wall_seconds=time.monotonic()-self.startup_world_wall,
            elapsed_game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))-self.startup_world_game)
        self.report['startup_world'] = row
        return row

    def clear_review_view(self, target, ignored=()):
        ignore = [self.hero]+[actor for actor in ignored if valid(actor)]+[food for food in self.fixtures if valid(food)]
        if valid(self.camera):
            ignore.append(self.camera)
        for offset in (u.Vector(400,450,300),u.Vector(-400,-450,300),
                       u.Vector(-450,400,360),u.Vector(450,-400,360),u.Vector(0,-400,500)):
            origin = target+offset
            hit = self.blocking_hit(u.SystemLibrary.line_trace_single(self.world, origin, target,
                u.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignore, u.DrawDebugTrace.NONE, True))
            if hit is None or hit['distance'] >= math.dist(vec(origin),vec(target))-20.:
                self.frame_camera(origin, target)
                self.report.setdefault('review_cameras', []).append(dict(phase=self.phase,
                    target_cm=vec(target),origin_cm=vec(origin),unoccluded_line_trace=True))
                return
        self.require(False, 'No clear review-camera view of the actual test subject')

    def isolate(self):
        self.tongue = self.actors(u.MCTongue)[0]
        for cls in (u.MCSingleDayDirector, u.MCGameDirector, u.MCDayDirector):
            for scheduler in self.actors(cls):
                scheduler.set_actor_tick_enabled(False)
        gs = u.GameplayStatics.get_game_state(self.world)
        gs.set_editor_property('dev_manual_events', True)
        gs.set_editor_property('phase', u.MCShiftPhase.WORKING)
        gs.set_editor_property('phase_ends_at', 0.)
        self.collection = prop(self.hero, 'food_collection')
        self.report['isolation'] = dict(game_seconds=u.GameplayStatics.get_time_seconds(self.world),
            normal_first_perk_resolved=True, scheduled_event_ticks_paused=True,
            health_changed=False, reserve_changed=False)

    def next_case(self):
        self.clip = None
        if not self.pending_cases:
            self.stage('capture_flush')
            return
        case = self.pending_cases.pop(0)
        if case == 'spawn':
            self.prepare_spawn()
        elif case == 'intake':
            self.prepare_intake()
        else:
            self.prepare_rafts()

    def prepare_spawn(self):
        self.reserve_before = self.reserve_snapshot()
        self.player_states_before = {path(actor) for actor in self.actors(u.PlayerState)}
        self.controllers_before = {path(actor) for actor in self.actors(u.PlayerController)}
        capsule = self.hero.capsule_component
        half, radius = capsule.get_scaled_capsule_half_height(), capsule.get_scaled_capsule_radius()
        center, extent = self.tongue.get_actor_bounds(False)
        floor = None
        for dx, dy in ((0., 0.), (-.2, .2), (.2, -.2), (-.3, -.2), (.3, .2)):
            candidate = self.floor(center+u.Vector(extent.x*dx, extent.y*dy, 0))
            if candidate is None or math.dist(vec(candidate), vec(self.hero.get_actor_location())) < 300.:
                continue
            pose = candidate+u.Vector(0, 0, half+30.)
            hit = self.blocking_hit(u.SystemLibrary.capsule_trace_single(self.world, pose, pose+u.Vector(0,0,.2),
                radius, half, u.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [self.hero], u.DrawDebugTrace.NONE, True))
            if hit is None:
                floor = candidate
                break
        self.require(floor is not None, 'No supported capsule-clear point for native spawn test')
        mode = u.GameplayStatics.get_game_mode(self.world)
        self.spawn_controller = self.spawn(u.PlayerController.static_class(), u.Transform())
        self.spawn_state = prop(self.spawn_controller, 'player_state')
        self.report['spawn_fixture'] = dict(controller=path(self.spawn_controller), player_state=path(self.spawn_state),
            method='GameMode.RestartPlayerAtTransform', requested_floor_level_cm=vec(floor),
            native_spawn_rule='max(requestedZ+30, tissueZ+capsuleHalfHeight+30)', requested_z_offset_cm=0.)
        mode.call_method('RestartPlayerAtTransform', args=(self.spawn_controller, u.Transform(location=floor)))
        self.spawn_pawn = self.spawn_controller.get_controlled_pawn()
        self.require(isinstance(self.spawn_pawn, u.MCToothCharacter), 'Native restart did not create/possess a tooth pawn')
        self.sample_spawn(float(u.GameplayStatics.get_time_seconds(self.world)), immediate=True)
        target = self.spawn_pawn.get_actor_location()
        self.clear_review_view(target-u.Vector(0,0,25), [self.spawn_pawn])
        self.stage('spawn_settle')
        self.begin_clip('09_safe_player_spawn', 2.5)

    def sample_spawn(self, now, immediate=False):
        if not valid(self.spawn_pawn):
            return
        point = self.spawn_pawn.get_actor_location()
        floor = self.floor(point)
        self.require(floor is not None, 'Spawned pawn has no tongue surface under its capsule')
        half = self.spawn_pawn.capsule_component.get_scaled_capsule_half_height()
        status = prop(self.spawn_pawn, 'status')
        row = dict(game_seconds=now, immediate=immediate, pawn=path(self.spawn_pawn), position_cm=vec(point),
            tissue_cm=vec(floor), capsule_half_height_cm=half, capsule_floor_clearance_cm=point.z-half-floor.z,
            alive=bool(status.is_alive()), health=float(prop(prop(status, 'state'), 'health')),
            reserve=self.reserve_snapshot())
        self.spawn_rows.append(row)
        self.report['spawn_samples'] = self.spawn_rows
        self.require(row['alive'] and row['capsule_floor_clearance_cm'] >= -4., 'Native spawned pawn sank below tissue or died')
        self.require(row['reserve'] == self.reserve_before, 'A reserve tooth was consumed during safe native spawn')
        if immediate:
            self.require(row['capsule_floor_clearance_cm'] >= 27., 'Initial authoritative capsule did not receive the new 30cm floor clearance')

    def cleanup_spawn(self):
        if self.spawn_cleanup_done:
            return True
        if not self.world or not self.owned_pie_is_active():
            return False
        now = float(u.GameplayStatics.get_time_seconds(self.world))
        if self.spawn_cleanup_requested_at is None:
            self.spawn_cleanup_requested_at = now
            for actor in (self.spawn_pawn, self.spawn_state):
                if valid(actor):
                    actor.destroy_actor()
            # AController::K2_DestroyActor intentionally does nothing. The
            # inherited native lifespan timer calls Destroy() instead, and the
            # next normal PIE tick must actually remove this owned controller.
            if valid(self.spawn_controller):
                self.spawn_controller.set_life_span(.001)
            self.report['spawn_cleanup'] = dict(requested_game_seconds=now,
                controller_method='AActor.SetLifeSpan(0.001) -> native LifeSpanExpired -> Destroy',
                awaiting_actual_timer_tick=True, completed_before_pie_end=False)
            return False
        if self.world and self.owned_pie_is_active():
            leftover = [actor for actor in self.actors(u.PlayerController)
                if path(actor) not in self.controllers_before]
            states = [actor for actor in self.actors(u.PlayerState) if path(actor) not in self.player_states_before]
            removed = not leftover and not states and not valid(self.spawn_pawn)
            self.report['spawn_cleanup'].update(extra_controllers=[path(actor) for actor in leftover],
                extra_player_states=[path(actor) for actor in states],
                fixture_pawn_destroyed=not valid(self.spawn_pawn),
                observed_game_seconds=now, timer_wait_game_seconds=now-self.spawn_cleanup_requested_at,
                awaiting_actual_timer_tick=not removed, completed_before_pie_end=removed)
            self.require(removed or now-self.spawn_cleanup_requested_at < 1.,
                'Temporary spawn controller/pawn/state survived the actual cleanup timer ticks')
            if removed:
                self.spawn_cleanup_done = True
                self.report['checks']['temporary_spawn_fixture_fully_removed'] = True
                return True
        return False

    def load_intake_geometry(self):
        throats = self.actors(u.MCThroat)
        self.require(len(throats) == 1, 'Expected one native authored throat')
        self.throat = throats[0]
        result = self.throat.get_intake_aim_target()
        values = list(result) if isinstance(result, (tuple, list)) else [result]
        self.target = next(value for value in values if isinstance(value, u.Vector))
        self.radius = next(float(value) for value in values if isinstance(value, (float, int)) and not isinstance(value, bool))
        mouth = prop(self.throat, 'authored_mouth')
        art = mouth.get_world_transform()
        measured = json.loads((ROOT/'Tools/Unreal/manual_intake_aperture.json').read_text())
        expansion = float(prop(self.throat, 'manual_authored_expansion'))
        morph = 1-expansion if prop(self.throat, 'reverse_authored_open') else expansion
        points = [u.Vector(*(a*(1-morph)+b*morph for a,b in zip(v,w)))
            for v,w in zip(measured['basis'], measured['open'])]
        self.aperture_world_points = [art.transform_location(point) for point in points]
        self.inward = u.MathLibrary.transform_direction(art, u.Vector(*measured['normal']))
        self.aperture_up = u.MathLibrary.transform_direction(art, u.Vector(*measured['up']))
        self.aperture_right = u.MathLibrary.transform_direction(art, u.Vector(1,0,0))
        guard = self.floor(self.throat.get_actor_transform().transform_location(prop(self.throat, 'zone_center')))
        minimum = guard.z-10. if guard else min(point.z for point in self.aperture_world_points)-1.
        self.visible_aperture_points = self.clip_ring_above(self.aperture_world_points, minimum)
        self.require(len(self.visible_aperture_points) >= 3, 'Artist aperture has no visible upper opening')
        self.visible_target = sum(self.visible_aperture_points, u.Vector())/len(self.visible_aperture_points)
        self.start_swallowed = int(prop(self.throat, 'food_swallowed'))
        self.report['intake_settings'] = dict(capture_depth_cm=float(prop(self.throat, 'intake_capture_depth')),
            pull_seconds=float(prop(self.throat, 'intake_pull_seconds')), target_cm=vec(self.target),
            artist_open_morph=float(mouth.get_morph_target('Open')), expansion=expansion,
            note='Read only; existing artist transform/morph and native tuning are not assigned')
        self.require(self.throat.open_amount() >= .99, 'Intake is not open by default')

    def frame_camera(self, location, target):
        self.native_player_view = False
        Base.frame_camera(self, location, target)

    def hole_review_camera(self):
        # External CameraActors bypass the pawn's real wall-probe/reveal path.
        # Record the native player camera instead; steer only its public mouse-
        # orbit input towards the opening, retaining normal collision and reveal.
        self.native_player_view = True
        self.pc.set_view_target_with_blend(self.hero, 0.)
        self.hero.apply_camera_orbit_input(u.Vector2D(.001, 0.))
        self.report.setdefault('review_camera_fixtures', []).append(dict(
            method='Native pawn camera, ordinary ApplyCameraOrbitInput; camera collision/wall reveal retained',
            target=vec(self.visible_target), expected_view_target=path(self.hero)))

    def steer_native_intake_camera(self):
        if not getattr(self, 'native_player_view', False):
            return
        manager = prop(self.pc, 'player_camera_manager')
        actual = manager.get_camera_rotation()
        wanted = u.MathLibrary.find_look_at_rotation(manager.get_camera_location(), self.target)
        sensitivity = float(prop(self.hero, 'camera_orbit_sensitivity'))
        yaw = (wanted.yaw-actual.yaw+180.) % 360.-180.
        pitch = wanted.pitch-actual.pitch
        self.hero.apply_camera_orbit_input(u.Vector2D(
            max(-2., min(2., yaw))/sensitivity, max(-2., min(2., pitch))/sensitivity))

    def camera_evidence(self):
        if not getattr(self, 'native_player_view', False):
            return Parent.camera_evidence(self)
        manager = prop(self.pc, 'player_camera_manager')
        return dict(view_target=path(self.pc.get_view_target()), expected_target=path(self.hero),
            actual_cm=vec(manager.get_camera_location()), rotation=str(manager.get_camera_rotation()),
            native_player_camera=True, location_comparison='Native camera has no external CameraActor target')

    def prepare_intake(self):
        self.load_intake_geometry()
        self.require(not prop(self.collection, 'pieces') and not valid(prop(self.hero, 'held_food')), 'Preserve an existing held item; empty hands are required for this owned fixture')
        self.throw_floors = []
        for side in (-160., 160., 0.):
            chosen = None
            for distance in (650., 550., 850., 450., 1050.):
                floor = self.floor(self.target-self.inward*distance+self.aperture_right*side)
                if floor is None:
                    continue
                half = self.hero.capsule_component.get_scaled_capsule_half_height()
                pose = floor+u.Vector(0,0,half+4.)
                hit = self.blocking_hit(u.SystemLibrary.capsule_trace_single(self.world, pose, pose+u.Vector(0,0,.2),
                    self.hero.capsule_component.get_scaled_capsule_radius(), half, u.TraceTypeQuery.TRACE_TYPE_QUERY1,
                    False, [self.hero], u.DrawDebugTrace.NONE, True))
                if hit is None and self.clear_camera_line(pose+u.Vector(0,0,70), self.visible_target):
                    chosen = floor
                    break
            self.require(chosen is not None, 'No clear native Q-throw approach for side '+str(side))
            self.throw_floors.append(chosen)
        self.report['throw_position_fixtures'] = [vec(point) for point in self.throw_floors]
        self.move_to(self.throw_floors[0], u.MathLibrary.find_look_at_rotation(self.throw_floors[0], self.target).yaw)
        forward, right = self.hero.get_actor_forward_vector(), self.hero.get_actor_right_vector()
        for side in (-35., -12., 12., 35.):
            self.spawn_fragment(self.hero.get_actor_location()+forward*85+right*side+u.Vector(0,0,-25))
        self.hero.call_method('ServerToggleFoodCollection', args=())
        self.hole_review_camera()
        self.stage('intake_collect')

    def sample_intake(self, now):
        vortex = self.actors(u.MCThroatVortex)
        self.peak_vortex_count = max(self.peak_vortex_count, len(vortex))
        self.peak_queue = max(self.peak_queue, int(prop(self.throat, 'queued_food_count')))
        self.vortex_paths.update(path(actor) for actor in vortex)
        foods = []
        for food in self.fixtures:
            if not valid(food):
                continue
            row = dict(path=path(food), phase=str(prop(food, 'phase')), position_cm=vec(food.get_actor_location()),
                scale=vec(food.get_actor_scale3d()), carrier=path(prop(food, 'stack_carrier')))
            foods.append(row)
            if 'SWALLOWING' in row['phase'].upper():
                self.trajectory_rows.setdefault(row['path'], []).append(dict(time=now, position_cm=row['position_cm']))
        self.report['samples'].append(dict(time=now, phase=self.phase,
            pieces=len(prop(self.collection, 'pieces')), food_swallowed=int(prop(self.throat, 'food_swallowed')),
            queued_food=int(prop(self.throat, 'queued_food_count')), hero_cm=vec(self.hero.get_actor_location()),
            vortex=[self.vortex_snapshot(actor) for actor in vortex], foods=foods))

    def vortex_snapshot(self, actor):
        # These native Replicated-only fields have no Editor/Blueprint flags.
        # Both Python and ToolsetLibrary enforce PropertyAccessUtil permissions.
        # The editor-only helper copies actual native values without changing
        # runtime visibility or deriving source counts from the probe fixtures.
        actor_path = path(actor)
        readers = self.report.setdefault('native_property_readers', {})
        if actor_path not in readers:
            rejected = []
            for key in ('food_sources', 'FoodSources'):
                try:
                    actor.get_editor_property(key)
                except Exception as error:
                    rejected.append(dict(property=key, error=str(error)))
            readers[actor_path] = dict(method='MCGameplayUIEditorLibrary.ReadPIEThroatVortex',
                native_properties=['FoodSources', 'StartedAt', 'Duration'], python_getter_errors=rejected)
        try:
            result = u.MCGameplayUIEditorLibrary.read_pie_throat_vortex(self.world, actor)
            self.require(isinstance(result, (tuple, list)),
                'ReadPIEThroatVortex rejected the PIE/world/actor or returned an unexpected shape: '+repr(result))
            packed = list(result)
            readers[actor_path]['python_return_types'] = [type(value).__name__ for value in packed]
            # UE Python normally packs a successful bool+three outs as only
            # the three outs; also accept an explicit leading success bool.
            if len(packed) == 4 and isinstance(packed[0], bool):
                self.require(packed.pop(0), 'ReadPIEThroatVortex explicitly rejected this PIE actor')
            self.require(len(packed) == 3,
                'ReadPIEThroatVortex did not return Sources/StartedAt/Duration: '+repr(result))
            references, started, duration = packed
            self.require(not isinstance(references, (str, bytes, dict)),
                'ReadPIEThroatVortex Sources is not a native object-reference array: '+repr(references))
            references = list(references)
            self.require(len(references) <= 24, 'Native vortex exceeded its bounded food-wake source count')
            live_food = {path(food): food for food in self.actors(u.MCFoodActor) if valid(food)}
            sources = []
            for reference in references:
                if reference is None:
                    continue
                self.require(isinstance(reference, u.MCFoodActor),
                    'ReadPIEThroatVortex returned a non-food source wrapper: '+repr(reference))
                if not valid(reference):
                    continue
                source_path = path(reference)
                self.require(source_path in live_food,
                    'ReadPIEThroatVortex source is not an actual food actor in this PIE world: '+repr(reference))
                sources.append(path(live_food[source_path]))
            self.require(all(isinstance(value, (int, float)) and not isinstance(value, bool)
                and math.isfinite(value) for value in (started, duration)),
                'Vortex native time fields are not finite numbers: '+repr((started, duration)))
            readers[actor_path]['last_native_values'] = dict(sources=sources, source_slots=len(references),
                started_at=float(started), duration=float(duration))
            airflow = prop(actor, 'airflow')
            return dict(path=actor_path, sources=sources, started_at=float(started), duration=float(duration),
                airflow_visible=bool(airflow.is_visible()), mesh_sections=int(airflow.get_num_sections()),
                native_property_reader='MCGameplayUIEditorLibrary.ReadPIEThroatVortex')
        except Exception as error:
            readers[actor_path]['reflection_error'] = str(error)
            self.write()
            raise RuntimeError('Cannot read actual vortex reflection for '+str(actor_path)+': '+str(error)) from error

    def prove_intake(self):
        rows = []
        for food, samples in self.trajectory_rows.items():
            if len(samples) < 5:
                continue
            a, b = samples[0]['position_cm'], samples[-1]['position_cm']
            delta = [y-x for x,y in zip(a,b)]
            length2 = sum(value*value for value in delta)
            deviations = []
            middle_curl = None
            for sample in samples:
                p = sample['position_cm']
                t = sum((x-y)*d for x,y,d in zip(p,a,delta))/max(length2, 1.e-9)
                line = [x+t*d for x,d in zip(a,delta)]
                deviations.append(math.dist(p,line))
                if sample is samples[len(samples)//2]:
                    residual = u.Vector(*(x-y for x,y in zip(p,line)))
                    middle_curl = [dot(residual,self.aperture_right), dot(residual,self.aperture_up)]
            rows.append(dict(food=food, samples=len(samples), observed_seconds=samples[-1]['time']-samples[0]['time'],
                max_deviation_from_straight_path_cm=max(deviations), middle_curl_in_aperture_plane_cm=middle_curl, path=samples))
        self.report['individual_intake_paths'] = rows
        self.report['vortex_evidence'] = dict(peak_queued_food=self.peak_queue, peak_vortex_actors=self.peak_vortex_count,
            observed_vortex_paths=sorted(self.vortex_paths))
        self.require(int(prop(self.throat, 'food_swallowed')) == self.start_swallowed+4, 'Four client Q deliveries did not score exactly four ingredients')
        self.require(len(rows) == 4 and all(row['observed_seconds'] >= .7 for row in rows), 'A food disappeared without its sustained native intake animation')
        self.require(all(row['max_deviation_from_straight_path_cm'] > 5. for row in rows), 'An intake path lacks the expected procedural curl')
        directions = [row['middle_curl_in_aperture_plane_cm'] for row in rows
            if row['middle_curl_in_aperture_plane_cm'] and math.hypot(*row['middle_curl_in_aperture_plane_cm']) > 2.]
        spread = max((math.acos(max(-1.,min(1.,sum(x*y for x,y in zip(a,b))/(math.hypot(*a)*math.hypot(*b)))))
            for a in directions for b in directions), default=0.)
        self.report['individual_curl_direction_spread_degrees'] = math.degrees(spread)
        self.require(spread > math.radians(20.), 'All ingredients reused the same apparent curl direction')
        self.require(self.peak_queue >= 2, 'Held Q did not demonstrate concurrent independent food intakes')
        self.require(self.vortex_paths and self.peak_vortex_count == 1, 'Expected one shared live vortex while several ingredients curl independently')
        self.require(sum(any(vortex['airflow_visible'] and vortex['mesh_sections'] > 0 for vortex in row.get('vortex', []))
            for row in self.report['samples']) >= 3, 'Vortex actor exists but no sustained visible procedural VFX mesh was observed')
        self.report['checks'].update(four_client_Q_deliveries=True, held_Q_releases_one_by_one=True,
            four_individual_curved_intake_paths=True, concurrent_food_pull=True, one_shared_visible_vortex=True)

    def prepare_rafts(self):
        self.require(not self.actors(u.MCCataclysmRaft), 'An unrelated raft already exists; do not delete it to pass this check')
        hero = self.hero.get_actor_location()
        self.clear_review_view(hero+u.Vector(0,0,30))
        self.pc.toggle_dev_panel()
        self.panel_open = True
        # DevPanel and Steps are private UPROPERTYs without Edit/Blueprint
        # visibility. Find the actual viewport widget using the public world-
        # filtered UMG API, then inspect its constructed native widget tree.
        panels = [widget for widget in u.WidgetLibrary.get_all_widgets_of_class(self.world,u.MCDevPanelWidget,True)
            if valid(widget) and widget.get_owning_player()==self.pc and widget.is_visible()]
        self.require(len(panels)==1,'F3 did not expose exactly one visible developer panel owned by this PIE player')
        panel=panels[0]
        prefix=path(panel)+'.'
        owned=[widget for widget in u.ObjectIterator(u.Widget) if path(widget).startswith(prefix)]
        roots=[widget for widget in owned if isinstance(widget,u.CanvasPanel) and widget.get_parent() is None]
        self.require(len(roots)==1,'The actual F3 panel has no unique native CanvasPanel root')

        widgets=[];tree=[]
        def visit(widget,depth=0):
            self.require(path(widget).startswith(prefix),'F3 widget tree escaped the owned developer panel')
            widgets.append(widget)
            tree.append(dict(path=path(widget),depth=depth,widget_class=widget.get_class().get_name(),
                text=str(widget.get_text()) if isinstance(widget,u.TextBlock) else None))
            if isinstance(widget,u.PanelWidget):
                for index in range(widget.get_children_count()):
                    visit(widget.get_child_at(index),depth+1)
        visit(roots[0])

        def text(widget):
            if isinstance(widget, u.TextBlock):
                return str(widget.get_text())
            if isinstance(widget, u.PanelWidget):
                return ' '.join(text(widget.get_child_at(index)) for index in range(widget.get_children_count()))
            if isinstance(widget, u.ContentWidget):
                child = widget.get_content()
                return text(child) if child else ''
            return ''

        start=next((widget for widget in widgets if isinstance(widget,u.MCDevActionButton)
            and text(widget)=='Катаклизм — начать'),None)
        self.require(start is not None,'The owned F3 widget tree lacks its real native random-cataclysm button')
        steps=start.get_parent()
        self.require(isinstance(steps,u.VerticalBox) and steps in widgets,'The actual cataclysm button is outside its native event column')
        buttons = [(steps.get_child_at(index), text(steps.get_child_at(index)))
            for index in range(steps.get_children_count()) if isinstance(steps.get_child_at(index), u.Button)]
        labels = [label for _,label in buttons]
        self.require(not any('плот' in label.lower() for label in labels), 'F3 still exposes a raft action')
        start = next((button for button,label in buttons if label == 'Катаклизм — начать'), None)
        self.require(start is not None and any('живое желе' in label.lower() for label in labels), 'F3 lacks the native random/jelly actions')
        steps.get_parent().scroll_widget_into_view(start, False, u.DescendantScrollDestination.TOP_OR_LEFT, 0.)
        self.report['f3_event_buttons'] = labels
        self.report['f3_panel_evidence']=dict(panel=path(panel),owning_player=path(panel.get_owning_player()),
            query_world=path(self.world),root=path(roots[0]),event_column=path(steps),root_widget_tree=tree,
            discovery='WidgetLibrary world-filtered viewport query plus exact owning player; actual native widget descendants')
        self.report['checks']['actual_F3_has_no_raft_action'] = True
        self.stage('rafts_f3_panel')
        self.begin_clip('06_f3_without_raft_action', 1.6)

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 210., 'Owned feedback probe exceeded its wall-time bound')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    self.require(time.monotonic()-self.started < 10.,
                        'StartPIE did not produce an owned PIE world within 10 wall seconds')
                    return
                self.require(self.requested_pie, 'Preserve a PIE world that this probe did not request')
                self.require(len(worlds) == 1 and u.SystemLibrary.is_standalone(worlds[0]), 'Expected exactly one owned standalone PIE')
                # Adopt the world before waiting for a pawn. A failed/spectator
                # login must still let finish() end exactly this owned PIE.
                self.world = worlds[0]
                self.startup_world_wall = time.monotonic()
                self.startup_world_game = float(u.GameplayStatics.get_time_seconds(self.world))
                self.startup_snapshot()
                self.stage('waiting_player_pawn')
            self.require(self.owned_pie_is_active(worlds), 'Owned PIE ended or was replaced')
            if self.phase == 'waiting_player_pawn':
                startup = self.startup_snapshot()
                pc = u.GameplayStatics.get_player_controller(self.world, 0)
                pawn = pc.get_controlled_pawn() if valid(pc) else None
                if isinstance(pc, u.MCPlayerController) and isinstance(pawn, u.MCToothCharacter) and valid(pawn):
                    self.pc, self.hero = pc, pawn
                    startup['expected_player_and_pawn_ready'] = True
                    self.stage('first_perk')
                    return
                self.require(startup['elapsed_game_seconds'] < 5. and startup['elapsed_wall_seconds'] < 10.,
                    'Owned PIE did not create MCPlayerController with a tooth pawn within 5 game / 10 wall seconds; '
                    'runtime='+json.dumps(startup, ensure_ascii=False))
                return
            self.verify_pending_capture_dimensions()
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.pc.is_reward_menu_open():
                self.require(self.phase in ('first_perk', 'perk_resolution'), 'A new reward menu interrupted this bounded proof; do not hide the interruption')
                self.pc.choose_reward_perk(0)
                self.stage('perk_resolution')
                return
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.phase_at
            self.steer_native_intake_camera()
            if self.phase == 'first_perk':
                self.require(elapsed < 25., 'First ordinary perk UI did not become ready')
                return
            if self.phase == 'perk_resolution':
                self.isolate()
                self.next_case()
                return
            self.require(prop(self.hero, 'status').is_alive(), 'The original player died; no health helper was used')
            if now >= self.sample_at:
                self.sample_at = now+.04
                if self.phase == 'spawn_settle':
                    self.sample_spawn(now)
                if self.phase.startswith('intake_'):
                    self.sample_intake(now)
            if self.clip and now <= self.clip_until and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                camera = self.camera_evidence()
                matching = camera and camera['view_target'] == camera['expected_target']
                if matching and (camera.get('native_player_camera') or
                        (camera['location_error_cm'] < 12. and camera['rotation_error_degrees'] < 5.)):
                    self.screenshot('%05d.png'%len(self.report['frames']), True)
            if self.phase == 'spawn_settle' and elapsed >= 2.6:
                self.sample_spawn(now)
                self.report['checks'].update(native_spawn_has_30cm_tissue_clearance=True,
                    native_spawn_stays_alive_after_settle=True, native_spawn_preserves_reserve=True)
                self.cleanup_spawn()
                self.stage('spawn_cleanup')
            elif self.phase == 'spawn_cleanup':
                if self.cleanup_spawn():
                    self.next_case()
            elif self.phase == 'intake_collect':
                self.require(elapsed < 9., 'E did not build the four-piece food tower')
                if len(prop(self.collection, 'pieces')) == 4 and elapsed >= 1.:
                    self.report['checks']['native_E_collects_four_pieces'] = True
                    self.stage('intake_first_settle')
                    self.begin_clip('10_delivery_procedural_vortex', 25.)
            elif self.phase == 'intake_first_settle' and elapsed >= .5:
                self.throw(True)
                self.throw(False)
                self.stage('intake_first_arrival')
            elif self.phase == 'intake_first_arrival':
                self.require(elapsed < 7., 'First real client Q throw missed or failed to complete')
                if int(prop(self.throat, 'food_swallowed')) == self.start_swallowed+1:
                    self.move_to(self.throw_floors[1], u.MathLibrary.find_look_at_rotation(self.throw_floors[1], self.target).yaw)
                    self.stage('intake_second_settle')
            elif self.phase == 'intake_second_settle' and elapsed >= .6:
                self.throw(True)
                self.throw(False)
                self.stage('intake_second_arrival')
            elif self.phase == 'intake_second_arrival':
                self.require(elapsed < 7., 'Second side approach Q throw missed or failed to complete')
                if int(prop(self.throat, 'food_swallowed')) == self.start_swallowed+2:
                    self.move_to(self.throw_floors[2], u.MathLibrary.find_look_at_rotation(self.throw_floors[2], self.target).yaw)
                    self.stage('intake_held_settle')
            elif self.phase == 'intake_held_settle' and elapsed >= .6:
                self.throw(True)
                self.stage('intake_held_arrival')
            elif self.phase == 'intake_held_arrival':
                self.require(elapsed < 8., 'Held Q did not deliver the final two food pieces')
                if not prop(self.collection, 'pieces') and int(prop(self.throat, 'food_swallowed')) == self.start_swallowed+4:
                    self.throw(False)
                    self.prove_intake()
                    self.stage('intake_finish_hold')
            elif self.phase == 'intake_finish_hold' and elapsed >= .8:
                self.next_case()
            elif self.phase == 'rafts_f3_panel' and elapsed >= 1.7:
                self.pc.toggle_dev_panel()
                self.panel_open = False
                self.clip = None
                self.pc.request_dev_action(u.MCDevAction.CATACLYSM_RANDOM, -1)
                # Start the real event first; its warning gives time to acquire
                # an actual tooth handhold after F3 restored gameplay input.
                wave_support['brace_observer_for_wave'](self)
                self.wave_observer_held = True
                self.stage('rafts_random_wait')
            elif self.phase == 'rafts_random_wait':
                self.require(elapsed < 45., 'Native random cataclysm did not reach the jelly branch')
                self.require(not self.actors(u.MCCataclysmRaft), 'Random cataclysm spawned an active raft')
                self.require(prop(self.hero, 'grip').is_brace_input_held(),
                    'The actual opening-wave RMB brace unexpectedly released')
                events = self.actors(u.MCCataclysmEvent)
                if events:
                    self.cat_event = events[0]
                    self.require(prop(self.cat_event, 'variant') == u.MCCataclysmVariant.JELLIES, 'Random selected the deactivated raft branch')
                    self.require(not prop(self.cat_event, 'failed'), 'Random cataclysm failed setup')
                    if prop(self.cat_event, 'phase') == u.MCCataclysmPhase.JELLY_FIGHT:
                        jellies = list(prop(self.cat_event, 'jellies'))
                        self.require(jellies, 'Jelly branch reached combat with no actual jelly actors')
                        wave_support['release_wave_observer'](self)
                        self.wave_observer_held = False
                        target = jellies[0].get_actor_location()
                        self.clear_review_view(target, jellies)
                        self.report['raft_deactivation'] = dict(variant=str(prop(self.cat_event, 'variant')),
                            phase=str(prop(self.cat_event, 'phase')), raft_actors=0, jelly_actors=[path(actor) for actor in jellies])
                        self.stage('rafts_jelly_clip')
                        self.begin_clip('06_random_jellies_without_rafts', 2.4)
            elif self.phase == 'rafts_jelly_clip' and elapsed >= 2.5:
                self.require(not self.actors(u.MCCataclysmRaft), 'A raft appeared during the jelly-branch clip')
                self.pc.request_dev_action(u.MCDevAction.CATACLYSM_STOP, -1)
                self.report['checks'].update(random_cataclysm_only_jelly_branch=True, no_active_raft_actors=True)
                self.next_case()
            elif self.phase == 'capture_flush' and elapsed >= 1.:
                self.finalize_feedback_frames()
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finalize_feedback_frames(self):
        required = {'spawn': ['09_safe_player_spawn'], 'intake': ['10_delivery_procedural_vortex'],
            'rafts': ['06_f3_without_raft_action', '06_random_jellies_without_rafts']}
        for case in CASES:
            for clip in required[case]:
                self.require(len(self.report['clips'].get(clip, [])) >= 8, 'Too few verified gameplay frames for '+clip)
        for entry in self.report['frames']:
            data = Path(entry['file']).read_bytes()
            self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Capture is not a real engine PNG')
            dimensions = (int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big'))
            self.require(dimensions in manual['CAPTURE_DIMENSIONS'], 'Wrong viewport dimensions in final feedback clip')
            entry.update(sha256=hashlib.sha256(data).hexdigest(), width=dimensions[0], height=dimensions[1])
        self.report['checks']['verified_source_frames_for_requested_cases'] = True

    def finish(self, error=None):
        if self.done:
            return
        if self.world and self.owned_pie_is_active():
            try:
                if self.spawn_controller is not None:
                    self.cleanup_spawn()
                if self.wave_observer_held and valid(self.hero):
                    wave_support['release_wave_observer'](self)
                    self.wave_observer_held = False
                if self.panel_open:
                    self.pc.toggle_dev_panel()
                    self.panel_open = False
                if valid(self.cat_event):
                    self.pc.request_dev_action(u.MCDevAction.CATACLYSM_STOP, -1)
            except Exception:
                error = error or traceback.format_exc()
        Parent.finish(self, error)


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve the active feedback spawn/intake probe')
    probe = FeedbackSpawnIntakeSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
