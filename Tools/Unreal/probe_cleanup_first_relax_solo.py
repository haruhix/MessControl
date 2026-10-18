"""Focused owned solo PIE: actual first-meal dirt -> brushing -> MealRest.

Run only in idle L_Mouth after the matching Editor build and root approval.
No stage, task counter, care state, coverage mask or dirt actor is overwritten.
The private native run-profile COPY is found by its PIE Director outer and tuned
to one portion / 1 s opening pause / 12 s final pause. The real opening wave and
ordinary food-dirt producer run normally. One issued food is broken with normal
repeated tool press/release input, its native pieces collected with E and delivered by actual held Q.
Ordinary held Brush input visibly erases both stain kinds. Then the existing
MeshaBrush reward is granted through its existing F3 command to bound the
remaining sweep, still using real held input and native bristle contact.

The native MCCountDirectedFoodDirt function is not reflected. This probe observes
its real TickFirstMeal caller's TasksLeft and automatic FirstMeal -> MealRest
transition, and reports its independent read-only surface census separately.
No test-helper counter is substituted for that native transition.

Outputs: E:/DEVGAME/MessControl/Saved/Feedback5_11_20261018/Cleanup8.
Optional MC_CLEANUP_OUTPUT / MC_CLEANUP_CAPTURE_HZ. No package saves/network.
Cancel: builtins.mc_cleanup_first_relax_solo.finish('Stopped by operator').
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
OUTPUT = Path(globals().get('MC_CLEANUP_OUTPUT', ROOT/'Saved/Feedback5_11_20261018/Cleanup8')).resolve()
if OUTPUT.drive.upper() != 'E:':
    raise RuntimeError('Cleanup evidence must remain on E:')
support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_cataclysm_solo.py'),
    run_name='cleanup_capture_support', init_globals={'MC_CATACLYSM_MODE': 'jellies',
        'MC_CATACLYSM_OUTPUT': OUTPUT, 'MC_CATACLYSM_CAPTURE_HZ': globals().get('MC_CLEANUP_CAPTURE_HZ', 8)})
Parent, prop, valid, path, vec, enum_name = (support[key] for key in ('CataclysmSolo', 'prop', 'valid', 'path', 'vec', 'enum_name'))
wave_support = runpy.run_path(str(ROOT/'Tools/Unreal/probe_wave_observer_support.py'), run_name='cleanup_wave_observer_support')
KEY = 'mc_cleanup_first_relax_solo'


def optional(obj, key, default=None):
    try:
        return prop(obj, key)
    except Exception:
        return default


def ordinary_inside(uv, seed):
    # Read-only point selection mirrors the native seeded gameplay interior.
    # The visible material outline still requires the recorded visual review.
    angle = seed*2.39996
    x, y = (uv[0]-.5)*2, (uv[1]-.5)*2
    x, y = x*math.cos(angle)-y*math.sin(angle), x*math.sin(angle)+y*math.cos(angle)
    y *= 1.06+.20*math.sin(seed*1.731)
    def join(a, b):
        h = max(.065-abs(a-b), 0.)/.065
        return max(a, b)+h*h*.01625
    d = .48-math.hypot(x, y)
    for radius, cx, cy in ((.30+.045*math.sin(seed*2.17), .32, .08),
                          (.27+.045*math.cos(seed*3.11), -.35, -.09), (.25, -.12, .34), (.24, .20, -.30)):
        d = join(d, radius-math.hypot(x-cx, y-cy))
    return d+.018*math.sin(x*24+seed)*math.sin(y*19-seed) >= .025


class CleanupFirstRelaxSolo(Parent):
    def __init__(self):
        super().__init__()
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        self.output = OUTPUT/'Runs'/(stamp+'_'+uuid.uuid4().hex[:8])
        self.output.mkdir(parents=True, exist_ok=True)
        self.sequence = self.run_profile = self.food = self.throat = None
        self.profile_before = None
        self.batch = None
        self.batch_read_supported = None
        self.native_stages = []
        self.native_food_paths = set()
        self.collection = None
        self.food_cut_started = None
        self.cut_follow_at = self.collect_follow_at = 0.
        self.cut_release_at = self.next_cut_press_at = -100.
        self.cut_contact_hint = None
        self.swallowed_before_cut = None
        self.native_q_press_count = 0
        self.food_health_changes = []
        self.last_food_health = None
        self.throw_held = False
        self.resume_throw = False
        self.fragments = []
        self.last_held_count = 0
        self.last_held_change_at = 0.
        self.current_patch = self.brush_point = None
        self.point_at = self.patch_started_at = self.reposition_at = 0.
        self.point_masks_before = self.partial_before = None
        self.partial_kinds = set()
        self.partial_contact_seen = False
        self.native_first_meal_seen = False
        self.last_native_tasks = None
        self.before_brush_snapshot = None
        self.wave_observer_held = False
        self.central_overview_done = False
        self.report.update(mode='cleanup_first_relax', scope=__doc__, clips={}, frames=[], screenshots=[], samples=[], checks={},
            run_directory=str(self.output), source_sha256={}, geometry_snapshots=[], normal_reward_choices=[],
            tuning_reason='Only the exact native PIE Director profile copy uses one portion, a 1 s opening pause and a 12 s post-meal pause; the opening wave and gameplay remain native.',
            fixture_tuning={}, completed_patches=[], partial_brush_proofs=[],
            food_cut_attempts=[], food_health_changes=self.food_health_changes, CentralPlacement=[],
            limitations=[
                'This is a cleaning-gate fixture, not a full food-combat, balance, multiplayer or performance test.',
                'Only the native private run-profile copy is tuned to one food portion and short pauses; no saved profile is changed.',
                'The naturally issued food is cut by normal LMB press/release edges every .90 s, collected with E and delivered by held Q; native contact, recovery and intake remain authoritative.',
                'Read-only GripSurface triangle lines choose a supported approach; they are hints, not substitutes for the native swept tool contact or real health decreases.',
                'Default brush contact is proved on both surface kinds before the existing MeshaBrush reward accelerates the remaining real-input sweep.',
                'Observer transforms are position-only fixtures; approach time, bristle reach, surface occlusion and native masks remain authoritative.',
                'Normal held RMB anchors the observer to a real tooth through the opening wave; it is released before ordinary food cutting.',
                'The native global MCCountDirectedFoodDirt has no Python entry point. Its actual caller/automatic stage transition is observed instead.',
                'The exact native regression separately tests unsupported-mask rejection and MCCountDirectedFoodDirt returning zero after real contact.'])
        for source in ('Tools/Unreal/probe_cleanup_first_relax_solo.py', 'Tools/Unreal/inspect_cleanup_debt.py',
                       'Tools/Unreal/probe_wave_observer_support.py',
                       'Source/MessControl/Private/MCDirtPlacement.cpp',
                       'Source/MessControl/Private/MCSingleDayDirector.cpp', 'Source/MessControl/Private/MCFoodDirectorHooks.cpp',
                       'Source/MessControl/Private/MCMouthLiquid.cpp', 'Source/MessControl/Private/MCMouthSurface.cpp'):
            self.report['source_sha256'][source] = hashlib.sha256((ROOT/source).read_bytes()).hexdigest()

    def write(self):
        self.output.mkdir(parents=True, exist_ok=True)
        data = json.dumps(self.report, ensure_ascii=False, indent=2, allow_nan=False)
        (self.output/'Report.json').write_text(data, encoding='utf-8')
        OUTPUT.mkdir(parents=True, exist_ok=True)
        (OUTPUT/'Report_cleanup.json').write_text(data, encoding='utf-8')

    def configure_profile(self):
        self.sequence = prop(self.gs, 'single_day_director')
        self.require(valid(self.sequence), 'The native single-day owner is missing')
        stage = enum_name(prop(self.sequence, 'stage'))
        self.require(stage in ('TRAINING', 'FIRST_PERK', 'FIRSTPERK', 'OPENING_PAUSE', 'OPENINGPAUSE'),
                     'The short fixture must be configured before the native first meal')
        profiles = [p for p in u.ObjectIterator(u.MCSingleDayProfile) if p.get_outer() == self.sequence]
        self.require(len(profiles) == 1, 'Cannot identify the exact native private run-profile copy')
        self.run_profile = profiles[0]
        self.require('UEDPIE_' in path(self.sequence) and self.run_profile.get_outer() == self.sequence,
                     'Refuse to edit a saved profile or a different world')
        fields = dict(opening_meal_items=1, after_training_pause_seconds=1., before_nuts_pause_seconds=12.)
        self.profile_before = {key: prop(self.run_profile, key) for key in fields}
        self.require(bool(prop(self.run_profile, 'opening_coffee_wave')), 'The real opening coffee wave is required')
        self.run_profile.set_editor_properties(fields)
        self.report['fixture_tuning'] = dict(profile=path(self.run_profile), outer=path(self.sequence),
            before=self.profile_before, after=fields, saved_profile_changed=False)

    def surface_batch(self, patch):
        if self.batch_read_supported is False:
            return None
        try:
            value = patch.get_editor_property('Batch')
            self.batch_read_supported = True
            return int(value)
        except Exception as error:
            # Batch is replicated but has no Edit/BlueprintVisible flag in the
            # current native class. ToolsetLibrary uses the same permission
            # check; retrying either API per patch/frame only spams errors.
            self.batch_read_supported = False
            self.report['surface_batch_read'] = dict(readable=False, value=None, error=str(error),
                fallback='Unknown; native FirstMeal gate remains authoritative. Source food Batch is readable.',
                repeated_access_attempts_suppressed=True)
            return None

    def surfaces(self):
        rows = []
        for patch in self.actors(u.MCMouthSurface):
            if not patch.actor_has_tag('MCFoodServedDirt') or bool(prop(patch, 'ulcer')):
                continue
            batch = self.surface_batch(patch)
            if self.batch is not None and batch is not None and batch != self.batch:
                continue
            rows.append(patch)
        return rows

    def is_clean(self, patch):
        if bool(prop(patch, 'paint_tile')):
            return not any(int(cell) > 63 for cell in prop(patch, 'wipe_mask'))
        return int(prop(prop(patch, 'status'), 'state').coffee_left) == 0

    def surface_row(self, patch):
        mask = [int(value) for value in prop(patch, 'wipe_mask')]
        return dict(actor=path(patch), batch=self.surface_batch(patch), paint_tile=bool(prop(patch, 'paint_tile')),
            coffee_left=int(prop(prop(patch, 'status'), 'state').coffee_left),
            raw_wet_bytes=sum(value > 63 for value in mask), wipe_sum=sum(mask), clean=self.is_clean(patch),
            half_size_cm=float(prop(patch, 'liquid_half_size')), liquid_visible=bool(prop(patch, 'liquid').is_visible()),
            tags=[str(tag) for tag in prop(patch, 'tags')])

    def geometry_snapshot(self, label):
        namespace = runpy.run_path(str(ROOT/'Tools/Unreal/inspect_cleanup_debt.py'), run_name='cleanup_debt_'+label)
        snapshot = namespace['report']
        self.report['geometry_snapshots'].append(dict(label=label, report=str(namespace['target']),
            physical_tooth_ids=[row['tooth_id'] for row in snapshot['teeth']],
            unsupported_wet_centers=sum(row['unsupported_wet_centers'] for row in snapshot['surfaces'] if row['paint_tile']),
            wet_centers_without_mesh=sum(row['wet_centers_without_mesh'] for row in snapshot['surfaces'] if row['paint_tile'])))
        return snapshot

    def sample(self, now):
        if not valid(self.sequence):
            return
        stage = enum_name(prop(self.sequence, 'stage'))
        if not self.native_stages or self.native_stages[-1] != stage:
            self.native_stages.append(stage)
        rows = [self.surface_row(p) for p in self.surfaces()]
        self.report['samples'].append(dict(game_seconds=now, phase=self.phase, native_stage=stage,
            native_tasks_left=int(prop(self.gs, 'tasks_left')), native_tasks_total=int(prop(self.gs, 'tasks_total')),
            independently_observed_tagged_dirty_surfaces=sum(not row['clean'] for row in rows), surfaces=rows,
            brushing=bool(prop(self.hero, 'brushing')), native_care_target=path(prop(self.hero, 'care_target')),
            hero_cm=vec(self.hero.get_actor_location()), body_state=enum_name(self.hero.tooth_physics.get_body_state()),
            wide_brush=bool(self.hero.inventory.has_upgrade(u.MCToolUpgrade.MESHA_BRUSH))))
        if self.phase == 'native_cut':
            self.report['samples'][-1]['food_cut'] = self.food_cut_snapshot(now)
        if valid(self.current_patch) and prop(self.hero, 'care_target') == self.current_patch and bool(prop(self.hero, 'brushing')):
            self.partial_contact_seen = True

    def place_observer(self, foot, yaw, reason):
        position = foot+u.Vector(0, 0, self.hero.capsule_component.get_scaled_capsule_half_height()+3.)
        self.hero.set_actor_location_and_rotation(position, u.Rotator(yaw=yaw), False, True)
        self.pc.set_control_rotation(u.Rotator(yaw=yaw))
        self.report.setdefault('position_fixtures', []).append(dict(time=float(u.GameplayStatics.get_time_seconds(self.world)),
            hero_cm=vec(position), yaw=yaw, reason=reason))

    def central_work_bounds(self, margin=0.):
        center,extent,_=u.SystemLibrary.get_component_bounds(prop(self.tongue,'surface'))
        near=float(prop(self.tongue,'gameplay_spawn_near_depth'))
        far=float(prop(self.tongue,'gameplay_spawn_far_depth'))
        near=max(0.,min(.95,near if math.isfinite(near) else .18))
        far=max(near+.05,min(1.,far if math.isfinite(far) else .82))
        result=dict(min_x=center.x+extent.x-2*extent.x*far+margin,
            max_x=center.x+extent.x-2*extent.x*near-margin,
            min_y=center.y-extent.y*.72+margin,max_y=center.y+extent.y*.72-margin)
        self.require(result['min_x']<result['max_x'] and result['min_y']<result['max_y'],'Runtime tongue has no central work area')
        return result,dict(center_cm=vec(center),extent_cm=vec(extent),spawn_near=near,spawn_far=far,y_extent_fraction=.72,margin_cm=margin)

    @staticmethod
    def in_xy_bounds(point,bounds):
        # Native/Python FTransform operations can differ by a fraction of a mm.
        return bounds['min_x']-.1<=point.x<=bounds['max_x']+.1 and bounds['min_y']-.1<=point.y<=bounds['max_y']+.1

    def central_placement_snapshot(self,label):
        bounds,source=self.central_work_bounds(65.)
        rows=[];paint_outside=ordinary_outside=static_blocked=wet_tested=0
        for patch in self.surfaces():
            if self.is_clean(patch):
                continue
            tile=bool(prop(patch,'paint_tile'));half=float(prop(patch,'liquid_half_size'));transform=patch.get_actor_transform()
            row=dict(actor=path(patch),paint_tile=tile,center_cm=vec(patch.get_actor_location()),half_size_cm=half)
            if tile:
                mask=[int(value) for value in prop(patch,'wipe_mask')];size=math.isqrt(len(mask))
                self.require(size and size*size==len(mask),'Invalid native painted mask in central-placement census')
                wet=outside=0;examples=[]
                for index,value in enumerate(mask):
                    if value<=63:
                        continue
                    wet+=1
                    point=transform.transform_location(u.Vector(((index%size+.5)/size-.5)*2*half,((index//size+.5)/size-.5)*2*half,0))
                    if not self.in_xy_bounds(point,bounds):
                        outside+=1
                        if len(examples)<12:examples.append(dict(cell=index,world_cm=vec(point),wet_byte=value))
                row.update(wet_bytes_tested=wet,outside_central_wet_bytes=outside,outside_examples=examples,
                    canvas_center_is_not_a_placement_assertion=True)
                paint_outside+=outside;wet_tested+=wet
            else:
                corners=[transform.transform_location(u.Vector(x*half,y*half,0)) for x,y in ((-1,-1),(-1,1),(1,1),(1,-1))]
                outside=sum(not self.in_xy_bounds(point,bounds) for point in corners)
                row.update(full_footprint_corners_cm=[vec(point) for point in corners],outside_central_corners=outside)
                ordinary_outside+=outside
                floor=self.floor(patch.get_actor_location())
                self.require(floor is not None,'An ordinary stain has no actual tissue for static-clearance inspection')
                sx=max(abs(point.x-floor.x) for point in corners)+65.
                sy=max(abs(point.y-floor.y) for point in corners)+65.
                center=floor+u.Vector(0,0,100.)
                object_type=getattr(u.ObjectTypeQuery,'OBJECT_TYPE_QUERY1',None)
                if object_type is None:object_type=getattr(u.ObjectTypeQuery,'ECC_WORLD_STATIC',None)
                self.require(object_type is not None,'WorldStatic object-query enum is unavailable')
                trace=u.SystemLibrary.box_trace_single_for_objects(self.world,center,center+u.Vector(0,0,.5),
                    u.Vector(sx,sy,85.),u.Rotator(),[object_type],False,[self.tongue],u.DrawDebugTrace.NONE,True)
                hit=next((value for value in (list(trace) if isinstance(trace,(tuple,list)) else [trace]) if isinstance(value,u.HitResult)),None)
                row['static_footprint_plus_brush_clearance']=dict(center_cm=vec(center),half_extent_cm=[sx,sy,85.],
                    object_query='WorldStatic',blocked=hit is not None)
                if hit is not None:
                    fields=self.statics.call_method('BreakHitResult',args=(hit,))
                    row['static_footprint_plus_brush_clearance'].update(actor=path(fields[9]),component=path(fields[10]),impact_cm=vec(fields[5]))
                    static_blocked+=1
            rows.append(row)
        snapshot=dict(label=label,time=float(u.GameplayStatics.get_time_seconds(self.world)),runtime_source=source,
            central_bounds_with_65cm_brush_margin=bounds,tolerance_cm=.1,patches=rows,paint_wet_bytes_tested=wet_tested,
            paint_wet_bytes_outside=paint_outside,ordinary_corners_outside=ordinary_outside,ordinary_static_footprints_blocked=static_blocked,
            limitations=['Paint tests every actual wet byte in world XY, not its potentially off-centre canvas origin.',
                'Ordinary tests the full four-corner footprint plus65cm brush margin; WorldStatic box checks the whole ordinary footprint.',
                'No material pixels or paint-wide static-space claim is inferred from the central XY census; rendered overview still requires visual review.'])
        self.report['CentralPlacement'].append(snapshot)
        self.write()
        self.require(rows and wet_tested>0,'Central placement proof requires actual first-meal floor stains')
        self.require(paint_outside==0 and ordinary_outside==0,'Actual wet dirt extends outside the central work area/brush margin')
        self.require(static_blocked==0,'An ordinary dirt footprint or its brush approach intersects WorldStatic geometry')
        self.report['checks']['actual_first_meal_dirt_within_central_work_area']=True
        return snapshot

    def central_overview_camera(self,snapshot):
        bounds=snapshot['central_bounds_with_65cm_brush_margin']
        point=u.Vector((bounds['min_x']+bounds['max_x'])*.5,(bounds['min_y']+bounds['max_y'])*.5,0)
        floor=self.floor(point)
        self.require(floor is not None,'No tongue surface under the central overview target')
        target=floor+u.Vector(0,0,30)
        points=[target]+[u.Vector(*row['center_cm'])+u.Vector(0,0,15) for row in snapshot['patches']
            if not row['paint_tile']]
        ignore=[self.hero]+self.actors(u.MCFoodActor)
        if valid(self.camera):ignore.append(self.camera)
        choices=[]
        for offset in (u.Vector(-1250,-700,900),u.Vector(-1600,0,1000),u.Vector(-900,850,900),
                       u.Vector(-1800,-500,1200),u.Vector(-1000,0,1400)):
            eye=target+offset;clear=0
            for subject in points:
                trace=u.SystemLibrary.line_trace_single(self.world,eye,subject,u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignore,u.DrawDebugTrace.NONE,True)
                hit=next((v for v in (list(trace) if isinstance(trace,(tuple,list)) else [trace]) if isinstance(v,u.HitResult)),None)
                if hit is None:
                    clear+=1
                else:
                    fields=self.statics.call_method('BreakHitResult',args=(hit,))
                    if not fields[0] or fields[3]>=math.dist(vec(eye),vec(subject))-15.:clear+=1
            choices.append((clear,eye))
            if clear==len(points):break
        clear,eye=max(choices,key=lambda item:item[0])
        self.frame_camera(eye,target)
        self.report['central_overview_camera']=dict(method='Explicit external review CameraActor; normal actor viewpoint, no geometry/material hiding',
            actor=path(self.camera),eye_cm=vec(eye),target_cm=vec(target),clear_subject_rays=clear,subject_rays=len(points),
            all_sampled_rays_clear=clear==len(points),visual_approval='Pending actual rendered frames; ray tests do not establish whole-frame visibility')

    def begin_native_cut(self,now):
        if self.wave_observer_held:
            wave_support['release_wave_observer'](self)
            self.wave_observer_held=False
        self.food_cut_started=now
        self.swallowed_before_cut=int(prop(self.throat,'food_swallowed'))
        self.report['native_food_swallowed_before_cut']=self.swallowed_before_cut
        self.last_food_health=float(prop(self.food,'health'))
        row=prop(self.food,'food_data')
        hard=prop(row,'resistance')==u.MCFoodResistance.HARD or (prop(row,'resistance')==u.MCFoodResistance.AUTOMATIC
            and str(prop(self.food,'item_name')) in ('Carrot','Nut','Crust','Tartar'))
        self.hero.inventory.server_select(u.MCToolSlot.PICKAXE if hard else u.MCToolSlot.KNIFE)
        self.primary_input(False);self.follow_food_for_cut(now)
        self.cut_release_at=self.next_cut_press_at=now
        self.begin_clip('08_native_food_cut',24.);self.stage('native_cut');self.drive_food_cut_input(now)

    def dirty_point(self, patch):
        mask = [int(value) for value in prop(patch, 'wipe_mask')]
        size = math.isqrt(len(mask))
        self.require(size and size*size == len(mask), 'Invalid actual liquid mask')
        half = float(prop(patch, 'liquid_half_size'))
        seed = int(prop(patch, 'liquid_seed'))
        tile = bool(prop(patch, 'paint_tile'))
        transform = patch.get_actor_transform()
        candidates = []
        for stride in (4, 1):
            for y in range(0, size, stride):
                for x in range(0, size, stride):
                    value = mask[y*size+x]
                    uv = ((x+.5)/size, (y+.5)/size)
                    if value <= 63 or (not tile and not ordinary_inside(uv, seed)):
                        continue
                    score = value-((uv[0]-.5)**2+(uv[1]-.5)**2)*160
                    candidates.append((score, uv))
            if candidates:
                break
        candidates.sort(reverse=True)
        for _, uv in candidates[:32]:
            point = transform.transform_location(u.Vector((uv[0]-.5)*2*half, (uv[1]-.5)*2*half, 0))
            foot = self.floor(point)
            if foot is None:
                continue
            if tile and not patch.is_paint_wet_at(foot):
                continue
            for angle in (0., math.pi/2., -math.pi/2., math.pi):
                approach = self.floor(foot-u.Vector(math.cos(angle), math.sin(angle), 0)*65.)
                if approach is not None and abs(approach.z-foot.z) < 45.:
                    yaw = math.degrees(math.atan2(foot.y-approach.y, foot.x-approach.x))
                    return foot, approach, yaw
        return None

    def begin_brush(self, patch, now, partial=False):
        result = self.dirty_point(patch)
        self.require(result is not None, 'No supported real dirty contact for '+path(patch))
        point, foot, yaw = result
        self.current_patch, self.brush_point = patch, point
        self.point_at = self.patch_started_at = now
        self.point_masks_before = [int(v) for v in prop(patch, 'wipe_mask')]
        self.partial_before = self.surface_row(patch)
        self.partial_contact_seen = False
        self.place_observer(foot, yaw, 'Supported approach to an actual occupied coffee cell')
        self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
        self.frame_camera(point+u.Vector(250, -370, 300), point+u.Vector(0, 0, 30))
        self.primary_input(True)
        if partial:
            self.begin_clip('08_default_brush_'+('painted_wave' if bool(prop(patch, 'paint_tile')) else 'ordinary'), 4.5)
        self.stage('partial_brush' if partial else 'full_brush')

    def grant_wide_brush(self):
        perks = prop(self.pc.player_state, 'perks')
        table = perks.get_perk_table()
        self.require(table is not None, 'The actual perk table is unavailable')
        rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        candidates = [row['Name'] for row in rows if str(row.get('ToolUpgrade', '')).upper().endswith('MESHABRUSH')
                      and perks.can_grant_perk(row['Name'])]
        self.require(candidates, 'No validated native MeshaBrush reward can be granted')
        chosen = sorted(candidates)[0]
        # F3 orders its usable Rare/Legendary tool rows lexically. The native
        # command still validates and applies the actual reward; verify the
        # result rather than assigning TeamToolUpgrades or an inventory mask.
        tools = sorted(row['Name'] for row in rows if int(row.get('MaxStacks', 0)) > 0
            and str(row.get('ToolUpgrade', '')).upper().rsplit('::',1)[-1] in ('MESHABRUSH', 'CHAINSAW', 'WATERGUN')
            and str(row.get('Rarity', '')).upper().rsplit('::',1)[-1] in ('RARE', 'LEGENDARY'))
        self.require(chosen in tools, 'Chosen native brush reward is absent from the F3 tool-row order')
        index = tools.index(chosen)
        self.pc.request_dev_action(u.MCDevAction.GRANT_TOOL_BOOSTER, index)
        self.require(self.hero.inventory.has_upgrade(u.MCToolUpgrade.MESHA_BRUSH), 'The native grant did not unlock its brush')
        self.report['wide_brush_fixture'] = dict(perk_id=chosen, f3_index=index,
            api='AMCPlayerController::RequestDevAction(GrantToolBooster) -> UMCPerkComponent::ServerGrantReward',
            masks_or_care_written=False, purpose='Bound the remaining sweep after default-brush proof of both source kinds')

    def food_cut_snapshot(self, now):
        result = dict(time=now, requested_primary_held=self.primary,
            selected_tool=enum_name(prop(self.hero.inventory, 'selected')),
            selected_upgrade=enum_name(self.hero.inventory.selected_upgrade()),
            hero_cm=vec(self.hero.get_actor_location()), hero_yaw=self.hero.get_actor_rotation().yaw,
            controller_yaw=self.pc.get_control_rotation().yaw, in_coffee=bool(prop(self.hero, 'in_coffee')),
            self_care=bool(prop(self.hero, 'self_care')), collecting=bool(prop(self.collection, 'collecting')),
            body_state=enum_name(self.hero.tooth_physics.get_body_state()),
            triangle_approach_hint=self.cut_contact_hint,
            native_swing_counter='Not reflected; real food health changes are the contact proof')
        if valid(self.food):
            visual_center, visual_extent, _ = u.SystemLibrary.get_component_bounds(prop(self.food, 'visual'))
            body_center, body_extent, _ = u.SystemLibrary.get_component_bounds(prop(self.food, 'body'))
            hero = self.hero.get_actor_location()
            delta = [max(0., abs(a-b)-e) for a,b,e in zip(vec(hero),vec(visual_center),vec(visual_extent))]
            row = prop(self.food, 'food_data')
            result.update(actor=path(self.food), item=str(prop(self.food, 'item_name')),
                health=float(prop(self.food, 'health')), phase=enum_name(prop(self.food, 'phase')),
                resistance=enum_name(prop(row, 'resistance')), actor_cm=vec(self.food.get_actor_location()),
                visual_center_cm=vec(visual_center), visual_extent_cm=vec(visual_extent),
                body_center_cm=vec(body_center), body_extent_cm=vec(body_extent),
                visual_aabb_distance_cm=math.sqrt(sum(v*v for v in delta)))
        return result

    def triangle_line_point(self, component, start, end):
        # UE5.8 PyGenUtil::PackReturnValues returns None on a false main bool;
        # on true it omits that bool and packs the four K2 out parameters.
        # Also reject an explicit false if a wrapper retains the native bool.
        hit = component.line_trace_component(start, end, True, False, False)
        if hit is None or hit is False:
            return None
        values = list(hit) if isinstance(hit, (tuple, list)) else [hit]
        if any(value is False for value in values):
            return None
        if values and values[0] is True:
            values=values[1:]
        self.require(len(values)==4 and isinstance(values[0],u.Vector) and isinstance(values[1],u.Vector)
                     and isinstance(values[3],u.HitResult),'Unexpected LineTraceComponent success output')
        fields=self.statics.call_method('BreakHitResult',args=(values[3],))
        # Same NativeBreak contract as the already-passed manual-intake probe.
        # A component-only query uses OverlapAll to classify its geometric hit;
        # its bBlockingHit can be false even though K2 returned true.
        self.require(len(fields)==18,'Unexpected NativeBreakHitResult outputs')
        self.require(fields[10]==component and 0<=fields[2]<=1
                     and isinstance(fields[4],u.Vector) and isinstance(fields[5],u.Vector)
                     and math.dist(vec(values[0]),vec(fields[4]))<1.
                     and math.dist(vec(values[1]),(0.,0.,0.))>.5
                     and all(math.isfinite(v) for v in vec(fields[5])),
                     'LineTraceComponent success lacks the expected geometric contact')
        return fields[5]

    def follow_food_for_cut(self, now):
        if now < self.cut_follow_at:
            return
        self.cut_follow_at = now+.15
        center, extent, _ = u.SystemLibrary.get_component_bounds(prop(self.food, 'visual'))
        grip_surface = prop(self.food, 'grip_surface')
        radius = self.hero.capsule_component.get_scaled_capsule_radius()
        height = self.hero.capsule_component.get_scaled_capsule_half_height()
        # Physics hulls and visual bounds are distinct. In particular, an empty
        # hull/AABB corner is not proof that FindToolContact can see triangles.
        # Ray-search the actual mesh at the native chop's three height bands,
        # then place only the observer outside that surface on a real floor.
        turn = int((now-self.food_cut_started)/2.) % 8
        fallback = None
        for offset in range(8):
            angle = math.pi+(turn+offset)*math.pi/4.
            direction = u.Vector(math.cos(angle),math.sin(angle),0)
            tangent = u.Vector(-direction.y,direction.x,0)
            exits = [extent.x/abs(direction.x) if abs(direction.x)>.001 else 1.e9,
                     extent.y/abs(direction.y) if abs(direction.y)>.001 else 1.e9]
            for side in (0., -.35, .35):
                lateral = tangent*(min(extent.x,extent.y)*side)
                foot = self.floor(center+lateral+direction*(min(exits)+radius+18.))
                if foot is None:
                    continue
                fallback = fallback or foot
                for band in (-45.,15.,75.):
                    z = foot.z+height+3.+band
                    start = u.Vector(foot.x,foot.y,z)
                    target = u.Vector(center.x+lateral.x,center.y+lateral.y,z)-direction*(extent.x+extent.y+60.)
                    point = self.triangle_line_point(grip_surface,start,target)
                    if point is None:
                        continue
                    supported = self.floor(point+direction*(radius+18.))
                    if supported is None or abs(supported.z-foot.z)>35.:
                        continue
                    yaw = math.degrees(math.atan2(point.y-supported.y,point.x-supported.x))
                    self.cut_contact_hint = dict(time=now, triangle_cm=vec(point), band_cm=band,
                        native_swept_contact_called_directly_by_probe=False, approach_source='Visible GripSurface line; native swing decides actual contact')
                    self.place_observer(supported,yaw,'Position-only supported triangle approach for real primary-input food cutting')
                    self.frame_camera(center+u.Vector(400,-550,400),center)
                    return
        if fallback is not None:
            self.cut_contact_hint = dict(time=now, triangle_cm=None, approach_source='Visual AABB fallback; no triangle proof')
            yaw = math.degrees(math.atan2(center.y-fallback.y,center.x-fallback.x))
            self.place_observer(fallback,yaw,'Position-only visual-bound fallback; native contact still decides the hit')
            self.frame_camera(center+u.Vector(400,-550,400),center)
            return
        self.require(False, 'No supported blade approach to the naturally issued food')

    def drive_food_cut_input(self, now):
        # Food auto-repeat is conditional on a reachable native target. A first
        # press during wave recovery or outside the real mesh must not leave
        # this bounded observer holding an inert combat sweep indefinitely.
        if self.primary and now >= self.cut_release_at:
            self.primary_input(False)
            self.report['food_cut_attempts'].append(dict(event='release', **self.food_cut_snapshot(now)))
        if now >= self.next_cut_press_at:
            self.primary_input(True)
            self.cut_release_at, self.next_cut_press_at = now+.55, now+.90
            self.report['food_cut_attempts'].append(dict(event='press', **self.food_cut_snapshot(now)))

    def set_collecting(self, active):
        if bool(prop(self.collection, 'collecting')) != bool(active):
            self.hero.call_method('ServerToggleFoodCollection', args=())

    def loose_fragments(self):
        return [f for f in self.fragments if valid(f) and enum_name(prop(f, 'phase')) != 'DISPOSED'
                and prop(f, 'stack_carrier') is None and enum_name(prop(f, 'phase')) != 'SWALLOWING']

    def prepare_native_throw(self, now):
        self.primary_input(False)
        values = self.throat.get_intake_aim_target()
        vectors = [v for v in values if isinstance(v, u.Vector)] if isinstance(values, (tuple, list)) else []
        self.require(vectors, 'No actual visible intake aim target')
        target = vectors[0]
        art = prop(self.throat, 'authored_mouth').get_world_transform()
        inward = u.MathLibrary.transform_direction(art, u.Vector(0, .9747185974, .2234360218))
        length = math.sqrt(inward.x**2+inward.y**2+inward.z**2)
        self.require(length > .9, 'Invalid authored intake direction')
        inward = inward/length
        right = u.MathLibrary.transform_direction(art, u.Vector(1,0,0))
        for distance in (650., 850., 1050., 450.):
            foot = self.floor(target-inward*distance)
            if foot is None:
                continue
            yaw = math.degrees(math.atan2(target.y-foot.y, target.x-foot.x))
            self.place_observer(foot, yaw, 'Supported approach for genuine held Q into the visible throat')
            self.frame_camera(self.hero.get_actor_location()-inward*300.+right*300.+u.Vector(0,0,240), target)
            self.pc.set_control_rotation(u.MathLibrary.find_look_at_rotation(self.hero.get_actor_location(), target))
            self.report.setdefault('native_throw_batches', []).append(dict(time=now, held=[path(f) for f in prop(self.collection, 'pieces')],
                target_cm=vec(target), actor_was_naturally_issued=True, direct_delivery_or_disposal_called=False))
            self.stage('native_throw_settle')
            return
        self.require(False, 'No supported real-Q throw approach to the actual aperture')

    def begin_cleanup_after_delivery(self, now):
        swallowed=int(prop(self.throat,'food_swallowed'))
        self.report['native_delivery_proof']=dict(before_cut=self.swallowed_before_cut,after=swallowed,
            expected_fragments=len(self.fragments),native_q_presses_with_food=self.native_q_press_count,
            fragment_phases=[dict(actor=path(f),phase=enum_name(prop(f,'phase')) if valid(f) else 'DESTROYED') for f in self.fragments])
        self.require(self.swallowed_before_cut is not None and self.fragments and self.native_q_press_count>0,
                     'Cleanup cannot begin without native fragments and a real Q press carrying food')
        self.require(swallowed-self.swallowed_before_cut==len(self.fragments),
                     'Missing native delivery: FoodSwallowed delta must equal every fragment from the issued portion')
        self.require(not prop(self.collection,'pieces') and all(not valid(f) or enum_name(prop(f,'phase'))=='DISPOSED' for f in self.fragments),
                     'Native delivery has not consumed every issued fragment')
        self.hero.call_method('SetThrowInputHeld', args=(False,))
        self.throw_held = False
        self.set_collecting(False)
        self.require(not self.hero.inventory.has_upgrade(u.MCToolUpgrade.MESHA_BRUSH),
                     'Default-brush proof requires the ordinary brush before the later disclosed F3 reward')
        dirty = [p for p in self.surfaces() if not self.is_clean(p)]
        kinds = {bool(prop(p, 'paint_tile')) for p in dirty}
        self.require(kinds == {False, True}, 'Both native ordinary and wave-painted stain kinds are required')
        self.before_brush_snapshot = self.geometry_snapshot('before_brush')
        ids = [row['tooth_id'] for row in self.before_brush_snapshot['teeth']]
        self.require(sorted(ids) == list(range(1, 13)), 'Saved L_Mouth did not spawn the twelve expected physical teeth')
        unsupported = sum(row['unsupported_wet_centers'] for row in self.before_brush_snapshot['surfaces'] if row['paint_tile'])
        self.require(unsupported == 0, 'A wave canvas retains unsupported invisible dirty texels')
        self.report['checks'].update(native_first_meal_and_wave_issued_work=True, two_real_stain_kinds_exist=True,
            native_cut_E_and_held_Q_resolve_issued_portion=True, twelve_physical_teeth_observed=True,
            no_unsupported_wet_paint_before_brush=True)
        self.last_native_tasks = int(prop(self.gs, 'tasks_left'))
        self.require(enum_name(prop(self.sequence, 'stage')) in ('FIRST_MEAL', 'FIRSTMEAL') and self.last_native_tasks > 0,
                     'The real first-meal gate did not wait for its actual dirty residue')
        ordinary = next(p for p in dirty if not bool(prop(p, 'paint_tile')))
        self.begin_brush(ordinary, now, True)

    def update(self, now):
        stage = enum_name(prop(self.sequence, 'stage'))
        elapsed = now-self.phase_at
        if stage in ('FIRST_MEAL', 'FIRSTMEAL'):
            self.native_first_meal_seen = True
        if self.phase == 'wait_meal':
            self.require(elapsed < 40., 'Native first meal did not issue the one portion after its real wave')
            foods = [f for f in self.actors(u.MCFoodActor) if f.get_owner() == self.sequence and not bool(prop(f, 'brush_tool'))]
            for food in foods:
                self.native_food_paths.add(path(food))
                self.batch = int(prop(food, 'batch'))
                self.food = food
            if not valid(self.food):
                return
            self.require(len(self.native_food_paths) == 1, 'The private profile fixture issued more than one portion')
            food_phase = enum_name(prop(self.food, 'phase'))
            if food_phase != 'FREE':
                return
            if not self.central_overview_done:
                snapshot=self.central_placement_snapshot('first_meal_before_native_cut')
                self.central_overview_camera(snapshot)
                self.begin_clip('08_central_floor_placement',3.2)
                self.stage('central_placement_overview')
                return
            self.begin_native_cut(now)
        elif self.phase == 'central_placement_overview' and elapsed>=3.:
            self.central_placement_snapshot('first_meal_after_three_second_overview')
            self.central_overview_done=True
            self.clip=None
            self.begin_native_cut(now)
        elif self.phase == 'native_cut':
            self.require(elapsed < 22., 'Normal tool contact did not break the naturally issued portion')
            health = float(prop(self.food, 'health')) if valid(self.food) else 0.
            if health < self.last_food_health:
                self.food_health_changes.append(dict(time=now, before=self.last_food_health, after=health))
            self.last_food_health = health
            if valid(self.food) and enum_name(prop(self.food, 'phase')) != 'DISPOSED':
                self.follow_food_for_cut(now)
                self.drive_food_cut_input(now)
                return
            self.primary_input(False)
            self.fragments = [f for f in self.actors(u.MCFoodActor) if bool(prop(f, 'fragment')) and int(prop(f, 'batch')) == self.batch]
            self.require(self.fragments and self.food_health_changes, 'Real cutting did not produce native batch fragments')
            self.report['food_cut'] = dict(parent=path(self.food), batch=self.batch, fragments=[path(f) for f in self.fragments],
                health_changes=self.food_health_changes, health_written=False)
            self.hero.inventory.server_select(u.MCToolSlot.BRUSH)
            self.set_collecting(True)
            self.last_held_count, self.last_held_change_at = 0, now
            self.stage('native_collect')
        elif self.phase == 'native_collect':
            self.require(elapsed < 25., 'Native E did not collect the issued fragments')
            held = list(prop(self.collection, 'pieces'))
            capacity = int(prop(self.collection, 'max_pieces'))
            loose = self.loose_fragments()
            if len(held) != self.last_held_count:
                self.last_held_count, self.last_held_change_at = len(held), now
            if held and (len(held) >= capacity or not loose or now-self.last_held_change_at >= 2.):
                self.prepare_native_throw(now)
                return
            if not loose:
                self.begin_cleanup_after_delivery(now)
                return
            if now >= self.collect_follow_at:
                self.collect_follow_at = now+.55
                candidate = loose[0]
                center = candidate.get_actor_location()
                for offset in (u.Vector(-85,0,0), u.Vector(0,-85,0), u.Vector(85,0,0), u.Vector(0,85,0)):
                    foot = self.floor(center+offset)
                    if foot is not None:
                        yaw = math.degrees(math.atan2(center.y-foot.y, center.x-foot.x))
                        self.place_observer(foot, yaw, 'Position-only approach; real E collection chooses each fragment')
                        self.set_collecting(True)
                        break
        elif self.phase == 'native_throw_settle' and elapsed >= .7:
            held=list(prop(self.collection,'pieces'))
            self.require(held,'Native Q proof requires a real nonempty carried stack')
            self.hero.call_method('SetThrowInputHeld', args=(True,))
            self.throw_held = True
            self.native_q_press_count+=1
            self.report.setdefault('native_q_presses',[]).append(dict(time=now,held=[path(f) for f in held],
                swallowed_before_press=int(prop(self.throat,'food_swallowed')),input='SetThrowInputHeld(true)'))
            self.stage('native_throw_arrival')
        elif self.phase == 'native_throw_arrival':
            self.require(elapsed < 12., 'Native held Q did not deliver the collected portion')
            if prop(self.collection, 'pieces') or any(valid(f) and enum_name(prop(f, 'phase')) == 'SWALLOWING' for f in self.fragments):
                return
            self.hero.call_method('SetThrowInputHeld', args=(False,))
            self.throw_held = False
            if self.loose_fragments():
                self.set_collecting(True)
                self.last_held_count, self.last_held_change_at = 0, now
                self.stage('native_collect')
            elif all(not valid(f) or enum_name(prop(f, 'phase')) == 'DISPOSED' for f in self.fragments):
                self.begin_cleanup_after_delivery(now)
        elif self.phase == 'partial_brush':
            self.require(elapsed < 6., 'Ordinary brush failed to change its real target mask')
            if elapsed < 3.:
                return
            after = [int(v) for v in prop(self.current_patch, 'wipe_mask')]
            reduction = sum(max(0, a-b) for a, b in zip(self.point_masks_before, after))
            if reduction < 32 or not self.partial_contact_seen:
                return
            kind = bool(prop(self.current_patch, 'paint_tile'))
            self.partial_kinds.add(kind)
            self.primary_input(False)
            self.report['partial_brush_proofs'].append(dict(before=self.partial_before, after=self.surface_row(self.current_patch),
                fixed_cell_byte_reduction=reduction, actual_care_target_seen=self.partial_contact_seen))
            if len(self.partial_kinds) < 2:
                other = next((p for p in self.surfaces() if not self.is_clean(p) and bool(prop(p, 'paint_tile')) != kind), None)
                self.require(other is not None, 'The second real stain kind disappeared')
                self.begin_brush(other, now, True)
            else:
                self.report['checks']['ordinary_brush_erases_both_real_stain_kinds'] = True
                self.grant_wide_brush()
                self.clip = None
                self.stage('choose_remaining_patch')
        elif self.phase == 'choose_remaining_patch':
            dirty = [p for p in self.surfaces() if not self.is_clean(p)]
            if not dirty:
                self.primary_input(False)
                self.begin_clip('08_cleanup_completion', 5.)
                self.stage('wait_native_gate')
                return
            self.begin_brush(dirty[0], now)
            if len(dirty) <= 2:
                self.begin_clip('08_cleanup_completion', 30.)
        elif self.phase == 'full_brush':
            self.require(elapsed < 16., 'A real supported stain could not be finished by ordinary native contact')
            if self.is_clean(self.current_patch):
                self.report['completed_patches'].append(dict(before=self.partial_before, after=self.surface_row(self.current_patch),
                    actual_care_target_seen=self.partial_contact_seen, elapsed_seconds=elapsed))
                self.primary_input(False)
                self.stage('choose_remaining_patch')
                return
            if now-self.point_at >= 1.25:
                result = self.dirty_point(self.current_patch)
                self.require(result is not None, 'Dirty native gate has no reachable occupied coffee cell')
                point, foot, yaw = result
                self.brush_point, self.point_at = point, now
                self.place_observer(foot, yaw, 'Continue actual held brush at another remaining supported dirty cell')
                self.frame_camera(point+u.Vector(250, -370, 300), point+u.Vector(0, 0, 30))
        elif self.phase == 'wait_native_gate':
            self.require(elapsed < 4., 'All observed residue is clean but native TickFirstMeal did not release its gate')
            if stage not in ('MEAL_REST', 'MEALREST'):
                return
            self.require(self.native_first_meal_seen, 'Native FirstMeal was never observed')
            self.require(not any(not self.is_clean(p) for p in self.surfaces()), 'MealRest started with real tagged dirty residue remaining')
            self.report['checks']['native_first_meal_to_meal_rest_without_state_override'] = True
            self.report['native_gate_evidence'] = dict(caller='AMCSingleDayDirector::TickFirstMeal',
                predicate='MCCountDirectedFoodDirt(World, FirstMealBatch)==0 with issued food/hazards resolved',
                observed_stages=self.native_stages, tasks_before_brush=self.last_native_tasks,
                tasks_after=int(prop(self.gs, 'tasks_left')), direct_global_call_possible=False,
                stage_or_task_or_mask_assignments=False, batch=self.batch)
            after_snapshot = self.geometry_snapshot('native_meal_rest')
            self.report['native_gate_evidence'].update(first_meal_gate_counts_tagged_floor_residue=True,
                overall_mouth_cleanliness_claim=False,
                remaining_dirty_teeth=[row for row in after_snapshot['teeth'] if row['coffee_left'] > 0])
            self.screenshot('08_native_meal_rest.png')
            self.stage('completion_hold')
        elif self.phase == 'completion_hold' and elapsed >= 2.:
            self.clip = None
            self.stage('flush_capture')
        elif self.phase == 'flush_capture' and elapsed >= .8:
            required = ('08_central_floor_placement', '08_default_brush_ordinary', '08_default_brush_painted_wave', '08_cleanup_completion')
            if self.capture_hz:
                self.require(all(len(self.report['clips'].get(name, [])) >= 8 for name in required), 'Missing a required genuine cleanup clip')
            for entry in self.report['frames']:
                data = Path(entry['file']).read_bytes()
                self.require(data[:8] == b'\x89PNG\r\n\x1a\n', 'An engine frame did not finish writing')
                entry.update(sha256=hashlib.sha256(data).hexdigest(), width=int.from_bytes(data[16:20], 'big'),
                    height=int.from_bytes(data[20:24], 'big'))
            self.report['native_stages'] = self.native_stages
            self.finish()

    def tick(self, delta):
        if self.done:
            return
        try:
            self.require(time.monotonic()-self.started < 240., 'Focused cleanup proof exceeded its wall limit')
            worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:
                    return
                self.require(len(worlds) == 1 and u.SystemLibrary.is_standalone(worlds[0]), 'Expected one owned standalone PIE')
                pc = u.GameplayStatics.get_player_controller(worlds[0], 0)
                if not valid(pc) or not valid(pc.get_controlled_pawn()):
                    return
                self.world, self.pc = worlds[0], pc
                self.hero = pc.get_controlled_pawn()
                self.collection = prop(self.hero, 'food_collection')
                self.gs = u.GameplayStatics.get_game_state(self.world)
                self.tongue = self.actors(u.MCTongue)[0]
                self.throat = self.actors(u.MCThroat)[0]
                self.configure_profile()
                self.report['world'] = path(self.world)
                self.stage('first_perk')
                return
            self.require(self.world in worlds and len(worlds) == 1, 'Owned PIE ended or was replaced')
            self.hero = self.pc.get_controlled_pawn()
            self.require(isinstance(self.hero, u.MCToothCharacter) and prop(self.hero, 'status').is_alive(), 'The real observer died or changed pawn')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            self.require(now < 150., 'Focused cleanup proof exceeded 150 game seconds')
            if self.pc.is_reward_menu_open():
                self.resume_primary = self.resume_primary or self.primary
                self.primary_input(False)
                if self.throw_held:
                    self.resume_throw = True
                    self.hero.call_method('SetThrowInputHeld', args=(False,))
                    self.throw_held = False
                self.report['normal_reward_choices'].append(dict(time=now, phase=self.phase))
                self.pc.choose_reward_perk(0)
                if self.phase == 'first_perk':
                    self.stage('wait_meal')
                return
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            if self.resume_primary:
                self.primary_input(True)
                self.resume_primary = False
            if self.resume_throw:
                self.hero.call_method('SetThrowInputHeld', args=(True,))
                self.throw_held = True
                self.resume_throw = False
            if self.phase == 'first_perk':
                self.require(now-self.phase_at < 25., 'The native first-perk UI did not appear')
                return
            if self.phase == 'wait_meal' and not self.wave_observer_held:
                wave_support['brace_observer_for_wave'](self)
                self.wave_observer_held = True
            if now >= self.sample_at:
                self.sample_at = now+.2
                self.sample(now)
            self.keep_camera()
            if self.clip and now > self.clip_until:
                self.clip = None
            if self.capture_hz and self.clip and now >= self.frame_at:
                self.frame_at = now+1./self.capture_hz
                camera = self.camera_evidence()
                if camera and self.pc.get_view_target() == self.camera and camera['location_error_cm'] < 12.:
                    self.screenshot('%05d.png' % len(self.report['frames']), True)
            self.update(now)
        except Exception:
            self.finish(traceback.format_exc())

    def finish(self, error=None):
        if not self.done and valid(self.hero):
            try:
                self.hero.call_method('SetThrowInputHeld', args=(False,))
                self.throw_held = False
            except Exception:
                error = error or traceback.format_exc()
        if not self.done and valid(self.run_profile) and self.profile_before is not None:
            try:
                self.run_profile.set_editor_properties(self.profile_before)
                self.report['runtime_profile_restored'] = True
            except Exception:
                error = error or traceback.format_exc()
        super().finish(error)


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('A cleanup proof is already running; it was left intact')
    probe = CleanupFirstRelaxSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
