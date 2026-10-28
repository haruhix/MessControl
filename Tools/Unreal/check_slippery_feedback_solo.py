"""Owned, bounded rendered solo observation of native slippery movement.

Run only in idle L_Mouth after an authorized ordinary Editor build. No Computer
Use. The probe never calls BeginSlipperyFall, sets body/movement state, velocity,
health, carried ownership, masks or readiness. Native AddMovementInput drives
every timed case. The disclosed flat PIE support and LocomotionSurface override
exercise slippery locomotion, NOT the actual Ice event visuals or warm zones.
Root records the owned1280x720 window through the four capture gate files.
Optional globals: MC_SLIP_ONLY_CARRY=True runs only the carry scenario;
MC_SLIP_CARRY_PIECES requests1..6 real fragments (default1). Carry telemetry
is sampled at8Hz; every other case retains2Hz sampling.
Cancel: builtins.mc_slippery_feedback_solo.finish('cancelled').
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
KEY='mc_slippery_feedback_solo'
RUN='Run_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8]
OUT=Path(globals().get('MC_SLIP_OUTPUT',Path('E:/CodexWork/MessControl/SlipperyFeedback_20261028')/RUN)).resolve()
assert OUT.drive.upper()=='E:' and not OUT.exists(), 'Choose a fresh output directory on E:'
old=getattr(builtins,KEY,None)
assert old is None or getattr(old,'done',True), 'An owned slip probe is already active'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True), 'Manual PIE left untouched'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Resolve unsaved user map edits first'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Resolve unsaved user content edits first'
sources=[ROOT/'Source/MessControl/Public/MCSlipperyRisk.h',ROOT/'Source/MessControl/Private/MCToothPhysicsComponent.cpp',
 ROOT/'Source/MessControl/Public/MCSlipperyFood.h',ROOT/'Source/MessControl/Private/MCSlipperyFood.cpp',
 ROOT/'Source/MessControl/Public/MCFoodActor.h',ROOT/'Source/MessControl/Private/MCFoodReaction.cpp']
dll=ROOT/'Binaries/Win64/UnrealEditor-MessControl.dll'
assert dll.is_file() and dll.stat().st_mtime_ns>=max(p.stat().st_mtime_ns for p in sources), 'Authorized current Editor rebuild required'
OUT.mkdir(parents=True,exist_ok=False)
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),run_name='slippery_feedback_support',
 init_globals={'MC_ENV_OUTPUT':OUT,'MC_ENV_CAPTURE_HZ':0})
Base,prop,vec=support['ApprovedEnvironmentSolo'],support['prop'],support['vec']
support['PLAY']['clientFixedFPS']=[0]


class SlipperyFeedbackSolo(Base):
    def __init__(self):
        super().__init__()
        self.cvars={name:float(u.SystemLibrary.get_console_variable_float_value(name)) for name in
         ('MC.Slip.EmptyHandsMode','MC.Slip.FirstRiskSeconds','MC.Slip.RepeatRiskSeconds','MC.Slip.FallChance','MC.Slip.Debug','t.MaxFPS')}
        self.start_gate=OUT/'StartCapture.gate';self.finish_gate=OUT/'FinishCapture.gate'
        self.case_index=-1;self.case=None;self.floor_actor=self.surface=None
        self.next_sample=self.flush_at=0.;self.last_body=None;self.previous_position=None
        self.capture_started=False;self.pending_error=None;self.fixture_food=None
        self.fixture_foods=[];self.food_observations={}
        self.only_carry=bool(globals().get('MC_SLIP_ONLY_CARRY',False))
        self.carry_pieces=max(1,min(6,int(globals().get('MC_SLIP_CARRY_PIECES',1))))
        self.cases=[dict(name='risk_straight_empty',mode=1,slippery=True,seconds=20.,pattern='straight',scale=.18),
                    dict(name='legacy_straight_empty',mode=0,slippery=True,seconds=10.,pattern='straight',scale=.18),
                    dict(name='legacy_sharp_turn_empty',mode=0,slippery=True,seconds=10.,pattern='square',scale=.85),
                    dict(name='normal_ground_empty',mode=1,slippery=False,seconds=6.,pattern='straight',scale=.18),
                    dict(name='native_E_carry_on_slippery',mode=1,slippery=True,seconds=8.,pattern='straight',scale=.30,carry=True)]
        if self.only_carry:self.cases=[self.cases[-1]]
        self.report=dict(status='running',passed=False,output=str(OUT),started_utc=datetime.now(timezone.utc).isoformat(),
         scope=('OnlyCarry: one owned standalone PIE with actual E pickup and observed native food toss' if self.only_carry else
                'One owned floating1280x720 standalone PIE; actual movement and observed native state changes'),
         only_carry=self.only_carry,requested_carry_pieces=self.carry_pieces,
         source=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sources],
         previous_play_settings=self.old_play,previous_cvars=self.cvars,cases=[],samples=[],transitions=[],checks={},screenshots=[],frames=[],
         limitations=['Flat transient support and slippery LocomotionSurface are diagnostic ice-equivalent footing, not the actual Ice event or safe-zone visuals.',
          'Risk mode uses the actual50% native random roll; absence of an observed fall cannot prove that its visual transition works.',
          'Initial director-stop fixture resets initial health/perks through the existing editor review endpoint; timed cases never write them.',
          'Food pickup invokes the same native server endpoint as E, not a hardware key; inaccessible/rejected pickup remains explicitly untested.',
          'Settled-after-arc telemetry requires observed upward and descending physics velocities followed by two slow samples; floor contact and motion quality still require video review.',
          'No network, microphone, package saves, forced falls, velocity assignments or movement-mode overrides.'],
         time_policy=dict(real_elapsed_game_time=True,fixed_timestep=False,maximum_wall_seconds=240,
                          maximum_case_game_seconds=sum(c['seconds'] for c in self.cases),sample_hz=2,carry_sample_hz=8),
         fixture=dict(surface='MCLocomotionSurface at priority10000; ground response only',support='Transient basic cube slab; no new materials',
                      initial_placement_only=True,review_camera='Native player camera; no scripted camera override'))

    def optional(self,read):
        try:return read()
        except Exception:return None

    def write(self):
        pending=OUT/'Report.next.json'
        pending.write_text(json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
        pending.replace(OUT/'Report.json')

    def body(self):
        return str(prop(self.hero,'tooth_physics').call_method('GetBodyState'))

    def carried(self):
        pieces=self.optional(lambda:list(prop(prop(self.hero,'food_collection'),'pieces'))) or []
        held=self.optional(lambda:prop(self.hero,'held_food'))
        return len([p for p in pieces if p is not None and u.SystemLibrary.is_valid(p)])+(1 if held and u.SystemLibrary.is_valid(held) else 0)

    def standing(self):
        return 'STANDING' in self.body().upper()

    def path_if_valid(self,obj):
        return obj.get_path_name() if obj is not None and u.SystemLibrary.is_valid(obj) else None

    def food_snapshot(self,now):
        rows=[]
        for food in self.fixture_foods:
            path=self.optional(lambda:food.get_path_name())
            if food is None or not u.SystemLibrary.is_valid(food):
                rows.append(dict(path=path,valid=False));continue
            position=food.get_actor_location();body=prop(food,'body')
            velocity=self.optional(lambda:body.get_physics_linear_velocity())
            owner=self.optional(lambda:self.path_if_valid(prop(food,'stack_carrier')))
            holders=self.optional(lambda:[self.path_if_valid(h) for h in prop(food,'holders')])
            phase=str(prop(food,'phase'))
            is_carried=owner==self.hero.get_path_name() or self.hero.get_path_name() in (holders or [])
            record=dict(path=path,valid=True,position_cm=vec(position),actor_velocity_cm_s=vec(food.get_velocity()),
                        body_velocity_cm_s=vec(velocity) if velocity is not None else None,
                        velocity_z_cm_s=float(velocity.z) if velocity is not None else None,
                        phase=phase,stack_carrier=owner,holders=holders,owned_by_probe_player=is_carried,
                        simulating_physics=self.optional(lambda:bool(body.is_simulating_physics())))
            rows.append(record)
            history=self.food_observations.setdefault(path,dict(path=path,observed_carried=False,released=False,
                upward_after_release=False,descending_after_upward=False,settled_after_arc=False,
                settled_samples=0,first_carried_game_seconds=None,release_game_seconds=None,
                max_upward_velocity_z_cm_s=0.,minimum_velocity_z_cm_s=0.))
            if is_carried:
                if not history['observed_carried']:history['first_carried_game_seconds']=now
                history['observed_carried']=True
            elif history['observed_carried']:
                if not history['released']:
                    history.update(released=True,release_game_seconds=now,first_observed_release_position_cm=vec(position))
                if velocity is not None:
                    vz=float(velocity.z)
                    history['max_upward_velocity_z_cm_s']=max(history['max_upward_velocity_z_cm_s'],vz)
                    history['minimum_velocity_z_cm_s']=min(history['minimum_velocity_z_cm_s'],vz)
                    if vz>10:history['upward_after_release']=True
                    if history['upward_after_release'] and vz<-10:history['descending_after_upward']=True
                    settled=history['descending_after_upward'] and abs(vz)<20 and math.sqrt(sum(v*v for v in vec(velocity)))<40
                    history['settled_samples']=history['settled_samples']+1 if settled else 0
                    if history['settled_samples']>=2:history['settled_after_arc']=True
            history.update(last_game_seconds=now,last_position_cm=vec(position),last_phase=phase,last_stack_carrier=owner)
        return rows

    def owned_fixture_count(self):
        return sum(bool(r.get('owned_by_probe_player')) for r in self.food_snapshot(float(u.GameplayStatics.get_time_seconds(self.world))))

    def deferred(self,cls,pose):
        actor=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,cls.static_class(),pose,
         u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(actor is not None,'Could not create owned transient fixture '+str(cls))
        return actor

    def finish_spawn(self,actor,pose):
        self.statics.call_method('FinishSpawningActor',args=(actor,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))

    def setup(self,now):
        result=u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'level')
        self.require(not str(result).startswith('REJECT'),'Native director-stop fixture rejected: '+str(result))
        self.report['diagnostic_fixture_result']=str(result)
        self.tongue=self.actors(u.MCTongue)[0]
        center,_=self.tongue.get_actor_bounds(False);floor=self.floor(center)
        self.require(floor is not None,'No native tongue center support; do not fabricate successful footing')
        self.center=floor+u.Vector(0,0,80)
        pose=u.Transform(location=self.center-u.Vector(0,0,7.5),scale=u.Vector(38,18,.15))
        self.floor_actor=self.deferred(u.StaticMeshActor,pose)
        mesh=prop(self.floor_actor,'static_mesh_component')
        mesh.set_static_mesh(u.load_object(None,'/Engine/BasicShapes/Cube.Cube'))
        mesh.set_collision_profile_name('BlockAll')
        self.finish_spawn(self.floor_actor,pose)
        pose=u.Transform(location=self.center)
        self.surface=self.deferred(u.MCLocomotionSurface,pose)
        self.surface.set_editor_properties(dict(half_extent=u.Vector(1800,800,250),priority=10000,surface=u.MCGroundSurface.SLIPPERY))
        self.finish_spawn(self.surface,pose)
        self.report['fixture'].update(center_floor_cm=vec(self.center),slab=self.floor_actor.get_path_name(),surface_actor=self.surface.get_path_name())
        self.report['hero_path']=self.hero.get_path_name();self.ready_at=time.monotonic()
        self.phase='capture_gate'
        (OUT/'Ready.json').write_text(json.dumps(dict(status='ready_for_capture',world=self.world.get_path_name(),output=str(OUT),
         ready_wall_unix=time.time(),game_seconds=now,start_gate=str(self.start_gate),finish_gate=str(self.finish_gate),gate_timeout_seconds=30,
         window='Owned1280x720 floating standalone PIE; timed cases have not started'),indent=2),encoding='utf-8')
        self.write()

    def request_case(self,now):
        self.case_index+=1
        if self.case_index>=len(self.cases):self.queue_finish();return
        self.phase='between_cases';self.phase_at=now;self.gap_wall=time.monotonic()

    def begin_case(self,now):
        plan=self.cases[self.case_index]
        self.require(self.standing(),'Policy changes and initial placement require a naturally standing pawn')
        self.require(self.carried()==0,'Unexpected carried item before an empty-hand scenario')
        self.console('MC.Slip.EmptyHandsMode '+str(plan['mode']))
        self.console('MC.Slip.FirstRiskSeconds 1.5');self.console('MC.Slip.RepeatRiskSeconds 3')
        self.console('MC.Slip.FallChance 0.5');self.console('MC.Slip.Debug 1')
        self.surface.set_editor_property('surface',u.MCGroundSurface.SLIPPERY if plan['slippery'] else u.MCGroundSurface.NORMAL)
        start=self.center+u.Vector(-700,-160 if plan['pattern']=='square' else 0,0)
        half=self.hero.capsule_component.get_scaled_capsule_half_height()
        self.hero.set_actor_location_and_rotation(start+u.Vector(0,0,half+3),u.Rotator(yaw=0),False,True)
        self.pc.set_control_rotation(u.Rotator(pitch=-20,yaw=0))
        self.case=dict(plan,setup_game_seconds=now,setup_wall_unix=time.time(),initial_pose_cm=vec(self.hero.get_actor_location()),
                       initial_body=self.body(),observed_slip_entries=0,observed_state_changes=[],distance_cm=0.,samples=0,
                       correct_surface_samples=0,moving_samples=0,completed=False)
        self.report['cases'].append(self.case);self.previous_position=self.hero.get_actor_location();self.last_body=self.body()
        self.phase='settle_case';self.phase_at=now;self.case_wall=time.monotonic();self.next_sample=now

    def prepare_carry(self,now):
        self.case['carry_attempt']=dict(native_endpoint='ServerToggleFoodCollection: same authority endpoint as E',started_game_seconds=now,
                                        requested_pieces=self.carry_pieces,initial_fixtures=[])
        self.fixture_foods=[];self.food_observations={}
        try:
            columns=min(3,self.carry_pieces)
            for index in range(self.carry_pieces):
                # Separate the default96x64cm physics boxes while keeping all
                # six optional pieces inside the real180cm pickup reach.
                offset=u.Vector(75 if index//columns==0 else -75,(index%columns-(columns-1)/2)*75,5)
                pose=u.Transform(location=self.hero.get_actor_location()+offset)
                food=self.deferred(u.MCFoodActor,pose)
                food.set_editor_property('fragment',True)
                self.finish_spawn(food,pose);self.fixture_foods.append(food)
                self.case['carry_attempt']['initial_fixtures'].append(dict(path=food.get_path_name(),position_cm=vec(food.get_actor_location())))
            self.fixture_food=self.fixture_foods[0]
            self.case['carry_attempt']['initial_fixture']=self.fixture_food.get_path_name()
            self.hero.call_method('ServerToggleFoodCollection',args=())
            self.phase='carry_pickup';self.phase_at=now;self.next_sample=now
        except Exception:
            self.case['carry_attempt'].update(executed=False,limitation=traceback.format_exc())
            self.end_case(now,'native_pickup_unavailable')

    def begin_movement(self,now):
        self.case.update(started_game_seconds=now,started_wall_unix=time.time(),carried_at_start=self.carried())
        if self.case.get('carry'):
            self.case['fixture_carried_at_start']=self.owned_fixture_count()
            self.sample(now);self.next_sample=now+.125
        self.phase='moving';self.phase_at=now;self.case_wall=time.monotonic()

    def move(self,age):
        direction=u.Vector(1,0,0)
        if self.case['pattern']=='square':
            direction=(u.Vector(1,0,0),u.Vector(0,1,0),u.Vector(-1,0,0),u.Vector(0,-1,0))[int(age/1.2)%4]
        self.hero.add_movement_input(direction,float(self.case['scale']),False)
        self.case['last_input']=dict(direction=vec(direction),scale=self.case['scale'],elapsed_game_seconds=age)

    def sample(self,now):
        position=self.hero.get_actor_location();velocity=self.hero.get_velocity();body=self.body()
        self.require(all(math.isfinite(v) for v in vec(position)+vec(velocity)),'Nonfinite native pose or velocity')
        status=prop(prop(self.hero,'status'),'state');health=float(prop(status,'health'))
        self.require(health>0,'The probe pawn died; do not replace/reset it to continue a capture')
        if self.previous_position is not None:self.case['distance_cm']+=math.dist(vec(position),vec(self.previous_position))
        self.previous_position=position
        if body!=self.last_body:
            event=dict(case=self.case['name'],game_seconds=now,body_before=self.last_body,body_after=body,hero_cm=vec(position))
            self.report['transitions'].append(event);self.case['observed_state_changes'].append(event)
            if 'SLIPPING' in body.upper():self.case['observed_slip_entries']+=1
            self.last_body=body
        movement=self.hero.get_movement_component()
        actual_surface=str(prop(movement,'ground_surface'))
        if ('SLIPPERY' if self.case['slippery'] else 'NORMAL') in actual_surface.upper():self.case['correct_surface_samples']+=1
        if math.hypot(velocity.x,velocity.y)>20:self.case['moving_samples']+=1
        row=dict(case=self.case['name'],game_seconds=now,wall_unix=time.time(),body=body,hero_cm=vec(position),velocity_cm_s=vec(velocity),
         surface=actual_surface,movement_mode=str(prop(movement,'movement_mode')),health=health,carried_count=self.carried(),
         move_input_ignored=bool(self.pc.is_move_input_ignored()),look_input_ignored=bool(self.pc.is_look_input_ignored()),
         brushing=self.optional(lambda:bool(prop(self.hero,'brushing'))),primary_held=self.optional(lambda:bool(self.hero.call_method('IsPrimaryHeld'))),
         controller=self.pc.get_path_name(),last_input=self.case.get('last_input'))
        if self.case.get('carry'):
            row['fixture_foods']=self.food_snapshot(now)
            row['fixture_food_valid_count']=sum(bool(f.get('valid')) for f in row['fixture_foods'])
            row['fixture_food_owned_count']=sum(bool(f.get('owned_by_probe_player')) for f in row['fixture_foods'])
        self.report['samples'].append(row);self.case['samples']+=1
        offset=position-self.center
        self.require(abs(offset.x)<1750 and abs(offset.y)<750,'Pawn approached the bounded support edge; stopping rather than teleporting during a case')

    def end_case(self,now,result='completed_duration'):
        if result=='completed_duration':
            self.require(self.case['correct_surface_samples']>0,'Requested native footing was never observed; do not credit a dry-ground slip check')
            self.require(self.case['moving_samples']>0,'Actual native velocity never exceeded the20cm/s risk threshold')
        self.case.update(completed=result=='completed_duration',result=result,ended_game_seconds=now,ended_wall_unix=time.time(),
                         final_body=self.body(),carried_at_end=self.carried())
        if self.case.get('carry'):
            final_foods=self.food_snapshot(now)
            self.case.update(final_fixture_foods=final_foods,food_motion=list(self.food_observations.values()),
                fixture_count_preserved=sum(bool(f.get('valid')) and 'DISPOSED' not in str(f.get('phase','')).upper() for f in final_foods)==self.carry_pieces,
                all_fixture_foods_observed_carried=len(self.food_observations)==self.carry_pieces and all(f['observed_carried'] for f in self.food_observations.values()),
                all_fixture_foods_observed_upward_then_descending_then_settled=len(self.food_observations)==self.carry_pieces and
                    all(f['upward_after_release'] and f['descending_after_upward'] and f['settled_after_arc'] for f in self.food_observations.values()))
        # Retain food IDs and final positions in the report before destroying
        # unheld transient fixtures. Destruction is never credited as a landing.
        for food in self.fixture_foods:
            if food is not None and u.SystemLibrary.is_valid(food) and self.carried()==0:food.destroy_actor()
        self.fixture_food=None;self.fixture_foods=[];self.write();self.request_case(now)

    def queue_finish(self,error=None):
        empty=[c for c in self.report['cases'] if not c.get('carry')]
        carry=[c for c in self.report['cases'] if c.get('carry')]
        carry_complete=len(carry)==1 and carry[0].get('completed',False) and carry[0].get('carry_attempt',{}).get('executed',False) and carry[0].get('fixture_carried_at_start',0)>=self.carry_pieces
        carry_motion_complete=carry_complete and carry[0].get('fixture_count_preserved',False) and carry[0].get('all_fixture_foods_observed_carried',False) and carry[0].get('all_fixture_foods_observed_upward_then_descending_then_settled',False)
        required_complete=carry_motion_complete if self.only_carry else len(empty)==4 and all(c.get('completed') for c in empty)
        if self.only_carry and not required_complete:error=error or 'Carry-only evidence incomplete: inspect pickup, preserved count and each observed upward/descending/settled arc; no visual success is inferred'
        self.pending_error=error;self.phase='capture_end_gate';self.end_ready_at=time.monotonic()
        self.report['checks'].update(all_required_empty_hand_cases_completed=None if self.only_carry else required_complete,
         empty_hand_case_scope='not_requested' if self.only_carry else 'four_required_cases',all_required_cases_completed=required_complete,
         requested_fixture_pickup_completed=carry_complete,
         carry_motion_evidence_complete=carry_motion_complete,
         fixture_count_preserved=carry[0].get('fixture_count_preserved') if carry else None,
         observed_upward_then_descending_then_settled=carry[0].get('all_fixture_foods_observed_upward_then_descending_then_settled') if carry else None,
         observed_native_slip_entries=sum(c['observed_slip_entries'] for c in self.report['cases']),
         no_forced_body_or_movement_state=True,actual_ice_event_tested=False)
        (OUT/'CaptureEndReady.json').write_text(json.dumps(dict(status='capture_end_ready',game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
         ready_wall_unix=time.time(),finish_gate=str(self.finish_gate),timeout_seconds=15,error=error),indent=2),encoding='utf-8')
        self.write()

    def tick(self,_delta):
        if self.done:return
        try:
            wall=time.monotonic()-self.started
            if self.phase=='capture_end_gate':
                if self.finish_gate.exists() or time.monotonic()-self.end_ready_at>=15:self.finish(self.pending_error)
                return
            self.require(wall<240,'Bounded240s wall budget exceeded')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'Exactly one owned standalone authority world required')
                self.world=worlds[0];self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                if not self.pc or not self.pc.get_controlled_pawn():self.world=None;return
                self.hero=self.pc.get_controlled_pawn();self.require(isinstance(self.hero,u.MCToothCharacter),'A native tooth pawn is required')
                self.engine.set_editor_property('bUseFixedFrameRate',False);self.console('t.MaxFPS 60')
                self.phase='readiness';self.phase_at=float(u.GameplayStatics.get_time_seconds(self.world));return
            self.require(self.owned_pie_is_active(worlds),'Owned PIE was replaced; preserve any external session')
            self.require(self.pc.get_controlled_pawn()==self.hero,'Pawn was replaced; never silently continue on a replacement')
            now=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now<=self.last_game_time:return
            self.last_game_time=now
            if self.phase=='readiness':
                self.require(wall<40,'Initial loading/readiness timeout')
                loading=[w for w in u.ObjectIterator(u.load_class(None,'/Script/MessControl.MCLoadingWidget')) if w.get_owning_player()==self.pc]
                if any(w.is_in_viewport() and w.get_visibility() not in [u.SlateVisibility.COLLAPSED,u.SlateVisibility.HIDDEN] for w in loading):return
                if now-self.phase_at>=1.5:self.setup(now)
            elif self.phase=='capture_gate':
                self.require(time.monotonic()-self.ready_at<30,'StartCapture.gate was not released within30s')
                if self.start_gate.exists():self.capture_started=True;self.request_case(now)
            elif self.phase=='between_cases':
                self.require(time.monotonic()-self.gap_wall<12,'Pawn did not naturally stand/settle between cases')
                velocity=self.hero.get_velocity()
                # Wait out the real6.5s fall grace between variants rather than
                # resetting either native cooldown or risk-exposure fields.
                if self.standing() and math.hypot(velocity.x,velocity.y)<20 and (self.case_index==0 or now-self.phase_at>=7.):self.begin_case(now)
            elif self.phase=='settle_case' and now-self.phase_at>=1.:
                if self.case.get('carry'):self.prepare_carry(now)
                else:self.begin_movement(now)
            elif self.phase=='carry_pickup':
                owned=self.owned_fixture_count()
                if self.carried()>=self.carry_pieces and owned>=self.carry_pieces:
                    self.case['carry_attempt'].update(executed=True,observed_carried_count=self.carried(),observed_fixture_carried_count=owned);self.begin_movement(now)
                elif now-self.phase_at>=3.:
                    self.case['carry_attempt'].update(executed=False,observed_carried_count=self.carried(),observed_fixture_carried_count=owned,
                        limitation='Requested fixture count not collected by native E; partial pickup is untested and no carried fields were assigned')
                    self.end_case(now,'native_pickup_partial_untested' if owned else 'native_pickup_not_observed')
                elif now>=self.next_sample:self.next_sample=now+.125;self.sample(now)
            elif self.phase=='moving':
                age=now-self.phase_at
                if age>=self.case['seconds']:self.end_case(now)
                else:
                    self.move(age)
                    if now>=self.next_sample:self.next_sample=now+(.125 if self.case.get('carry') else .5);self.sample(now)
            if time.monotonic()>=self.flush_at:self.flush_at=time.monotonic()+1.;self.write()
        except Exception:
            error=traceback.format_exc()
            if self.capture_started and self.owned_pie_is_active():self.queue_finish(error)
            else:self.finish(error)

    def finish(self,error=None):
        if self.done:return
        restoration=[]
        for name,value in self.cvars.items():
            try:
                u.SystemLibrary.execute_console_command(self.editor_world,name+' '+str(value))
                self.require(abs(float(u.SystemLibrary.get_console_variable_float_value(name))-value)<.001,'CVar restore failed: '+name)
            except Exception:restoration.append(traceback.format_exc())
        self.report['cvars_restored']=not restoration
        if restoration:self.report['cvar_restore_errors']=restoration;error=error or 'CVar restoration failed'
        super().finish(error)


probe=SlipperyFeedbackSolo()
setattr(builtins,KEY,probe)
try:probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
