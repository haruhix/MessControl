"""Same-placement old/new solo recording of RMB while carrying ordinary food.

Run only in an idle saved L_Mouth Editor with the intended old/new binary and
-MCHubOwned -MCHubTransientProfile. This recipe does not launch/rebuild Editor.
MC_FOOD_RMB_EXPECT_FIXED=False records the old failure as baseline evidence,
never as a fix pass. True requires retained identities, collection/brace state,
baseline-comparative body/hand continuity, then normal Q/E and empty RMB input.

Disclosed fixture: native review_rogue_gameplay('level') stops the director and
resets initial status/loadout. Player placement/camera and three newly spawned
real broccoli fragments are diagnostic. E, RMB and Q use their ordinary public
input handlers. No fabricated carriers, bone poses, physics masks, CanAct,
camera smoothing, perks, persistent writes, Computer Use, network or microphone.
Cancel: builtins.mc_food_rmb_solo.finish('cancelled explicitly').
"""
import builtins
import csv
import hashlib
import json
import math
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
EXPECT_FIXED=bool(globals().get('MC_FOOD_RMB_EXPECT_FIXED',True))
OUT=Path(globals().get('MC_FOOD_RMB_OUTPUT',
    'E:/CodexWork/MessControl/FoodRMB_20261028/'+('FixedA' if EXPECT_FIXED else 'BaselineA'))).resolve()
KEY='mc_food_rmb_solo'
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol' and OUT.drive.upper()=='E:'
assert not (OUT/'Report.json').exists(), 'Use a fresh MC_FOOD_RMB_OUTPUT; retain earlier evidence'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved packages'
previous=getattr(builtins,KEY,None)
assert previous is None or getattr(previous,'done',True), 'Food RMB probe already active'
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='food_rmb_support',init_globals={'MC_ENV_OUTPUT':OUT,'MC_ENV_CAPTURE_HZ':0})
Base,prop=support['ApprovedEnvironmentSolo'],support['prop']


def valid(obj):
    return obj is not None and u.SystemLibrary.is_valid(obj)


def path(obj):
    return obj.get_path_name() if valid(obj) else None


def xyz(value):
    return [float(value.x),float(value.y),float(value.z)]


class FoodRMBSolo(Base):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__,expect_fixed=EXPECT_FIXED,checks={},stages=[],input_requests=[],
            recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            placement=dict(forward_cm=85,lateral_cm=[-25,0,25],floor_offset_cm=35),
            limitations=['One standalone authority player, not remote prediction/network validation.',
                'Director-stop/placement/spawn fixtures; input handlers and food simulation are real.',
                'Empty RMB intent is checked; this scene does not certify a particular valid brace anchor.',
                'Bone continuity is compared with this same load idle baseline, with scale-relative numerical tolerance.',
                'Exact PIE SWindow screenshots require visual review; CSV is measurement evidence, not a motion-quality opinion.'])
        self.report.pop('fixture_tuning',None);self.report.pop('tuning_reason',None)
        self.busy=False;self.mode_at=0.;self.rmb_held=False;self.throw_held=False
        self.fixtures=[];self.rows=[];self.expected_ids=[];self.bones={}
        self.collection=self.grip=self.inventory=None
        self.capture_at=0.;self.rmb_started=None

    def write(self):
        self.output.mkdir(parents=True,exist_ok=True)
        (self.output/'Report.json').write_text(json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf8')

    def check(self,name,ok):
        self.report['checks'][name]=bool(ok);self.require(ok,name)

    def enter(self,phase):
        self.phase=phase;self.mode_at=time.monotonic()-self.started
        self.report['stages'].append(dict(name=phase,wall_seconds=self.mode_at,
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))))
        self.write()

    def e(self):
        self.hero.set_handle_input_held(False);self.hero.set_handle_input_held(True);self.hero.set_handle_input_held(False)
        self.report['input_requests'].append(dict(input='E',method='SetHandleInputHeld false/true/false',phase=self.phase))

    def rmb(self,held):
        if bool(held)==self.rmb_held:return
        self.hero.set_brace_input_held(bool(held));self.rmb_held=bool(held)
        self.report['input_requests'].append(dict(input='RMB',held=bool(held),method='SetBraceInputHeld',
            phase=self.phase,wall_seconds=time.monotonic()-self.started,
            game_seconds=float(u.GameplayStatics.get_time_seconds(self.world))))

    def q(self,held):
        self.hero.set_throw_input_held(bool(held));self.throw_held=bool(held)
        self.report['input_requests'].append(dict(input='Q',held=bool(held),method='SetThrowInputHeld',phase=self.phase))

    def pieces(self):
        return list(prop(self.collection,'pieces'))

    def ids(self):
        return [path(food) for food in self.pieces()]

    def input_snapshot(self):
        loading=[widget for widget in u.ObjectIterator(u.MCLoadingWidget)
            if widget.get_owning_player()==self.pc and widget.is_in_viewport()
            and widget.get_visibility() not in (u.SlateVisibility.HIDDEN,u.SlateVisibility.COLLAPSED)]
        return dict(world_ready=bool(self.pc.is_world_presentation_ready()),
            reward_menu_open=bool(self.pc.is_reward_menu_open()),loading_visible=bool(loading),
            mouse_cursor=bool(prop(self.pc,'show_mouse_cursor')),move_ignored=bool(self.pc.is_move_input_ignored()),
            look_ignored=bool(self.pc.is_look_input_ignored()),boss_intro=bool(self.pc.is_boss_intro_playing()))

    def spawn_fragment(self,position,index):
        # Same production actor recipe as check_manual_intake_solo.py::spawn_fragment.
        pose=u.Transform(location=position)
        food=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,u.MCFoodActor.static_class(),pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(food),'Actual fragment allocation')
        row=prop(food,'food_data');row.set_editor_property('kind',u.MCFoodKind.FOOD)
        row.set_editor_property('fragment_scale',u.Vector(.25,.25,.25));row.set_editor_property('scale',u.Vector(.25,.25,.25))
        row.set_editor_property('spoil_seconds',600.)
        food.set_editor_property('food_data',row);food.set_editor_property('item_name','FoodRMBFixture'+str(index))
        food.set_editor_property('item_mesh',u.load_asset('/Game/Stylized_Vegetables/Meshes/SM_Broccoli'))
        food.set_editor_property('fragment',True);food.set_editor_property('spoiled',False);food.set_editor_property('batch',281028)
        self.statics.call_method('FinishSpawningActor',args=(food,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.fixtures.append(food)

    def sample(self):
        actor=self.hero.get_actor_transform()
        bones={role:xyz(u.MathLibrary.inverse_transform_location(actor,self.hero.mesh.get_socket_location(name)))
            for role,name in self.bones.items()}
        tool_visibility={component.get_name():bool(component.is_visible())
            for component in self.hero.get_components_by_class(u.PrimitiveComponent)
            if any(term in component.get_name().lower() for term in ('brush','knife','pick','spray','tool','watergun','chainsaw'))}
        row=dict(wall_seconds=time.monotonic()-self.started,game_seconds=float(u.GameplayStatics.get_time_seconds(self.world)),
            phase=self.phase,rmb_requested=self.rmb_held,collecting=bool(prop(self.collection,'collecting')),
            piece_ids=self.ids(),foods=[dict(id=path(food),phase=str(prop(food,'phase')),
                carrier=path(prop(food,'stack_carrier')),holders=[path(h) for h in prop(food,'holders')])
                for food in self.fixtures if valid(food)],brace_input_held=bool(self.grip.is_brace_input_held()),
            is_bracing=bool(self.grip.is_bracing()),brace_target=path(self.grip.brace_target()),
            selected_tool=str(prop(self.inventory,'selected')),tool_visibility=tool_visibility,
            actor_location=xyz(self.hero.get_actor_location()),bones_local_cm=bones)
        self.rows.append(row)
        return row

    def motion_summary(self):
        baseline=[row for row in self.rows if row['phase']=='idle_baseline']
        rmb=[row for row in self.rows if row['phase']=='rmb_window']
        self.require(len(baseline)>=5 and len(rmb)>=5,'Enough actual callback samples for before/after motion comparison')
        result={}
        for role in self.bones:
            def steps(rows):
                values=[]
                for previous,current in zip(rows,rows[1:]):
                    dt=current['game_seconds']-previous['game_seconds']
                    if dt>1.e-5:
                        delta=math.dist(previous['bones_local_cm'][role],current['bones_local_cm'][role])
                        values.append(dict(delta_cm=delta,dt=dt,speed_cm_s=delta/dt,wall_seconds=current['wall_seconds']))
                return values
            base_steps=steps(baseline);test_steps=steps([baseline[-1]]+rmb)
            self.require(base_steps and test_steps,'Actual positive-time bone samples')
            points=[row['bones_local_cm'][role] for row in baseline]
            span=math.dist([min(p[i] for p in points) for i in range(3)],[max(p[i] for p in points) for i in range(3)])
            speed=max(step['speed_cm_s'] for step in base_steps)
            # Compare displacement to observed idle rate at the SAME dt, plus
            # half its excursion. Scale-relative epsilon is .5% of bone radius,
            # not a fixed hand-jump threshold or a forced pose.
            radius=max(math.dist([0,0,0],point) for point in points)
            epsilon=max(radius*.005,1.e-4)
            for step in test_steps:
                step['comparative_limit_cm']=max(speed*3*step['dt'],span*.5,epsilon)
            breached=[step for step in test_steps if step['delta_cm']>step['comparative_limit_cm']]
            result[role]=dict(baseline_max_step_cm=max(step['delta_cm'] for step in base_steps),
                baseline_max_speed_cm_s=speed,baseline_span_cm=span,scale_relative_epsilon_cm=epsilon,
                rmb_max_step_cm=max(step['delta_cm'] for step in test_steps),
                rmb_max_speed_cm_s=max(step['speed_cm_s'] for step in test_steps),
                baseline_envelope_breached=bool(breached),breaches=breached)
        return result

    def finish(self,error=None):
        if self.done:return
        if self.owned_pie_is_active():
            try:
                self.rmb(False)
                if self.throw_held:self.q(False)
            except Exception:error=error or traceback.format_exc()
        self.report['samples']=self.rows
        if self.rows:
            with (self.output/'BoneSamples.csv').open('w',newline='',encoding='utf8') as stream:
                names=['wall_seconds','game_seconds','phase','rmb_requested','collecting','piece_count','piece_ids','brace_input_held','is_bracing','brace_target','selected_tool','tool_visibility']
                names += [role+'_'+axis+'_cm' for role in self.bones for axis in ('x','y','z')]
                writer=csv.DictWriter(stream,fieldnames=names);writer.writeheader()
                for sample in self.rows:
                    row={name:sample.get(name) for name in names if name in sample}
                    row['piece_count']=len(sample['piece_ids']);row['piece_ids']=json.dumps(sample['piece_ids'])
                    row['tool_visibility']=json.dumps(sample['tool_visibility'])
                    for role,value in sample['bones_local_cm'].items():
                        for axis,number in zip(('x','y','z'),value):row[role+'_'+axis+'_cm']=number
                    writer.writerow(row)
        super().finish(error)
        if not EXPECT_FIXED and error is None:
            self.report.update(status='baseline_reproduced' if self.report.get('baseline_reproduced') else 'baseline_not_reproduced',passed=False,
                verdict='Baseline recording only; this result does not claim the fix works.')
            self.write()

    def tick(self,_delta):
        if self.done or self.busy:return
        self.busy=True
        try:
            wall=time.monotonic()-self.started;self.require(wall<90,'Bounded 90-second owned solo check')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'One standalone authority PIE')
                pc=u.GameplayStatics.get_player_controller(worlds[0],0);hero=pc.get_controlled_pawn() if pc else None
                if hero is None:return
                self.require(isinstance(hero,u.MCToothCharacter),'Actual tooth pawn')
                self.world,self.pc,self.hero=worlds[0],pc,hero;self.engine.set_editor_property('bUseFixedFrameRate',False)
                self.enter('readiness');return
            self.require(len(worlds)==1 and self.owned_pie_is_active(worlds),'Only the owned PIE is observed')
            age=wall-self.mode_at
            if self.phase=='readiness':
                self.require(age<45,'Real presentation readiness is bounded')
                if not self.pc.is_world_presentation_ready() or age<2:return
                result=u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'level')
                self.report['native_fixture']=result;self.require(not result.startswith('REJECT'),'Native director-stop fixture accepted')
                self.collection=self.hero.get_component_by_class(u.MCFoodCollectionComponent)
                self.grip=self.hero.get_component_by_class(u.MCGripComponent)
                self.inventory=self.hero.get_component_by_class(u.MCInventoryComponent)
                tongues=self.actors(u.MCTongue);self.require(len(tongues)>0,'Actual tongue exists');self.tongue=tongues[0]
                mapping={str(key):str(value) for key,value in self.hero.appearance.bone_map.items()}
                self.bones={role:mapping.get(role,role) for role in ('body','hand_l','hand_r')}
                for role,name in self.bones.items():self.require(self.hero.mesh.does_socket_exist(name),'Appearance-mapped real bone: '+role)
                self.report['bones']=self.bones;self.enter('arrival_release');return
            if self.phase=='arrival_release':
                state=self.input_snapshot();self.report['arrival_observed']=state
                self.require(age<15,'Natural arrival/input release after the fixture')
                if state['reward_menu_open']:
                    self.require(age<5,'Unexpected reward menu; no automatic choice/grant');return
                if age<1 or not state['world_ready'] or any(state[key] for key in ('loading_visible','mouse_cursor','move_ignored','look_ignored','boss_intro')):return
                floor=self.floor(self.hero.get_actor_location());self.require(floor is not None,'Actual inspected tongue support')
                location=self.move_to(floor,yaw=0.)
                self.frame_camera(location+u.Vector(420,-430,225),location+u.Vector(40,0,15))
                self.report['review_camera']=dict(location=xyz(location+u.Vector(420,-430,225)),target=xyz(location+u.Vector(40,0,15)))
                for index,lateral in enumerate((-25.,0.,25.)):
                    point=location+self.hero.get_actor_forward_vector()*85+self.hero.get_actor_right_vector()*lateral
                    support=self.floor(point);self.require(support is not None,'Fragment spawn has real tongue support')
                    self.spawn_fragment(support+u.Vector(0,0,35),index)
                self.report['fixture_ids']=[path(food) for food in self.fixtures]
                self.enter('fragment_settle');return
            if self.phase=='fragment_settle' and age>.6:
                self.e();self.enter('collecting');return
            if self.phase=='collecting':
                self.require(age<8,'Ordinary E collects all three real fragments')
                if len(self.pieces())!=3:return
                self.expected_ids=self.ids();self.enter('pickup_settle');return
            if self.phase=='pickup_settle':
                self.require(self.ids()==self.expected_ids,'Three original identities survive pickup settling')
                if age<1.3:return
                self.screenshot('01_three_food_settled.png');self.enter('idle_baseline');return
            row=self.sample()
            if self.phase=='idle_baseline':
                self.require(self.ids()==self.expected_ids and bool(prop(self.collection,'collecting')),'Idle carrying baseline remains stable')
                if age>=2:
                    self.screenshot('02_idle_baseline_end.png');self.enter('rmb_window');self.rmb_started=wall;self.rmb(True)
            elif self.phase=='rmb_window':
                if wall>=self.capture_at:
                    self.capture_at=wall+1/12.;self.screenshot('%05d.png'%len(self.report['frames']),sequence=True)
                held=age<3.6 and age%1.2<.6
                self.rmb(held)
                if EXPECT_FIXED:
                    self.check('RMB retains all three original food identities',row['piece_ids']==self.expected_ids)
                    self.check('RMB leaves automatic collection enabled',row['collecting'])
                    self.check('RMB with food never enters brace intent/target',not row['brace_input_held'] and not row['is_bracing'] and row['brace_target'] is None)
                    self.check('Every original food remains carried by the player',all(food['carrier']==path(self.hero) for food in row['foods']))
                if age>=4:
                    self.rmb(False);motion=self.motion_summary();self.report['motion_comparison']=motion
                    samples=[sample for sample in self.rows if sample['phase']=='rmb_window']
                    lost=any(sample['piece_ids']!=self.expected_ids for sample in samples)
                    zero=any(len(sample['piece_ids'])==0 for sample in samples)
                    jump=any(summary['baseline_envelope_breached'] for summary in motion.values())
                    self.report['reproduction']=dict(start_count=3,minimum_count=min(len(sample['piece_ids']) for sample in samples),lost_originals=lost,reached_zero=zero,pose_jump_vs_idle=jump)
                    self.report['baseline_reproduced']=bool(lost and zero and jump)
                    self.screenshot('03_rmb_window_end.png')
                    if not EXPECT_FIXED:self.finish();return
                    self.check('RMB body and hands stay inside the comparative idle envelope',not jump)
                    self.q(True);self.q(False);self.enter('q_observed')
            elif self.phase=='q_observed':
                self.require(age<3,'Q throws one through the normal input handler')
                if len(self.pieces())!=2:return
                self.check('Q retains two original identities',set(self.ids()).issubset(set(self.expected_ids)))
                self.check('Q releases exactly one original carrier',sum(prop(food,'stack_carrier')==self.hero for food in self.fixtures)==2)
                self.screenshot('04_q_throws_one.png');self.e();self.enter('e_drop_observed')
            elif self.phase=='e_drop_observed':
                self.require(age<3,'Explicit E releases the rest')
                if self.pieces() or bool(prop(self.collection,'collecting')):return
                self.check('E explicitly drops the remaining two and stops collecting',all(prop(food,'stack_carrier')!=self.hero for food in self.fixtures))
                self.screenshot('05_e_explicit_drop.png');self.rmb(True);self.enter('empty_rmb')
            elif self.phase=='empty_rmb' and age>.3:
                self.report['empty_rmb_observed']=row
                self.check('Ordinary empty-handed RMB intent still reaches Grip',row['brace_input_held'])
                self.rmb(False);self.enter('empty_rmb_released')
            elif self.phase=='empty_rmb_released' and age>.3:
                self.check('Ordinary empty-handed RMB release clears intent',not self.grip.is_brace_input_held())
                self.screenshot('06_empty_rmb_released.png');self.finish()
        except Exception:
            self.finish(traceback.format_exc())
        finally:self.busy=False


probe=FoodRMBSolo();setattr(builtins,KEY,probe)
try:probe.start()
except Exception:
    probe.finish(traceback.format_exc());raise
