"""One curated standalone proof of the new uvula, open hole, Q throws and gums.

Run only in idle L_Mouth after native build and author_manual_intake_arena apply.
Normal first-perk UI resolves first. A few small food actors and observer/player
positions are disclosed PIE fixtures; native E collection, Q/held Q, gravity,
throat intake and watergun input remain active. No package saves or network.
Optional MC_MANUAL_INTAKE_FOCUS='throws' skips uvula/gum checks, preserving the
full probe by default. MC_MANUAL_INTAKE_SPASM optionally adds the final gag check.
MC_MANUAL_INTAKE_OVERBOARD optionally adds the spoiled overboard check; both
extras default off for the focused throws run and remain on for the full run.
Cancel: builtins.mc_manual_intake_solo.finish('Stopped by operator').
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
OUTPUT = ROOT / 'Saved/ManualIntake/Capture'
RUN = OUTPUT / 'Runs' / (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8])
support = runpy.run_path(str(ROOT / 'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='manual_intake_support', init_globals={'MC_ENV_OUTPUT': RUN, 'MC_ENV_CAPTURE_HZ': 10})
Base, prop, vec = (support[n] for n in ('ApprovedEnvironmentSolo', 'prop', 'vec'))
KEY = 'mc_manual_intake_solo'
FOCUS = str(globals().get('MC_MANUAL_INTAKE_FOCUS','full'))
assert FOCUS in ('full','throws'), 'Supported focus: full or throws'
CHECK_SPASM = bool(globals().get('MC_MANUAL_INTAKE_SPASM',FOCUS == 'full'))
CHECK_OVERBOARD = bool(globals().get('MC_MANUAL_INTAKE_OVERBOARD',FOCUS == 'full'))
CAPTURE_DIMENSIONS = ((1280,720),(1286,760))
# UE5.8's floating Win64 window reports a 722px game viewport while its SHOWUI
# PNG includes the 1286x760 window frame. Keep these two measurements distinct.
GAME_VIEWPORT_DIMENSIONS = ((1280,720),(1280,722))
HIT_FIELDS = ('blocking_hit', 'initial_overlap', 'time', 'distance', 'location', 'impact_point',
              'normal', 'impact_normal', 'phys_mat', 'actor', 'component', 'hit_bone_name',
              'bone_name', 'item', 'element_index', 'face_index', 'trace_start', 'trace_end')


def valid(obj):
    # A consumed ingredient can already have an invalidated Python wrapper by
    # the next sample. Such a destroyed actor is not a live fixture.
    try:
        return obj is not None and u.SystemLibrary.is_valid(obj)
    except (TypeError, RuntimeError):
        return False


def path(obj):
    return obj.get_path_name() if valid(obj) else None


class ManualIntakeSolo(Base):
    def __init__(self):
        super().__init__()
        self.throat = self.collection = None
        self.fixtures = []
        self.throw_held = False
        self.replay_held = False
        self.clip = None
        self.clip_until = 0.
        self.focus = FOCUS
        self.report.update(scope='One standalone L_Mouth PIE; ordinary first-perk, E collection, Q throws and watergun input',
            fixture_tuning={}, mode='manual_intake', clips={}, source_sha256={}, run_directory=str(RUN),
            limitations=['Small broccoli fragment actors and player/review-camera positions are isolated PIE fixtures.',
                         'The unrelated day scheduler is paused after the first perk; throw speed, cadence, gravity and uvula response are unchanged.',
                         'The review CameraActor uses the ordinary ServerCommitSprayView camera-input RPC, because LocalSprayView intentionally accepts only the hero view target.',
                         'The full client Q/held-Q checks run first. A separate three-piece visual replay uses the existing server input RPC so an observer can show the character, projectiles and opening together.',
                         'Gum contact is checked by actual capsule queries against the newly cooked authored triangles; this is not a full traversal playthrough.',
                         'No tutorial sequence, network behavior, build/cook/package or editor package save is claimed by this probe.'])
        self.report.update(focus=self.focus,check_spasm=CHECK_SPASM,check_overboard=CHECK_OVERBOARD)
        if self.focus == 'throws':
            self.report['scope'] = 'Focused standalone Q throws on the current user-authored mouth placement; no uvula/gum checks'
            self.report['not_run'] = ['uvula watergun','gum collision/contact']
            if not CHECK_OVERBOARD:
                self.report['not_run'].append('spoiled overboard')
            if not CHECK_SPASM:
                self.report['not_run'].append('spasm regression')
            self.report['limitations'] = [line for line in self.report['limitations']
                if 'watergun' not in line.lower() and 'ServerCommitSprayView' not in line and 'Gum contact' not in line]
        for name in ('Tools/Unreal/check_manual_intake_solo.py', 'Source/MessControl/Private/MCThroat.cpp',
                     'Source/MessControl/Private/MCFoodCollectionComponent.cpp', 'Source/MessControl/Private/MCToothCharacter.cpp'):
            self.report['source_sha256'][name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        # These RPC parameters are distinct reflected structs. Their Python
        # constructors invoke Engine's NativeMakeFunc; plain Vector is rejected.
        # Check construction before this probe requests its own PIE.
        self.report['camera_rpc_struct_check'] = dict(
            origin_type=type(u.Vector_NetQuantize(1., 2., 3.)).__name__,
            direction_type=type(u.Vector_NetQuantizeNormal(1., 0., 0.)).__name__)
        hit_schema = self.statics.call_method('BreakHitResult', args=(u.HitResult(),))
        self.require(len(hit_schema) == len(HIT_FIELDS) and isinstance(hit_schema[0], bool)
                     and isinstance(hit_schema[5], u.Vector) and isinstance(hit_schema[7], u.Vector),
                     'UE HitResult native-break signature changed; review its actual outputs')
        self.report['hit_result_marshal_check'] = dict(method='GameplayStatics.BreakHitResult', outputs=len(hit_schema))

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report.json').write_text(data, encoding='utf-8')

    def screenshot(self, name, sequence=False):
        super().screenshot(name, sequence)
        entry = self.report['frames' if sequence else 'screenshots'][-1]
        entry['clip_window'] = self.clip
        entry['camera_evidence'] = self.camera_evidence()
        entry['requested_wall_seconds'] = time.monotonic()-self.started
        entry['game_viewport_size'] = list(self.pc.get_viewport_size())
        if sequence and self.clip in ('manual_intake_reticle', 'manual_intake_throw'):
            entry['composition_evidence'] = self.hole_composition()
            if self.clip == 'manual_intake_reticle':
                self.require(entry['composition_evidence']['actual_food_aim_strength'] > 0.,
                             'Captured reticle frame has no actual food-aim signal')
        if sequence and self.clip:
            self.report['clips'][self.clip].append(entry)

    def verify_pending_capture_dimensions(self):
        for entry in self.report['frames']:
            if entry.get('dimension_gate_passed'):
                continue
            file = Path(entry['file'])
            self.require(time.monotonic()-self.started-entry['requested_wall_seconds'] < 5.,
                         'Owned game viewport did not save a readable requested frame within five seconds')
            if not file.is_file():
                continue
            try:
                with file.open('rb') as stream:
                    header = stream.read(24)
            except OSError:
                continue
            if len(header) < 24:
                continue
            self.require(header[:8] == b'\x89PNG\r\n\x1a\n','Captured source is not a PNG')
            dimensions = (int.from_bytes(header[16:20],'big'),int.from_bytes(header[20:24],'big'))
            viewport = tuple(entry['game_viewport_size'])
            if dimensions not in CAPTURE_DIMENSIONS or viewport not in GAME_VIEWPORT_DIMENSIONS:
                self.report['capture_failure'] = dict(file=str(file),png_dimensions=list(dimensions),
                    actual_game_viewport=list(viewport),allowed_dimensions=CAPTURE_DIMENSIONS,
                    allowed_game_viewport_dimensions=GAME_VIEWPORT_DIMENSIONS,
                    reason='Shot SHOWUI captured the wrong/editor viewport or an unsupported client size')
                self.write()
                self.require(False,'Wrong capture viewport: PNG%s; actual game viewport%s. Select the owned PIE window before recording.' % (dimensions,viewport))
            entry['dimension_gate_passed'] = True

    def camera_evidence(self):
        if not valid(self.camera):
            return None
        manager = prop(self.pc, 'player_camera_manager')
        actual, expected = manager.get_camera_location(), self.camera.get_actor_location()
        rotation, target = manager.get_camera_rotation(), self.camera.get_actor_rotation()
        return dict(view_target=path(self.pc.get_view_target()), expected_target=path(self.camera),
            actual_cm=vec(actual), expected_cm=vec(expected), location_error_cm=math.dist(vec(actual), vec(expected)),
            rotation_error_degrees=max(abs((a-b+180.) % 360.-180.) for a, b in
                ((rotation.pitch, target.pitch), (rotation.yaw, target.yaw), (rotation.roll, target.roll))))

    def begin_clip(self, name, seconds):
        now = float(u.GameplayStatics.get_time_seconds(self.world))
        self.require(self.clip is None or now >= self.clip_until, 'Capture windows must not overlap')
        self.clip, self.clip_until = name, now+seconds
        self.report['clips'].setdefault(name, [])

    def sample(self, now):
        if not self.throat:
            return
        inventory = prop(self.hero, 'inventory')
        self.report['samples'].append(dict(time=now, phase=self.phase, hero=vec(self.hero.get_actor_location()),
            count=len(prop(self.collection, 'pieces')), swallowed=int(prop(self.throat, 'food_swallowed')),
            open=float(self.throat.open_amount()), tickle=float(prop(self.throat, 'tickle_started_at')),
            artist_open_morph=float(prop(self.throat,'authored_mouth').get_morph_target('Open')),
            uvula_transform=str(prop(self.throat, 'uvula').get_world_transform()),
            watergun=dict(selected=str(prop(inventory, 'selected')), pressure=bool(prop(inventory, 'pressure_mode')),
                ammo=int(prop(inventory, 'water_ammo')), last_shot=float(prop(inventory, 'last_water_shot_at')),
                aim=vec(inventory.spray_aim())),
            hero_alive=bool(prop(self.hero, 'status').is_alive()),
            foods=[dict(path=path(f), phase=str(prop(f, 'phase')), position=vec(f.get_actor_location()),
                        carrier=path(prop(f, 'stack_carrier'))) for f in self.fixtures if valid(f)]))

    def spawn_fragment(self, position, spoiled=False):
        pose = u.Transform(location=position)
        food = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world, u.MCFoodActor.static_class(), pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(food), 'Food fixture spawn failed')
        row = prop(food, 'food_data')
        row.set_editor_property('kind', u.MCFoodKind.FOOD)
        row.set_editor_property('fragment_scale', u.Vector(.25, .25, .25))
        row.set_editor_property('scale', u.Vector(.25, .25, .25))
        row.set_editor_property('spoil_seconds', 600.)
        food.set_editor_property('food_data', row)
        food.set_editor_property('item_name', 'ManualIntakeFixture')
        food.set_editor_property('item_mesh', u.load_asset('/Game/Stylized_Vegetables/Meshes/SM_Broccoli'))
        food.set_editor_property('fragment', True)
        food.set_editor_property('spoiled', spoiled)
        food.set_editor_property('batch', 191017)
        self.statics.call_method('FinishSpawningActor', args=(food, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.fixtures.append(food)
        return food

    def throw(self, held):
        self.hero.call_method('SetThrowInputHeld', args=(bool(held),))
        self.throw_held = bool(held)

    def aim(self, target, camera_origin=None):
        origin = camera_origin or (self.hero.get_actor_location()+u.Vector(-350, 0, 150))
        self.frame_camera(origin, target)
        self.pc.set_control_rotation(u.MathLibrary.find_look_at_rotation(self.hero.get_actor_location(), target))

    def clear_camera_line(self, origin, point):
        ignore = [self.hero]+[food for food in self.fixtures if valid(food)]
        if valid(self.camera):
            ignore.append(self.camera)
        trace = u.SystemLibrary.line_trace_single(self.world, origin, point,
            u.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignore, u.DrawDebugTrace.NONE, True)
        hit = self.blocking_hit(trace)
        return hit is None or hit['distance'] >= math.dist(vec(origin), vec(point))-25.

    def project_review_point(self, point):
        pose = self.camera.get_actor_rotation()
        forward, right, up = (u.MathLibrary.get_forward_vector(pose),
                              u.MathLibrary.get_right_vector(pose), u.MathLibrary.get_up_vector(pose))
        delta = point-self.camera.get_actor_location()
        dot = lambda a, b: a.x*b.x+a.y*b.y+a.z*b.z
        depth = dot(delta, forward)
        if depth <= 1.:
            return None
        fov = float(self.camera.get_component_by_class(u.CameraComponent).field_of_view)
        half = math.tan(math.radians(fov*.5))
        return [.5+.5*dot(delta, right)/(depth*half), .5-.5*dot(delta, up)/(depth*half*.5625)]

    def hole_composition(self):
        hero = self.hero.get_actor_location()+u.Vector(0, 0, 20)
        points = dict(hero=hero,opening=self.visible_target,
            hero_head=self.hero.get_actor_location()+u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()),
            hero_feet=self.hero.get_actor_location()-u.Vector(0,0,self.hero.capsule_component.get_scaled_capsule_half_height()))
        points.update({'rim_%02d'%i:p for i,p in enumerate(self.visible_aperture_points)})
        return dict(projected_uv={name:self.project_review_point(point) for name,point in points.items()},
            hero_line_clear=self.clear_camera_line(self.camera.get_actor_location(), hero),
            opening_line_clear=self.clear_camera_line(self.camera.get_actor_location(), self.visible_target),
            actual_food_aim_strength=float(self.hero.call_method('FoodIntakeAimStrength', args=())))

    def hole_review_camera(self):
        # High side view clears the rising foreground tongue while keeping the
        # low final-slope character and the full visible aperture in one frame.
        target = self.visible_target
        chosen = None
        for offset in (u.Vector(-1450,0,520),u.Vector(-1450,450,520),u.Vector(-1450,-450,520),
                       u.Vector(-1050,0,520),u.Vector(-850,450,460),u.Vector(-850,-450,460),
                       u.Vector(-620, 1110, 462), u.Vector(-620, -1110, 462),
                       u.Vector(-920, 1410, 562), u.Vector(-920, -1410, 562)):
            origin = self.target+offset
            if not self.clear_camera_line(origin, self.visible_target) or not self.clear_camera_line(
                    origin, self.hero.get_actor_location()+u.Vector(0, 0, 20)):
                continue
            self.frame_camera(origin, target)
            self.camera.get_component_by_class(u.CameraComponent).set_field_of_view(90.)
            composition = self.hole_composition()
            uv = composition['projected_uv']
            direction = u.MathLibrary.get_forward_vector(self.camera.get_actor_rotation())
            predicted_reticle = float(self.throat.intake_aim_strength(origin,direction))
            if predicted_reticle > 0. and all(point and .22 < point[0] < .92 and .18 < point[1] < .81 for point in uv.values()):
                chosen = dict(origin=vec(origin), target=vec(target), composition=composition)
                break
        self.require(chosen is not None, 'No unoccluded review camera frames both the player and complete opening')
        self.report.setdefault('review_camera_fixtures', []).append(chosen)

    def replay_throw(self, held, direction):
        self.hero.call_method('ServerSetThrowHeld', args=(bool(held),
            u.Vector_NetQuantizeNormal(direction.x, direction.y, direction.z)))
        self.replay_held = bool(held)

    def prepare_visual_replay(self):
        self.require(self.report['checks'].get('held_q_series_delivers_one_by_one')
                     and self.report['checks'].get('physical_hole_entry_scores'),
                     'Visual replay must not replace the original client Q/held-Q checks')
        self.move_to(self.base)
        for side in (-25, 0, 25):
            self.spawn_fragment(self.hero.get_actor_location()+u.Vector(85, side, -25))
        self.hero.call_method('ServerToggleFoodCollection', args=())
        self.report['visual_throw_replay'] = dict(disclosed_fixture=True, separate_pieces=3,
            client_input_checks_already_passed=True,
            input_path='Existing ServerSetThrowHeld RPC; native cadence, trajectory, collision and scoring',
            swallowed_before=int(prop(self.throat, 'food_swallowed')))
        self.stage('visual_replay_collect')

    def overboard_review_camera(self):
        hero = self.hero.get_actor_location()
        for offset in (u.Vector(350, 550, 350), u.Vector(350, -550, 350), u.Vector(500, 850, 550)):
            eye = hero+offset
            if self.clear_camera_line(eye, hero+u.Vector(0,0,25)):
                self.frame_camera(eye, hero+u.Vector(-180,0,100))
                return
        self.require(False, 'No unoccluded overboard review view of the player')

    def prepare_spasm_regression(self):
        self.require(self.clip is None and int(prop(self.throat,'food_swallowed')) == self.expected_good_swallowed,
                     'Run the spasm check only after the five ordinary capture windows')
        self.require(prop(self.throat,'throat_phase') == u.MCThroatPhase.COLLECTING,
                     'A previous event still owns the throat')
        self.move_to(self.base)
        self.gag_bad = self.spawn_fragment(self.hero.get_actor_location()+u.Vector(85,0,-25),True)
        self.report['spasm_regression'] = dict(input_path='Spoiled fragment fixture, ordinary E collection and client Q',
            fixture_food=path(self.gag_bad),fixture_floor_cm=vec(self.base),
            spasm_count_before=int(prop(self.throat,'spasm_count')),
            vomit_count_before=int(prop(self.throat,'vomit_count')),samples=[])
        self.hero.call_method('ServerToggleFoodCollection',args=())
        self.stage('spasm_collect')

    def record_spasm_presentation(self, now):
        opened = float(self.throat.open_amount())
        expansion = float(prop(self.throat,'manual_authored_expansion'))*opened
        expected = 1-expansion if prop(self.throat,'reverse_authored_open') else expansion
        actual = float(prop(self.throat,'authored_mouth').get_morph_target('Open'))
        barrier = prop(self.throat,'closed_barrier').get_collision_enabled()
        self.report['spasm_regression']['samples'].append(dict(time=now,logical_open=opened,
            actual_open_morph=actual,expected_open_morph=expected,
            morph_error=abs(actual-expected),barrier=str(barrier)))
        # Native presentation and the observer callback can straddle one frame
        # of the fast gag pulse. This bound is far below the .16+ regression.
        self.require(abs(actual-expected) < .04,'Spasm no longer contracts the fixed artist opening')
        self.require(barrier == u.CollisionEnabled.QUERY_AND_PHYSICS,'Spasm closes visually without enabling its barrier')

    def native_hit(self, trace):
        """Unpack the supported UE5.8 NativeBreak surface, never private fields."""
        values = list(trace) if isinstance(trace, (tuple, list)) else [trace]
        hit = next((value for value in values if isinstance(value, u.HitResult)), None)
        if hit is None:
            return None
        fields = self.statics.call_method('BreakHitResult', args=(hit,))
        self.require(len(fields) == len(HIT_FIELDS), 'Unexpected native HitResult break outputs')
        return dict(zip(HIT_FIELDS, fields))

    def blocking_hit(self, trace):
        hit = self.native_hit(trace)
        return hit if hit and hit['blocking_hit'] else None

    def component_hit(self, trace, component):
        # K2_LineTraceComponent returns its geometric bDidHit. UE Python packs
        # bool false as None, true as its four out values. This component-only
        # query does not classify a channel response, so bBlockingHit may remain
        # false even with a real triangle hit (PrimitiveComponentPhysics.cpp1461).
        if trace is None or trace is False:
            return None
        self.require(isinstance(trace, (tuple, list)) and len(trace) == 4
                     and isinstance(trace[0], u.Vector) and isinstance(trace[1], u.Vector),
                     'Unexpected K2_LineTraceComponent success output')
        hit = self.native_hit(trace)
        self.require(hit and hit['component'] == component and 0 <= hit['time'] <= 1
                     and math.dist(vec(trace[0]), vec(hit['location'])) < 1.
                     and math.dist(vec(trace[1]), (0., 0., 0.)) > .5,
                     'Component trace success lacks the expected geometric contact')
        return hit

    def floor(self, point):
        component = prop(self.tongue, 'surface')
        trace = component.line_trace_component(
            point+u.Vector(0, 0, 2500), point-u.Vector(0, 0, 2500), True, False, False)
        hit = self.component_hit(trace, component)
        return hit['impact_point'] if hit else None

    @staticmethod
    def clip_ring_above(points, minimum_z):
        result = []
        previous = points[-1]
        for point in points:
            before,after = previous.z >= minimum_z,point.z >= minimum_z
            if before != after:
                fraction = (minimum_z-previous.z)/(point.z-previous.z)
                result.append(previous+(point-previous)*fraction)
            if after:
                result.append(point)
            previous = point
        return result

    def aperture_clearance(self, point):
        dot = lambda a,b:a.x*b.x+a.y*b.y+a.z*b.z
        project = lambda value:(dot(value-self.target,self.aperture_right),dot(value-self.target,self.aperture_up))
        px,py = project(point)
        polygon = [project(v) for v in self.aperture_world_points]
        previous = polygon[-1]
        inside,clearance = False,float('inf')
        for current in polygon:
            ax,ay = previous
            bx,by = current
            if (ay > py) != (by > py) and px < (bx-ax)*(py-ay)/(by-ay)+ax:
                inside = not inside
            dx,dy = bx-ax,by-ay
            denominator = dx*dx+dy*dy
            t = min(1.,max(0.,((px-ax)*dx+(py-ay)*dy)/denominator)) if denominator > .000001 else 0.
            clearance = min(clearance,math.hypot(px-(ax+dx*t),py-(ay+dy*t)))
            previous = current
        return clearance if inside else -clearance

    def setup(self):
        throats = self.actors(u.MCThroat)
        self.require(len(throats) == 1, 'Expected one authored native throat')
        self.throat, self.tongue = throats[0], self.actors(u.MCTongue)[0]
        self.collection = prop(self.hero, 'food_collection')
        for scheduler in self.actors(u.MCSingleDayDirector):
            scheduler.set_actor_tick_enabled(False)
        gs = u.GameplayStatics.get_game_state(self.world)
        for name, value in {'dev_manual_events': True, 'phase': u.MCShiftPhase.WORKING, 'phase_ends_at': 0.}.items():
            gs.set_editor_property(name, value)
        self.require(self.throat.open_amount() >= .99, 'Throat is not open by default')
        self.require(prop(self.throat, 'closed_barrier').get_collision_enabled() == u.CollisionEnabled.NO_COLLISION, 'Closed barrier still blocks open throat')
        self.require(all(not prop(z, 'enable_legacy_delivery_zone') for z in self.actors(u.MCFoodDisposal)), 'An automatic delivery/disposal cap is still enabled')
        forbidden = [c for a in self.actors(u.MCFoodDisposal) for c in a.get_components_by_class(u.PrimitiveComponent)
                     if c.get_name() in ('DeliveryFloorGuide', 'DeliveryArrowGuide', 'DeliveryCaption') and c.is_visible()]
        self.require(not forbidden and not prop(self.throat, 'zone_ring').is_visible(), 'Retired zone guide is visible')
        self.report['checks'].update(default_open_no_barrier=True, no_legacy_automatic_caps_or_guides=True)
        # The aim helper exposes the world point actually used by the native hole.
        result = self.throat.get_intake_aim_target()
        values = list(result) if isinstance(result, (tuple, list)) else [result]
        self.target = next(v for v in values if isinstance(v, u.Vector))
        self.radius = next(float(v) for v in values if isinstance(v, (float, int)) and not isinstance(v, bool))
        self.start_swallowed = int(prop(self.throat, 'food_swallowed'))
        mouth = prop(self.throat,'authored_mouth')
        self.require(mouth.get_skeletal_mesh_asset().get_path_name() == '/Game/FromBlender4/SK_Exit.SK_Exit',
                     'Use the original artist exit, without a replacement mesh')
        art = mouth.get_world_transform()
        measured = json.loads((ROOT/'Tools/Unreal/manual_intake_aperture.json').read_text())
        self.require(hashlib.sha256((ROOT/'Content/FromBlender4/SK_Exit.uasset').read_bytes()).hexdigest()
                     == measured['source_saved_package_sha256'],'Existing artist mesh changed after the front-ring measurements')
        expansion = float(prop(self.throat,'manual_authored_expansion'))
        self.fixed_morph = 1-expansion if prop(self.throat,'reverse_authored_open') else expansion
        self.require(abs(mouth.get_morph_target('Open')-self.fixed_morph) < .001,
                     'Existing artist morph does not use the fixed expansion setting')
        self.aperture_points = [u.Vector(*(a*(1-self.fixed_morph)+b*self.fixed_morph for a,b in zip(v,w)))
                               for v,w in zip(measured['basis'],measured['open'])]
        local_center = u.Vector(*(a*(1-self.fixed_morph)+b*self.fixed_morph
                                 for a,b in zip(measured['basis_area_center'],measured['open_area_center'])))
        expected_center = u.MathLibrary.transform_location(art,local_center)
        self.require(math.dist(vec(expected_center),vec(self.target)) < .1,'Native aim target disagrees with actual fixed morph geometry')
        self.inward = u.MathLibrary.transform_direction(art,u.Vector(*measured['normal']))
        self.aperture_up = u.MathLibrary.transform_direction(art,u.Vector(*measured['up']))
        self.aperture_right = u.MathLibrary.transform_direction(art, u.Vector(1, 0, 0))
        dot = lambda a,b:a.x*b.x+a.y*b.y+a.z*b.z
        self.aperture_half_width = max(abs(v.x-local_center.x) for v in self.aperture_points)*abs(art.scale3d.x)
        self.aperture_half_height = max(abs(dot(v-local_center,u.Vector(*measured['up'])))
                                      for v in self.aperture_points)*abs(art.scale3d.z)
        self.report['fixed_artist_opening'] = dict(mesh=mouth.get_skeletal_mesh_asset().get_path_name(),
            editable_expansion=expansion,actual_open_morph=self.fixed_morph,
            world_transform=str(art),native_aim_center_cm=vec(self.target),
            half_width_cm=self.aperture_half_width,half_height_cm=self.aperture_half_height,
            visible_ring_world_cm=[vec(u.MathLibrary.transform_location(art,p)) for p in self.aperture_points])
        self.report['checks']['original_morph_fixed_and_native_geometry_matches'] = True
        self.aperture_world_points = [u.MathLibrary.transform_location(art,p) for p in self.aperture_points]
        guard_point = self.throat.get_actor_transform().transform_location(prop(self.throat,'zone_center'))
        guard_floor = self.floor(guard_point)
        self.minimum_intake_z = guard_floor.z-10. if guard_floor is not None else min(p.z for p in self.aperture_world_points)-1.
        self.visible_aperture_points = self.clip_ring_above(self.aperture_world_points,self.minimum_intake_z)
        self.require(len(self.visible_aperture_points) >= 3,'Current opening has no region above the unchanged native floor guard')
        self.visible_target = sum(self.visible_aperture_points,u.Vector())/len(self.visible_aperture_points)
        self.report['native_floor_guard'] = dict(sample_position_cm=vec(guard_point),
            sampled_floor_cm=vec(guard_floor) if guard_floor is not None else None,
            minimum_intake_z_cm=self.minimum_intake_z,upper_review_target_cm=vec(self.visible_target),
            acceptance_ring_above_guard_cm=[vec(p) for p in self.visible_aperture_points],
            note='The existing gameplay guard is measured, not changed. Camera framing shows its usable upper opening.')
        self.unthrown = self.spawn_fragment(self.visible_target+self.inward*90)
        if self.focus == 'throws':
            self.report.setdefault('capture_omissions',{}).update(
                uvula_watergun='Excluded by the explicitly selected throws focus',
                gum_contact='Excluded by the explicitly selected throws focus')
            self.prepare_stack()
            self.stage('throws_unthrown_guard')
            return
        uvula = prop(self.throat, 'uvula')
        self.require(prop(self.throat, 'use_authored_uvula') and uvula.static_mesh.get_name() == 'SM_InteractiveUvula', 'Old uvula is still in use')
        self.require(not [a for a in self.actors(u.StaticMeshActor) if a.static_mesh_component.static_mesh and a.static_mesh_component.static_mesh.get_name() == 'SM_MiniTong'], 'Duplicate decorative MiniTong remains')
        self.require(uvula.get_collision_enabled() == u.CollisionEnabled.QUERY_ONLY, 'New uvula lacks query-only shot collision')
        self.report['checks']['new_uvula_replaces_old_decoration'] = True
        self.uvula_target = uvula.get_world_location()-u.Vector(0, 0, 100)
        floor = self.floor(self.uvula_target-u.Vector(600, 0, 0))
        self.require(floor is not None, 'No supported approach for the authored uvula')
        self.move_to(floor)
        self.aim(self.uvula_target)
        self.pc.call_method('RequestDevAction', args=(u.MCDevAction.GRANT_ALL_TOOL_BOOSTERS, 0))
        self.stage('uvula_watergun')
        self.begin_clip('uvula_watergun', 3.)

    def prepare_stack(self):
        distances = (550, 650, 850, 450, 350, 250, 1050, 1250)
        candidates = [self.floor(self.target-u.Vector(distance, 0, 0)) for distance in distances]
        self.report['throw_approach_candidates'] = [dict(before_hole_cm=distance,
            supported_floor=vec(point) if point is not None else None) for distance, point in zip(distances, candidates)]
        self.base = next((p for p in candidates if p is not None), None)
        self.require(self.base is not None, 'No supported throw approach before the real hole')
        self.report['selected_throw_approach'] = dict(floor=vec(self.base),
            before_hole_cm=next(distance for distance, point in zip(distances, candidates) if point is not None),
            reason='Nearest supported tongue point, at least 250cm before the visible hole; native physics unchanged')
        self.move_to(self.base)
        for side in (-25, 0, 25):
            self.spawn_fragment(self.hero.get_actor_location()+u.Vector(85, side, -25))
        self.hero.call_method('ServerToggleFoodCollection', args=())
        self.aim(self.visible_target)
        self.stage('collect_stack')

    def native_arc(self, launch, speed, gravity, damping, clearance=25.):
        attempts, chosen = [], None
        depth = float(prop(self.throat,'intake_depth'))+10.
        safe_down = max(0.,self.aperture_half_height-clearance-15.)
        offsets = (0.,60.,-60.,100.,-100.,130.,-130.,180.,-180.,safe_down,-safe_down)
        for above in dict.fromkeys(offsets):
            if abs(above) > safe_down:
                continue
            goal = self.target+self.aperture_up*above+self.inward*depth
            rim_clearance = self.aperture_clearance(goal)
            if goal.z < self.minimum_intake_z+clearance or rim_clearance < clearance+10.:
                attempts.append(dict(aim_point=vec(goal),above_aperture_center_cm=above,
                    rejection='Below native floor guard plus fragment clearance, or too close to the measured artist rim',
                    actual_rim_clearance_cm=rim_clearance,required_rim_clearance_cm=clearance+10.,
                    minimum_intake_z_cm=self.minimum_intake_z,required_center_z_cm=self.minimum_intake_z+clearance))
                continue
            delta = goal-launch
            horizontal = math.hypot(delta.x,delta.y)
            candidates = []
            for degree in range(-100,801):
                angle = math.radians(degree*.1)
                fraction = damping*horizontal/(speed*math.cos(angle))
                if not 0 < fraction < 1:
                    continue
                flight = -math.log(1-fraction)/damping
                height = (speed*math.sin(angle)+gravity/damping)*fraction/damping-gravity*flight/damping
                candidates.append((abs(height-delta.z),angle,flight,height))
            maximum = max((c[3] for c in candidates),default=None)
            row = dict(launch=vec(launch),target=vec(self.target),aim_point=vec(goal),
                below_aperture_center_cm=-above,above_aperture_center_cm=above,
                actual_rim_clearance_cm=rim_clearance,inward_depth_cm=depth,delta_cm=vec(delta),
                horizontal_cm=horizontal,speed=speed,world_gravity=gravity,native_linear_damping=damping,
                estimated_max_height_at_distance_cm=maximum,
                estimated_min_height_error_cm=min((c[0] for c in candidates),default=None),
                maximum_height_reserve_cm=maximum-delta.z if maximum is not None else None)
            attempts.append(row)
            close = [c for c in candidates if c[0] < 5.]
            if close and maximum-delta.z >= 30.:
                chosen = (row,delta,horizontal,min(close,key=lambda c:c[1]))
                break
        return chosen,attempts

    def select_supported_throw_approach(self):
        pieces = list(prop(self.collection,'pieces'))
        self.require(len(pieces) == 3,'Approach selection requires the actual collected tower')
        capsule = self.hero.capsule_component
        radius,half = capsule.get_scaled_capsule_radius(),capsule.get_scaled_capsule_half_height()
        relative_z = min(p.get_actor_location().z-self.hero.get_actor_location().z for p in pieces)
        top = pieces[-1]
        damping = max(.0001,float(prop(top,'body').get_linear_damping()))
        speed = float(prop(self.collection,'throw_speed'))
        movement = self.hero.get_movement_component()
        gravity = abs(float(movement.get_gravity_z())/float(prop(movement,'gravity_scale')))
        extent = top.get_actor_bounds(False)[1]
        clearance = max(20.,min(45.,extent.z))
        rows,chosen = [],None
        for distance in (300,350,400,450,500,550,650):
            for side in (0.,160.,-160.,300.,-300.):
                floor = self.floor(self.target-u.Vector(distance,0,0)+u.Vector(0,side,0))
                row = dict(before_hole_cm=distance,side_offset_cm=side,supported_floor=vec(floor) if floor is not None else None)
                rows.append(row)
                if floor is None:
                    continue
                pose = floor+u.Vector(0,0,half+4.)
                trace = u.SystemLibrary.capsule_trace_single(self.world,pose,pose+u.Vector(0,0,.2),
                    radius,half,u.TraceTypeQuery.TRACE_TYPE_QUERY1,False,
                    [self.hero]+[p for p in self.fixtures if valid(p)],u.DrawDebugTrace.NONE,True)
                hit = self.blocking_hit(trace)
                row['capsule_clear'] = hit is None
                row['blocking_actor'] = path(hit['actor']) if hit else None
                row['hole_visible'] = self.clear_camera_line(pose+u.Vector(0,0,70),self.visible_target)
                if hit or not row['hole_visible']:
                    continue
                arc,attempts = self.native_arc(pose+u.Vector(0,0,relative_z),speed,gravity,damping,clearance)
                row['native_arc_attempts'] = attempts
                row['reachable_with_30cm_reserve'] = arc is not None
                if arc:
                    score = abs(arc[0]['above_aperture_center_cm'])+abs(side)*.05
                    if chosen is None or score < chosen[0]:
                        chosen = (score,floor,row)
        self.report['supported_throw_approach_search'] = dict(actual_capsule_radius_cm=radius,
            actual_capsule_half_height_cm=half,actual_minimum_tower_layer_height_cm=relative_z,
            actual_throw_speed_cm_s=speed,actual_damping=damping,candidates=rows)
        self.write()
        self.require(chosen is not None,'No supported, capsule-clear, visible approach has a usable native throw arc; review real designer throw strength')
        self.base = chosen[1]
        self.report['selected_throw_approach'] = chosen[2]
        self.move_to(self.base)

    def ballistic_aim(self, move_camera=True):
        top = list(prop(self.collection, 'pieces'))[-1]
        launch = top.get_actor_location()
        movement = self.hero.get_movement_component()
        speed = float(prop(self.collection, 'throw_speed'))
        gravity = abs(float(movement.get_gravity_z())/float(prop(movement, 'gravity_scale')))
        damping = max(.0001, float(prop(top, 'body').get_linear_damping()))
        extent = top.get_actor_bounds(False)[1]
        chosen,attempts = self.native_arc(launch,speed,gravity,damping,max(20.,min(45.,extent.z)))
        for row in attempts:
            row['remaining_pieces'] = len(prop(self.collection,'pieces'))
        self.report.setdefault('ballistic_aim_attempts', []).append(dict(phase=self.phase, candidate_points=attempts))
        self.report['ballistic_position_fixture'] = chosen[0] if chosen else attempts[-1]
        self.write()
        self.require(chosen is not None, 'No visible-aperture aim point has a reachable native throw arc with a 30cm height reserve')
        _row, delta, horizontal, (error, angle, flight, _height) = chosen
        direction = u.Vector(delta.x/horizontal*math.cos(angle), delta.y/horizontal*math.cos(angle), math.sin(angle))
        origin = launch-direction*200+u.Vector(0, 0, 260)
        if move_camera:
            self.aim(origin+direction*10000, origin)
        self.report['ballistic_position_fixture'].update(direction=vec(direction),
            estimated_flight_seconds=flight, estimated_height_error_cm=error)
        return direction

    def check_gums(self):
        gums = [a for a in self.actors(u.StaticMeshActor) if a.static_mesh_component.static_mesh and
                a.static_mesh_component.static_mesh.get_path_name().startswith('/Game/Gameplay/Arena/ManualIntake/SM_Gum_')]
        self.require(len(gums) == 2, 'Expected two exact-contact gum derivatives')
        rows = []
        for actor in gums:
            comp, (center, extent) = actor.static_mesh_component, actor.get_actor_bounds(False)
            mesh = comp.static_mesh
            self.require(prop(prop(mesh, 'body_setup'), 'collision_trace_flag') == u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE, 'Gum collision complexity not saved')
            found = None
            for fy in (-.65, 0, .65):
                for fz in (-.4, 0, .4):
                    start, end = center+u.Vector(-extent.x-100, extent.y*fy, extent.z*fz), center+u.Vector(extent.x+100, extent.y*fy, extent.z*fz)
                    trace = comp.line_trace_component(start, end, False, False, False)
                    hit = self.component_hit(trace, comp)
                    if hit:
                        point, normal = hit['impact_point'], hit['impact_normal']
                        ignore = [a for a in self.actors(u.Actor) if a != actor]
                        sweep = u.SystemLibrary.capsule_trace_single(self.world, point+normal*130, point-normal*25, 35., 70.,
                            u.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignore, u.DrawDebugTrace.NONE, True)
                        found = self.blocking_hit(sweep)
                        if found and found['actor'] == actor and found['component'] == comp:
                            break
                        found = None
                if found:
                    break
            self.require(found is not None, 'Authored gum triangles do not stop an actual capsule query: '+actor.get_name())
            rows.append(dict(actor=path(actor), mesh=path(mesh), impact=vec(found['impact_point']),
                             hit_actor=path(found['actor']), hit_component=path(found['component'])))
        self.report['gum_capsule_queries'] = rows
        self.report['checks']['new_exact_gum_collision_blocks_capsules'] = True
        # Find an exposed INNER rim from the playable tongue. The gum actor
        # bounds mostly include its underside; a camera around those bounds
        # looked at empty tongue instead of the contact being checked.
        lower = next(a for a in gums if 'Bot' in a.static_mesh_component.static_mesh.get_name())
        comp, (center, extent) = lower.static_mesh_component, lower.get_actor_bounds(False)
        self.gum_walk_component = comp
        self.gum_walk_base = None
        radius = self.hero.capsule_component.get_scaled_capsule_radius()
        half_height = self.hero.capsule_component.get_scaled_capsule_half_height()
        tested = 0
        contact = None
        ignored = [self.hero]+[food for food in self.fixtures if valid(food)]
        for fx in (-.6, -.4, -.15, .1, .35, .6):
            floor = self.floor(center+u.Vector(extent.x*fx, 0, 0))
            if floor is None:
                continue
            for side in (-1., 1.):
                for above in (15., 35., 65., 100., -75., -150., -250.):
                    tested += 1
                    start = floor+u.Vector(0, 0, above)
                    toward = u.Vector(0, side, 0)
                    trace = comp.line_trace_component(start, start+toward*(extent.y+300), False, False, False)
                    hit = self.component_hit(trace, comp)
                    if not hit:
                        continue
                    approach = self.floor(hit['impact_point']-toward*100)
                    if approach is None:
                        continue
                    pose = approach+u.Vector(0, 0, half_height+2)
                    sweep = u.SystemLibrary.capsule_trace_single(self.world, pose, pose+toward*180,
                        radius, half_height, u.TraceTypeQuery.TRACE_TYPE_QUERY1, False,
                        ignored, u.DrawDebugTrace.NONE, True)
                    block = self.blocking_hit(sweep)
                    if block and block['component'] == comp:
                        self.gum_walk_base, self.gum_walk_direction = approach, toward
                        contact = block['impact_point']
                        break
                if contact is not None:
                    break
            if contact is not None:
                break
        if contact is None:
            reason = 'No exposed gum rim was reachable from the supported tongue without another blocker in the bounded camera search.'
            self.report['gum_walk_attempt'] = dict(status='not_demonstrated', reason=reason,
                search_traces=tested, actual_contact_observed=False)
            self.report.setdefault('capture_omissions', {})['gum_contact'] = reason
            self.report['limitations'].append('Gum collision passed exact blocking capsule queries; an ordinary player/gum contact video was not demonstrated. '+reason)
            self.prepare_stack()
            return
        self.move_to(self.gum_walk_base)
        self.gum_walk_start = self.hero.get_actor_location()
        side = u.Vector(-self.gum_walk_direction.y, self.gum_walk_direction.x, 0)
        eye = self.gum_walk_start-self.gum_walk_direction*350+side*300+u.Vector(0, 0, 260)
        self.require(self.clear_camera_line(eye, self.gum_walk_start+u.Vector(0,0,20)), 'Gum review camera is occluded')
        self.frame_camera(eye, self.gum_walk_start+self.gum_walk_direction*90+u.Vector(0,0,10))
        self.gum_probe_at = 0.
        self.report['gum_walk_attempt'] = dict(placement=vec(self.gum_walk_base), contact_target=vec(contact),
            direction=vec(self.gum_walk_direction), search_traces=tested, component=path(comp),
            observed_movement_bases=[], supported_walk_observed=False, actual_contact_observed=False)
        self.stage('gum_walk_attempt')
        self.begin_clip('gum_contact', 2.5)

    def finalize_frames(self):
        required = ('intake_no_automatic_caps','manual_intake_reticle','manual_intake_throw')
        if CHECK_OVERBOARD:
            required += ('spoiled_overboard',)
        if self.focus == 'full':
            required += ('uvula_watergun',)
        if self.focus == 'full' and 'gum_contact' in self.report['clips']:
            required += ('gum_contact',)
        elif self.focus == 'full':
            self.require(self.report.get('capture_omissions', {}).get('gum_contact')
                         and self.report['checks'].get('new_exact_gum_collision_blocks_capsules'),
                         'A missing gum video must retain its explicit omission and successful capsule collision evidence')
        self.require(all(len(self.report['clips'].get(name, [])) >= 8 for name in required), 'A curated clip has fewer than eight actual-camera frames')
        for entry in self.report['frames']:
            disk = Path(entry['file'])
            self.require(disk.is_file(), 'Engine capture missing: '+str(disk))
            data = disk.read_bytes()
            self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Capture is not a PNG')
            entry.update(sha256=hashlib.sha256(data).hexdigest(), width=int.from_bytes(data[16:20], 'big'), height=int.from_bytes(data[20:24], 'big'))
            self.require((entry['width'],entry['height']) in CAPTURE_DIMENSIONS,'Final frame is not an allowed game-client capture')
            camera = entry['camera_evidence']
            self.require(camera['view_target'] == camera['expected_target'] and camera['location_error_cm'] < 12. and camera['rotation_error_degrees'] < 5., 'Saved frame failed actual-camera gate')
        self.report['checks']['curated_clips_have_verified_source_frames'] = True

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 180., 'Bounded manual-intake probe timed out')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_standalone(worlds[0]), 'Expected one standalone owned PIE')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if not pc or not pc.get_controlled_pawn():
                    return
                self.world, self.pc = worlds[0], pc
                self.stage('first_perk')
                return
            if not self.owned_pie_is_active(worlds):
                self.finish('Owned PIE ended or was replaced externally')
                return
            self.verify_pending_capture_dimensions()
            self.hero = self.pc.get_controlled_pawn()
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            # Reward UI can pause the game clock while Slate is still ticking.
            if self.pc.is_reward_menu_open():
                if self.phase == 'first_perk':
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                elif self.phase != 'perk_resolution':
                    self.report.setdefault('reward_ui_interruptions', []).append(dict(phase=self.phase, time=now))
                    self.pc.choose_reward_perk(0)
                return
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.phase_at
            if self.throat and now >= self.sample_at:
                self.sample_at = now+.1
                self.sample(now)
            if self.clip and now >= self.clip_until:
                self.clip = None
            if self.throat and self.clip and now >= self.frame_at:
                self.frame_at = now+1/self.capture_hz
                camera = self.camera_evidence()
                if camera and camera['view_target'] == camera['expected_target'] and camera['location_error_cm'] < 12. and camera['rotation_error_degrees'] < 5.:
                    self.screenshot('%05d.png' % len(self.report['frames']), True)
            if self.phase == 'first_perk':
                self.require(elapsed < 25., 'Normal first-perk UI never appeared')
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
            elif self.phase == 'perk_resolution' and not self.pc.is_reward_menu_open():
                self.report['checks']['normal_first_perk_resolved'] = True
                self.setup()
            elif self.phase == 'throws_unthrown_guard' and elapsed >= .8:
                self.require(int(prop(self.throat,'food_swallowed')) == self.start_swallowed,
                             'Unthrown loose food was automatically taken by the hole')
                self.report['checks']['unthrown_loose_food_never_scores'] = True
                if valid(self.unthrown):
                    self.unthrown.destroy_actor()
                self.stage('collect_stack')
            elif self.phase == 'uvula_watergun':
                inventory = prop(self.hero, 'inventory')
                if elapsed >= .5 and not self.primary:
                    if not inventory.has_upgrade(u.MCToolUpgrade.WATERGUN):
                        self.pc.call_method('RequestDevAction', args=(u.MCDevAction.GRANT_ALL_TOOL_BOOSTERS, 1))
                    inventory.server_select(u.MCToolSlot.SPRAY)
                    if not prop(inventory, 'pressure_mode'):
                        inventory.server_select(u.MCToolSlot.SPRAY)
                    # Native LocalSprayView requires ViewTarget == Hero. Our
                    # disclosed review CameraActor must send the same reliable
                    # view snapshot normally sent by UpdateSprayAim(true).
                    # Fire, muzzle sweep, hit actor and tickle stay native.
                    origin = self.camera.get_actor_location()
                    direction = u.MathLibrary.get_forward_vector(self.camera.get_actor_rotation())
                    inventory.call_method('ServerCommitSprayView', args=(
                        u.Vector_NetQuantize(origin.x, origin.y, origin.z),
                        u.Vector_NetQuantizeNormal(direction.x, direction.y, direction.z)))
                    self.report['watergun_camera_input_fixture'] = dict(origin=vec(origin), direction=vec(direction),
                        target=vec(self.uvula_target), native_aim=vec(inventory.spray_aim()),
                        method='Existing ServerCommitSprayView RPC; no direct shot or tickle call')
                    self.tickle_before = float(prop(self.throat, 'tickle_started_at'))
                    self.primary_input(True)
                if elapsed >= 3.:
                    self.primary_input(False)
                    self.require(float(prop(self.throat, 'tickle_started_at')) > self.tickle_before, 'Real watergun input did not tickle the new uvula')
                    self.report['checks']['real_watergun_animates_new_uvula'] = True
                    self.require(abs(prop(self.throat,'authored_mouth').get_morph_target('Open')-self.fixed_morph) < .001,
                                 'Fixed opening expanded again during ordinary gameplay/tickle')
                    self.report['checks']['fixed_expansion_does_not_breathe_or_animate_open'] = True
                    self.require(int(prop(self.throat, 'food_swallowed')) == self.start_swallowed, 'Unthrown loose food was automatically taken by the hole')
                    self.report['checks']['unthrown_loose_food_never_scores'] = True
                    self.unthrown.destroy_actor()
                    self.check_gums()
            elif self.phase == 'gum_walk_attempt':
                movement = self.hero.get_movement_component()
                base = movement.call_method('GetMovementBaseObject', args=())
                seen = self.report['gum_walk_attempt']['observed_movement_bases']
                if path(base) not in seen:
                    seen.append(path(base))
                attempt = self.report['gum_walk_attempt']
                if path(base) == path(self.gum_walk_component):
                    attempt['actual_contact_observed'] = True
                if now >= self.gum_probe_at:
                    self.gum_probe_at = now+.1
                    position = self.hero.get_actor_location()
                    capsule = self.hero.capsule_component
                    trace = u.SystemLibrary.capsule_trace_single(self.world, position,
                        position+self.gum_walk_direction*45, capsule.get_scaled_capsule_radius(),
                        capsule.get_scaled_capsule_half_height(), u.TraceTypeQuery.TRACE_TYPE_QUERY1,
                        False, [self.hero], u.DrawDebugTrace.NONE, True)
                    hit = self.blocking_hit(trace)
                    if hit and hit['component'] == self.gum_walk_component and hit['distance'] < 35.:
                        attempt['actual_contact_observed'] = True
                if .35 < elapsed < 1.25:
                    self.hero.add_movement_input(self.gum_walk_direction, .5, False)
                if elapsed >= 2.6:
                    movement.stop_movement_immediately()
                    moved = math.dist(vec(self.hero.get_actor_location()), vec(self.gum_walk_start))
                    attempt.update(displacement_cm=moved, supported_walk_observed=path(self.gum_walk_component) in seen and moved > 10.)
                    if attempt['actual_contact_observed']:
                        self.report['checks']['ordinary_movement_contacts_visible_gum'] = True
                    else:
                        reason = 'The bounded movement attempt did not demonstrate actual player/gum contact; its frames are retained as rejected evidence.'
                        self.report.setdefault('rejected_clips', {})['gum_contact'] = self.report['clips'].pop('gum_contact', [])
                        self.report.setdefault('capture_omissions', {})['gum_contact'] = reason
                        self.report['limitations'].append(reason)
                    self.prepare_stack()
            elif self.phase == 'collect_stack':
                self.require(elapsed < 8., 'E collection did not collect the three ordinary fragment fixtures')
                if len(prop(self.collection, 'pieces')) == 3 and elapsed >= 1.:
                    self.report['checks']['native_E_collection_three_piece_tower'] = True
                    old_center = self.throat.get_actor_transform().transform_location(prop(self.throat, 'zone_center'))
                    old_floor = self.floor(old_center)
                    self.require(old_floor is not None, 'Old zone has no supported point for the no-auto-delivery regression')
                    self.move_to(old_floor)
                    self.aim(self.visible_target)
                    self.stage('held_near_hole')
                    self.begin_clip('intake_no_automatic_caps', 1.4)
            elif self.phase == 'held_near_hole' and elapsed >= 1.5:
                self.require(len(prop(self.collection, 'pieces')) == 3 and int(prop(self.throat, 'food_swallowed')) == self.start_swallowed, 'Held stack auto-delivered before Q')
                self.report['checks']['held_stack_never_auto_delivers'] = True
                self.report['aim_with_food'] = float(self.hero.call_method('FoodIntakeAimStrength', args=()))
                self.require(self.report['aim_with_food'] > 0., 'Hole/near-hole camera aim failed to reveal the food reticle')
                self.report['checks']['held_food_near_hole_reveals_reticle'] = True
                self.select_supported_throw_approach()
                self.hole_review_camera()
                self.stage('throw_approach_settle')
            elif self.phase == 'throw_approach_settle' and elapsed >= .4:
                self.stage('reticle_camera_look')
                self.begin_clip('manual_intake_reticle', 1.5)
            elif self.phase == 'reticle_camera_look' and elapsed >= 1.6:
                self.ballistic_aim()
                self.stage('aim_camera_settle')
            elif self.phase == 'aim_camera_settle' and elapsed >= .4:
                self.report['aim_during_ballistic_throw_fixture'] = float(self.hero.call_method('FoodIntakeAimStrength', args=()))
                self.throw(True)
                self.throw(False)
                self.stage('single_q_throw')
            elif self.phase == 'single_q_throw' and elapsed >= .15:
                self.require(len(prop(self.collection, 'pieces')) == 2, 'One Q press did not release exactly the top piece')
                self.report['checks']['Q_releases_exactly_one_top_piece'] = True
                self.stage('single_q_arrival')
            elif self.phase == 'single_q_arrival':
                self.require(elapsed < 5., 'Ordinary Q throw never physically reached the hole')
                if int(prop(self.throat, 'food_swallowed')) == self.start_swallowed+1:
                    self.report['checks']['physical_hole_entry_scores'] = True
                    self.ballistic_aim()
                    self.stage('held_q_camera_settle')
            elif self.phase == 'held_q_camera_settle' and elapsed >= .3:
                self.throw(True)
                self.stage('held_q_series')
            elif self.phase == 'held_q_series':
                self.require(elapsed < 6., 'Held Q did not finish the remaining native throws')
                if not prop(self.collection, 'pieces') and int(prop(self.throat, 'food_swallowed')) == self.start_swallowed+3:
                    self.throw(False)
                    self.report['checks']['held_q_series_delivers_one_by_one'] = True
                    self.report['aim_empty_hands'] = float(self.hero.call_method('FoodIntakeAimStrength', args=()))
                    self.require(self.report['aim_empty_hands'] == 0., 'Reticle still active without held food')
                    self.report['checks']['empty_hands_hide_food_reticle'] = True
                    self.require(float(prop(prop(self.throat, 'suction_state'), 'duration')) == 0. and not self.actors(u.MCThroatVortex), 'A Q delivery started the retired arena suction')
                    self.report['checks']['manual_delivery_has_no_ambient_suction'] = True
                    self.prepare_visual_replay()
            elif self.phase == 'visual_replay_collect':
                self.require(elapsed < 8., 'Visual replay fixtures were not collected through E')
                if len(prop(self.collection, 'pieces')) == 3 and elapsed >= 1.:
                    self.replay_first_direction = self.ballistic_aim(move_camera=False)
                    self.hole_review_camera()
                    self.stage('visual_replay_camera_settle')
            elif self.phase == 'visual_replay_camera_settle' and elapsed >= .4:
                self.stage('visual_replay_start')
                self.begin_clip('manual_intake_throw', 5.5)
            elif self.phase == 'visual_replay_start' and elapsed >= .5:
                self.replay_throw(True, self.replay_first_direction)
                self.replay_throw(False, self.replay_first_direction)
                self.stage('visual_replay_single')
            elif self.phase == 'visual_replay_single':
                self.require(elapsed < 5., 'Visual replay single throw missed the physical opening')
                if int(prop(self.throat, 'food_swallowed')) == self.start_swallowed+4:
                    self.replay_series_direction = self.ballistic_aim(move_camera=False)
                    self.replay_throw(True, self.replay_series_direction)
                    self.stage('visual_replay_series')
            elif self.phase == 'visual_replay_series':
                self.require(elapsed < 6., 'Visual replay held RPC did not physically deliver the remaining pieces')
                if not prop(self.collection, 'pieces') and int(prop(self.throat, 'food_swallowed')) == self.start_swallowed+6:
                    self.replay_throw(False, self.replay_series_direction)
                    self.expected_good_swallowed = self.start_swallowed+6
                    self.report['visual_throw_replay']['swallowed_after'] = self.expected_good_swallowed
                    self.report['checks']['separate_visual_replay_three_physical_deliveries'] = True
                    self.stage('manual_throw_clip_hold')
            elif self.phase == 'manual_throw_clip_hold' and self.clip is None:
                if not CHECK_OVERBOARD:
                    if CHECK_SPASM:
                        self.prepare_spasm_regression()
                    else:
                        self.stage('capture_flush')
                    return
                center, extent = self.tongue.get_actor_bounds(False)
                self.discard_floor = next((point for distance in (450, 650, 850)
                    if (point := self.floor(u.Vector(center.x-extent.x+distance, center.y, center.z))) is not None), None)
                self.require(self.discard_floor is not None, 'No supported front-rim throw approach')
                self.move_to(self.discard_floor, 180.)
                self.bad = self.spawn_fragment(self.hero.get_actor_location()+u.Vector(-85, 0, -25), True)
                self.hero.call_method('ServerToggleFoodCollection', args=())
                self.stage('overboard_collect')
            elif self.phase == 'overboard_collect':
                self.require(elapsed < 5., 'Could not collect the small spoiled fixture through E')
                if len(prop(self.collection, 'pieces')) == 1 and elapsed >= 1.:
                    launch = self.bad.get_actor_location()
                    direction = u.Vector(-.8, 0, .6)
                    origin = launch+u.Vector(350, 0, 250)
                    self.aim(origin+direction*10000, origin)
                    self.stage('overboard_aim_settle')
            elif self.phase == 'overboard_aim_settle' and elapsed >= .4:
                self.require(float(self.hero.call_method('FoodIntakeAimStrength', args=())) == 0., 'Food reticle remained open while looking away from the hole')
                self.report['checks']['look_away_hides_food_reticle'] = True
                self.throw(True)
                self.throw(False)
                self.overboard_review_camera()
                self.stage('overboard_q_throw')
                self.begin_clip('spoiled_overboard', 3.5)
            elif self.phase == 'overboard_q_throw':
                self.require(elapsed < 10., 'The deliberate spoiled-food overboard throw was not disposed')
                if not valid(self.bad) or prop(self.bad, 'phase') == u.MCFoodPhase.DISPOSED:
                    self.require(int(prop(self.throat, 'food_swallowed')) == self.expected_good_swallowed, 'Discard wrongly scored as food delivery')
                    self.report['checks']['deliberate_bad_overboard_throw_disposes_without_delivery'] = True
                    self.stage('overboard_clip_hold')
            elif self.phase == 'overboard_clip_hold' and self.clip is None:
                if CHECK_SPASM:
                    self.prepare_spasm_regression()
                else:
                    self.stage('capture_flush')
            elif self.phase == 'spasm_collect':
                self.require(elapsed < 6.,'Native E did not collect the spoiled spasm fixture')
                if len(prop(self.collection,'pieces')) == 1 and elapsed >= 1.:
                    self.require(list(prop(self.collection,'pieces'))[0] == self.gag_bad,
                                 'Spasm fixture collected an unrelated ingredient')
                    self.ballistic_aim()
                    self.stage('spasm_aim_settle')
            elif self.phase == 'spasm_aim_settle' and elapsed >= .4:
                self.throw(True)
                self.throw(False)
                self.stage('spasm_native_arrival')
            elif self.phase == 'spasm_native_arrival':
                self.require(elapsed < 5.,'Real spoiled Q throw did not provoke the native spasm')
                if prop(self.throat,'throat_phase') == u.MCThroatPhase.SPASM:
                    row = self.report['spasm_regression']
                    self.require(int(prop(self.throat,'spasm_count')) == row['spasm_count_before']+1,
                                 'Observed spasm did not belong to the actual spoiled throw')
                    self.require(int(prop(self.throat,'food_swallowed')) == self.expected_good_swallowed,
                                 'Spoiled throat throw received a good-delivery score')
                    self.record_spasm_presentation(now)
                    self.report['checks']['spoiled_native_q_provokes_spasm'] = True
                    self.stage('spasm_native_recovery')
            elif self.phase == 'spasm_native_recovery':
                self.require(elapsed < 9.,'Native spasm/vomit cycle did not return to its fixed normal opening')
                phase = prop(self.throat,'throat_phase')
                if phase == u.MCThroatPhase.SPASM:
                    self.record_spasm_presentation(now)
                elif phase == u.MCThroatPhase.COLLECTING:
                    row = self.report['spasm_regression']
                    self.require(len(row['samples']) >= 3,'Spasm animation/barrier was not observed across multiple frames')
                    self.require(int(prop(self.throat,'vomit_count')) == row['vomit_count_before']+1,
                                 'Native gag skipped its existing vomit consequence')
                    actual = float(prop(self.throat,'authored_mouth').get_morph_target('Open'))
                    self.require(abs(actual-self.fixed_morph) < .001 and self.throat.open_amount() >= .99,
                                 'Gag recovery did not restore the fixed normal expansion')
                    self.require(prop(self.throat,'closed_barrier').get_collision_enabled() == u.CollisionEnabled.NO_COLLISION,
                                 'Recovered normal opening remains physically blocked')
                    self.require(int(prop(self.throat,'food_swallowed')) == self.expected_good_swallowed,
                                 'Native vomit/recovery wrongly awarded the spoiled ingredient')
                    row.update(status='passed',returned_morph=actual,
                        spasm_count_after=int(prop(self.throat,'spasm_count')),
                        vomit_count_after=int(prop(self.throat,'vomit_count')))
                    self.report['checks']['spasm_contracts_fixed_opening_and_enables_barrier'] = True
                    self.report['checks']['native_gag_recovers_to_fixed_expansion'] = True
                    self.stage('capture_flush')
            elif self.phase == 'capture_flush' and elapsed >= .8:
                self.finalize_frames()
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        if valid(self.hero) and self.owned_pie_is_active():
            try:
                if self.replay_held:
                    self.replay_throw(False, u.Vector(1,0,0))
                self.throw(False)
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


if __name__ == '__main__':
    previous = getattr(builtins, KEY, None)
    if previous and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve active manual-intake probe')
    probe = ManualIntakeSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
