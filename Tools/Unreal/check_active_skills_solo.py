"""One owned native-input active-skill solo case with real-window evidence.

Run only after root approves the common build and this recipe. Set MC_ACTIVE_CASE
to Sponge/Hook/Grenade/Air/Rocket/Driver with runpy init_globals. Optional
MC_ACTIVE_VARIANT='overhold' for Grenade, 'timer' for Sponge, 'overflow' for Air,
or 'steer' for Driver (ordinary movement input changes direction during its ride).
Hook also accepts 'boss' for a disclosed stationary live-boss target fixture,
or 'miss' with no spawned Hook target. MC_ACTIVE_HOOK_FACE_REVEAL optionally
sends an ordinary pawn-camera orbit only AFTER native contact/miss is observed.
Default variants are repeat-stop / release. MC_ACTIVE_LAST_CHARGE defaults True:
consumables use a disclosed TRANSIENT copy of the current table with ONLY this
row's InitialCharges=1. The original production count is still checked/reported.
Set False for an ordinary full-charge smoke check. No saved table is modified.

The recorder owns exact gameplay-window capture. This observer publishes Ready.json
and waits for StartCapture.gate, then observes native outcomes and >=.8 game seconds
of quiet before CaptureEndReady.json / FinishCapture.gate. Never records a cinematic
camera, calls action Finish/CanStart, or writes timed health/charge/food/mask state.
Cancel via builtins.mc_active_skills_solo.finish('cancelled'). No auto build/launch.
"""
import builtins,copy,hashlib,json,math,os,runpy,time,traceback,uuid
from datetime import datetime,timezone
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
KEY='mc_active_skills_solo'
CASES={
 'Sponge':('Active_Sponge','MCSpongeSkillAction',-1),
 'Hook':('Active_Hook','MCHookSkillAction',-1),
 'Grenade':('Consumable_Grenade','MCGrenadeSkillAction',3),
 'Air':('Consumable_Air','MCAirSkillAction',2),
 'Rocket':('Consumable_Rocket','MCRocketSkillAction',3),
 'Driver':('Consumable_Driver','MCDriverSkillAction',3),
}
CASE=str(globals().get('MC_ACTIVE_CASE','Sponge'))
VARIANT=str(globals().get('MC_ACTIVE_VARIANT','repeat' if CASE=='Sponge' else 'release'))
LAST_CHARGE=bool(globals().get('MC_ACTIVE_LAST_CHARGE',True))
CAMERA_PITCH=float(globals().get('MC_ACTIVE_CAMERA_PITCH',-10))
CAMERA_YAW=float(globals().get('MC_ACTIVE_CAMERA_YAW',0))
CONTACT_PROBE=bool(globals().get('MC_ACTIVE_CONTACT_PROBE',False))
POSE_AUDIT=bool(globals().get('MC_ACTIVE_POSE_AUDIT',False))
POSE_SNAPSHOTS=bool(globals().get('MC_ACTIVE_POSE_SNAPSHOTS',POSE_AUDIT))
HOOK_FACE_REVEAL=bool(globals().get('MC_ACTIVE_HOOK_FACE_REVEAL',False))
HOOK_FACE_YAW=float(globals().get('MC_ACTIVE_HOOK_FACE_YAW',180))
HOOK_FACE_ZOOM=float(globals().get('MC_ACTIVE_HOOK_FACE_ZOOM',0))
assert 90<=HOOK_FACE_YAW<=210 and 0<=HOOK_FACE_ZOOM<=4,'Bounded ordinary face-view camera input required'
LOADING_BUDGET=float(globals().get('MC_ACTIVE_LOADING_SECONDS',40))
assert 40<=LOADING_BUDGET<=90,'Bounded cold-editor loading allowance required'
assert -50<=CAMERA_PITCH<=-5 and math.isfinite(CAMERA_YAW),'Use an ordinary gameplay orbit'
OUT=Path(globals().get('MC_ACTIVE_OUTPUT',Path('E:/CodexWork/MessControl/ActiveSkills_20261029/Runs')/
    (CASE+'_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8]))).resolve()
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol' and OUT.drive.upper()=='E:'
assert CASE in CASES and not OUT.exists(),'Select one case and a fresh E: run directory'
assert VARIANT in ({'repeat','timer'} if CASE=='Sponge' else {'release','boss','miss'} if CASE=='Hook' else {'release','overhold'} if CASE=='Grenade' else {'release','overflow'} if CASE=='Air' else {'release','steer'} if CASE=='Driver' else {'release'})
assert not HOOK_FACE_REVEAL or CASE=='Hook','Face reveal is an optional Hook-only ordinary camera input'
old=getattr(builtins,KEY,None);assert old is None or old.done,'Another owned active probe is running'
assert 'mchubtransientprofile' in u.SystemLibrary.get_command_line().lower(), 'Use the approved owned transient-profile Editor; do not touch real profiles'
assert 'mcactiveskillreview' in u.SystemLibrary.get_command_line().lower(), 'Use the root-owned diagnostic Editor with -MCActiveSkillReview'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor() and not u.EditorLevelLibrary.get_pie_worlds(True),'Preserve manual PIE'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Preserve unsaved work'
files=['Source/MessControl/Private/MCActiveSkillAction.cpp','Source/MessControl/Private/MCActiveSkillPerkEffect.cpp',
 'Source/MessControl/Private/MCPerkLoadout.cpp','Source/MessControl/Private/MCPerkComponent.cpp','Source/MessControl/Private/MCPlayerController.cpp',
 'Source/MessControl/Private/MCActiveSkillMotion.cpp','Source/MessControl/Private/MCActiveSkillImpact.cpp',
 'Source/MessControl/Private/MCFoodActor.cpp','Source/MessControl/Private/MCFoodActiveContainer.cpp','Source/MessControl/Private/MCFoodEnvironment.cpp',
 'Source/MessControl/Private/MCToothActiveSkills.cpp','Source/MessControl/Private/'+CASES[CASE][1]+'.cpp',
 'Source/MessControl/Public/MCActiveSkillAction.h','Source/MessControl/Public/MCPerkComponent.h']
if POSE_AUDIT:files+=['Source/MessControl/Private/MCToothAnimInstance.cpp','Source/MessControl/Private/MCGripComponent.cpp',
    'Source/MessControl/Public/MCToothCharacter.h']
if CASE=='Hook':files+=['Source/MessControl/Public/MCHookSkillAction.h','Source/MessControl/Public/MCExpressionComponent.h',
    'Source/MessControl/Private/MCExpressionComponent.cpp','Source/MessControl/Private/MCToothAnimInstance.cpp',
    'Source/MessControl/Private/MCGripComponent.cpp']
if CASE=='Driver':files+=['Source/MessControl/Public/MCDriverSkillAction.h','Source/MessControl/Public/MCSuperCarVisualComponent.h',
    'Source/MessControl/Private/MCSuperCarVisualComponent.cpp']
dll=ROOT/'Binaries/Win64/UnrealEditor-MessControl.dll'
assert dll.exists() and all(dll.stat().st_mtime_ns>=(ROOT/f).stat().st_mtime_ns for f in files),'Approved current DLL required before PIE'
OUT.mkdir(parents=True,exist_ok=False)
RECIPE_BYTES=Path(__file__).read_bytes();RECIPE_SHA256=hashlib.sha256(RECIPE_BYTES).hexdigest()
RECIPE_SNAPSHOT=OUT/'Recipe.py';RECIPE_SNAPSHOT.write_bytes(RECIPE_BYTES)
assert hashlib.sha256(RECIPE_SNAPSHOT.read_bytes()).hexdigest()==RECIPE_SHA256,'Retained recipe snapshot must exactly match freshness SHA'
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),run_name='active_skills_capture_support',
    init_globals={'MC_ENV_OUTPUT':OUT,'MC_ENV_CAPTURE_HZ':0})
Base,prop,vec=support['ApprovedEnvironmentSolo'],support['prop'],support['vec']
support['PLAY']['clientFixedFPS']=[0]

def valid(a):
    if a is None:return False
    try:return u.SystemLibrary.is_valid(a)
    except TypeError as error:
        # A destroyed native actor's Python wrapper may survive its UObject.
        # Treat only that documented conversion failure as an invalid object;
        # other type errors still fail the observer instead of hiding defects.
        if 'ObjectInstance is null!' in str(error):return False
        raise
def path(a):return a.get_path_name() if valid(a) else None
def distance(a,b):return math.dist(vec(a),vec(b))
def enum_token(value):
    # UE Python formats enum items as <Type.ITEM: integer>, not ITEM alone.
    return str(value).rsplit('.',1)[-1].split(':',1)[0].strip('<> ').upper()
def transform_row(value):
    return dict(cm=vec(value.translation),quaternion_xyzw=[float(x) for x in value.rotation.to_tuple()],scale=vec(value.scale3d))

HOOK_MOUTHS=('Mouth_Smile','Mouth_Frown','Mouth_Surprise','Mouth_Pain','Mouth_Effort')
CAR_PARTS=('Chassis','WheelFrontLeft','WheelFrontRight','WheelRearLeft','WheelRearRight',
    'BrushFrontRight','BrushFrontLeft','BrushRearRight','BrushRearLeft')
def quaternion_angle(a,b):
    dot=abs(sum(x*y for x,y in zip(a,b)));return math.degrees(2*math.acos(max(-1,min(1,dot))))
def guid_text(value):
    # Guid.__str__ is a reflected struct wrapper repr containing its address.
    # UE's verified GuidLibrary conversion reads the actual 128-bit value.
    text=str(u.GuidLibrary.conv_guid_to_string(value)).replace('-','').replace('{','').replace('}','').upper()
    if len(text)!=32 or any(c not in '0123456789ABCDEF' for c in text):raise RuntimeError('Native GUID conversion returned an invalid identity: '+text)
    return text

def normalize_parameters(values):
    if not isinstance(values,dict):raise TypeError('Authored Parameters export must be an object')
    normalized={}
    for key,value in values.items():
        if isinstance(value,dict):
            if set(value)!={'Parameters'}:raise TypeError('Unsupported UE parameter wrapper for '+key)
            value=value['Parameters']
        if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(float(value)):
            raise TypeError('Parameter must be a finite numeric value: '+key)
        normalized[key]=float(value)
    return normalized

class ActiveSkillsSolo(Base):
    def __init__(self):
        super().__init__()
        self.start_gate=OUT/'StartCapture.gate';self.finish_gate=OUT/'FinishCapture.gate'
        self.capture_started=False;self.pending_error=None;self.quiet_since=None
        self.fixtures=[];self.foods=[];self.enemies=[];self.ally=None;self.patch=None
        self.boss=None;self.tooth=None;self.whole_food=None;self.fx_seen=set();self.repeat_action_id=None
        self.repeat_before=None;self.repeat_ended_at=None;self.catalog_swapped=False
        self.perks=self.original_table=self.transient_table=None
        self.held=False;self.released=False;self.repeated=False;self.cooldown_attempted=False
        self.actions={};self.initial=None;self.last_seen_action=None;self.max_stored=0
        self.contained=set();self.old_max_fps=float(u.SystemLibrary.get_console_variable_float_value('t.MaxFPS'))
        self.next_sample=0;self.flush_at=0;self.ready_wall=0;self.case_started=0
        self.orbit_sent=False;self.orbit_stable_since=None;self.orbit_next_sample=0
        self.hook_reveal_sent=False;self.hook_observation_next=0
        self.driver_input_leg=None
        self.pose_cameras=[];self.pose_captured=set()
        self.report=dict(status='prepared_running',passed=False,case=CASE,variant=VARIANT,checks={},samples=[],inputs=[],actions=[],
            limitations=['Controlled solo fixtures; native input/outcomes are observed. This does not certify networking or a full earned run.',
             'One standalone player plus optional AI-controlled tooth target: not a network test.',
             'Native BeginSkillInput/EndSkillInput handlers emulate slot events; no physical keyboard test.',
             'Native director-stop setup, initial placement, temporary target/food/stain fixtures and optional one-charge transient row are diagnostic.',
             'Native player camera receives an initial ordinary orbit-input adjustment; an explicitly enabled Hook face clip may orbit after observed outcome. No CameraActor/cinematic view target.',
             'Timed observers never write health, mask, charges, action phase, readiness, food ownership or velocity.',
             'No microphone APIs, package saves, persistent profile writes, broad test suite or build.'],
            freshness=dict(recipe_sha256=RECIPE_SHA256,recipe_snapshot=str(RECIPE_SNAPSHOT),dll=str(dll),
                sources=[dict(path=f,sha256=hashlib.sha256((ROOT/f).read_bytes()).hexdigest()) for f in files]),
            policy=dict(max_wall_seconds=180,max_work_wall_seconds=120,max_failure_drain_wall_seconds=40,max_case_game_seconds=35,sample_hz=10,quiet_game_seconds=.8,
                capture='External WGC recorder must target this process owned floating PIE gameplay SWindow, not desktop/Editor',
                start_gate=str(self.start_gate),finish_gate=str(self.finish_gate)),fixtures=[])
        if CASE=='Hook':
            self.report['policy'].update(sample_hz='each advancing game tick for Hook; actual deltas retained',
                hook_face_reveal=HOOK_FACE_REVEAL,hook_tail='All native actions/FX ended, actual source/ally Hook mouth morphs back to pre-input baseline, then >=.8 game seconds quiet')
            self.report['observation_disclosure']=dict(read_only=['Native cast phase/contact identity and phase timestamps',
                'Evaluated skeletal root/body/wrist transforms, actual mouth morph values and replicated face lease',
                'Actual Niagara tether override endpoints and visible prop transform',
                'Position/velocity, slot identity/cooldown and actual camera component/manager'],
                fixture_writes=['Before Ready: director stop, source placement and canonical skill grant',
                    'Before Ready: one AI tooth or stationary live boss on hit variants; no Hook target on miss',
                    'During capture: native slot input; optional disclosed ordinary camera orbit after native outcome',
                    'After FinishCapture.gate: owned fixture/grant cleanup'],
                outcome_writes=False,exact_phase_timer_reconstruction=False,
                visual_limit='Read-only evaluated pose/morph/override observations do not replace review of the real gameplay-window video')

    def optional(self,fn):
        try:return fn()
        except Exception:return None
    def write(self):
        pending=OUT/'Report.next.json';pending.write_text(json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
        # Windows readers can briefly omit FILE_SHARE_DELETE. A single failed
        # rename must not turn completed gameplay checks into a false failure.
        # Keep the atomic report; retry only this diagnostic I/O for <=28ms.
        for attempt in range(8):
            try:
                pending.replace(OUT/'Report.json');return
            except PermissionError:
                if attempt==7:raise
                time.sleep(.004)
    def spawn(self,cls,at,scale=None,prepare=None):
        pose=u.Transform(location=at,scale=scale or u.Vector(1,1,1))
        a=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,cls.static_class(),pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(a is not None,'Could not spawn diagnostic fixture '+str(cls))
        if prepare:prepare(a)
        self.statics.call_method('FinishSpawningActor',args=(a,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.fixtures.append(a);self.report['fixtures'].append(dict(path=path(a),class_name=cls.__name__,initial_cm=vec(at)))
        return a
    def hp(self,a):
        return float(prop(prop(prop(a,'status'),'state'),'health')) if isinstance(a,(u.MCToothCharacter,u.MCArenaTooth)) else float(prop(a,'health'))
    def slots(self):
        return [dict(id=str(prop(s,'perk_id')),instance=guid_text(prop(s,'instance_id')),charges=int(prop(s,'charges')),
            cooldown=float(self.perks.get_skill_cooldown_remaining(i))) for i,s in enumerate(prop(self.perks,'active_skill_slots'))]
    def check(self,name,ok):self.report['checks'][name]=self.report['checks'].get(name,True) and bool(ok)
    def param(self,name,fallback):
        parameters=self.report['resolved_parameters']
        if name not in parameters:self.report.setdefault('native_parameter_defaults_used',{})[name]=fallback
        value=float(parameters.get(name,fallback))
        self.require(math.isfinite(value),'Non-finite authored parameter '+name)
        return value

    def gate_facts(self):
        errors={}
        def read(name,fn):
            try:return fn()
            except Exception as error:errors[name]=str(error);return None
        ps=self.pc.player_state;game=u.GameplayStatics.get_game_state(self.world)
        can_work=getattr(self.hero,'can_work',None)
        result=dict(local_controller=read('local_controller',lambda:self.pc.is_local_controller()),
            show_mouse_cursor=read('show_mouse_cursor',lambda:bool(prop(self.pc,'show_mouse_cursor'))),
            move_input_ignored=read('move_input_ignored',lambda:self.pc.is_move_input_ignored()),
            look_input_ignored=read('look_input_ignored',lambda:self.pc.is_look_input_ignored()),
            world_presentation_ready=read('world_presentation_ready',lambda:self.pc.is_world_presentation_ready()),
            reward_menu_open=read('reward_menu_open',lambda:self.pc.is_reward_menu_open()),
            boss_intro_playing=read('boss_intro_playing',lambda:self.pc.is_boss_intro_playing()),
            wardrobe_open=read('wardrobe_open',lambda:self.pc.is_wardrobe_open()),
            hub_shop_open=read('hub_shop_open',lambda:self.pc.is_hub_shop_open()),
            paused=read('paused',lambda:u.GameplayStatics.is_game_paused(self.world)),
            game_phase=read('game_phase',lambda:str(prop(game,'phase'))),
            lobby_waiting=read('lobby_waiting',lambda:bool(prop(game,'lobby_waiting'))),
            tutorial_active=read('tutorial_active',lambda:bool(prop(game,'tutorial_active'))),
            state_owner_matches_controller=read('state_owner',lambda:ps.get_owner()==self.pc),
            state_pawn_matches_source=read('state_pawn',lambda:ps.get_pawn()==self.hero),
            source_pawn_matches_controller=self.pc.get_controlled_pawn()==self.hero,
            hero_in_coffee=read('hero_in_coffee',lambda:bool(prop(self.hero,'in_coffee'))),
            hero_reward_interaction=read('hero_reward_interaction',lambda:path(prop(self.hero,'reward_interaction'))),
            hero_can_work=read('hero_can_work',can_work) if callable(can_work) else None,
            hero_can_work_reflected=callable(can_work),
            unreflected_not_inferred=['private loading input blocked/pause flags','native CanUseSkillActor aggregate','active motion ownership','CanWork()' if not callable(can_work) else ''])
        result['unavailable_reads']=errors
        return result
    def press(self,now,label='Started'):
        before=self.gate_facts();slots_before=self.slots()
        self.pc.call_method('BeginSkillInput',args=(0,));self.held=True
        self.report['inputs'].append(dict(event=label,game_seconds=now,slot=0,original_instance=self.initial['slots'][0]['instance'],
            gates_before=before,gates_after=self.gate_facts(),slots_before=slots_before,slots_after=self.slots(),
            live_actions_after=[dict(path=path(a),action_id=guid_text(a.get_action_id()),press_sequence=int(a.get_press_sequence_for_review())) for a in self.current_actions()]))
    def release(self,now,canceled=False):
        self.pc.call_method('EndSkillInput',args=(0,bool(canceled)));self.held=False;self.released=True
        self.report['inputs'].append(dict(event='Canceled' if canceled else 'Completed',game_seconds=now,slot=0))
    def food_rows(self):
        rows=[]
        for f in self.foods:
            row=dict(path=self.food_ids.get(id(f)),valid=valid(f))
            if valid(f):
                phase=str(prop(f,'phase'));body=prop(f,'body')
                row.update(phase=phase,location_cm=vec(f.get_actor_location()),velocity_cm_s=vec(body.get_physics_linear_velocity()),
                    container=path(prop(f,'active_container')),spoiled=bool(prop(f,'spoiled')),spoil_at=float(prop(f,'spoil_at')),
                    item_name=str(prop(f,'item_name')),batch=int(prop(f,'batch')),fragment=bool(prop(f,'fragment')),body_sim=body.is_simulating_physics())
                if 'CONTAINED' in phase.upper():self.contained.add(row['path'])
            rows.append(row)
        return rows
    def current_actions(self,draining=False):
        return [a for a in self.actors(getattr(u,CASES[CASE][1])) if valid(a) and (draining or a.hero()==self.hero) and a.is_committed() and not a.is_finished()]
    def active_fx(self):
        return [c for c in u.ObjectIterator(u.NiagaraComponent) if valid(c) and self.optional(lambda:c.get_world())==self.world
            and c.get_asset() and c.get_asset().get_path_name().startswith('/Game/Gameplay/ActiveSkills/VFX/') and c.is_active()]

    def hit(self,a):
        if not valid(a):return None
        last=prop(prop(a,'feedback'),'last_hit')
        return dict(source=path(prop(last,'source')),health_damage=float(prop(last,'health_damage')),serial=int(prop(last,'serial')))

    def driver_visual(self,action):
        visual=prop(action,'super_car_visual');parts={}
        for name,part in prop(visual,'mesh_parts').items():
            if not valid(part):continue
            local=part.get_relative_transform();rotation=local.rotation.rotator()
            parts[str(name)]=dict(component=path(part),mesh=path(prop(part,'static_mesh')),parent=path(part.get_attach_parent()),
                visible=bool(part.is_visible()),local=transform_row(local),world=transform_row(part.get_world_transform()),
                local_axis_x=vec(local.transform_direction(u.Vector(1,0,0))),
                local_axis_y=vec(local.transform_direction(u.Vector(0,1,0))),
                local_axis_z=vec(local.transform_direction(u.Vector(0,0,1))),
                local_euler_degrees=dict(pitch=float(rotation.pitch),yaw=float(rotation.yaw),roll=float(rotation.roll)))
        return dict(component=path(visual),current_steering_degrees=float(prop(visual,'current_steering_angle')),
            brush_rpm=float(prop(visual,'brush_rpm')),frame=transform_row(visual.get_world_transform()),
            authored_part_pivots={str(k):vec(v.translation) for k,v in prop(visual,'part_transforms').items()},parts=parts,
            observed_actor=transform_row(action.get_actor_transform()))

    def driver_steering_input(self,now):
        age=now-self.case_started;leg=0 if age<1 else 1 if age<2 else 2
        angle=0 if leg==0 else 35 if leg==1 else -35
        right=u.Vector(-self.forward.y,self.forward.x,0);radians=math.radians(angle)
        direction=self.forward*math.cos(radians)+right*math.sin(radians)
        self.hero.add_movement_input(direction,1,False)
        if leg!=self.driver_input_leg:
            self.driver_input_leg=leg
            self.report['inputs'].append(dict(event='Driver ordinary movement direction',game_seconds=now,age=age,
                leg=leg,requested_offset_degrees=angle,direction=vec(direction),method='hero.add_movement_input; no velocity, heading or visual writes'))

    def check_driver_visual(self,samples):
        rows=[a['super_car_visual'] for sample in samples for a in sample['actions'] if 'super_car_visual' in a]
        self.check('supercar_nine_real_mesh_parts',bool(rows) and all(set(row['parts'])==set(CAR_PARTS)
            and all(p['mesh'] and p['visible'] and p['parent']==row['component'] for p in row['parts'].values()) for row in rows))
        if not rows:return
        first=rows[0];pivot_errors={};rotation_changes={};axle_changes={}
        for name in CAR_PARTS:
            present=[row['parts'][name] for row in rows if name in row['parts']]
            if len(present)!=len(rows):continue
            pivot_errors[name]=max(math.dist(part['local']['cm'],first['authored_part_pivots'][name]) for part in present)
            rotation_changes[name]=max(quaternion_angle(present[0]['local']['quaternion_xyzw'],part['local']['quaternion_xyzw']) for part in present)
            axle_changes[name]=max(math.dist(present[0]['local_axis_y'],part['local_axis_y']) for part in present)
        self.check('supercar_all_part_pivots_stay_fixed',len(pivot_errors)==9 and max(pivot_errors.values(),default=999)<.05)
        self.check('supercar_four_wheels_roll',all(rotation_changes.get(name,0)>15 for name in CAR_PARTS if name.startswith('Wheel')))
        self.check('supercar_four_independent_brushes_spin',all(rotation_changes.get(name,0)>15 for name in CAR_PARTS if name.startswith('Brush'))
            and len({p['component'] for name,p in first['parts'].items() if name.startswith('Brush')})==4)
        steering=[row['current_steering_degrees'] for row in rows]
        if VARIANT=='steer':
            self.check('supercar_native_steering_both_directions',min(steering,default=0)<-2 and max(steering,default=0)>2)
            self.check('supercar_front_axles_steer',all(axle_changes.get(name,0)>.04 for name in ('WheelFrontLeft','WheelFrontRight')))
            self.check('supercar_rear_axles_remain_unsteered',all(axle_changes.get(name,999)<.02 for name in ('WheelRearLeft','WheelRearRight')))
            self.check('driver_three_ordinary_input_legs',self.driver_input_leg==2)
            # Actual actor heading must change as well as the decorative wheels.
            headings=[r['observed_actor']['quaternion_xyzw'] for r in rows]
            self.check('driver_actor_turned_during_travel',max((quaternion_angle(headings[0],q) for q in headings),default=0)>10)
        self.report['driver_visual_observation']=dict(sample_interval_seconds=.1,part_pivot_error_max_cm=pivot_errors,
            part_local_rotation_change_max_degrees=rotation_changes,part_axle_y_vector_change_max=axle_changes,
            native_steering_min_degrees=min(steering),native_steering_max_degrees=max(steering),
            read_only='Only reflected public component state, actual mesh relative/world transforms and actor travel are read; native TravelDistance/BrushDegrees are not read',
            fixture='Separate steer variant sends ordinary movement input straight until 1s, +35 degrees until 2s, then -35 degrees; no outcome or mesh pose forcing')

    def hook_pose(self,pawn):
        # Read the evaluated skeletal output, independently of the action's
        # authored hand frame. Never substitute the intent for a missing bone.
        mesh=prop(pawn,'mesh');appearance=prop(pawn,'appearance');mapping={}
        if valid(appearance) and prop(appearance,'skeletal_mesh')==mesh.get_skeletal_mesh_asset():
            mapping={str(k):str(v) for k,v in prop(appearance,'bone_map').items()}
        actor=pawn.get_actor_transform();bones={}
        names={'root':str(mesh.get_bone_name(0))}
        names.update({role:mapping.get(role,role) for role in ('body','hand_r','hand_l','forearm_r','forearm_l','foot_r','foot_l','knee_r','knee_l')})
        for role,bone in names.items():
            if mesh.get_bone_index(bone)<0:continue
            value=mesh.get_socket_transform(bone,u.RelativeTransformSpace.RTS_WORLD)
            bones[role]=dict(bone=bone,parent=str(mesh.get_parent_bone(bone)),world=transform_row(value),
                actor_local_cm=vec(actor.inverse_transform_location(value.translation)))
        return dict(mesh=path(mesh),skeletal_mesh=path(mesh.get_skeletal_mesh_asset()),visible=bool(mesh.is_visible()),
            actor=transform_row(actor),mesh_frame=transform_row(mesh.get_world_transform()),bones=bones)

    def pose_observation(self,now):
        """Observe actual evaluated output; never set a character/prop pose."""
        pose=self.hook_pose(self.hero);targets={}
        for left,role in ((True,'hand_l'),(False,'hand_r')):
            result=self.hero.active_skill_hand_target(left)
            values=result if isinstance(result,tuple) else (result,)
            frame=next((x for x in values if isinstance(x,u.Transform)),None)
            enabled=next((x for x in values if isinstance(x,bool)),frame is not None)
            row=dict(available=enabled,target=transform_row(frame) if frame else None)
            bone=pose['bones'].get(role)
            if enabled and frame and bone:
                actual=prop(self.hero,'mesh').get_socket_transform(bone['bone'],u.RelativeTransformSpace.RTS_WORLD)
                row['evaluated_position_error_cm']=float(u.MathLibrary.vector_distance(actual.translation,frame.translation))
                q,p=actual.rotation,frame.rotation
                dot=abs(q.x*p.x+q.y*p.y+q.z*p.z+q.w*p.w)
                row['evaluated_rotation_error_degrees']=math.degrees(2*math.acos(min(1.,max(0.,dot))))
            targets[role]=row
        return dict(game_seconds=now,pose=pose,targets=targets)

    def capture_pose_views(self,now):
        age=now-self.case_started
        times={'Rocket':(.06,.24,.48),'Grenade':(.6,1.8,2.12),'Air':(.3,.9,1.7),
            'Sponge':(.3,.9,1.5),'Driver':(.4,1.5,2.7),'Hook':(.08,.28,.65)}[CASE]
        pending=next((i for i,t in enumerate(times) if i not in self.pose_captured and age>=t),None)
        if pending is None:return
        if not self.pose_cameras:
            for view in (('side','front_three_quarter','cockpit') if CASE=='Driver' else ('side','front_three_quarter')):
                actor=self.spawn(u.SceneCapture2D,self.hero.get_actor_location())
                component=actor.get_component_by_class(u.SceneCaptureComponent2D)
                target=u.RenderingLibrary.create_render_target2d(self.world,896,896,u.TextureRenderTargetFormat.RTF_RGBA8)
                component.set_editor_properties(dict(capture_every_frame=False,capture_on_movement=False,
                    capture_source=u.SceneCaptureSource.SCS_BASE_COLOR,texture_target=target,fov_angle=50.))
                self.pose_cameras.append((view,actor,component,target))
            self.report['pose_audit_camera_policy']='Transient unlit base-color observer SceneCapture2D views for contact inspection; original player camera/aim and actual gameplay lighting in the main movie unchanged. No pose forcing. Diagnostic render/readback is not a performance benchmark.'
        folder=OUT/'PoseViews';folder.mkdir(exist_ok=True)
        actor=self.hero.get_actor_transform();origin=actor.translation
        scale=1.8 if CASE=='Driver' else 1.
        for view,camera,component,target in self.pose_cameras:
            offset=u.Vector(10,330*scale,70*scale) if view=='side' else u.Vector(260*scale,180*scale,70*scale)
            look_local=u.Vector(40,0,10)
            if view=='cockpit':offset=u.Vector(-180,150,240);look_local=u.Vector(5,0,75)
            eye=actor.transform_location(offset);look=actor.transform_location(look_local)
            camera.set_actor_location_and_rotation(eye,u.MathLibrary.find_look_at_rotation(eye,look),False,True)
            component.capture_scene();name='Pose_%02d_%s.png'%(pending,view)
            u.RenderingLibrary.export_render_target(self.world,target,str(folder),name)
            self.report.setdefault('pose_audit_frames',[]).append(dict(file=str(folder/name),view=view,age=age,game_seconds=now,
                player_camera_unchanged=True,observation=self.pose_observation(now)))
        self.pose_captured.add(pending)

    def hook_face(self,pawn):
        expression=prop(pawn,'expression');state=prop(expression,'hook_face_state');mesh=prop(pawn,'mesh')
        alpha=getattr(expression,'hook_face_alpha',None)
        return dict(pawn=path(pawn),emotion=enum_token(prop(expression,'current_emotion')),
            action_id=guid_text(prop(state,'action_id')),reaction=enum_token(prop(state,'reaction')),
            started_at=float(prop(state,'started_at')),stopped_at=float(prop(state,'stopped_at')),
            native_alpha=float(alpha()) if callable(alpha) else None,
            mouth={name:float(mesh.get_morph_target(name)) for name in HOOK_MOUTHS})

    def hook_faces(self):
        result={'source':self.hook_face(self.hero)}
        if valid(self.ally):result['ally']=self.hook_face(self.ally)
        return result

    def hook_faces_at_baseline(self,faces):
        baseline=self.initial.get('hook_faces',{}) if self.initial else {}
        if not baseline or set(faces)!=set(baseline):return False
        # Observe actual output decay rather than copying the implementation's
        # attack/hold/release timer. Facial leases may outlive their action actor.
        return all((row['native_alpha'] is None or row['native_alpha']<=.001)
            and all(row['mouth'][name]<=baseline[key]['mouth'][name]+.03
                for name in ('Mouth_Smile','Mouth_Frown','Mouth_Surprise')) for key,row in faces.items())

    def hook_cable_position(self,component,name):
        result=component.get_variable_position(name)
        # Native FVector return plus bool& validity is packed as a tuple in UE.
        self.require(isinstance(result,tuple) and len(result)==2 and isinstance(result[0],u.Vector),
            'Unexpected Niagara GetVariablePosition reflected output for '+name)
        return dict(valid=bool(result[1]),cm=vec(result[0]))

    def hook_action_observation(self,a):
        phase=enum_token(a.get_cast_phase());start=a.get_tether_start();end=a.get_tether_end()
        hand=a.get_caster_hand_frame(False);loop=prop(a,'loop_component');prop_mesh=prop(a,'prop_mesh')
        result=dict(cast_phase=phase,phase_started_at=float(a.get_phase_started_at()),phase_alpha=float(a.get_phase_alpha()),
            contacted=bool(a.did_contact_target()),pull_stop_reason=str(a.get_pull_stop_reason()),target=path(prop(a,'hook_target')),pulled_pawn=path(prop(a,'pulled_pawn')),
            cast_facing=dict(pitch=float(a.get_cast_facing().pitch),yaw=float(a.get_cast_facing().yaw),roll=float(a.get_cast_facing().roll)),
            tether_start_cm=vec(start),tether_end_cm=vec(end),tether_attachment_cm=vec(a.get_tether_attachment_location()),
            hook_shaft_world=vec(a.get_hook_shaft_world_direction()),hook_curl_world=vec(a.get_hook_curl_world_direction()),tether_length_cm=distance(start,end),
            hook_head_cm=vec(a.get_hook_head_location()),caster_right_hand=transform_row(hand),
            caster_left_hand=transform_row(a.get_caster_hand_frame(True)),body_offset=transform_row(a.get_cast_body_offset()),
            prop_frame=transform_row(prop_mesh.get_world_transform()),prop_visible=bool(prop_mesh.is_visible()),
            loop_component=path(loop),loop_active=bool(loop.is_active()) if valid(loop) else False)
        if valid(loop):
            result['cable_start']=self.hook_cable_position(loop,'User.TetherStart')
            result['cable_end']=self.hook_cable_position(loop,'User.TetherEnd')
        return result

    def hook_ray_candidates(self,start,end):
        # Read-only actual shape intersections before input. The miss has no
        # spawned target; native flight remains free to hit ordinary world geometry.
        rows=[]
        for cls,shape_names in ((u.MCToothCharacter,('capsule_component',)),
                (u.MCNutBoss,('body',)),(u.MCBossCharacter,('body_hitbox',))):
            for actor in self.actors(cls):
                if actor==self.hero or not valid(actor):continue
                for name in shape_names:
                    shape=prop(actor,name)
                    if valid(shape):rows.append(dict(actor=path(actor),shape=path(shape),intersects=self.ray_hits(shape,start,end)))
        return rows

    def hook_reveal_face(self,now,active):
        if not HOOK_FACE_REVEAL or self.hook_reveal_sent:return
        # A result must already exist in native gameplay; orbit can never decide
        # a target or change the accepted cast aim. Main launchers keep this off.
        outcome=next((a for a in active if a.did_contact_target()
            or enum_token(a.get_cast_phase()) in ('RETURNING','RECOVERY')),None)
        if not outcome:return
        camera=prop(self.hero,'camera');rot=camera.get_world_rotation();gates=self.gate_facts()
        self.require(gates['look_input_ignored'] is False and gates['show_mouse_cursor'] is False,
            'Optional ordinary face orbit is blocked by the native input gate')
        facing=outcome.get_cast_facing();target_yaw=float(facing.yaw)+HOOK_FACE_YAW
        yaw_error=(target_yaw-rot.yaw+180)%360-180;pitch_error=-10-float(rot.pitch)
        sensitivity=max(.02,min(1.,float(prop(self.hero,'camera_orbit_sensitivity'))))
        delta=u.Vector2D(yaw_error/sensitivity,pitch_error/sensitivity)
        self.hero.call_method('ApplyCameraOrbitInput',args=(delta,));self.hook_reveal_sent=True
        if HOOK_FACE_ZOOM:self.hero.zoom_camera(HOOK_FACE_ZOOM)
        self.report['camera']['post_outcome_orbit']=dict(game_seconds=now,action_id=guid_text(outcome.get_action_id()),
            contacted=bool(outcome.did_contact_target()),phase=enum_token(outcome.get_cast_phase()),
            before=dict(pitch=float(rot.pitch),yaw=float(rot.yaw)),target=dict(pitch=-10,yaw=target_yaw),
            delta=[float(delta.x),float(delta.y)],zoom_scroll_delta=HOOK_FACE_ZOOM,native_gates=gates,
            note='Optional separate clip: ordinary player orbit only after observed outcome; no CameraActor or view-target change')

    def spawn_food(self,at,fragment,index):
        # Initial diagnostic food data only. Native BeginPlay owns its absolute
        # freshness deadline and native ReceiveActiveSkillImpact owns splitting.
        mesh=u.load_asset('/Engine/BasicShapes/Cube.Cube')
        self.require(mesh is not None,'Engine Cube diagnostic food mesh missing')
        row=u.MCFoodRow()
        row.set_editor_properties(dict(whole_meshes=[mesh],fragment_meshes=[mesh],scale=u.Vector(.6,.6,.6),
            fragment_scale=u.Vector(.28,.28,.28),kind=u.MCFoodKind.FOOD,health=75.,mass=9.,fragments=3,spoil_seconds=300.))
        if fragment:row.set_editor_properties(dict(mass=3.,health=25.))
        name='ActiveSkillApprovalFood_'+str(index)
        def prepare(a):
            a.set_editor_properties(dict(food_data=row,item_name=name,item_mesh=mesh,fragment=bool(fragment),batch=890100+index))
        food=self.spawn(u.MCFoodActor,at,prepare=prepare)
        self.report.setdefault('food_setup',[]).append(dict(path=path(food),name=name,fragment=fragment,batch=890100+index,
            mesh=path(mesh),geometry='Engine Cube diagnostic ingredient; native food actor and fragmentation',initial_health=self.hp(food)))
        return food

    def ray_hits(self,component,origin,end):
        result=component.line_trace_component(origin,end,False,False,False)
        query=dict(component=path(component),origin_cm=vec(origin),end_cm=vec(end),blocking_hit=False)
        self.report.setdefault('shape_trace_queries',[]).append(query)
        # UE 5.8 PyGenUtil::PackReturnValues omits a successful bool return
        # and packs K2_LineTraceComponent's four outs; false becomes None.
        if result is None:return False
        self.require(isinstance(result,tuple) and len(result)==4 and isinstance(result[3],u.HitResult),
            'Unexpected UE 5.8 LineTraceComponent output: '+type(result).__name__)
        # FHitResult's native fields are not editor-readable properties.
        # GameplayStatics::BreakHitResult is the reflected NativeBreakFunc;
        # its void return packs all 18 outs in declaration order.
        # NativeBreakFunc is exposed by Python on the struct, not GameplayStatics.
        # Live UE5.8 reflection/default-struct probe confirms all18 outputs.
        hit=result[3].to_tuple()
        self.require(isinstance(hit,tuple) and len(hit)==18 and isinstance(hit[0],bool),
            'Unexpected UE 5.8 BreakHitResult output: '+type(hit).__name__)
        query.update(blocking_hit=hit[0],initial_overlap=bool(hit[1]),distance_cm=float(hit[3]),
            impact_cm=vec(hit[5]),hit_actor=path(hit[9]),hit_component=path(hit[10]))
        # A direct geometry trace can hit a capsule that ignores Visibility.
        # Tuple (rather than None) is native success; require exact shape identity.
        # Production Hook uses LineTraceComponent success, not bBlockingHit.
        return hit[9]==component.get_owner() and hit[10]==component and math.isfinite(float(hit[3])) and float(hit[3])>=0

    def aimed_floor(self,origin,direction,height,min_distance,max_ray):
        candidates=[]
        for d in range(450,int(max_ray)-20,40):
            point=origin+direction*d;floor=self.floor(point)
            if floor is not None and math.hypot(floor.x-self.start_location.x,floor.y-self.start_location.y)>=min_distance:
                candidates.append((abs(point.z-floor.z-height),floor,d))
        self.require(bool(candidates),'Camera ray has no supported diagnostic target position')
        error,floor,d=min(candidates,key=lambda x:x[0])
        self.report['aimed_fixture']=dict(camera_cm=vec(origin),direction=vec(direction),ray_distance=d,
            center_height_error_cm=error,chosen_floor_cm=vec(floor),placement='Read-only camera/support search before Ready; actual shape hit is required next')
        return floor

    def setup(self,now):
        result=str(u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'level'))
        self.require(not result.startswith('REJECT'),'Initial native fixture rejected: '+result)
        self.report['director_stop_fixture']=result
        self.perks=prop(self.pc.player_state,'perks');self.original_table=self.perks.get_perk_table()
        rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(self.original_table))
        selected=next(r for r in rows if r['Name']==CASES[CASE][0]);expected=CASES[CASE][2]
        self.require(selected.get('bEnabled',False),'Current canonical active definition must be enabled')
        self.report['production_definition']=copy.deepcopy(selected)
        self.report['resolved_parameters']=normalize_parameters(selected.get('Parameters',{}))
        if expected>0:self.require(int(selected['InitialCharges'])==expected,'Production initial charge count differs from approved 2/3')
        if LAST_CHARGE and expected>0:
            # Initial setup ONLY, before grant/Ready. Preserve all production fields except this count.
            self.transient_table=u.MCRoguelikeEditorLibrary.create_last_charge_skill_review_table(self.world,self.original_table,CASES[CASE][0])
            self.require(self.transient_table is not None,'Guarded native transient duplicate failed')
            copied=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(self.transient_table))
            want={r['Name']:copy.deepcopy(r) for r in rows};want[selected['Name']]['InitialCharges']=1
            got={r['Name']:r for r in copied}
            self.require(got==want,'Native duplicate changed fields other than the selected initial charge')
            self.require(self.perks.editor_use_skill_catalog_for_approval(self.transient_table),'Guarded diagnostic catalog swap rejected')
            self.catalog_swapped=True
            self.report['last_charge_fixture']=dict(production_initial_charges=expected,transient_initial_charges=1,
                source_table=path(self.original_table),transient_table=path(self.transient_table),only_changed_field='selected row InitialCharges')
        self.require(self.perks.server_grant_perk(CASES[CASE][0]),'Real ServerGrantPerk rejected canonical skill')
        self.require(self.slots()[0]['id']==CASES[CASE][0],'Canonical grant did not occupy slot 1')
        self.tongue=self.actors(u.MCTongue)[0];center,_=self.tongue.get_actor_bounds(False)
        floor=self.floor(center);self.require(floor is not None,'Native tongue support required')
        self.center=floor;self.move_to(floor,0)
        self.report['camera']=dict(type='Native pawn Camera component; ordinary initial orbit input only',
            component=path(prop(self.hero,'camera')),target_pitch_degrees=CAMERA_PITCH,target_yaw_degrees=CAMERA_YAW,
            max_setup_wall_seconds=15,max_setup_game_seconds=12,settle_game_seconds=.6,samples=[],orbit_inputs=[])
        self.setup_time=now;self.orbit_started_wall=time.monotonic();self.phase='orbit_gate'

    def initial_orbit_setup(self,now):
        self.require(time.monotonic()-self.orbit_started_wall<15 and now-self.setup_time<12,
            'Native initial orbit did not reach/settle at the requested view within its bounded setup; inspect camera/gate facts')
        gates=self.gate_facts();camera=prop(self.hero,'camera')
        rot=camera.get_world_rotation();location=camera.get_world_location()
        manager=u.GameplayStatics.get_player_camera_manager(self.world,0)
        manager_rot=manager.get_camera_rotation();manager_location=manager.get_camera_location()
        yaw_error=(CAMERA_YAW-rot.yaw+180)%360-180;pitch_error=CAMERA_PITCH-rot.pitch
        clear=gates['local_controller'] is True and gates['world_presentation_ready'] is True and gates['look_input_ignored'] is False
        clear=clear and gates['show_mouse_cursor'] is False and gates['move_input_ignored'] is False and gates['reward_menu_open'] is False and gates['boss_intro_playing'] is False
        if now>=self.orbit_next_sample:
            self.orbit_next_sample=now+.2
            self.report['camera']['samples'].append(dict(game_seconds=now,public_gates=gates,
                component_rotation=dict(pitch=float(rot.pitch),yaw=float(rot.yaw),roll=float(rot.roll)),component_cm=vec(location),
                manager_rotation=dict(pitch=float(manager_rot.pitch),yaw=float(manager_rot.yaw),roll=float(manager_rot.roll)),
                manager_cm=vec(manager_location),target_pitch_error=float(pitch_error),target_yaw_error=float(yaw_error)))
        if not clear:self.orbit_stable_since=None;return
        if not self.orbit_sent:
            sensitivity=max(.02,min(1.,float(prop(self.hero,'camera_orbit_sensitivity'))))
            delta=u.Vector2D(yaw_error/sensitivity,pitch_error/sensitivity)
            self.hero.call_method('ApplyCameraOrbitInput',args=(delta,))
            self.orbit_sent=True;self.orbit_sent_at=now;self.phase='aim_settle'
            after=camera.get_world_rotation()
            self.report['camera']['orbit_inputs'].append(dict(game_seconds=now,public_gates=gates,
                before=dict(pitch=float(rot.pitch),yaw=float(rot.yaw)),delta=[float(delta.x),float(delta.y)],
                target=dict(pitch=CAMERA_PITCH,yaw=CAMERA_YAW),immediate_component_result=dict(pitch=float(after.pitch),yaw=float(after.yaw)),
                note='Native camera easing may apply after this call; final settled result is observed separately'))
            return
        if now-self.orbit_sent_at<1:return
        manager_yaw_error=(manager_rot.yaw-rot.yaw+180)%360-180
        settled=abs(yaw_error)<=.5 and abs(pitch_error)<=.5 and abs(manager_rot.pitch-rot.pitch)<=.5
        settled=settled and abs(manager_yaw_error)<=.5 and distance(location,manager_location)<=2
        self.orbit_stable_since=(self.orbit_stable_since if self.orbit_stable_since is not None else now) if settled else None
        if self.orbit_stable_since is not None and now-self.orbit_stable_since>=.6:
            self.report['camera']['settled_game_seconds']=now
            self.report['camera']['settled_component_result']=dict(pitch=float(rot.pitch),yaw=float(rot.yaw),location_cm=vec(location))
            self.report['camera']['settled_manager_result']=dict(pitch=float(manager_rot.pitch),yaw=float(manager_rot.yaw),location_cm=vec(manager_location))
            self.orbit_settled_at=now;self.prepare_targets(now)

    def prepare_targets(self,now):
        # Native camera has settled; place diagnostic targets once, never chase the ray during play.
        camera=prop(self.hero,'camera');rot=camera.get_world_rotation()
        self.require(abs(CAMERA_PITCH-rot.pitch)<=1 and abs((CAMERA_YAW-rot.yaw+180)%360-180)<=1,'Settled native camera changed before fixture search')
        forward=u.MathLibrary.get_forward_vector(rot);flat=u.Vector(forward.x,forward.y,0);length=math.hypot(flat.x,flat.y)
        self.require(length>.2,'Camera ray must have a usable planar direction')
        self.forward=u.Vector(flat.x/length,flat.y/length,0);right=u.Vector(-self.forward.y,self.forward.x,0)
        start=self.hero.get_actor_location();self.start_location=start
        ray_origin=camera.get_world_location();ray_end=ray_origin+forward*6000
        def ground(d,side=0):
            f=self.floor(start+self.forward*d+right*side)
            self.require(f is not None,'Fixture has no native tongue support');return f
        if CASE in ('Sponge','Driver'):
            at=ground(360)+u.Vector(0,0,3)
            self.patch=self.spawn(u.MCMouthSurface,at,prepare=lambda a:a.set_editor_properties(dict(
                randomize_liquid_size=False,liquid_half_size=210.,ulcer=False,paint_tile=False,ground_response=u.MCGroundSurface.NORMAL)))
            status=prop(self.patch,'status');state=prop(status,'state');total=int(prop(state,'coffee_total'))
            self.require(total>0,'Ordinary stain needs a positive native total')
            state.set_editor_property('coffee_left',total);status.set_editor_property('State',state)
            self.report['stain_fixture']=dict(path=path(self.patch),ground_response='Normal to isolate cleaning',initial_coffee_left=total,
                mask_untouched=all(int(x)==255 for x in prop(self.patch,'wipe_mask')))
        if CASE=='Hook' and VARIANT!='miss':
            # Place once beneath the ACTUAL camera ray, then prove intersection
            # with the native query shape. Visibility responses stay untouched.
            half=self.hero.capsule_component.get_scaled_capsule_half_height()
            at=self.aimed_floor(ray_origin,forward,58 if VARIANT=='boss' else half,350,max(500,min(3000,self.param('HookRange',1400))))
            if VARIANT=='boss':
                def boss(a):a.set_editor_properties(dict(state=u.MCNutBossState.IDLE,tongue=self.tongue))
                self.boss=self.spawn(u.MCNutBoss,at+u.Vector(0,0,58),prepare=boss)
                self.boss.set_actor_tick_enabled(False)
                target=self.boss;shape=prop(target,'body')
                self.report['hook_fixture']='Live native knight (initial Idle, AI tick disabled), stationary query target; not a fought encounter'
            else:
                half=self.hero.capsule_component.get_scaled_capsule_half_height()
                self.ally=self.spawn(u.MCToothCharacter,at+u.Vector(0,0,half+3))
                controller=self.spawn(u.AIController,at+u.Vector(0,0,half+3));controller.possess(self.ally)
                target=self.ally;shape=target.capsule_component
                self.report['hook_fixture']='AI-controlled native tooth; native Hook traces its actual capsule without changing Visibility responses'
            self.require(self.hp(target)>0 and self.ray_hits(shape,ray_origin,ray_end),'Actual camera ray misses native Hook target shape')
        elif CASE=='Hook':
            self.report['hook_fixture']='Genuine no-target miss: no ally/boss/Hook target is spawned; native cast may contact ordinary world geometry and reel back'
        if CASE=='Rocket':
            self.impact_floor=self.aimed_floor(ray_origin,forward,78,500,2600)
            self.tooth=self.spawn(u.MCArenaTooth,self.impact_floor+u.Vector(0,0,78))
            self.tooth.set_actor_tick_enabled(False)
            prop(self.tooth,'body').set_box_extent(u.Vector(48,48,78),True)
            self.require(self.hp(self.tooth)>0 and self.ray_hits(prop(self.tooth,'body'),ray_origin,ray_end),'Actual camera ray misses native arena tooth fixture')
            self.report['tooth_fixture']='Native arena tooth/status/callbacks, default-sized 48/48/78 cm body and AI/animation actor tick disabled before Ready'
        if CASE in ('Driver','Grenade','Rocket'):
            at=self.impact_floor-self.forward*120+right*110 if CASE=='Rocket' else ground(450 if CASE=='Driver' else 160 if VARIANT=='overhold' else 700,0 if CASE=='Driver' else 85)
            half=self.hero.capsule_component.get_scaled_capsule_half_height()
            self.ally=self.spawn(u.MCToothCharacter,at+u.Vector(0,0,half+3))
            controller=self.spawn(u.AIController,at+u.Vector(0,0,half+3));controller.possess(self.ally)
            self.require(self.hp(self.ally)>0,'Simulated ally must be alive before timing')
            if CASE in ('Grenade','Rocket'):
                points=[self.impact_floor-self.forward*180-right*160,self.impact_floor-self.forward*260+right*170] if CASE=='Rocket' else [ground(d,-70) for d in ((150,300) if VARIANT=='overhold' else (550,800,1050))]
                for point in points:
                    at=point+u.Vector(0,0,58)
                    def enemy(a):a.set_editor_properties(dict(nut_mesh=u.load_asset('/Engine/BasicShapes/Sphere'),mesh_scale=u.Vector(1,1,1),tongue=self.tongue))
                    e=self.spawn(u.MCNutEnemy,at,prepare=enemy);e.set_actor_tick_enabled(False);self.enemies.append(e)
                self.report['enemy_fixture']='Native damageable MCNutEnemy bodies, AI actor tick disabled initially to isolate impact attribution'
        self.foods=[]
        if CASE=='Air':
            count=7 if CASE=='Air' and VARIANT=='overflow' else 3
            held_rotation=u.Rotator(pitch=max(-35,min(25,rot.pitch)),yaw=rot.yaw,roll=rot.roll)
            local=u.Vector(self.param('IntakeX',128.7575),self.param('IntakeY',-11.1948),self.param('IntakeZ',-30.4591))
            cdo=u.get_default_object(u.MCAirSkillAction);source_mount=prop(cdo,'held_mount');model=prop(cdo,'prop_transform')
            mount=u.Transform(location=source_mount.translation,scale=source_mount.scale3d);mount.rotation=source_mount.rotation
            mount.translation=mount.translation+u.Vector(self.param('HeldX',35)-35,self.param('HeldY',70)-70,self.param('HeldZ',15)-15)
            hero_frame=self.hero.get_actor_transform()
            origin=hero_frame.transform_location(mount.transform_location(model.translation))
            nozzle_unaligned=hero_frame.transform_location(mount.transform_location(model.transform_location(local)))
            forward_unaligned=hero_frame.transform_location(mount.transform_location(model.transform_location(prop(cdo,'intake_forward_local'))))-origin
            desired=u.Transform(rotation=held_rotation).transform_location(u.Vector(1,0,0))
            correction=u.MathLibrary.quat_find_between_vectors(forward_unaligned,desired)
            correction_frame=u.Transform();correction_frame.rotation=correction
            nozzle=origin+correction_frame.transform_location(nozzle_unaligned-origin)
            self.report['air_fixture_nozzle_estimate_cm']=vec(nozzle)
            for index in range(count):
                floor=self.floor(nozzle+self.forward*(140+(index//3)*55)+right*((index%3-1)*34))
                self.require(floor is not None,'Air fixture requires native supported food')
                at=floor+u.Vector(0,0,16)
                food=self.spawn_food(at,True,index)
                self.foods.append(food)
        elif CASE in ('Grenade','Rocket'):
            point=self.impact_floor-self.forward*80-right*100 if CASE=='Rocket' else ground(140 if VARIANT=='overhold' else 700,40)
            self.whole_food=self.spawn_food(point+u.Vector(0,0,31),False,90)
            self.foods=[self.whole_food]
        self.food_ids={id(f):path(f) for f in self.foods}
        self.fixture_sealed_at=now;self.phase='fixture_settle';self.report['fixture_sealed_game_seconds']=now

    def ready(self,now):
        self.require(self.pc.is_world_presentation_ready() and not self.pc.is_reward_menu_open(),'Presentation/input must still be ready at the capture gate')
        move=self.hero.get_movement_component();body=prop(self.hero,'tooth_physics').get_body_state()
        ground=bool(prop(prop(move,'current_floor'),'walkable_floor'))
        self.report['readiness']=dict(body_state=str(body),movement_mode=str(prop(move,'movement_mode')),walkable_floor=ground,
            hero_speed=float(self.hero.get_velocity().length()),world_presentation_ready=True,input_gates=self.gate_facts())
        gates=self.report['readiness']['input_gates']
        self.write() # Preserve the real rejection facts even if no Ready gate is emitted.
        self.require(gates['local_controller'] is True and gates['state_owner_matches_controller'] is True and gates['state_pawn_matches_source'] is True,'Native local source ownership is not ready')
        self.require(gates['show_mouse_cursor'] is False and gates['move_input_ignored'] is False and gates['look_input_ignored'] is False,'Native cursor/input-ignore gate is not ready; inspect facts, never force flags')
        self.require(gates['paused'] is False and gates['lobby_waiting'] is False and gates['tutorial_active'] is False and 'WORKING' in str(gates['game_phase']).upper(),'Native working phase is not ready')
        self.require(gates['boss_intro_playing'] is False and gates['wardrobe_open'] is False and gates['hub_shop_open'] is False and gates['hero_reward_interaction'] is None and gates['hero_can_work'] is not False,'Native gameplay/presentation gate is not ready')
        self.require('STANDING' in str(body).upper(),'A native standing body is required before Ready')
        if CASE in ('Driver','Sponge'):self.require(ground and 'WALKING' in str(prop(move,'movement_mode')).upper(),'Native grounded walking eligibility required before ride trial')
        if CASE=='Hook':
            cam=u.GameplayStatics.get_player_camera_manager(self.world,0);start=cam.get_camera_location();end=start+u.MathLibrary.get_forward_vector(cam.get_camera_rotation())*self.param('HookRange',1400)
            if VARIANT=='miss':
                candidates=self.hook_ray_candidates(start,end);self.report['hook_miss_ready_shape_rays']=candidates
                self.require(not any(r['intersects'] for r in candidates),'No-target miss aim intersects an existing native Hook candidate; preserve it and reject this fixture')
                self.report['hook_ready_no_candidate_on_aim']=True
            else:
                target=self.boss if self.boss else self.ally
                self.require(valid(target) and self.ray_hits(prop(target,'body') if self.boss else target.capsule_component,start,end),'Settled native camera no longer hits Hook fixture')
                self.report['hook_ready_ray_hits_actual_shape']=True
        self.initial=dict(slots=self.slots(),hero_cm=vec(self.hero.get_actor_location()),hero_hp=self.hp(self.hero),
            source_player_state=path(self.pc.player_state),
            ally_hp=self.hp(self.ally) if self.ally else None,ally_cm=vec(self.ally.get_actor_location()) if self.ally else None,
            enemies={path(a):self.hp(a) for a in self.enemies},foods=self.food_rows(),
            mask=list(prop(self.patch,'wipe_mask')) if self.patch else None)
        if self.boss:self.initial.update(boss_hp=self.hp(self.boss),boss_cm=vec(self.boss.get_actor_location()))
        if self.tooth:self.initial['tooth_hp']=self.hp(self.tooth)
        if CASE=='Hook':
            self.initial['hook_faces']=self.hook_faces();self.initial['hook_source_pose']=self.hook_pose(self.hero)
            self.require(all(role in self.initial['hook_source_pose']['bones'] for role in ('root','body','hand_r','hand_l')),
                'Actual rendered source root/body/wrists required; no native hand intent may substitute for them')
            self.require(self.hook_faces_at_baseline(self.initial['hook_faces']),'Pre-input actual Hook face baseline is unavailable')
        if CASE=='Air':self.require(all(r['spoil_at']>now and r['item_name'] not in ('','None') for r in self.initial['foods']),'Native food identities/freshness deadlines must exist before Ready')
        self.initial['world_food_ids']=[path(f) for f in self.actors(u.MCFoodActor)]
        self.report['initial']=copy.deepcopy(self.initial)
        self.ready_wall=time.monotonic();self.phase='capture_gate'
        (OUT/'Ready.json').write_text(json.dumps(dict(status='ready_for_capture',case=CASE,variant=VARIANT,world=path(self.world),
            process_id=os.getpid(),pc=path(self.pc),pawn=path(self.hero),game_seconds=now,ready_wall_unix=time.time(),
            output=str(OUT),start_gate=str(self.start_gate),finish_gate=str(self.finish_gate),gate_timeout_seconds=30,
            window='Owned standalone floating 1280x720 PIE gameplay window; native pawn view; timed input has not started'),indent=2))
        self.write()

    def sample_case(self,now):
        active=self.current_actions();rows=[]
        for a in active:
            ident=path(a);entry=self.actions.setdefault(ident,dict(path=ident,first_seen=now,last_seen=now,phases=[],max_detonations=0))
            entry['last_seen']=now
            phase=enum_token(a.get_cast_phase()) if CASE=='Hook' else self.optional(lambda:str(prop(a,'phase')))
            if phase and phase not in entry['phases']:entry['phases'].append(phase)
            det=self.optional(lambda:int(prop(a,'detonation_count')))
            if det is not None:entry['max_detonations']=max(entry['max_detonations'],det)
            stored=self.optional(lambda:int(prop(a,'stored_count')))
            if stored is not None:self.max_stored=max(self.max_stored,stored)
            row=dict(path=ident,phase=phase,cm=vec(a.get_actor_location()),detonations=det,stored_count=stored,
                slot_instance=guid_text(a.get_slot_instance_id()),action_id=guid_text(a.get_action_id()),slot_index=int(a.get_slot_index()),
                press_sequence=int(a.get_press_sequence_for_review()),perk_id=str(a.get_perk_id()),committed=bool(a.is_committed()),source_player_state=path(a.source_state()))
            entry['action_id']=row['action_id'];entry['press_sequence']=row['press_sequence']
            if 'parameters_verified' not in entry:
                expected=self.report['resolved_parameters']
                parameters={k:float(a.param(k,-99912345.)) for k in expected}
                # UE TJsonPrintPolicy::WriteFloat writes "%g" (six significant
                # digits), while the reflected Param getter returns the full
                # float32. Compare in the actual DT export representation;
                # retain both forms rather than accepting arbitrary tolerance.
                exported={k:float(format(v,'.6g')) for k,v in parameters.items()}
                same=all(math.isfinite(parameters[k]) and exported[k]==expected[k] for k in expected)
                entry['parameters_verified']=same;entry['actual_parameters']=parameters
                entry['actual_parameters_export_precision']=exported
                entry['parameter_comparison']='Exact equality after UE JsonPrintPolicy WriteFloat %g serialization'
                self.report['actions']=list(self.actions.values())
                self.check('production_parameters_preserved_in_action',same)
                self.require(same,'Actual native action parameters differ from the production row; no fallback-based success credited')
            if CASE=='Air':row.update(overflow_warning=self.optional(lambda:bool(prop(a,'overflow_warning'))),
                ruptured=self.optional(lambda:bool(prop(a,'ruptured'))),capacity=self.optional(lambda:int(prop(a,'bag_capacity'))))
            if POSE_AUDIT and CASE=='Air':row.update(intake_cm=vec(a.get_intake_point()),intake_forward=vec(a.get_intake_forward()))
            if POSE_AUDIT and CASE=='Rocket':row.update(held_muzzle_cm=vec(a.get_launcher_muzzle_point()),held_barrel_forward=vec(a.get_launcher_forward()),launcher_held=bool(a.is_launcher_held()))
            if CASE=='Driver':row['super_car_visual']=self.driver_visual(a)
            if CASE=='Hook':
                row.update(self.hook_action_observation(a))
                transitions=entry.setdefault('phase_observations',[])
                if not transitions or transitions[-1]['phase']!=phase:
                    transitions.append(dict(phase=phase,observed_game_seconds=now,native_phase_started_at=row['phase_started_at'],
                        alpha=row['phase_alpha'],contacted=row['contacted'],head_cm=row['hook_head_cm'],tether_start_cm=row['tether_start_cm']))
                if row['contacted'] and 'first_contact_observation' not in entry:
                    entry['first_contact_observation']=dict(game_seconds=now,phase=phase,target=row['target'],
                        pulled_pawn=row['pulled_pawn'],head_cm=row['hook_head_cm'])
            rows.append(row);self.last_seen_action=a
        fx=[c.get_asset().get_path_name() for c in self.active_fx()]
        for asset in fx:self.fx_seen.add(asset)
        sample=dict(game_seconds=now,age=now-self.case_started,slots=self.slots(),hero_cm=vec(self.hero.get_actor_location()),
            hero_hp=self.hp(self.hero),hero_velocity=vec(self.hero.get_velocity()),actions=rows,foods=self.food_rows(),
            ally_hp=self.hp(self.ally) if valid(self.ally) else None,ally_cm=vec(self.ally.get_actor_location()) if valid(self.ally) else None,
            enemy_hp={path(e):self.hp(e) for e in self.enemies if valid(e)},active_skill_fx=len(fx),active_skill_fx_paths=fx)
        if self.ally:sample['ally_hit']=self.hit(self.ally)
        if self.enemies:sample['enemy_hits']={path(e):self.hit(e) for e in self.enemies if valid(e)}
        if self.boss:sample['boss_cm']=vec(self.boss.get_actor_location()) if valid(self.boss) else None
        if CASE=='Hook':
            sample['hook_faces']=self.hook_faces();sample['hook_source_pose']=self.hook_pose(self.hero)
            if valid(self.ally):sample['hook_ally_pose']=self.hook_pose(self.ally)
            sample['hook_faces_at_baseline']=self.hook_faces_at_baseline(sample['hook_faces'])
            camera=prop(self.hero,'camera');rot=camera.get_world_rotation()
            sample['gameplay_camera']=dict(component=path(camera),location_cm=vec(camera.get_world_location()),
                rotation=dict(pitch=float(rot.pitch),yaw=float(rot.yaw),roll=float(rot.roll)),post_outcome_orbit_sent=self.hook_reveal_sent)
            previous=self.report['samples'][-1] if self.report['samples'] else None
            sample['previous_sample_gap_game_seconds']=now-previous['game_seconds'] if previous else None
            self.hook_latest_faces=sample['hook_faces']
        if POSE_AUDIT:sample['pose_audit']=self.pose_observation(now)
        if self.tooth:sample.update(tooth_hp=self.hp(self.tooth) if valid(self.tooth) else None,tooth_lost=bool(prop(prop(self.tooth,'state'),'lost')) if valid(self.tooth) else None)
        if self.whole_food:
            batch=890190;sample['new_native_fragments']=[dict(path=path(f),item_name=str(prop(f,'item_name')),batch=int(prop(f,'batch'))) for f in self.actors(u.MCFoodActor)
                if valid(f) and bool(prop(f,'fragment')) and int(prop(f,'batch'))==batch]
        if self.patch and valid(self.patch):
            sample['mask_changed']=list(prop(self.patch,'wipe_mask'))!=self.initial['mask']
            sample['coffee_left']=int(prop(prop(prop(self.patch,'status'),'state'),'coffee_left'))
        if CONTACT_PROBE and CASE=='Driver' and active and .2<sample['age']<1.1:
            # Independent read-only triangle normals at the wide ride footprint.
            # Initial-overlap Chaos MTD is not itself a geometric face normal.
            surface=prop(self.tongue,'surface');at=self.hero.get_actor_location();queries=[]
            points=[('logged_contact',u.Vector(211.795,50.143,at.z))]
            right=u.Vector(-self.forward.y,self.forward.x,0)
            for front in (-140,0,140):
                for side in (-120,0,120):points.append((str((front,side)),at+self.forward*front+right*side))
            for name,point in points:
                trace=surface.line_trace_component(point+u.Vector(0,0,300),point-u.Vector(0,0,350),True,False,False)
                item=dict(name=name,xy=[point.x,point.y],component=path(surface),hit=trace is not None)
                if trace is not None:
                    hit=trace[3].to_tuple()
                    item.update(point=vec(hit[5]),normal=vec(hit[7]),face_index=int(hit[15]),hit_component=path(hit[10]))
                queries.append(item)
            mtd=u.Vector(.745,-.661,0)
            contact=u.Vector(211.795,50.143,106.367)
            for name,axis in (('support_near',u.Vector(-.086,.043,.995)),('mtd_near',u.Vector(.745,-.661,.093))):
                trace=surface.line_trace_component(contact+axis*6,contact-axis*6,True,False,False)
                item=dict(name=name,component=path(surface),hit=trace is not None)
                if trace is not None:
                    hit=trace[3].to_tuple()
                    item.update(point=vec(hit[5]),normal=vec(hit[7]),face_index=int(hit[15]),hit_component=path(hit[10]))
                queries.append(item)
            for height in (0,3,12):
                contact=u.Vector(211.795,50.143,106.367+height)
                trace=surface.line_trace_component(contact+mtd*20,contact-mtd*20,True,False,False)
                item=dict(name='logged_contact_side_'+str(height),component=path(surface),hit=trace is not None)
                if trace is not None:
                    hit=trace[3].to_tuple()
                    item.update(point=vec(hit[5]),normal=vec(hit[7]),face_index=int(hit[15]),hit_component=path(hit[10]))
                queries.append(item)
            sample['independent_support_triangles']=queries
        self.report['samples'].append(sample)
        if active and CASES[CASE][2]>0 and LAST_CHARGE:self.check('last_charge_slot_empty_while_original_action_live',self.slots()[0]['id'] in ('None',''))
        return active

    def complete_checks(self,now):
        samples=self.report['samples'];ages=[r for r in samples if r['actions']]
        self.check('native_action_observed',bool(self.actions));self.check('real_slot_handler_used',bool(self.report['inputs']))
        self.check('input_released',not self.held)
        self.check('source_instance_preserved',bool(ages) and all(a['slot_instance']==self.initial['slots'][0]['instance'] for r in ages for a in r['actions']))
        self.check('native_identity_complete',bool(ages) and all(a['slot_index']==0 and a['press_sequence']>0 and a['perk_id']==CASES[CASE][0] and a['committed'] for r in ages for a in r['actions']))
        self.check('source_player_state_preserved',bool(ages) and all(a['source_player_state']==self.initial['source_player_state'] for r in ages for a in r['actions']))
        self.check('actual_skill_niagara_observed',bool(self.fx_seen))
        self.report['observed_skill_niagara_assets']=sorted(self.fx_seen)
        if CASES[CASE][2]>0 and not LAST_CHARGE:
            self.check('exactly_one_charge_spent',self.slots()[0]['charges']==self.initial['slots'][0]['charges']-1)
        if CASE in ('Sponge','Driver'):
            self.check('native_mask_changed',any(r.get('mask_changed') for r in samples))
            self.check('native_ride_moved',any(distance(u.Vector(*r['hero_cm']),u.Vector(*self.initial['hero_cm']))>100 for r in samples))
            if CASE=='Sponge':
                self.check('reusable_unlimited_charges',self.slots()[0]['charges']==-1)
                life=max((a['last_seen']-a['first_seen'] for a in self.actions.values()),default=0)
                if VARIANT=='repeat':
                    self.check('repeat_stopped_before_cooldown',self.repeated and self.repeat_before is not None and self.repeat_ended_at is not None
                        and 0<=self.repeat_ended_at-self.repeat_before<=.5 and len(self.actions)==1 and self.slots()[0]['cooldown']>0)
                    self.report['repeat_observation']=dict(original_action_id=self.repeat_action_id,alive_immediately_before_game_seconds=self.repeat_before,absent_after_game_seconds=self.repeat_ended_at)
                else:
                    duration=max(.2,min(30,self.param('RideSeconds',8)))
                    self.check('native_timer_duration_observed',duration-.35<=life<=duration+.25)
                    self.report['timer_observation']=dict(authored_seconds=duration,sampled_live_seconds=life)
            else:
                life=max((a['last_seen']-a['first_seen'] for a in self.actions.values()),default=0)
                duration=max(.2,min(30,self.param('RideSeconds',3)))
                self.check('authored_driver_duration',duration-.35<=life<=duration+.4)
                self.report['timer_observation']=dict(authored_seconds=duration,sampled_live_seconds=life)
                self.check('native_ally_runover_damage',any(r['ally_hp'] is not None and r['ally_hp']<self.initial['ally_hp'] for r in samples))
                self.check('runover_feedback_attributed_to_action',any(r.get('ally_hit') and r['ally_hit']['health_damage']>0 and r['ally_hit']['source'] in self.actions for r in samples))
                self.check_driver_visual(samples)
        if CASE=='Hook':
            hook_rows=[(r,a) for r in samples for a in r['actions']]
            expected=['WINDUP','OUTBOUND']+([] if VARIANT=='miss' else ['PULLING'])+['RETURNING','RECOVERY']
            self.check('one_native_hook_cast',len(self.actions)==1)
            self.check('staged_hook_phase_order',bool(self.actions) and all(a['phases']==expected for a in self.actions.values()))
            self.check('windup_visible_before_flight',any(a['cast_phase']=='WINDUP' and a['prop_visible'] and a['phase_alpha']>.1 for r,a in hook_rows))
            outbound=[(r,a) for r,a in hook_rows if a['cast_phase']=='OUTBOUND']
            self.check('outbound_observed_before_contact',bool(outbound) and all(not a['contacted'] and a['target'] is None and a['pulled_pawn'] is None for r,a in outbound))
            self.check('cast_head_separates_from_caster',max((a['tether_length_cm'] for r,a in hook_rows),default=0)>100)
            before_contact=[r for r,a in hook_rows if not a['contacted'] and a['cast_phase'] in ('WINDUP','OUTBOUND')]
            self.check('source_not_pulled_before_contact',bool(before_contact) and all(math.dist(r['hero_cm'],self.initial['hero_cm'])<45 for r in before_contact))
            if self.ally:self.check('ally_not_pulled_before_contact',all(math.dist(r['ally_cm'],self.initial['ally_cm'])<45 for r in before_contact))
            returns=[a['tether_length_cm'] for r,a in hook_rows if a['cast_phase']=='RETURNING']
            self.check('native_head_reels_back',len(returns)>=2 and returns[0]>returns[-1]+15 and returns[-1]<60)
            wrists=[];cable=[];head_errors=[];local_wrists=[]
            for r,a in hook_rows:
                bones=r['hook_source_pose']['bones'];wrist=bones.get('hand_r')
                if wrist:
                    wrists.append(math.dist(wrist['world']['cm'],a['tether_start_cm']))
                    local_wrists.append(wrist['actor_local_cm'])
                    a['evaluated_wrist_to_tether_start_cm']=wrists[-1]
                head_errors.append(math.dist(a['cm'],a['hook_head_cm']))
                if a['loop_active'] and a.get('cable_start',{}).get('valid') and a.get('cable_end',{}).get('valid'):
                    cable.append(dict(start_cm=math.dist(a['cable_start']['cm'],a['tether_start_cm']),
                        end_cm=math.dist(a['cable_end']['cm'],a['tether_attachment_cm'])))
            baseline_wrist=self.initial['hook_source_pose']['bones']['hand_r']['actor_local_cm']
            self.check('actual_evaluated_wrist_motion',bool(local_wrists) and max(math.dist(p,baseline_wrist) for p in local_wrists)>8)
            self.check('actual_wrist_stays_with_caster',bool(local_wrists) and max(math.sqrt(sum(x*x for x in p)) for p in local_wrists)<200)
            self.check('actual_wrist_matches_tether_root',bool(wrists) and max(wrists)<55)
            self.check('actual_loop_cable_endpoints_observed',bool(cable))
            self.check('actual_loop_follows_wrist_and_head',bool(cable) and max(max(x.values()) for x in cable)<40)
            self.check('visible_hook_actor_follows_head',bool(head_errors) and max(head_errors)<40)
            face_role='MISS' if VARIANT=='miss' else 'SUCCESS';mouth='Mouth_Frown' if VARIANT=='miss' else 'Mouth_Smile'
            action_ids={a['action_id'] for a in self.actions.values()}
            faces=[r['hook_faces']['source'] for r in samples]
            source_baseline=self.initial['hook_faces']['source']['mouth'][mouth]
            self.check('source_actual_outcome_face',any(f['action_id'] in action_ids and f['reaction']==face_role
                and f['mouth'][mouth]>source_baseline+.12 for f in faces))
            self.check('actual_outcome_faces_return_to_baseline',bool(samples) and samples[-1]['hook_faces_at_baseline'])
            if VARIANT=='miss':
                self.check('miss_has_no_spawned_hook_target',self.ally is None and self.boss is None and self.report.get('hook_ready_no_candidate_on_aim') is True)
                self.check('miss_never_contacts_or_pulls',bool(hook_rows) and all(not a['contacted'] and a['target'] is None and a['pulled_pawn'] is None for r,a in hook_rows))
                self.check('miss_source_remained_near_start',all(math.dist(r['hero_cm'],self.initial['hero_cm'])<60 for r in samples))
            else:
                target=self.boss if self.boss else self.ally;moved=self.hero if self.boss else self.ally
                old_target=self.initial['boss_cm'] if self.boss else self.initial['ally_cm']
                d0=math.dist(self.initial['hero_cm'],old_target);d1=distance(self.hero.get_actor_location(),target.get_actor_location())
                self.check('native_hook_closed_distance',d1<d0-100)
                self.check('native_hook_target_identity',any(a['contacted'] and a['target']==path(target) and a['pulled_pawn']==path(moved) for r,a in hook_rows))
                if self.boss:
                    self.check('source_pulled_to_live_boss',math.dist(vec(self.hero.get_actor_location()),self.initial['hero_cm'])>100 and self.hp(self.boss)>0)
                    self.check('boss_remained_near_fixture_start',math.dist(vec(self.boss.get_actor_location()),old_target)<20)
                else:
                    self.check('source_remained_near_start',math.dist(vec(self.hero.get_actor_location()),self.initial['hero_cm'])<100)
                    closest=min(math.dist(r['hero_cm'],r['ally_cm']) for r in samples if r.get('ally_cm') is not None)
                    self.check('full_ally_pull_reached_authored_stop_distance',closest<=self.param('StopDistance',120)+15)
                    self.check('ordinary_ally_pull_completed_without_early_physical_stop',any(a.get('pull_stop_reason')=='ReachedStopDistance' for r,a in hook_rows)
                        and all(a.get('pull_stop_reason') in ('None','ReachedStopDistance') for r,a in hook_rows))
                    ally_baseline=self.initial['hook_faces']['ally']['mouth']['Mouth_Surprise']
                    self.check('caught_ally_actual_surprise_face',any(r['hook_faces']['ally']['action_id'] in action_ids
                        and r['hook_faces']['ally']['reaction']=='CAUGHT' and r['hook_faces']['ally']['mouth']['Mouth_Surprise']>ally_baseline+.12 for r in samples))
            self.report['hook_observation_summary']=dict(expected_phase_order=expected,
                actual_sample_gap_max_seconds=max((r['previous_sample_gap_game_seconds'] or 0 for r in samples),default=0),
                wrist_to_tether_start_max_cm=max(wrists,default=None),visible_head_error_max_cm=max(head_errors,default=None),
                cable_endpoint_errors_cm=cable,source_face_peak=max((f['mouth'][mouth] for f in faces),default=0),
                actual_face_after_action_observed=any(not r['actions'] and not r['hook_faces_at_baseline'] for r in samples),
                tail_baseline_observed_game_seconds=samples[-1]['game_seconds'] if samples else None,
                thresholds='Broad spatial/observable-stage tolerances, not duplicate gameplay curves or exact implementation timer equations')
            if HOOK_FACE_REVEAL:self.check('optional_face_orbit_only_after_native_outcome',self.hook_reveal_sent)
        if CASE in ('Grenade','Rocket'):
            all_phases=' '.join(p for a in self.actions.values() for p in a['phases']).upper()
            wanted='DROPPED' if CASE=='Grenade' and VARIANT=='overhold' else 'THROWN' if CASE=='Grenade' else 'FLYING'
            self.check('native_flight_or_overhold_drop_observed',wanted in all_phases)
            self.check('one_detonation_per_action',bool(self.actions) and all(a['max_detonations']==1 for a in self.actions.values()))
            self.check('native_enemy_damage',any(any(hp<self.initial['enemies'].get(p,hp) for p,hp in r['enemy_hp'].items()) for r in samples))
            self.check('native_friendly_fire_damage',any(r['ally_hp'] is not None and r['ally_hp']<self.initial['ally_hp'] for r in samples))
            self.check('enemy_damage_attributed_to_native_action',any(hit and hit['health_damage']>0 and hit['source'] in self.actions for r in samples for hit in r.get('enemy_hits',{}).values()))
            self.check('friendly_damage_attributed_to_native_action',any(r.get('ally_hit') and r['ally_hit']['health_damage']>0 and r['ally_hit']['source'] in self.actions for r in samples))
            whole_id=self.food_ids[id(self.whole_food)]
            self.check('native_whole_food_destroyed',any(any(f['path']==whole_id and f['valid'] and 'DISPOSED' in f['phase'].upper() for f in r['foods']) for r in samples))
            self.check('native_whole_food_fragmented',any(len(r.get('new_native_fragments',[]))==3 for r in samples))
            self.check('native_fragment_item_and_batch_retained',any(len(r.get('new_native_fragments',[]))==3 and all(f['item_name']=='ActiveSkillApprovalFood_90' and f['batch']==890190 for f in r['new_native_fragments']) for r in samples))
            if CASE=='Rocket':self.check('native_arena_tooth_destroyed',any(r.get('tooth_hp') is not None and r['tooth_hp']<=0 and r.get('tooth_lost') for r in samples))
        if CASE=='Air':
            self.check('native_container_received_food',self.max_stored>0)
            self.check('original_fragment_identities_retained',bool(self.contained) and all(valid(f) for f in self.foods))
            final=self.food_rows();self.check('all_fragments_returned_without_container',all(r['valid'] and 'CONTAINED' not in r['phase'].upper() and not r['container'] for r in final))
            self.check('all_requested_fragments_observed_contained',VARIANT=='overflow' or len(self.contained)==len(self.foods))
            initial_deadlines={r['path']:r['spoil_at'] for r in self.initial['foods']}
            self.check('original_spoil_deadlines_retained',all(r['valid'] and abs(r['spoil_at']-initial_deadlines[r['path']])<.001 for r in final))
            self.check('air_did_not_spawn_duplicate_food_actors',not(set(path(f) for f in self.actors(u.MCFoodActor))-set(self.initial['world_food_ids'])))
            if VARIANT=='overflow':
                self.check('native_full_bag_warning_observed',any(a.get('overflow_warning') for r in samples for a in r['actions']))
                rupture=[r['game_seconds'] for r in samples if any('/NS_Air_Rupture.' in p for p in r.get('active_skill_fx_paths',[]))]
                self.check('native_bag_rupture_cue_observed',bool(rupture))
                completed=[e['game_seconds'] for e in self.report['inputs'] if e['event']=='Completed']
                self.check('rupture_preceded_completed_input',bool(rupture) and bool(completed) and min(completed)>min(rupture))
                capacity=max((a.get('capacity') or 0 for r in samples for a in r['actions']),default=0)
                self.check('bag_reached_native_capacity',capacity>0 and self.max_stored==capacity)
            else:
                completed=[e['game_seconds'] for e in self.report['inputs'] if e['event']=='Completed']
                self.check('native_forward_blow_observed',bool(completed) and any(completed[0]<=r['game_seconds']<=completed[0]+.6 and any(
                    f['valid'] and f.get('body_sim') and sum(v*d for v,d in zip(f['velocity_cm_s'],vec(self.forward)))>300 for f in r['foods']) for r in samples))
        self.report['actions']=list(self.actions.values());self.report['max_stored_count']=self.max_stored
        self.report['contained_fragment_ids']=sorted(self.contained);self.report['final_foods']=self.food_rows()
        self.report['final_slots']=self.slots();self.report['checks_complete_game_seconds']=now

    def queue_end(self,now,error=None):
        self.pending_error=error;self.end_gate_wall=time.monotonic();self.phase='capture_end_gate'
        (OUT/'CaptureEndReady.json').write_text(json.dumps(dict(status='capture_end_ready',case=CASE,game_seconds=now,
            ready_wall_unix=time.time(),quiet_game_seconds=now-self.quiet_since if self.quiet_since else 0,
            finish_gate=str(self.finish_gate),timeout_seconds=20,error=error),indent=2))
        self.write()

    def drain_timeout(self,reason):
        (OUT/'DrainTimeout.json').write_text(json.dumps(dict(error=self.pending_error,reason=reason,
            full_quiet_capture_end_gate_withheld=True,wall_unix=time.time()),indent=2))
        self.finish(self.pending_error or reason)

    def tick(self,_delta):
        if self.done:return
        try:
            wall=time.monotonic()-self.started
            if wall>=180:
                self.pending_error=self.pending_error or 'Absolute 180s observer limit'
                self.drain_timeout('Absolute wall safety limit; quiet tail was not certified');return
            if self.phase not in ('failure_drain','capture_end_gate'):self.require(wall<120,'Bounded 120s work limit; entering native tail drain')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'Exactly one owned standalone authority world')
                self.world=worlds[0];self.pc=u.GameplayStatics.get_player_controller(self.world,0)
                if not self.pc or not self.pc.get_controlled_pawn():self.world=None;return
                self.hero=self.pc.get_controlled_pawn();self.require(isinstance(self.hero,u.MCToothCharacter),'Real native pawn required')
                self.engine.set_editor_property('bUseFixedFrameRate',False);self.console('t.MaxFPS 60')
                self.phase='readiness';self.phase_at=float(u.GameplayStatics.get_time_seconds(self.world));return
            self.require(self.owned_pie_is_active(worlds),'Owned PIE ended/replaced; preserve external session')
            if self.phase not in ('failure_drain','capture_end_gate'):
                self.require(self.pc.get_controlled_pawn()==self.hero,'Source pawn changed; do not continue on replacement')
            now=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.phase=='capture_end_gate':
                if self.finish_gate.exists():self.finish(self.pending_error)
                elif time.monotonic()-self.end_gate_wall>=20:self.finish(self.pending_error or 'FinishCapture.gate timeout')
                return
            if self.phase=='failure_drain' and time.monotonic()-self.failure_wall>=40:
                self.drain_timeout('40s failure drain wall allowance exhausted; quiet tail was not certified');return
            if self.last_game_time is not None and now<=self.last_game_time:return
            self.last_game_time=now
            if self.phase=='readiness':
                self.require(wall<LOADING_BUDGET,'Loading/presentation timeout')
                if not self.pc.is_world_presentation_ready() or self.pc.is_reward_menu_open():return
                if now-self.phase_at>1.5:self.setup(now)
            elif self.phase in ('orbit_gate','aim_settle'):self.initial_orbit_setup(now)
            elif self.phase=='fixture_settle' and now-self.fixture_sealed_at>=1.5:self.ready(now)
            elif self.phase=='capture_gate':
                self.require(time.monotonic()-self.ready_wall<30,'StartCapture.gate timed out')
                if self.start_gate.exists():
                    self.capture_started=True;self.case_started=now;self.phase='running';self.press(now)
                    if CASE=='Hook' or POSE_AUDIT:self.sample_case(now)
            elif self.phase=='running':
                age=now-self.case_started;self.require(age<35,'Native action/quiet timeout; no forced finish is credited')
                active=self.current_actions()
                if CASE=='Hook' or POSE_AUDIT or now>=self.next_sample:self.sample_case(now);self.next_sample=now+.1
                if POSE_AUDIT and POSE_SNAPSHOTS:self.capture_pose_views(now)
                if CASE=='Driver' and VARIANT=='steer' and active:self.driver_steering_input(now)
                elif CASE in ('Sponge','Driver') and active:self.hero.add_movement_input(self.forward,1,False)
                if CASE=='Sponge' and not self.released and age>.2:self.release(now)
                if CASE=='Sponge' and VARIANT=='repeat' and not self.repeated and age>=1.6:
                    live=self.current_actions();self.require(len(live)==1,'Repeat trial requires its original ride still alive immediately before repeat')
                    self.repeat_action_id=guid_text(live[0].get_action_id());self.repeat_before=now
                    self.press(now,'Started_repeat');self.release(now);self.repeated=True
                if CASE=='Sponge' and self.repeated and self.repeat_ended_at is None:
                    if not any(guid_text(a.get_action_id())==self.repeat_action_id for a in self.current_actions()):self.repeat_ended_at=now
                    else:self.require(now-self.repeat_before<=.5,'Repeat input did not stop the live original action within .5s')
                if CASE=='Hook' and not self.released and age>.15:self.release(now)
                if CASE=='Hook':self.hook_reveal_face(now,active)
                if CASE=='Grenade' and not self.released and VARIANT=='release' and age>=1.9:self.release(now)
                if CASE=='Grenade' and not self.released and VARIANT=='overhold' and age>=3.5:self.release(now)
                if CASE=='Rocket' and not self.released and age>.2:self.release(now)
                if CASE=='Driver' and not self.released and age>.2:self.release(now)
                if CASE=='Air' and not self.released:
                    if VARIANT=='release' and ((self.max_stored>=len(self.foods) and age>.8) or age>=4):self.release(now)
                    if VARIANT=='overflow' and age>=6:self.release(now)
                standing='STANDING' in str(prop(self.hero,'tooth_physics').get_body_state()).upper()
                quiet=bool(self.actions) and not active and not self.active_fx() and not self.held and standing and self.hero.get_velocity().length()<40
                if CASE=='Hook':quiet=quiet and self.hook_faces_at_baseline(self.hook_latest_faces)
                self.quiet_since=(self.quiet_since if self.quiet_since is not None else now) if quiet else None
                if self.quiet_since is not None and now-self.quiet_since>=.8:
                    self.complete_checks(now)
                    error=None if all(self.report['checks'].values()) else 'One or more native observations failed; inspect Report.json'
                    self.queue_end(now,error)
            elif self.phase=='failure_drain':
                # A failed observation still keeps the visual tail in the recording.
                # Only genuine input cancellation was sent; no action Finish/state edits.
                quiet=not self.current_actions(draining=True) and not self.active_fx()
                if CASE=='Hook':quiet=quiet and self.hook_faces_at_baseline(self.hook_faces())
                self.quiet_since=(self.quiet_since if self.quiet_since is not None else now) if quiet else None
                if self.quiet_since is not None and now-self.quiet_since>=.8:self.queue_end(now,self.pending_error)
                elif now-self.failure_at>=36:
                    self.drain_timeout('Native action/visual tail did not end in 36 game seconds')
            if time.monotonic()>=self.flush_at:self.flush_at=time.monotonic()+1;self.write()
        except Exception:
            error=traceback.format_exc()
            if self.capture_started and self.owned_pie_is_active() and self.phase not in ('capture_end_gate','failure_drain'):
                now=float(u.GameplayStatics.get_time_seconds(self.world))
                self.pending_error=error;self.quiet_since=None;self.failure_at=now;self.failure_wall=time.monotonic();self.phase='failure_drain'
                self.check('observer_completed_without_exception',False)
                if self.held:
                    try:self.release(now,True)
                    except Exception:self.report['cancellation_error']=traceback.format_exc()
                self.write()
            elif self.capture_started and self.phase=='failure_drain' and self.owned_pie_is_active():
                # A broken observer read must not destroy a still-live native
                # hazard on the next frame. Retain the bounded drain allowance.
                self.quiet_since=None;self.report['drain_observer_error']=error
                if time.monotonic()-self.failure_wall>=40:self.drain_timeout('Observer reads prevented quiet-tail certification')
                else:self.write()
            else:self.finish(error)

    def finish(self,error=None):
        if self.done:return
        if error is None and self.phase!='capture_end_gate':error='Observer stopped before a certified capture-end gate'
        cleanup=[];errors=[]
        if self.owned_pie_is_active():
            if self.held:
                try:self.release(float(u.GameplayStatics.get_time_seconds(self.world)),True)
                except Exception:errors.append(traceback.format_exc())
            for a in reversed(self.fixtures):
                try:
                    if valid(a):a.destroy_actor()
                except Exception:errors.append(traceback.format_exc())
            if self.perks:
                try:self.perks.reset_perks() # Cleanup only, after observations/capture finish gate.
                except Exception:errors.append(traceback.format_exc())
                if self.catalog_swapped:
                    try:self.require(self.perks.editor_use_skill_catalog_for_approval(self.original_table),'Original diagnostic catalog restore rejected')
                    except Exception:errors.append(traceback.format_exc())
            cleanup.append('Owned transient fixtures/grants cleaned after observations; no cleanup credited as gameplay success')
        try:u.SystemLibrary.execute_console_command(self.editor_world,'t.MaxFPS '+str(self.old_max_fps))
        except Exception:errors.append(traceback.format_exc())
        if errors:self.report['observer_cleanup_errors']=errors;error=error or 'Observer cleanup failed'
        self.report['observer_cleanup']=cleanup
        super().finish(error)

def main():
    probe=ActiveSkillsSolo();setattr(builtins,KEY,probe)
    try:probe.start()
    except Exception:probe.finish(traceback.format_exc());raise

if __name__=='__main__':main()
