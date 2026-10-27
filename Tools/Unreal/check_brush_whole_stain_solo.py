"""Owned solo comparison of complete NORMAL stain cleanup, not raw erased bytes.

Run only after an authorized current MessControlEditor build, with idle L_Mouth
open. Never execute against a manual PIE. No package/profile saves, mask writes
or contact-readiness overrides. The initial native director-stop fixture resets
initial health/perks; timed cleanup never writes health. Fixtures are disclosed.

Both radii use the SAME current 5/3 dose and the SAME native held-brush/movement
policy. This isolates the footprint change (36cm -> 60cm), not a historical
binary comparison. Native CoffeeLeft==0 is the completion endpoint.

Optional globals: MC_BRUSH_OUTPUT (fresh directory on E:), MC_BRUSH_SIZES
(default180,240,440), MC_BRUSH_CASE_SECONDS(default26, max120),
MC_BRUSH_CASE_WALL_SECONDS(default29, max130), MC_BRUSH_TOTAL_WALL_SECONDS(default225,max420), MC_BRUSH_CAPTURE_HZ(default0,
max3), MC_BRUSH_CAPTURE_GATE(optional fresh E: marker path; max20s wait after Ready.json),
MC_BRUSH_FINISH_GATE(optional fresh E: marker path; max10s wait before closing captured PIE). Default total wall budget is225s; an explicit global can extend it to420s. No Computer Use.
Cancel: builtins.mc_brush_whole_stain_solo.finish('cancelled').
"""
import builtins
import hashlib
import json
import math
import runpy
import time
import traceback
import uuid
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
KEY='mc_brush_whole_stain_solo'
BASE_OUT=Path('E:/CodexWork/MessControl/BrushCleanupRegression_20261028')
RUN='Run_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8]
OUT=Path(globals().get('MC_BRUSH_OUTPUT',BASE_OUT/RUN)).resolve()
assert OUT.drive.upper()=='E:', 'All probe outputs must be on E:'
assert not OUT.exists(), 'Preserve old evidence: select a NEW output directory'
old=getattr(builtins,KEY,None)
assert old is None or getattr(old,'done',True), 'An owned brush probe is already running'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True), 'Existing manual PIE left untouched'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Save/resolve user map edits before this probe'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Save/resolve user content edits before this probe'

# Reject a known stale normal Editor DLL before starting any PIE or changing settings.
SOURCE_FILES=['Source/MessControl/Public/MCBrushCleaningBalance.h',
 'Source/MessControl/Public/MCInventoryComponent.h','Source/MessControl/Private/MCInventoryUpgrades.cpp']
dll=ROOT/'Binaries/Win64/UnrealEditor-MessControl.dll'
assert dll.is_file(), 'Current ordinary Editor DLL is required'
source_evidence=[dict(file=str(ROOT/f),mtime_ns=(ROOT/f).stat().st_mtime_ns,
 sha256=hashlib.sha256((ROOT/f).read_bytes()).hexdigest()) for f in SOURCE_FILES]
assert dll.stat().st_mtime_ns>=max(s['mtime_ns'] for s in source_evidence), 'Editor DLL predates brush source: perform the authorized rebuild first'
profile_default=u.get_default_object(u.MCEquipmentProfile)
assert abs(float(profile_default.get_editor_property('BrushCleaningRadius'))-60.)<.001, 'Loaded BrushCleaningRadius CDO is not current60cm'
OUT.mkdir(parents=True,exist_ok=False)
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
 run_name='brush_cleanup_support',init_globals={'MC_ENV_OUTPUT':OUT,'MC_ENV_CAPTURE_HZ':0})
Base,prop,vec=support['ApprovedEnvironmentSolo'],support['prop'],support['vec']
support['PLAY']['clientFixedFPS']=[0]


def outline_cells(seed):
    """Read-only steering port of FMCCoffeeWipe::WetAt; never writes a mask.
    Completion is checked independently on the actual native surface status.
    """
    angle=seed*2.39996
    c,s=math.cos(angle),math.sin(angle)
    def join(a,b):
        h=max(.065-abs(a-b),0.)/.065
        return max(a,b)+h*h*.01625
    cells=[]
    for y in range(64):
        for x in range(64):
            ux,uy=((x+.5)/64.-.5)*2,((y+.5)/64.-.5)*2
            px,py=ux*c-uy*s,(ux*s+uy*c)*(1.06+.20*math.sin(seed*1.731))
            d=.48-math.hypot(px,py)
            d=join(d,.30+.045*math.sin(seed*2.17)-math.hypot(px-.32,py-.08))
            d=join(d,.27+.045*math.cos(seed*3.11)-math.hypot(px+.35,py+.09))
            d=join(d,.25-math.hypot(px+.12,py-.34))
            d=join(d,.24-math.hypot(px-.20,py+.30))
            d+=.018*math.sin(px*24+seed)*math.sin(py*19-seed)
            if d>=.025:cells.append((y*64+x,ux,uy))
    return cells


class BrushWholeStainSolo(Base):
    def __init__(self):
        super().__init__()
        sizes=tuple(float(v) for v in globals().get('MC_BRUSH_SIZES',(180,240,440)))
        self.require(1<=len(sizes)<=3 and all(20<=v<=440 for v in sizes),'One to three ordinary stain half-sizes <=440cm')
        self.cases=[(size,radius) for size in sizes for radius in (36.,60.)]
        self.case_seconds=max(5.,min(120.,float(globals().get('MC_BRUSH_CASE_SECONDS',26.))))
        self.case_wall_seconds=max(8.,min(130.,float(globals().get('MC_BRUSH_CASE_WALL_SECONDS',29.))))
        self.total_wall_seconds=max(45.,min(420.,float(globals().get('MC_BRUSH_TOTAL_WALL_SECONDS',225.))))
        self.old_max_fps=float(u.SystemLibrary.get_console_variable_float_value('t.MaxFPS'))
        self.capture_hz=max(0.,min(3.,float(globals().get('MC_BRUSH_CAPTURE_HZ',0.))))
        self.capture_gate=Path(globals()['MC_BRUSH_CAPTURE_GATE']).resolve() if globals().get('MC_BRUSH_CAPTURE_GATE') else None
        self.require(self.capture_gate is None or (self.capture_gate.drive.upper()=='E:' and not self.capture_gate.exists()),
                     'Capture gate must be a fresh marker path on E:')
        self.finish_gate=Path(globals()['MC_BRUSH_FINISH_GATE']).resolve() if globals().get('MC_BRUSH_FINISH_GATE') else None
        self.require(self.finish_gate is None or (self.finish_gate.drive.upper()=='E:' and not self.finish_gate.exists()),
                     'Finish gate must be a fresh marker path on E:')
        self.require(self.finish_gate is None or self.capture_gate is None or self.finish_gate!=self.capture_gate,
                     'Capture start and finish gates must be distinct')
        self.pending_finish_error=None
        self.case_index=-1
        self.patch=self.inventory=self.original_settings=None
        self.profiles=[]
        self.last_game_time=None
        self.case_row=None
        self.cells=[]
        self.route_cache={}
        self.route=[];self.waypoint=0;self.route_laps=0
        self.last_command={}
        self.report_flush_wall=0.
        self.midshot=False
        self.walk_distance=0.
        self.previous_location=None
        self.next_sample=self.next_frame=0.
        self.report=dict(status='running',passed=False,started_utc=datetime.now(timezone.utc).isoformat(),
         scope='Whole normal AMCMouthSurface cleanup via held primary input and walking in one owned solo PIE',
         comparison='Radius36 vs60, identical current5/3 dose; native cleaning/contact/reach checks remain active',
         limitations=['Diagnostic fixture stops the director; this is not a full earned-run playthrough.',
          'Both variants receive the exact same precomputed world-space serpentine route, waypoint order and input scaling.',
          'Temporary review camera and diagnostic walking are shared by both variants; no manual gameplay skill comparison.',
          'No network, microphone, cook/package or historical binary benchmark.',
          'Timed-out/incomplete stains are reported as failures, never credited by erased-mask amount.'],
         freshness=dict(output_created_new=True,source=source_evidence,editor_dll=str(dll),editor_dll_mtime_ns=dll.stat().st_mtime_ns,
          loaded_profile_default_cm=60.,current_recipe_sha256=hashlib.sha256((ROOT/'Tools/Unreal/check_brush_whole_stain_solo.py').read_bytes()).hexdigest()),
         previous_play_settings=self.old_play,cases=[],comparisons=[],checks={},screenshots=[],frames=[],samples=[],
         policy=dict(case_game_seconds=self.case_seconds,case_wall_seconds=self.case_wall_seconds,total_wall_limit=self.total_wall_seconds,
          movement='Fixed world-space serpentine,48cm rows,75cm border margin,12cm waypoint arrival; native AddMovementInput and native facing',
          time_policy='Real elapsed game time, fixed timestep disabled, t.MaxFPS30 cap; game and wall timings both recorded',
          old_max_fps=self.old_max_fps,capture_gate=str(self.capture_gate) if self.capture_gate else None,
          finish_gate=str(self.finish_gate) if self.finish_gate else None,
          targeting='Exact same fixed waypoints per size and seed; no mask-dependent target retention, no per-frame yaw override; loop including return travel',
          row_spacing_cm=48.,border_stance_margin_cm=75.,waypoint_arrival_cm=12.,input_scale_min=.12,input_scale_max=.45,
          no_active_case_teleports=True,completion='Actual surface Status.State.CoffeeLeft==0; actor end alone is not success',
          steering_port='FMCCoffeeWipe::WetAt normal seeded outline, not a gameplay mask writer'))

    def write(self):
        self.output.mkdir(parents=True,exist_ok=True)
        pending=self.output/'Report.next.json'
        pending.write_text(json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
        for attempt in range(10):
            try:
                pending.replace(self.output/'Report.json')
                break
            except PermissionError:
                if attempt==9:raise
                time.sleep(.002)
        (self.output/'README.md').write_text(
         '# Whole normal stain cleanup comparison\n\n'
         'An owned standalone L_Mouth PIE compares36cm and60cm brush footprints at the same current5/3 dose. '
         'A new normal stain receives a disclosed initial CoffeeLeft=CoffeeTotal; its native mask starts untouched255. Held primary input and native character movement do all cleaning. '
         'Timed cleanup never writes masks, health, care-progress or contact readiness. Before timing, the existing native editor fixture stops the director and resets initial player health/perks. '
         'One fixed world-space serpentine route is generated from the initial outline, with48cm rows and75cm stance margin. Both radii receive identical waypoints, order and input scaling; return travel is timed and natural movement owns facing. '
         'Completion requires native CoffeeLeft==0; incomplete runs are never credited by coverage removed.\n\n'
         'Profiles are transient UMCEquipmentProfile instances used only in this PIE; the original inventory Settings reference is restored. '
         'Only this owned PIE is stopped, and play/frame/foreground settings are restored by the shared capture recipe. '
         'Screenshots are synchronous exact game-window captures with hashes/timestamps in Report.json. '
         'No content package is saved and this is not a full-run or multiplayer test.\n',encoding='utf-8')

    def choose_floor(self):
        origin,_=self.tongue.get_actor_bounds(False)
        for dx,dy in ((0,0),(350,0),(-350,0),(0,300),(0,-300)):
            candidate=origin+u.Vector(dx,dy,0)
            floor=self.floor(candidate)
            if floor is None:continue
            tests=[self.floor(floor+u.Vector(math.cos(a)*460,math.sin(a)*460,0)) for a in (0,math.pi/2,math.pi,3*math.pi/2)]
            if all(v is not None and abs(v.z-floor.z)<130 for v in tests):return floor
        raise RuntimeError('No supported interior footprint for the largest normal stain; do not fake an unsupported completion')

    def begin_case(self):
        self.primary_input(False)
        self.hero.get_movement_component().stop_movement_immediately()
        if self.patch is not None and u.SystemLibrary.is_valid(self.patch):self.patch.destroy_actor()
        self.patch=None
        self.case_index+=1
        if self.case_index>=len(self.cases):
            self.complete_report();return
        half_size,radius=self.cases[self.case_index]
        profile=u.new_object(u.MCEquipmentProfile,outer=self.inventory,name='BrushProbe_%d_%d'%(self.case_index,int(radius)))
        profile.set_editor_property('BrushCleaningRadius',radius)
        self.require(u.MCGameplayUIEditorLibrary.set_pie_equipment_profile(self.hero,profile),'Standalone PIE equipment swap rejected')
        self.profiles.append(profile)
        self.require(u.MCGameplayUIEditorLibrary.get_pie_equipment_profile(self.hero)==profile,'Transient equipment profile was not assigned')
        pose=u.Transform(location=self.center+u.Vector(0,0,5))
        self.patch=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.MCMouthSurface.static_class(),pose,
         u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(self.patch is not None,'Could not create owned ordinary stain')
        self.patch.set_editor_properties(dict(randomize_liquid_size=False,liquid_half_size=half_size,ulcer=False,paint_tile=False))
        self.statics.call_method('FinishSpawningActor',args=(self.patch,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        # Initial fixture dirt only: native ApplyCoffee is not reflected to Python.
        # Never write this status again; all progress must come from real brush work.
        status=prop(self.patch,'status');state=prop(status,'state')
        total=int(prop(state,'coffee_total'))
        self.require(total>0,'Authored stain CoffeeTotal must be positive')
        state.set_editor_property('coffee_left',total)
        status.set_editor_property('State',state)
        seed=int(prop(self.patch,'liquid_seed'))
        self.cells=outline_cells(seed)
        self.require(self.cells,'Seeded normal stain has no visible working cells')
        route_key=(half_size,seed)
        if route_key not in self.route_cache:
            # Construct ONCE from the original visible outline, before any work.
            # Both variants reuse these exact WORLD XY waypoints, even as their
            # native residue and contact paths diverge. No dry-section skipping.
            transform=self.patch.get_actor_transform()
            points=[transform.transform_location(u.Vector(x*half_size,y*half_size,0)) for _,x,y in self.cells]
            min_x,max_x=min(v.x for v in points)-75.,max(v.x for v in points)+75.
            min_y,max_y=min(v.y for v in points)-75.,max(v.y for v in points)+75.
            rows=max(1,int(math.ceil((max_y-min_y)/48.)))
            route=[]
            for row in range(rows+1):
                y=min_y+(max_y-min_y)*row/rows
                xs=(min_x,max_x) if row%2==0 else (max_x,min_x)
                for x in xs:
                    self.require(self.floor(u.Vector(x,y,self.center.z)) is not None,'Serpentine waypoint has no native tongue support')
                    route.append([float(x),float(y)])
            self.route_cache[route_key]=route
        self.route=self.route_cache[route_key];self.waypoint=0;self.route_laps=0
        self.last_command=dict(submitted=False,vector=[0.,0.,0.],scale=0.,waypoint=0,distance_cm=None)
        mask=list(prop(self.patch,'wipe_mask'))
        self.require(len(mask)==4096 and all(int(v)==255 for v in mask),'Normal stain must start with native untouched64x64 mask')
        start=self.start_floor
        self.require(start is not None,'No floor at paired starting pose')
        self.move_to(start,0.)
        self.inventory.server_select(u.MCToolSlot.BRUSH)
        now=float(u.GameplayStatics.get_time_seconds(self.world))
        self.case_row=dict(index=self.case_index,half_size_cm=half_size,radius_cm=radius,seed=seed,
         actor=self.patch.get_path_name(),position_cm=vec(self.patch.get_actor_location()),start_pose_cm=vec(self.hero.get_actor_location()),
         initial_coffee_left=int(prop(prop(prop(self.patch,'status'),'state'),'coffee_left')),initial_coffee_total=total,
         initialization='New transient ordinary stain: only initial CoffeeLeft=positive CoffeeTotal; no health/mask/contact writes',
         initial_mask_sha256=hashlib.sha256(bytes(mask)).hexdigest(),visible_seed_cells=len(self.cells),
         traversal_waypoints_xy_cm=self.route,traversal_sha256=hashlib.sha256(json.dumps(self.route,separators=(',',':')).encode()).hexdigest(),
         game_started_at=now,wall_started_at=time.monotonic()-self.started,wall_started_unix=time.time(),completed=False,outcome='running')
        self.require(self.case_row['initial_coffee_left']>0,'Fixture was not actually dirty')
        self.report['cases'].append(self.case_row)
        self.midshot=False;self.walk_distance=0.;self.previous_location=self.hero.get_actor_location()
        self.next_sample=now;self.next_frame=now
        self.frame_camera(self.center+u.Vector(half_size*.3,-max(680,half_size*2.3),max(650,half_size*2.1)),self.center)
        self.phase='settle_case';self.phase_at=now;self.case_wall_started=time.monotonic()
        self.write()

    @staticmethod
    def optional_read(function):
        try:return function()
        except Exception:return None

    def liquid_visible(self):
        return self.optional_read(lambda:bool(prop(self.patch,'liquid').is_visible()))

    def steer(self,mask):
        # mask is diagnostic-only. Body traversal must not change with radius.
        position=self.hero.get_actor_location()
        if self.previous_location is not None:self.walk_distance+=math.hypot(position.x-self.previous_location.x,position.y-self.previous_location.y)
        self.previous_location=position
        for _ in range(len(self.route)):
            x,y=self.route[self.waypoint]
            dx,dy=x-position.x,y-position.y
            distance=math.hypot(dx,dy)
            if distance>12.:break
            self.waypoint=(self.waypoint+1)%len(self.route)
            if self.waypoint==0:self.route_laps+=1
        self.last_command=dict(submitted=False,vector=[0.,0.,0.],scale=0.,
         waypoint=self.waypoint,waypoint_xy_cm=self.route[self.waypoint],distance_cm=distance,laps=self.route_laps)
        if distance<=12.:return
        direction=u.Vector(dx/distance,dy/distance,0.)
        # Shared proportional arrival slows corners without a48-82cm dead zone.
        # Never override actor yaw: normal locomotion and the actual bristle
        # contact own rotation. No teleport or forced brush-ready state.
        scale=min(.45,max(.12,distance*.45/80.))
        self.hero.add_movement_input(direction,scale,False)
        self.last_command.update(submitted=True,vector=vec(direction),scale=scale)

    def end_case(self,outcome,now):
        self.primary_input(False)
        self.hero.get_movement_component().stop_movement_immediately()
        self.case_row.update(outcome=outcome,completed=outcome=='native_clean',
         seconds_to_native_clean=now-self.brush_started if outcome=='native_clean' else None,
         measured_game_seconds=now-self.brush_started,wall_seconds=time.monotonic()-self.case_wall_started,
         walked_cm=self.walk_distance,completed_route_laps=self.route_laps,last_command=self.last_command,brush_ended_wall_unix=time.time(),brush_ended_wall_elapsed=time.monotonic()-self.started)
        if u.SystemLibrary.is_valid(self.patch):
            self.case_row['final_coffee_left']=int(prop(prop(prop(self.patch,'status'),'state'),'coffee_left'))
            self.case_row['final_mask_sha256']=hashlib.sha256(bytes(prop(self.patch,'wipe_mask'))).hexdigest()
        self.screenshot('%02d_%dcm_r%d_%s.png'%(self.case_index,int(self.case_row['half_size_cm']),int(self.case_row['radius_cm']),outcome))
        self.phase='case_gap';self.phase_at=now;self.write()

    def complete_report(self):
        for size in sorted(set(c['half_size_cm'] for c in self.report['cases'])):
            rows=[c for c in self.report['cases'] if c['half_size_cm']==size]
            before=next((c for c in rows if c['radius_cm']==36),None)
            after=next((c for c in rows if c['radius_cm']==60),None)
            comparable=bool(before and after and before['seed']==after['seed'] and before['initial_mask_sha256']==after['initial_mask_sha256']
             and before['initial_coffee_left']==after['initial_coffee_left'] and before['start_pose_cm']==after['start_pose_cm']
             and before['traversal_sha256']==after['traversal_sha256'])
            entry=dict(half_size_cm=size,comparable_initial_fixture=comparable,both_native_clean=bool(before and after and before['completed'] and after['completed']))
            if entry['both_native_clean']:
                entry.update(before_seconds=before['seconds_to_native_clean'],after_seconds=after['seconds_to_native_clean'],
                 duration_reduction_percent=100*(1-after['seconds_to_native_clean']/max(.0001,before['seconds_to_native_clean'])))
            self.report['comparisons'].append(entry)
        self.report['checks']=dict(all_pairs_identical_initial_fixture=all(v['comparable_initial_fixture'] for v in self.report['comparisons']),
         all_stains_reached_native_clean=len(self.report['cases'])==len(self.cases) and all(c['completed'] for c in self.report['cases']),
         larger_radius_faster_for_all_completed_pairs=all(v.get('duration_reduction_percent',-1)>0 for v in self.report['comparisons']))
        self.report['measured_goal']='Report actual whole-stain duration reductions; do not extrapolate to all levels or claim40% from raw coverage'
        error=None if all(self.report['checks'].values()) else 'One or more paired whole-stain comparisons failed or did not finish inside the disclosed budget'
        if self.finish_gate:
            self.pending_finish_error=error;self.capture_end_at=time.monotonic()
            self.report['capture_end_ready_wall_unix']=time.time()
            self.report['capture_end_ready_wall_elapsed']=self.capture_end_at-self.started
            self.report['capture_end_gate']=str(self.finish_gate)
            self.phase='capture_end_gate';self.report['last_phase']=self.phase
            marker=dict(status='capture_end_ready',wall_unix=self.report['capture_end_ready_wall_unix'],
             wall_elapsed=self.report['capture_end_ready_wall_elapsed'],finish_gate=str(self.finish_gate),
             timeout_seconds=10.,gameplay_checks_passed=not bool(error),error=error)
            (self.output/'CaptureEndReady.json').write_text(json.dumps(marker,indent=2),encoding='utf-8')
            self.write();return
        self.finish(error)

    def tick(self,_delta):
        if self.done:return
        try:
            wall=time.monotonic()-self.started
            # Keep the captured SWindow alive while its recorder flushes. This
            # bounded10s grace is independent of the gameplay/scenario budget.
            if self.phase=='capture_end_gate':
                if self.finish_gate.exists():
                    self.report['capture_finish_gate_released_wall_unix']=time.time()
                    self.finish(self.pending_finish_error);return
                if time.monotonic()-self.capture_end_at>=10.:
                    self.report.setdefault('warnings',[]).append('Finish capture gate timed out after10s; closing only owned PIE')
                    self.report['capture_finish_gate_timed_out']=True
                    self.finish(self.pending_finish_error);return
                return
            self.require(wall<self.total_wall_seconds,'Bounded%.0fs total wall-time limit reached; unfinished cases remain failures'%self.total_wall_seconds)
            if self.pie_start is not None:self.require(not prop(self.pie_start,'Error'),'StartPIE: '+str(prop(self.pie_start,'Error')))
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'Exactly one owned standalone authority PIE')
                self.world=worlds[0];self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                if not self.pc or not self.pc.get_controlled_pawn():self.world=None;return
                self.hero=self.pc.get_controlled_pawn();self.engine.set_editor_property('bUseFixedFrameRate',False)
                self.console('t.MaxFPS 30');self.phase='readiness';self.phase_at=float(u.GameplayStatics.get_time_seconds(self.world))
                self.report['world']=self.world.get_path_name();return
            self.require(self.owned_pie_is_active(worlds),'Owned PIE was replaced/stopped; preserve other sessions')
            now=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now<=self.last_game_time:return
            self.last_game_time=now
            if self.phase=='readiness':
                self.require(wall<40,'Loading/readiness exceeded bounded fixture setup time')
                loading=[w for w in u.ObjectIterator(u.load_class(None,'/Script/MessControl.MCLoadingWidget')) if w.get_owning_player()==self.pc]
                if any(w.is_in_viewport() and w.get_visibility() not in [u.SlateVisibility.COLLAPSED,u.SlateVisibility.HIDDEN] for w in loading):return
                if now-self.phase_at<1.5:return
                result=u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'level')
                self.require(not str(result).startswith('REJECT'),'Explicit director-stop fixture: '+str(result))
                self.report['diagnostic_fixture_result']=str(result)
                self.tongue=self.actors(u.MCTongue)[0]
                self.inventory=prop(self.hero,'inventory');self.original_settings=u.MCGameplayUIEditorLibrary.get_pie_equipment_profile(self.hero)
                self.require(self.original_settings is not None,'Native standalone equipment profile was not available')
                self.report['old_inventory_settings']=self.original_settings.get_path_name() if self.original_settings else None
                self.report['removed_interfering_pie_stains']=[a.get_path_name() for a in self.actors(u.MCMouthSurface)]
                for actor in self.actors(u.MCMouthSurface):actor.destroy_actor()
                self.center=self.choose_floor();self.report['paired_center_floor_cm']=vec(self.center)
                self.start_floor=self.floor(self.center-u.Vector(115,0,0))
                self.require(self.start_floor is not None,'Paired start floor exists')
                self.ready_wall=time.monotonic()
                ready=dict(status='ready_for_capture',world=self.world.get_path_name(),output=str(self.output),
                 ready_wall_unix=time.time(),ready_wall_elapsed=self.ready_wall-self.started,
                 capture_gate=str(self.capture_gate) if self.capture_gate else None,gate_timeout_seconds=20.,
                 game_time=now,window='Owned floating1280x720 standalone PIE; all timed scenarios are still pending')
                (self.output/'Ready.json').write_text(json.dumps(ready,indent=2),encoding='utf-8')
                self.report['capture_ready']=ready;self.phase='capture_gate';self.write();return
            if self.phase=='capture_gate':
                if self.capture_gate and not self.capture_gate.exists():
                    self.require(time.monotonic()-self.ready_wall<20.,'Capture gate was not released within20s; no timed scenario started')
                    return
                self.report['capture_gate_released_wall_unix']=time.time();self.begin_case();return
            if self.phase=='settle_case' and now-self.phase_at>=.5:
                self.screenshot('%02d_%dcm_r%d_start.png'%(self.case_index,int(self.case_row['half_size_cm']),int(self.case_row['radius_cm'])))
                self.case_row['native_liquid_visible_before_brushing']=self.liquid_visible()
                self.brush_started=now;self.case_row.update(brush_started_wall_unix=time.time(),
                 brush_started_wall_elapsed=time.monotonic()-self.started,brush_started_game_time=now,
                 actual_pose_at_brush_start_cm=vec(self.hero.get_actor_location()))
                self.primary_input(True);self.phase='brushing';self.phase_at=now;return
            if self.phase=='brushing':
                self.require(u.SystemLibrary.is_valid(self.patch),'Stain ended before native clean was observed; destruction alone is not success')
                left=int(prop(prop(prop(self.patch,'status'),'state'),'coffee_left'))
                if left==0:self.end_case('native_clean',now);return
                if now-self.brush_started>=self.case_seconds or time.monotonic()-self.case_wall_started>=self.case_wall_seconds:
                    self.end_case('timed_out_not_clean',now);return
                mask=list(prop(self.patch,'wipe_mask'))
                self.steer(mask)
                if now>=self.next_sample:
                    self.next_sample=now+.2
                    contact=prop(self.hero,'brush_contact')
                    visible_remaining=sum(max(0.,min(1.,(mask[i]/255.-.25)/.75)) for i,_,_ in self.cells)/len(self.cells)
                    self.report['samples'].append(dict(case=self.case_index,game_seconds=now,elapsed=now-self.brush_started,
                     coffee_left=left,diagnostic_weighted_mask_remaining=visible_remaining,hero_cm=vec(self.hero.get_actor_location()),
                     velocity_cm_s=vec(self.hero.get_velocity()),command=dict(self.last_command),
                     pc_move_input_ignored=self.optional_read(lambda:bool(self.pc.is_move_input_ignored())),
                     pc_look_input_ignored=self.optional_read(lambda:bool(self.pc.is_look_input_ignored())),
                     movement_mode=self.optional_read(lambda:str(prop(self.hero.get_movement_component(),'movement_mode'))),
                     native_care_target=str(prop(self.hero,'care_target')),native_brushing=bool(prop(self.hero,'brushing')),
                     body_state=str(prop(self.hero,'tooth_physics').call_method('GetBodyState'))))
                    if not self.midshot and visible_remaining<.5:
                        self.midshot=True;self.screenshot('%02d_midway.png'%self.case_index)
                if self.capture_hz and now>=self.next_frame:
                    self.next_frame=now+1/self.capture_hz;self.screenshot('%05d.png'%len(self.report['frames']),sequence=True)
            elif self.phase=='case_gap' and now-self.phase_at>=.65:
                if self.case_row['completed']:
                    self.case_row['native_liquid_visible_after_clean_fade']=self.liquid_visible()
                    self.case_row['native_fade_wait_game_seconds']=now-self.phase_at
                    self.case_row['native_fade_observed_wall_unix']=time.time()
                    self.case_row['fade_visibility_telemetry_available']=self.case_row['native_liquid_visible_after_clean_fade'] is not None
                    self.screenshot('%02d_after_native_fade.png'%self.case_index)
                self.write();self.begin_case()
            if not self.done and time.monotonic()>=self.report_flush_wall:
                self.report_flush_wall=time.monotonic()+1.;self.write()
        except Exception:self.finish(traceback.format_exc())

    def finish(self,error=None):
        if self.done:return
        try:
            if self.owned_pie_is_active() and self.inventory is not None and u.SystemLibrary.is_valid(self.inventory):
                self.primary_input(False)
                self.require(u.MCGameplayUIEditorLibrary.set_pie_equipment_profile(self.hero,self.original_settings),'Original equipment restoration rejected')
                self.report['inventory_settings_restored']=u.MCGameplayUIEditorLibrary.get_pie_equipment_profile(self.hero)==self.original_settings
        except Exception:
            self.report['inventory_settings_restore_error']=traceback.format_exc()
            error=error or 'Transient equipment Settings restoration failed'
        try:
            u.SystemLibrary.execute_console_command(self.editor_world,'t.MaxFPS '+str(self.old_max_fps))
            self.report['max_fps_restored']=abs(float(u.SystemLibrary.get_console_variable_float_value('t.MaxFPS'))-self.old_max_fps)<.001
            self.require(self.report['max_fps_restored'],'t.MaxFPS restoration failed')
        except Exception:
            self.report['max_fps_restore_error']=traceback.format_exc()
            error=error or 'Frame cap restoration failed'
        # Shared cleanup unregisters the callback, releases held input, stops ONLY
        # its still-owned PIE and restores play/frame/foreground/realtime settings.
        super().finish(error)


probe=BrushWholeStainSolo()
setattr(builtins,KEY,probe)
try:probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
