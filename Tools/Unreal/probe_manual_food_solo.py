"""One owned solo PIE: real grape cut -> twenty pieces -> E pickup -> Q tap/hold.

Run only in idle L_Mouth after the approved Editor build. Copies the actual
DT_BreakfastMenu grape row into ONE transient fixture; overrides its count to
20 and keeps one existing fragment mesh variant. The saved table is untouched.
Normal held knife/pickaxe input causes the fracture; actual actors and Chaos
masses are counted. E's normal ServerToggleFoodCollection RPC gathers the load;
SetThrowInputHeld is the same input entry point used by Q. No native damage,
direct collection/release helpers, fabricated count, asset save or network test.

The unrelated single-day scheduler is paused only in this owned PIE. Observer
placement and a temporary review camera are disclosed fixtures. 12 Hz engine
SHOWUI captures use the existing preview encoder's frame schema.
Cancel: builtins.mc_manual_food_solo.finish('Stopped by operator').
"""
import builtins
import datetime
import hashlib
import json
import math
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUTPUT = Path(globals().get('MC_MANUAL_FOOD_OUTPUT', ROOT/'Saved/ManualFood_20261017/Capture')).resolve()
RUN = OUTPUT/'Runs'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='manual_food_support', init_globals={'MC_ENV_OUTPUT': RUN, 'MC_ENV_CAPTURE_HZ': 12})
Base, prop, vec = (support[name] for name in ('ApprovedEnvironmentSolo', 'prop', 'vec'))
KEY = 'mc_manual_food_solo'
BATCH = 917020
HIT_FIELDS = ('blocking_hit', 'initial_overlap', 'time', 'distance', 'location', 'impact_point',
              'normal', 'impact_normal', 'phys_mat', 'actor', 'component', 'hit_bone_name',
              'bone_name', 'item', 'element_index', 'face_index', 'trace_start', 'trace_end')


def valid(obj):
    try:
        return obj is not None and u.SystemLibrary.is_valid(obj)
    except (TypeError, RuntimeError):
        return False


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def asset_ref(value):
    if isinstance(value, str) and "'" in value:
        value = value.split("'", 1)[1].rstrip("'")
    return value


class ManualFoodSolo(Base):
    def __init__(self):
        super().__init__()
        self.scheduler = None
        self.scheduler_tick = None
        self.clip = None
        self.initial_stack = None
        self.last_stack = None
        self.tap_before = None
        self.tap_top = None
        self.tap_top_path = None
        self.release_at = None
        self.health_before = None
        self.next_whole_approach_at = -1.
        self.report.update(mode='manual_food', run_directory=str(RUN), clips={}, samples=[],
            fixture_tuning={'transient_grape_row_fragment_count': 20, 'existing_fragment_variants': 1},
            scope='One owned standalone authority PIE; actual cut, E pickup and Q input path',
            checks={}, limitations=[
                'Only the transient PIE ingredient uses count 20 and one existing grape fragment variant. The saved menu is not edited.',
                'ConfigureItem/Sanitize are covered separately by MessControl.Food.ManualIntake.TwentyFragmentsOneVariant; Python fixture initializes reflected row state before normal BeginPlay.',
                'Observer position, a temporary review camera and pausing the unrelated single-day scheduler isolate the short check; this is not a balance playthrough.',
                'No direct damage, SpawnFragments, Collect, ThrowOne or reward helper is called. No asset save, build, cook/package or network clients.',
                'Per-fragment score and perk behavior are intentionally unchanged; this probe does not rebalance delivery rewards.'])
        sources = ['Tools/Unreal/probe_manual_food_solo.py', 'Source/MessControl/Private/MCFoodEnvironment.cpp',
            'Source/MessControl/Private/MCFoodCollectionComponent.cpp', 'Source/MessControl/Private/MCDayPlan.cpp',
            'Source/MessControl/Private/MCToothCharacter.cpp']
        self.report['source_sha256'] = {s: hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources}
        hit_schema = self.statics.call_method('BreakHitResult',args=(u.HitResult(),))
        self.require(len(hit_schema)==len(HIT_FIELDS) and isinstance(hit_schema[0],bool)
                     and isinstance(hit_schema[5],u.Vector) and isinstance(hit_schema[7],u.Vector),
                     'UE HitResult native-break signature changed; inspect before owned PIE')
        self.report['hit_result_marshal_check'] = dict(method='GameplayStatics.BreakHitResult',outputs=len(hit_schema))

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_manual_food.json').write_text(data, encoding='utf-8')

    def begin_clip(self, name):
        self.clip = name
        self.report['clips'].setdefault(name, [])

    def screenshot(self, name, sequence=False):
        super().screenshot(name, sequence)
        entry = self.report['frames' if sequence else 'screenshots'][-1]
        manager = prop(self.pc, 'player_camera_manager')
        actual = manager.get_camera_location()
        expected = self.camera.get_actor_location()
        rotation, wanted = manager.get_camera_rotation(), self.camera.get_actor_rotation()
        angle = max(abs((a-b+180.)%360.-180.) for a, b in
            ((rotation.pitch, wanted.pitch), (rotation.yaw, wanted.yaw), (rotation.roll, wanted.roll)))
        entry.update(clip_window=self.clip, camera_evidence=dict(
            view_target=path(self.pc.get_view_target()), expected_target=path(self.camera),
            actual_cm=vec(actual), expected_cm=vec(expected),
            location_error_cm=math.sqrt(sum((a-b)**2 for a, b in zip(vec(actual), vec(expected)))),
            rotation_error_degrees=angle))
        if sequence and self.clip:
            self.report['clips'][self.clip].append(entry)

    def grape_fixture(self):
        table = u.load_asset('/Game/Data/DT_BreakfastMenu')
        self.require(valid(table), 'DT_BreakfastMenu missing')
        exported = u.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(exported, (tuple, list)):
            exported = next((part for part in exported if isinstance(part, str)), None)
        rows = json.loads(exported)
        # The saved grape ingredient currently retains its historical "Bacon"
        # row name. Identify the actual configured art as well as the row key.
        candidates = [row for row in rows if 'grape' in str(row.get('Name', '')).lower()
                      or any('grape' in str(mesh).lower() for mesh in row.get('WholeMeshes', []))]
        self.require(candidates, 'Actual menu has no grape row')
        row = dict(candidates[0])
        name = row.pop('Name')
        choices = [choice for choice in row.get('FragmentMeshes', []) if choice and choice != 'None']
        whole_choices = [choice for choice in row.get('WholeMeshes', []) if choice and choice != 'None']
        self.require(choices and whole_choices, 'Grape row lacks configured whole/fragment meshes')
        row['Fragments'] = 20
        row['FragmentMeshes'] = [choices[0]]
        whole = u.load_asset(asset_ref(whole_choices[0]))
        self.require(valid(whole), 'Selected actual grape whole mesh failed to load')
        mesh_box = whole.get_bounding_box()
        raw_extent = (mesh_box.max-mesh_box.min)*.5
        authored_scale = row['Scale']
        whole_extent = u.Vector(raw_extent.x*abs(float(authored_scale['X'])),
                                raw_extent.y*abs(float(authored_scale['Y'])),
                                raw_extent.z*abs(float(authored_scale['Z'])))
        surface = prop(self.tongue, 'surface')
        origin, extent, _ = u.SystemLibrary.get_component_bounds(surface)
        foot = None
        support = None
        for x, y in ((0., 0.), (.15, 0.), (-.15, 0.), (0., .15), (0., -.15)):
            candidate = self.floor(origin+u.Vector(extent.x*x, extent.y*y, 0))
            if candidate is None:
                continue
            # Keep the actual Scale=40 bunch above the complete supported patch.
            # The old fixed +70 cm fixture started its ~372 cm half-height
            # inside the tongue, producing an unrelated depenetration launch.
            neighbors = [self.floor(candidate+u.Vector(dx, dy, 0)) for dx, dy in
                         ((whole_extent.x, 0), (-whole_extent.x, 0),
                          (0, whole_extent.y), (0, -whole_extent.y),
                          (whole_extent.x, whole_extent.y), (-whole_extent.x, whole_extent.y),
                          (whole_extent.x, -whole_extent.y), (-whole_extent.x, -whole_extent.y))]
            if all(p is not None and abs(p.z-candidate.z)<85 for p in neighbors):
                foot = candidate
                support = [candidate]+neighbors
                break
        self.require(foot is not None, 'No broad supported tongue patch for grape review')
        spawn_location = u.Vector(foot.x, foot.y, max(point.z for point in support)+whole_extent.z+8.)
        pose = u.Transform(location=spawn_location)
        self.food = self.statics.call_method('BeginDeferredActorSpawnFromClass', args=(self.world,
            u.MCFoodActor.static_class(), pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(self.food), 'Transient grape fixture could not spawn')
        self.require(u.ToolsetLibrary.set_object_properties(self.food, json.dumps({'FoodData': row})),
                     'Could not assign the transient actual grape menu row')
        self.food.set_editor_property('item_name', name)
        self.food.set_editor_property('item_mesh', whole)
        self.food.set_editor_property('health', float(row['Health']))
        self.food.set_editor_property('batch', BATCH)
        self.statics.call_method('FinishSpawningActor', args=(self.food, pose, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.health_before = float(prop(self.food, 'health'))
        self.report['grape_fixture'] = dict(table=path(table), row=name, original_count=candidates[0].get('Fragments'),
            fixture_count=int(prop(prop(self.food, 'food_data'), 'fragments')), selected_variant=choices[0],
            whole_mesh=path(whole), actual_whole_mass_kg=float(prop(self.food, 'body').get_mass()),
            configured_whole_mass_kg=float(row['Mass']), food=path(self.food), batch=BATCH,
            native_health=self.health_before, resistance=row.get('Resistance'),
            authored_scale=authored_scale, mesh_scaled_half_extent_cm=vec(whole_extent),
            supported_floor_cm=[vec(point) for point in support], initial_root_location_cm=vec(spawn_location))
        self.require(self.report['grape_fixture']['fixture_count']==20, 'Fixture did not retain configured twenty fragments')
        self.target = foot
        self.place_for_food()
        self.frame_subjects([self.food, self.hero], 'configured_whole_and_cutter')
        slot = u.MCToolSlot.PICKAXE if str(row.get('Resistance', '')).lower().endswith('hard') else u.MCToolSlot.KNIFE
        self.hero.inventory.server_select(slot)
        self.begin_clip('grape_twenty_fragments')
        self.stage('grape_before')

    def place_for_food(self):
        now = float(u.GameplayStatics.get_time_seconds(self.world))
        if now<self.next_whole_approach_at:
            return
        self.next_whole_approach_at = now+.2
        point = self.food.get_actor_location() if valid(self.food) else self.target
        visual = prop(self.food, 'visual')
        center, extent, _ = u.SystemLibrary.get_component_bounds(visual)
        # Stay outside the actual whole-food body instead of standing 100 cm
        # from the centre of a bunch almost 500 cm wide.
        body = prop(self.food, 'body')
        body_center, body_extent, _ = u.SystemLibrary.get_component_bounds(body)
        approaches = []
        half_height = self.hero.capsule_component.get_scaled_capsule_half_height()+2.
        grip = prop(self.food, 'grip_surface')
        for forward, yaw, distance in ((u.Vector(1,0,0),0.,max(extent.x,body_extent.x)),
                                       (u.Vector(-1,0,0),180.,max(extent.x,body_extent.x)),
                                       (u.Vector(0,1,0),90.,max(extent.y,body_extent.y)),
                                       (u.Vector(0,-1,0),-90.,max(extent.y,body_extent.y))):
            foot = self.floor(center-forward*(distance+75.))
            if foot is None:
                continue
            hero_location = foot+u.Vector(0,0,half_height)
            for height in (-45.,15.,75.):
                # Match FindToolContact's real 180 cm chop, including its
                # 45 cm sphere tolerance. This reflected API reaches the same
                # component SweepComponent; it does not apply any damage.
                start = hero_location+forward*45.+u.Vector(0,0,height)
                end = hero_location+forward*135.+u.Vector(0,0,height)
                result = grip.sphere_trace_component(start,end,45.,True,False,False)
                hit = self.component_hit(result,grip)
                if hit:
                    impact = hit['impact_point']
                    tool_distance = (impact-hero_location).length()
                    projected = sum(a*b for a,b in zip(vec(impact-hero_location),vec(forward)))
                    if tool_distance<=180. and projected>=-2.:
                        approaches.append((tool_distance,foot,yaw,impact))
        self.report['whole_tool_approach_candidates'] = [dict(distance_cm=d,floor_cm=vec(f),yaw=y,
            actual_mesh_contact_cm=vec(hit)) for d,f,y,hit in approaches]
        self.report['whole_geometry_before_approach'] = self.food_geometry(self.food)
        self.require(approaches, 'No supported outside-body approach within ordinary 180 cm melee reach')
        # Prefer the genuine front-side approach when available, so the player
        # remains in front of the large bunch in the in-mouth +X review view.
        _, foot, yaw, _ = min(approaches,key=lambda approach:(abs(approach[2]),approach[0]))
        self.move_to(foot, yaw)
        self.pc.set_control_rotation(u.Rotator(yaw=yaw))
        self.report['last_whole_tool_approach'] = dict(floor_cm=vec(foot),
            hero_cm=vec(self.hero.get_actor_location()), whole_center_cm=vec(center),
            whole_visual_extent_cm=vec(extent), whole_body_extent_cm=vec(body_extent),
            minimum_body_horizontal_gap_cm=75.)

    def component_hit(self, trace, component):
        # A successful component-only geometric query can leave bBlockingHit
        # false because no channel response was classified. UE Python returns
        # None on K2's bool false, and its four output values on true. FHitResult
        # is inspected through the supported NativeBreak function, not private
        # reflected-property names (which UE5.8 does not expose to Python).
        if trace is None or trace is False:
            return None
        self.require(isinstance(trace,(tuple,list)) and len(trace)==4
                     and isinstance(trace[0],u.Vector) and isinstance(trace[1],u.Vector),
                     'Unexpected successful K2 component sweep output')
        native = next((value for value in trace if isinstance(value,u.HitResult)),None)
        self.require(native is not None,'Successful K2 component query lacks native HitResult')
        hit = self.native_hit(native)
        self.require(hit['component']==component and 0.<=hit['time']<=1.
                     and math.dist(vec(trace[0]),vec(hit['location']))<1.
                     and math.dist(vec(trace[1]),(0.,0.,0.))>.5,
                     'Successful component query lacks matching geometric contact')
        return hit

    def native_hit(self, trace):
        values = list(trace) if isinstance(trace,(tuple,list)) else [trace]
        native = next((value for value in values if isinstance(value,u.HitResult)),None)
        if native is None:
            return None
        fields = self.statics.call_method('BreakHitResult',args=(native,))
        self.require(len(fields)==len(HIT_FIELDS),'Unexpected native BreakHitResult outputs')
        return dict(zip(HIT_FIELDS,fields))

    def floor(self, point):
        component = prop(self.tongue,'surface')
        trace = component.line_trace_component(point+u.Vector(0,0,2500),point-u.Vector(0,0,2500),True,False,False)
        hit = self.component_hit(trace,component)
        return hit['impact_point'] if hit and hit['impact_normal'].z>.5 else None

    def fragments(self):
        return [f for f in self.actors(u.MCFoodActor) if valid(f) and int(prop(f, 'batch'))==BATCH
                and bool(prop(f, 'fragment')) and prop(f, 'phase')!=u.MCFoodPhase.DISPOSED]

    def food_geometry(self, food):
        origin, extent, radius = u.SystemLibrary.get_component_bounds(prop(food, 'visual'))
        body = prop(food, 'body')
        return dict(path=path(food), location_cm=vec(food.get_actor_location()),
                    visual_center_cm=vec(origin), visual_extent_cm=vec(extent), visual_radius_cm=float(radius),
                    body_extent_cm=vec(body.get_scaled_box_extent()),
                    velocity_cm_s=vec(body.get_physics_linear_velocity()), phase=str(prop(food, 'phase')))

    def frame_subjects(self, subjects, reason):
        """Fit real geometry and prove a clear camera inside the actual mouth.

        Actor bounds include editor-only camera/frustum helpers (~1010 cm), so
        the hero uses only Mesh/Capsule bounds. Prefer front-centre cameras that
        look into the mouth, and reject scenery occlusion with world queries.
        The native HUD remains visible; fitting its narrow gap is not allowed
        to push the camera through the outer gums.
        """
        bounds = []
        anchors = []
        for actor in subjects:
            if not valid(actor):
                continue
            if isinstance(actor, u.MCFoodActor):
                origin, extent, _ = u.SystemLibrary.get_component_bounds(prop(actor, 'visual'))
                bounds.append((vec(origin),vec(extent)))
                anchors.append((actor,origin))
            else:
                for component in (prop(actor,'mesh'),actor.capsule_component):
                    origin, extent, _ = u.SystemLibrary.get_component_bounds(component)
                    bounds.append((vec(origin),vec(extent)))
                anchors.append((actor,actor.get_actor_location()+u.Vector(0,0,25)))
        self.require(bounds, 'No visible subject bounds for review camera')
        low = [min(center[i]-extent[i] for center, extent in bounds) for i in range(3)]
        high = [max(center[i]+extent[i] for center, extent in bounds) for i in range(3)]
        center = [(a+b)*.5 for a, b in zip(low, high)]
        left, right_edge, bottom, top = -.90,.90,-.80,.80
        bias_x,bias_y = .08,.04
        trials = []
        selected = None
        for fov in (92.,100.,85.):
            for raw in ((-1.,0.,.48),(-1.,-.25,.60),(-1.,.25,.60),
                        (-1.,-.55,.65),(-1.,.55,.65),(-1.,0.,.95)):
                length = math.sqrt(sum(v*v for v in raw))
                offset = [v/length for v in raw]
                forward = [-v for v in offset]
                right_length = math.hypot(forward[0],forward[1])
                right = [-forward[1]/right_length,forward[0]/right_length,0.]
                up = [forward[1]*right[2]-forward[2]*right[1],
                      forward[2]*right[0]-forward[0]*right[2],
                      forward[0]*right[1]-forward[1]*right[0]]
                tan_h = math.tan(math.radians(fov)*.5)
                tan_v = tan_h/(16./9.)
                distance = 350.
                for x in (low[0],high[0]):
                    for y in (low[1],high[1]):
                        for z in (low[2],high[2]):
                            delta = [x-center[0],y-center[1],z-center[2]]
                            depth = sum(a*b for a,b in zip(delta,forward))
                            px = sum(a*b for a,b in zip(delta,right))
                            py = sum(a*b for a,b in zip(delta,up))
                            distance = max(distance,
                                (px-right_edge*tan_h*depth)/((right_edge-bias_x)*tan_h),
                                (left*tan_h*depth-px)/((bias_x-left)*tan_h),
                                (py-top*tan_v*depth)/((top-bias_y)*tan_v),
                                (bottom*tan_v*depth-py)/((bias_y-bottom)*tan_v))
                distance *= 1.08
                target = [c-r*bias_x*tan_h*distance-v*bias_y*tan_v*distance
                          for c,r,v in zip(center,right,up)]
                location = [a+b*distance for a,b in zip(target,offset)]
                visibility = self.camera_clearance(u.Vector(*location),anchors)
                trial = dict(camera_cm=location,target_cm=target,horizontal_fov_degrees=fov,**visibility)
                trials.append(trial)
                if visibility['passed']:
                    selected = trial
                    break
            if selected:
                break
        record = dict(reason=reason,
            subjects=[path(actor) for actor in subjects if valid(actor)],
            bounds_min_cm=low, bounds_max_cm=high, subject_center_cm=center,
            normalized_safe_rectangle=[left,bottom,right_edge,top],
            aspect_ratio=16./9.,fit_margin=1.08,candidates=trials,selected=selected)
        self.report.setdefault('camera_fixtures',[]).append(record)
        self.require(selected is not None,'No inside-mouth camera passes real subject/scenery visibility; inspect camera_fixtures')
        self.frame_camera(u.Vector(*selected['camera_cm']),u.Vector(*selected['target_cm']))
        camera = prop(self.camera,'camera_component')
        camera.set_editor_property('FieldOfView',selected['horizontal_fov_degrees'])
        camera.set_editor_property('aspect_ratio',16./9.)
        camera.set_editor_property('constrain_aspect_ratio',True)

    def camera_clearance(self, eye, anchors):
        floor = self.floor(eye)
        result = dict(passed=False,supported_camera_xy=vec(floor) if floor is not None else None,rays=[])
        if floor is None or not 130.<eye.z-floor.z<1600.:
            result['rejected']='Camera is outside upward tongue support or beyond interior height range'
            return result
        ignored = [actor for actor,_ in anchors]+([self.camera] if valid(self.camera) else [])
        occupancy = self.native_hit(u.SystemLibrary.sphere_trace_single(self.world,eye,eye+u.Vector(0,0,1),15.,
            u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignored,u.DrawDebugTrace.NONE,True))
        if occupancy and occupancy['blocking_hit']:
            result['rejected']='Camera sphere touches '+str(path(occupancy['actor']))
            return result
        selected = [anchors[0]]+[anchors[i] for i in range(1,len(anchors),max(1,(len(anchors)-1)//6))]
        for actor,point in selected:
            trace = self.native_hit(u.SystemLibrary.line_trace_single(self.world,eye,point,
                u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignored,u.DrawDebugTrace.NONE,True))
            scenery_clear = not trace or not trace['blocking_hit'] or (trace['impact_point']-point).length()<12.
            # Test the hero a second time without ignoring the food, so a
            # perfectly framed player hidden behind the large bunch is rejected.
            hero_trace = None
            hero_clear = True
            if actor==self.hero:
                hero_trace = self.native_hit(u.SystemLibrary.line_trace_single(self.world,eye,point,
                    u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[self.camera] if valid(self.camera) else [],u.DrawDebugTrace.NONE,True))
                hero_clear = not hero_trace or not hero_trace['blocking_hit'] or hero_trace['actor']==self.hero \
                    or (hero_trace['impact_point']-point).length()<20.
            result['rays'].append(dict(subject=path(actor),target_cm=vec(point),scenery_clear=scenery_clear,
                scenery_hit=path(trace['actor']) if trace and trace['blocking_hit'] else None,
                hero_clear=hero_clear,hero_first_hit=path(hero_trace['actor']) if hero_trace and hero_trace['blocking_hit'] else None))
            if not scenery_clear or not hero_clear:
                result['rejected']='A real world Visibility query occludes the subject'
                return result
        result['passed']=True
        return result

    def place_for_settled_fragments(self):
        """One disclosed observer placement; fragments retain their native poses."""
        pieces = self.fragments()
        reach = float(prop(prop(self.hero, 'food_collection'), 'collection_reach'))
        height = self.hero.capsule_component.get_scaled_capsule_half_height()+2.
        candidates = []
        for piece in pieces:
            point = piece.get_actor_location()
            for dx, dy in ((0., 0.), (-70., 0.), (70., 0.), (0., -70.), (0., 70.),
                           (-110., -70.), (-110., 70.)):
                foot = self.floor(point+u.Vector(dx, dy, 0.))
                if foot is None:
                    continue
                location = foot+u.Vector(0., 0., height)
                distances = sorted((p.get_actor_location()-location).length() for p in pieces)
                inside = sum(distance<=reach for distance in distances)
                comfortably_inside = sum(distance<=reach*.85 for distance in distances)
                candidates.append((inside, comfortably_inside, -sum(distances[:6]), foot, distances))
        self.require(candidates, 'No supported tongue approach to actual settled grape fragments')
        best = max(candidates, key=lambda c:c[:3])
        self.report['collection_fixture'] = dict(
            placement='One observer placement after the native fracture settles; food is not moved',
            floor_cm=vec(best[3]), collection_reach_cm=reach,
            fragments_in_reach=best[0], fragments_inside_85_percent_reach=best[1],
            distances_cm=best[4], fragments=[self.food_geometry(p) for p in pieces])
        self.require(best[0]>=6, 'Real grape fragments have no supported six-piece pickup cluster; see collection_fixture')
        self.move_to(best[3], 0.)
        self.pc.set_control_rotation(u.Rotator())
        self.frame_subjects([self.hero]+pieces, 'settled_real_fragments_and_pickup')

    def sample(self, now):
        collection = prop(self.hero, 'food_collection')
        self.report['samples'].append(dict(time=now, phase=self.phase,
            health=float(prop(self.food, 'health')) if valid(self.food) else None,
            fragments=len(self.fragments()), held=[path(f) for f in prop(collection, 'pieces')],
            collecting=bool(prop(collection, 'collecting')),
            hero_location_cm=vec(self.hero.get_actor_location()),
            fragment_locations_cm=[vec(p.get_actor_location()) for p in self.fragments()]))

    def inspect_fracture(self):
        pieces = self.fragments()
        total = sum(float(prop(piece, 'body').get_mass()) for piece in pieces)
        meshes = sorted({path(prop(piece, 'item_mesh')) for piece in pieces})
        expected = self.report['grape_fixture']['configured_whole_mass_kg']
        self.report['fracture_result'] = dict(actual_count=len(pieces), unique_meshes=meshes,
            actual_total_mass_kg=total, configured_whole_mass_kg=expected,
            pieces=[dict(**self.food_geometry(p), mesh=path(prop(p, 'item_mesh')),
                         mass_kg=float(prop(p, 'body').get_mass())) for p in pieces])
        self.require(len(pieces)==20, 'Actual cutting produced '+str(len(pieces))+' fragments, expected twenty')
        self.require(len(meshes)==1, 'The one-variant fixture produced an unexpected mesh')
        self.require(abs(total-expected)<.02, 'Fragment masses do not conserve the original food mass')
        self.report['checks'].update(actual_cut_spawns_twenty=True, one_variant_reused=True, whole_mass_conserved=True)

    def tick(self, _delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started<240., 'Manual food probe exceeded wall-time limit')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]), 'Expected one owned standalone authority PIE')
                self.world = worlds[0]
                self.pc = u.GameplayStatics.get_player_controller(self.world, 0)
                if not valid(self.pc) or not valid(self.pc.get_controlled_pawn()):
                    self.world = None
                    return
                self.stage('first_perk')
                return
            self.require(self.world in worlds, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter), 'No owned tooth pawn')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.pc.is_reward_menu_open():
                self.primary_input(False)
                self.pc.choose_reward_perk(0)
                if self.phase=='first_perk':
                    self.stage('setup')
                else:
                    self.report.setdefault('reward_interruptions', []).append(dict(time=now, phase=self.phase))
                return
            if self.last_game_time is not None and now<=self.last_game_time:
                return
            self.last_game_time = now
            elapsed = now-self.phase_at
            if self.phase=='first_perk':
                self.require(elapsed<25., 'Initial reward menu never appeared')
                return
            if self.phase=='setup':
                tongues = self.actors(u.MCTongue)
                self.require(tongues, 'L_Mouth has no native tongue')
                self.tongue = tongues[0]
                state = u.GameplayStatics.get_game_state(self.world)
                self.scheduler = prop(state, 'single_day_director')
                if valid(self.scheduler):
                    self.scheduler_tick = self.scheduler.is_actor_tick_enabled()
                    self.scheduler.set_actor_tick_enabled(False)
                self.grape_fixture()
                return
            self.require(prop(self.hero, 'status').is_alive(), 'Observer died')
            if now>=self.sample_at:
                self.sample_at = now+.1
                self.sample(now)
            if self.clip and now>=self.frame_at:
                self.frame_at = now+1./self.capture_hz
                self.screenshot('%05d.png'%len(self.report['frames']), sequence=True)
            if self.phase=='grape_before' and elapsed>=1.:
                self.primary_input(True)
                self.stage('grape_cut')
            elif self.phase=='grape_cut':
                self.require(elapsed<18., 'Ordinary held cutting input did not break grape')
                if prop(self.food, 'phase')==u.MCFoodPhase.DISPOSED:
                    self.primary_input(False)
                    self.inspect_fracture()
                    self.stage('fragments_settle')
                else:
                    self.place_for_food()
            elif self.phase=='fragments_settle' and elapsed>=2.:
                self.place_for_settled_fragments()
                self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
                self.hero.call_method('ServerToggleFoodCollection', args=())
                self.begin_clip('stack_pickup_tap_hold')
                self.stage('collect')
            elif self.phase=='collect':
                collection = prop(self.hero, 'food_collection')
                pieces = list(prop(collection, 'pieces'))
                if len(pieces)>=6:
                    self.initial_stack = [path(p) for p in pieces]
                    self.frame_subjects([self.hero]+pieces, 'native_six_piece_tower')
                    self.stage('stack_ready')
                elif elapsed>10.:
                    self.require(False, 'Normal E collection did not gather a full six-piece tower')
            elif self.phase=='stack_ready' and elapsed>=.6:
                collection = prop(self.hero, 'food_collection')
                self.tap_before = len(prop(collection, 'pieces'))
                self.tap_top = list(prop(collection, 'pieces'))[-1]
                self.tap_top_path = path(self.tap_top)
                self.hero.call_method('SetThrowInputHeld', args=(True,))
                self.release_at = now+.05
                self.stage('tap')
            elif self.phase=='tap':
                if self.release_at is not None and now>=self.release_at:
                    self.hero.call_method('SetThrowInputHeld', args=(False,))
                    self.release_at = None
                    self.report['tap_projectile'] = dict(path=self.tap_top_path,
                        valid_at_sample=valid(self.tap_top),
                        velocity_cm_s=vec(prop(self.tap_top, 'body').get_physics_linear_velocity()) if valid(self.tap_top) else None)
                if elapsed>=.45:
                    held = list(prop(prop(self.hero, 'food_collection'), 'pieces'))
                    self.require(len(held)==self.tap_before-1, 'One short Q tap did not release exactly one piece')
                    self.require(self.tap_top_path not in {path(p) for p in held}, 'Q did not release the uppermost slot')
                    self.report['checks']['q_tap_releases_one_top_piece']=True
                    self.report['checks']['q_tap_preserves_remaining_stack']=all(path(p) in self.initial_stack[:-1] for p in held)
                    self.last_stack = len(held)
                    self.hero.call_method('SetThrowInputHeld', args=(True,))
                    self.stage('hold')
            elif self.phase=='hold':
                held = list(prop(prop(self.hero, 'food_collection'), 'pieces'))
                count = len(held)
                if count!=self.last_stack:
                    self.report.setdefault('hold_releases', []).append(dict(time=now, before=self.last_stack, after=count))
                    self.require(count==self.last_stack-1, 'Held Q released more than one piece in one step')
                    self.last_stack = count
                self.require(elapsed<5., 'Held Q did not empty the tower at a bounded cadence')
                if count==0:
                    self.hero.call_method('SetThrowInputHeld', args=(False,))
                    self.report['checks']['held_q_releases_one_by_one']=True
                    self.stage('finish_frames')
            elif self.phase=='finish_frames' and elapsed>=.8:
                self.clip = None
                self.stage('drain_frames')
            elif self.phase=='drain_frames' and elapsed>=.35:
                for entry in self.report['frames']:
                    data = Path(entry['file']).read_bytes()
                    self.require(data[:8]==b'\x89PNG\r\n\x1a\n', 'Engine frame is not a completed PNG')
                    entry.update(sha256=hashlib.sha256(data).hexdigest(), width=int.from_bytes(data[16:20], 'big'),
                                 height=int.from_bytes(data[20:24], 'big'))
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if self.done:
            return
        if valid(self.hero) and self.world in u.EditorLevelLibrary.get_pie_worlds(True):
            try:
                self.hero.call_method('SetThrowInputHeld', args=(False,))
            except Exception:
                self.report.setdefault('restore_errors', []).append(traceback.format_exc())
        if valid(self.scheduler) and self.scheduler_tick is not None:
            self.scheduler.set_actor_tick_enabled(self.scheduler_tick)
        super().finish(error)


if __name__=='__main__':
    previous = getattr(builtins, KEY, None)
    if previous is not None and not getattr(previous, 'done', True):
        raise RuntimeError('Preserve the existing owned manual food probe')
    probe = ManualFoodSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise
