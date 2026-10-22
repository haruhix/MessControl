"""Short owned solo PIE: existing BP chest drop/open/claim and placed rearm.

Open saved L_Mouth first. Two explicitly disclosed, ownerless BP_RewardChest
fixtures use InitializeTeamStageReward, normal landing validation, ordinary
approach/menu and ChooseRewardPerk. No director stage/counter, food, reward state,
pause flag, damage, grant or claim mask is forced. Player/camera placement is a
review fixture, not a recorded natural portion completion. No packages are saved.
MC_REWARD_CHEST_COMPARE_MATERIALS defaults to False. Set True only for an
explicit old/reviewed material A/B experiment; normal video never swaps materials.
"""
import builtins
import json
import runpy
import time
import traceback
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path(globals().get('MC_REWARD_CHEST_SOLO_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestSolo')).resolve()
assert OUT.drive.upper()=='E:'
assert not (OUT/'Report.json').exists(), 'Use a fresh E: output folder; preserve previous evidence'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),
    run_name='reward_chest_solo_support',init_globals={'MC_ENV_OUTPUT':OUT,'MC_ENV_CAPTURE_HZ':0})
Base,prop,vec=(support[k] for k in ['ApprovedEnvironmentSolo','prop','vec'])
KEY='mc_reward_chest_presentation_solo'
COMPARE_MATERIALS=globals().get('MC_REWARD_CHEST_COMPARE_MATERIALS',False)
assert isinstance(COMPARE_MATERIALS,bool),'MC_REWARD_CHEST_COMPARE_MATERIALS must be a bool'
CAPTURE_HZ=float(globals().get('MC_REWARD_CHEST_CAPTURE_GAME_HZ',12.))
CAPTURE_WALL_HZ=float(globals().get('MC_REWARD_CHEST_CAPTURE_WALL_HZ',12.))
ACTIVE_BUDGET=float(globals().get('MC_REWARD_CHEST_ACTIVE_WALL_BUDGET',70.))
assert 1.<=CAPTURE_HZ<=30. and 1.<=CAPTURE_WALL_HZ<=30.,'Bounded capture rates required'
assert 30.<=ACTIVE_BUDGET<=180.,'Bounded active wall budget required'
TOTAL_BUDGET=150.+ACTIVE_BUDGET+20.

def valid(obj):
    try:return obj is not None and u.SystemLibrary.is_valid(obj)
    except (TypeError,RuntimeError,ReferenceError):return False
def path(obj): return obj.get_path_name() if valid(obj) else None

class ChestSolo(Base):
    def __init__(self):
        super().__init__()
        self.chest=self.zone=None
        self.frame_wall=0.
        self.frame_game=-1.
        self.active_started=None
        self.claimed_wall=self.opened_wall=None
        self.kind=0
        self.offered=False
        self.approached=False
        self.states=[]
        self.fixture_started=None
        self.comparison_done=not COMPARE_MATERIALS
        self.report.update(scope=__doc__,checks={},fixtures=[],samples=[],frames=[],screenshots=[],
            material_comparison_enabled=COMPARE_MATERIALS,
            capture_hz=CAPTURE_HZ,capture_clock=f'At most {CAPTURE_HZ:g} per game second and {CAPTURE_WALL_HZ:g} per wall second; no repeated PNGs while game time is paused.',
            wall_budgets=dict(startup_seconds=150.,active_seconds=ACTIVE_BUDGET,total_seconds=TOTAL_BUDGET),
            limitations=['One standalone PIE; no network or packaged-game validation.',
                'Two ownerless reward actors are explicit presentation/lifecycle fixtures.',
                'Native initialization/landing/approach/menu/claim/reset remain active.',
                ('Explicit A/B experiment: the first landed fixture briefly switches old/reviewed surfaces on identical meshes before restoring the reviewed material.'
                    if COMPARE_MATERIALS else 'Clean presentation: the authored gold material is never replaced for an A/B experiment.'),
                'Player and temporary camera are placed for review; the director is left untouched.'])
        self.report.pop('fixture_tuning',None)
        self.report.pop('tuning_reason',None)

    def write(self):
        self.output.mkdir(parents=True,exist_ok=True)
        (self.output/'Report.json').write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')

    def check(self,name,ok):
        if self.report['checks'].get(name) is True:return
        self.require(ok,name)
        self.report['checks'][name]=True

    def spawn(self,cls,pose,configure=None):
        actor=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(
            self.world,cls,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(actor),'Fixture spawn failed: '+str(cls))
        if configure: configure(actor)
        self.statics.call_method('FinishSpawningActor',args=(actor,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        return actor

    def fixture(self,placed):
        self.tongue=self.actors(u.MCTongue)[0]
        center,_,_=u.SystemLibrary.get_component_bounds(prop(self.tongue,'surface'))
        obstacles=[a.get_actor_location() for a in self.actors(u.MCFoodActor)]
        landing=None
        for x,y in [(0,0),(-500,0),(500,0),(0,500),(0,-500),(-700,350),(700,-350)]:
            candidate=self.floor(center+u.Vector(x,y,0))
            if candidate is not None and all(candidate.distance(p)>450. for p in obstacles):
                landing=candidate+u.Vector(0,0,3)
                break
        self.require(landing is not None,'No clear central-tongue review point; leave genuine food untouched')
        far=self.floor(landing+u.Vector(850,0,0))
        self.require(far is not None,'No supported camera/worker staging point')
        self.move_to(far)
        self.landing=landing
        self.zone=self.spawn(u.MCRewardDropZone,u.Transform(location=landing))
        self.zone.set_editor_properties(dict(allowed_landing_surface=self.tongue,
            allow_reward_drops=False,inward_margin=20.))
        prop(self.zone,'area').set_box_extent(u.Vector(500,500,400),False)
        table=prop(self.pc.player_state,'perks').get_perk_table()
        self.require(valid(table),'Real player perk table is not loaded')
        bp=u.load_asset('/Game/Gameplay/Roguelike/BP_RewardChest')
        self.require(valid(bp),'Existing BP_RewardChest is missing')
        pose=u.Transform(location=landing)
        def initialize(chest):
            chest.set_editor_property('placed_reward',placed)
            chest.initialize_team_stage_reward(landing,landing+u.Vector(0,0,450),7619+self.kind,table,self.zone)
        self.chest=self.spawn(bp.generated_class(),pose,initialize)
        override=prop(self.chest,'surface_material_override')
        self.check('surface material matches its explicit authored override',valid(override)
            and prop(self.chest,'body').get_material(0)==override and prop(self.chest,'lid').get_material(0)==override)
        self.check('team stage reward starts falling immediately without a telegraph wait',
            prop(self.chest,'stage')==u.MCRewardChestStage.FALLING)
        self.check('team stage reward is airborne before its first presented frame',
            self.chest.get_actor_location().distance(landing)>400.)
        self.check('no ground landing marker or claim ring is rendered',
            not prop(self.chest,'telegraph').is_visible() and not prop(self.chest,'claim_ring').is_visible())
        if self.active_started is None:self.active_started=time.monotonic()
        self.check('fixture uses the saved BP_RewardChest',self.chest.get_class().get_path_name().startswith('/Game/Gameplay/Roguelike/BP_RewardChest.'))
        self.frame_camera(landing+u.Vector(450,-560,295),landing+u.Vector(0,0,90))
        self.camera.camera_component.set_field_of_view(50.)
        self.states=[];self.approached=False;self.offered=False
        self.claimed_wall=self.opened_wall=None
        self.fixture_started=float(u.GameplayStatics.get_time_seconds(self.world))
        self.report['fixtures'].append(dict(placed=placed,chest=path(self.chest),owner=path(self.chest.get_owner()),
            landing_cm=vec(landing),table=path(table),states=self.states,
            body_mesh=path(prop(self.chest,'body').static_mesh),lid_mesh=path(prop(self.chest,'lid').static_mesh),
            body_material=path(prop(self.chest,'body').get_material(0)),
            surface_material_override=path(override),initial_stage=str(prop(self.chest,'stage')),
            initial_choices=int(prop(self.pc.player_state,'personal_choices_taken'))))
        self.stage('placed_drop' if placed else 'spawned_drop')

    def capture(self,force=False):
        wall=time.monotonic()
        game=float(u.GameplayStatics.get_time_seconds(self.world))
        if not force and (wall<self.frame_wall or game<self.frame_game):return
        self.frame_wall=wall+1./CAPTURE_WALL_HZ
        self.frame_game=game+1./CAPTURE_HZ
        self.screenshot('%05d.png'%len(self.report['frames']),True)

    def tick(self,_dt):
        if self.done:return
        try:
            wall=time.monotonic()
            self.require(wall-self.started<TOTAL_BUDGET,'Chest solo exceeded total wall budget')
            if self.active_started is None:
                self.require(wall-self.started<150.,'Chest solo startup exceeded its separate wall budget')
            else:
                self.require(wall-self.active_started<ACTIVE_BUDGET,'Chest solo active check exceeded its separate wall budget')
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:return
                self.require(len(worlds)==1,'Preserve unrelated PIE worlds')
                # Claim the explicitly owned world before a pawn appears. If
                # startup fails, the inherited cleanup can still end our PIE.
                self.world=worlds[0]
                self.stage('waiting_pawn')
            self.require(self.owned_pie_is_active(worlds),'Owned PIE ended or changed')
            if self.hero is None:
                pc=u.GameplayStatics.get_player_controller(self.world,0)
                hero=pc.get_controlled_pawn() if valid(pc) else None
                if not isinstance(hero,u.MCToothCharacter):return
                self.pc,self.hero=pc,hero
                self.stage('natural_startup')
            self.require(valid(self.hero) and self.pc.get_controlled_pawn()==self.hero,
                'The reviewed pawn was replaced or destroyed; preserve the new pawn')
            now=float(u.GameplayStatics.get_time_seconds(self.world))
            if self.phase=='natural_startup':
                # Preserve the native preload/arrival barrier and initial input.
                if not self.pc.is_world_presentation_ready():return
                self.check('no free perk menu before a chest',not self.pc.is_reward_menu_open())
                if prop(self.hero.get_movement_component(),'movement_mode')!=u.MovementMode.MOVE_WALKING:return
                self.check('one standalone authority world',u.SystemLibrary.is_server(self.world) and len(worlds)==1)
                self.fixture(False)
                return
            self.pc.set_view_target_with_blend(self.camera,0.)
            self.capture()
            if valid(self.chest):
                state=str(prop(self.chest,'stage')).split('.')[-1].split(':')[0].strip(' <>').upper()
                if state not in self.states:self.states.append(state)
                self.report['samples'].append(dict(time=now,phase=self.phase,state=state,
                    actor=path(self.chest),location_cm=vec(self.chest.get_actor_location()),
                    visual_scale=vec(prop(self.chest,'presentation_pivot').relative_scale3d),
                    collision=str(prop(self.chest,'solid').get_collision_enabled()),
                    hidden=bool(prop(self.chest,'hidden')),claim_mask=int(prop(self.chest,'claimed_mask'))))
                if self.phase=='placed_reset':
                    if now-self.phase_at>=.7:
                        self.check('placed reset is visible, solid and landed without automatic reopening',
                            not bool(prop(self.chest,'hidden')) and prop(self.chest,'stage')==u.MCRewardChestStage.LANDED
                            and prop(self.chest,'solid').get_collision_enabled()==u.CollisionEnabled.QUERY_AND_PHYSICS)
                        self.check('placed reset returns deformation to unit scale',
                            prop(self.chest,'presentation_pivot').relative_scale3d.distance(u.Vector(1,1,1))<.001)
                        self.finish()
                    return
                if self.phase=='old_gold_comparison':
                    if now-self.phase_at>=.4:
                        self.screenshot('01_original_gold_material_same_mesh.png')
                        for component,material in self.compare_materials:
                            component.set_material(0,material)
                        self.stage('new_gold_comparison')
                    return
                if self.phase=='new_gold_comparison':
                    if now-self.phase_at>=.4:
                        self.screenshot('02_reviewed_gold_material_same_mesh.png')
                        self.comparison_done=True
                        self.stage('spawned_drop')
                    return
                if state=='LANDED' and not self.approached and now-float(prop(self.chest,'stage_started_at'))>=.75:
                    self.check('native landing settled before interaction',
                        prop(self.chest,'presentation_pivot').relative_scale3d.distance(u.Vector(1,1,1))<.02)
                    if COMPARE_MATERIALS and self.kind==0 and not self.comparison_done:
                        original=u.load_asset('/Game/Gameplay/VFX/RewardChest/MI_RewardChest_Glow')
                        self.require(valid(original),'Original gold material is missing')
                        self.compare_materials=[(prop(self.chest,key),prop(self.chest,key).get_material(0)) for key in ['body','lid']]
                        self.report['gold_surface_comparison']=dict(original_material=path(original),
                            reviewed_materials=[path(m) for _,m in self.compare_materials],
                            same_actor=path(self.chest),temporary_only=True)
                        for component,_ in self.compare_materials:component.set_material(0,original)
                        self.stage('old_gold_comparison')
                        return
                    near=self.floor(self.landing+u.Vector(205,0,0))
                    self.require(near is not None,'No supported approach point')
                    self.move_to(near,180.)
                    self.approached=True
                if state=='OPEN' and self.pc.is_reward_menu_open():
                    if self.opened_wall is None:
                        self.opened_wall=time.monotonic()
                        # One explicit menu frame retains the paused UI in the
                        # video without writing repeated paused PNGs every tick.
                        self.capture(force=True)
                    if not self.offered and time.monotonic()-self.opened_wall>=.6:
                        self.offered=True
                        ids=[str(n) for n in self.chest.get_editor_property('LootIDs')]
                        target='Fallback_Mouthwash' if self.kind==0 else 'Fallback_Grenade'
                        self.require(target in ids,'Normal offer did not contain the chosen shared booster')
                        self.report['fixtures'][-1]['choice']=dict(id=target,index=ids.index(target),method='ChooseRewardPerk')
                        self.pc.choose_reward_perk(ids.index(target))
                if state=='EXHAUSTED':
                    self.check('normal card API seals one shared reward',int(prop(self.chest,'claimed_mask'))==7)
                    self.check('exhausted releases solid collision immediately',
                        prop(self.chest,'solid').get_collision_enabled()==u.CollisionEnabled.NO_COLLISION)
                    if self.claimed_wall is None:self.claimed_wall=time.monotonic()
                    if self.kind==1 and bool(prop(self.chest,'hidden')):
                        self.check('placed chest hides after its short exit while retaining the same actor',valid(self.chest))
                        far=self.floor(self.landing+u.Vector(850,0,0));self.require(far is not None,'Reset staging floor vanished')
                        self.move_to(far)
                        self.chest.reset_placed_reward()
                        self.stage('placed_reset')
                self.require(now-self.fixture_started<20.,'Native chest did not reach/finish a normal claim')
            else:
                self.require(self.kind==0 and self.offered and self.claimed_wall is not None,
                    'Chest disappeared before an accepted card claim')
                self.check('spawned reward actor is removed after the exit',True)
                self.check('normal UI closes after claim',not self.pc.is_reward_menu_open())
                if valid(self.zone):self.zone.destroy_actor()
                self.kind=1
                self.fixture(True)
        except Exception:
            self.finish(traceback.format_exc())

old=getattr(builtins,KEY,None)
assert old is None or getattr(old,'done',True),'Preserve an active chest probe'
probe=ChestSolo();setattr(builtins,KEY,probe)
try:probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
