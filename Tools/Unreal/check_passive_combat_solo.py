"""Owned, bounded solo presentation of cleave, echo tool, lightning and billiards.

Prepare/run only after the coordinated current Editor build and saved catalog.
Requires idle clean L_Mouth, -MCHubOwned -MCHubTransientProfile -NoVoice.
This file does not launch Editor, build, save a package, change an open map,
touch profiles, transmit voice, test networking, or call combat subsystem/cue APIs.

Disclosed fixtures: native review_rogue_gameplay('level') stops directors and
resets the initial pawn/status. Real MCNutEnemy and whole-food actors are placed
on the supported tongue; high HP and stationary target ticks isolate visibility.
Normal TryAcquirePerk grants and SetPrimaryInputHeld LMB edges drive combat.
Thunder keeps the saved 20% chance: up to 24 real accepted attacks are attempted,
and an unobserved proc is reported as inconclusive, never forced or called passed.

Frames are genuine owned PIE SWindow/game-camera/HUD screenshots. A temporary
review CameraActor supplies a fixed close angle; no SceneCapture/composited art.
Cancel: builtins.mc_combat_passives_solo.finish('cancelled explicitly').
"""
import builtins
import datetime
import hashlib
import json
import math
import os
import runpy
import time
import traceback
import uuid
from pathlib import Path

import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
RUN_ID=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'_'+uuid.uuid4().hex[:8]
OUT=Path(globals().get('MC_COMBAT_SOLO_OUTPUT','E:/CodexWork/MessControl/Validation/PassivesCombat/'+RUN_ID)).resolve()
TEMP=Path('E:/CodexTemp/MessControl/PassivesCombat')/RUN_ID
KEY='mc_combat_passives_solo'
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol' and OUT.drive.upper()=='E:'
command=u.SystemLibrary.get_command_line().lower()
assert all(flag in command for flag in ('-mchubowned','-mchubtransientprofile','-novoice')), 'Owned/transient/no-voice launch markers required'
assert not OUT.exists(), 'Use a fresh MC_COMBAT_SOLO_OUTPUT directory; preserve even partial previous evidence'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved packages'
previous=getattr(builtins,KEY,None)
assert previous is None or getattr(previous,'done',True), 'Combat probe already active'
SOURCES=('Source/MessControl/Private/MCCombatPassiveSubsystem.cpp','Source/MessControl/Public/MCCombatPassiveSubsystem.h',
    'Source/MessControl/Private/MCPassiveCombatCue.cpp','Source/MessControl/Public/MCPassiveCombatCue.h',
    'Source/MessControl/Private/MCToothCharacter.cpp','Source/MessControl/Private/MCInventoryUpgrades.cpp',
    'Source/MessControl/Private/MCNutEnemy.cpp','Source/MessControl/Private/MCNutBoss.cpp',
    'Source/MessControl/Private/MCNutKamikaze.cpp','Source/MessControl/Private/MCCataclysmJelly.cpp',
    'Source/MessControl/Private/MCRoguePerks.cpp','Source/MessControl/Private/MCPerkComponent.cpp','Source/MessControl/Public/MCPerkTypes.h')
DLL=ROOT/'Binaries/Win64/UnrealEditor-MessControl.dll'
assert DLL.is_file(), 'Current runtime Editor DLL is missing'
assert all((ROOT/name).is_file() and (ROOT/name).stat().st_mtime<=DLL.stat().st_mtime+1 for name in SOURCES), 'A combat source is newer than runtime DLL; coordinate build first'
FRESHNESS=dict(dll=str(DLL),dll_mtime_ns=DLL.stat().st_mtime_ns,dll_sha256=hashlib.sha256(DLL.read_bytes()).hexdigest(),
    source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in SOURCES})
TEMP.mkdir(parents=True,exist_ok=True)
os.environ['TEMP']=os.environ['TMP']=str(TEMP)
support=runpy.run_path(str(ROOT/'Tools/Unreal/check_approved_environment_solo.py'),run_name='combat_passive_support',
    init_globals={'MC_ENV_OUTPUT':OUT,'MC_ENV_CAPTURE_HZ':0})
Base,prop=support['ApprovedEnvironmentSolo'],support['prop']
# The screenshot probe already restores these owned PIE settings. A fixed
# client FPS and synchronous capture coincided with slow native contact/lifespan
# progress in Solo05; use ordinary variable-rate PIE and log actual deltas.
support['PLAY']['clientFixedFPS']=[0]

def valid(obj): return obj is not None and u.SystemLibrary.is_valid(obj)
def path(obj): return obj.get_path_name() if valid(obj) else None
def xyz(value): return [float(value.x),float(value.y),float(value.z)]


class CombatPassivesSolo(Base):
    def __init__(self):
        super().__init__()
        self.report.update(scope=__doc__,checks={},stages=[],input_requests=[],samples=[],grants=[],clips={},
            binary_freshness=FRESHNESS,
            recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            source_bindings='MCToothCharacter: BrushAction maps LeftMouseButton; public SetPrimaryInputHeld -> StartPrimary -> ResolvePrimaryAction -> authoritative normal swing',
            capture_clock_policy='Owned PIE clientFixedFPS=0; global fixed-frame rate disabled once world exists, then restored by Base. Each primary request waits for actual accepted HP feedback before another request.',
            fixture_tuning={'enemy_max_health':1000,'enemy_radius_cm':30,'enemy_move_speed_cm_s':40,
                'stationary_targets':'Actor ticks disabled for cleave/echo/thunder only; native health/contact/damage/feedback paths remain real',
                'food_health':1000,'food_spoil_seconds':600,'food_physics':'Disabled only to retain visible chain endpoints',
                'billiards':'New small enemies; pushed target actor tick enabled for native launch motion, stationary collision target tick disabled'},
            limitations=['Diagnostic high-HP targets and director-stop review fixture, not a full earned combat run.',
                'LMB native input handler edges are exercised; physical mouse hardware/viewport focus are not tested.',
                'ValidatedSwingCount/ConfirmedHitCount are non-UPROPERTY native fields and are not guessed through Python; accepted attacks use actual target HP reductions.',
                'Normal 20% thunder is probabilistic. No proc in 24 accepted attempts is inconclusive, not proof of a defect or success.',
                'Exact damage/timing bounds rely on focused native tests; this probe checks real grants, accepted input, visible cues and ordinary movement consequences.',
                'Dash/first-hit gameplay is not included in this short stationary-target clip; native tests cover its exact consumption/window rules.',
                'Dedicated-server visual snapshot, networking, microphone and packaged first-launch/PSO behavior remain outside scope.',
                'Frames use actual timestamps; art quality requires human/frame review.'])
        self.report['checks']['launch_markers_present']=True
        self.busy=False;self.mode_at=0.;self.capture_at=self.sample_at=0.;self.tap_at=0.;self.taps=0
        self.inventory=self.perks=self.floor_origin=None;self.enemies=[];self.foods=[];self.fixtures=[]
        self.cues_seen={};self.hp_before={};self.accepted=0;self.last_health=None;self.thunder_start_taps=0
        self.cleave_accepted_at=self.billiards_accepted_at=None
        self.pending_attack=None;self.previous_enemy_health={};self.old_combo_debug=None
        self.cue_class=u.load_class(None,'/Script/MessControl.MCPassiveCombatCue')
        self.require(self.cue_class is not None,'Current native MCPassiveCombatCue class is available; do not use a stale binary')
        (self.output/'README.md').write_text(__doc__+'\n\nRun this prepared recipe through Unreal Python after the root-coordinated build. '
            'The default creates a unique UTC-named E: validation directory; an explicit override must also be fresh. '
            'Report.json records actual execution status and frame hashes.\n',encoding='utf8')

    def write(self):
        self.output.mkdir(parents=True,exist_ok=True)
        (self.output/'Report.json').write_text(json.dumps(self.report,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf8')

    def check(self,name,ok):
        self.report['checks'][name]=bool(ok);self.require(ok,name)

    def enter(self,phase):
        self.phase=phase;self.mode_at=time.monotonic()-self.started;self.tap_at=0.;self.taps=0
        self.pending_attack=None;self.previous_enemy_health={path(actor):float(prop(actor,'health')) for actor in self.enemies if valid(actor)}
        self.report['stages'].append(dict(name=phase,wall_seconds=self.mode_at,game_seconds=self.now()))
        self.report['clips'].setdefault(phase,[]);self.write()

    def now(self): return float(u.GameplayStatics.get_time_seconds(self.world))

    def input_ready(self):
        loading=[widget for widget in u.ObjectIterator(u.MCLoadingWidget) if widget.get_owning_player()==self.pc
            and widget.is_in_viewport() and widget.get_visibility() not in (u.SlateVisibility.HIDDEN,u.SlateVisibility.COLLAPSED)]
        result=dict(ready=bool(self.pc.is_world_presentation_ready()),reward=bool(self.pc.is_reward_menu_open()),
            loading=bool(loading),cursor=bool(prop(self.pc,'show_mouse_cursor')),move_ignored=bool(self.pc.is_move_input_ignored()),
            look_ignored=bool(self.pc.is_look_input_ignored()),intro=bool(self.pc.is_boss_intro_playing()))
        self.report['last_input_gates']=result
        return result['ready'] and not any(result[key] for key in ('reward','loading','cursor','move_ignored','look_ignored','intro'))

    def grant(self,*ids):
        for ident in ids:
            accepted=bool(self.perks.try_acquire_perk(ident))
            owned=[str(value) for value in prop(self.perks,'passive_perk_ids')]
            self.report['grants'].append(dict(id=ident,accepted=accepted,owned=owned,time=self.now()))
            self.check('Real acquisition accepted '+ident,accepted and ident in owned)

    def remove(self,*ids):
        for ident in ids: self.check('Real removal accepted '+ident,bool(self.perks.remove_perk(ident)))

    def defer(self,cls,pose):
        actor=self.statics.call_method('BeginDeferredActorSpawnFromClass',args=(self.world,cls.static_class(),pose,
            u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.require(valid(actor),'Diagnostic production actor allocation: '+str(cls));return actor

    def finish_spawn(self,actor,pose):
        self.statics.call_method('FinishSpawningActor',args=(actor,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));self.fixtures.append(actor)

    def enemy(self,forward,lateral,active=False):
        support=self.floor(self.floor_origin+u.Vector(forward,lateral,0));self.require(support is not None,'Enemy has actual tongue support')
        pose=u.Transform(location=support+u.Vector(0,0,34));actor=self.defer(u.MCNutEnemy,pose)
        settings=u.MCNutEnemySettings();settings.set_editor_properties(dict(MaxHealth=1000.,BodyRadius=30.,MoveSpeed=40.))
        mesh=u.load_asset('/Game/Gameplay/CoreLoop/SM_Walnut_Whole');self.require(mesh is not None,'Existing saved walnut mesh')
        scale=60./(2*mesh.get_bounds().box_extent.z)
        actor.set_editor_properties(dict(Tongue=self.tongue,Settings=settings,Health=1000.,NutMesh=mesh,MeshScale=u.Vector(scale,scale,scale)))
        self.finish_spawn(actor,pose);actor.set_actor_tick_enabled(bool(active));self.enemies.append(actor);return actor

    def spawn_food_fixture(self,forward,lateral):
        support=self.floor(self.floor_origin+u.Vector(forward,lateral,0));self.require(support is not None,'Food endpoint has actual tongue support')
        pose=u.Transform(location=support+u.Vector(0,0,30));actor=self.defer(u.MCFoodActor,pose)
        row=prop(actor,'food_data');row.set_editor_properties(dict(kind=u.MCFoodKind.FOOD,scale=u.Vector(.25,.25,.25),health=1000.,spoil_seconds=600.))
        actor.set_editor_properties(dict(food_data=row,item_name='CombatProbeWholeFood',item_mesh=u.load_asset('/Game/Stylized_Vegetables/Meshes/SM_Broccoli'),
            fragment=False,spoiled=False,batch=281028,health=1000.))
        self.finish_spawn(actor,pose);prop(actor,'body').set_simulate_physics(False);actor.set_actor_tick_enabled(False)
        self.foods.append(actor);return actor

    def setup(self):
        rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(self.perks.get_perk_table()))
        thunder=next(row for row in rows if row['Name']=='B2_ThunderGod')
        raw=thunder.get('Parameters')
        self.require(isinstance(raw,dict) and raw,'Saved thunder parameters are present')
        # UE5.8 DataTable JSON currently wraps each TMap<FName,float> value
        # under its map-field name. This exact encoding was inspected in
        # SavedParameterEncoding.json; do not guess other wrappers/defaults.
        parameters={}
        for key,value in raw.items():
            self.require(isinstance(value,dict) and set(value)=={'Parameters'},'Unexpected saved parameter encoding: '+repr((key,value)))
            number=value['Parameters']
            self.require(type(number) in (int,float) and math.isfinite(number),'Saved parameter must contain one finite actual number: '+repr((key,value)))
            parameters[key]=float(number)
        self.require('Chance' in parameters,'Saved thunder Chance is required; no fallback or guarantee')
        chance=parameters['Chance']
        self.report['saved_thunder_parameters']=dict(raw=raw,parsed=parameters,effective_chance=chance,native_fallback_used=False)
        self.check('Saved thunder chance is ordinary twenty percent',abs(chance-.2)<.000001)
        self.floor_origin=self.floor(self.hero.get_actor_location());self.require(self.floor_origin is not None,'Real supported tongue under reviewed spawn')
        self.move_to(self.floor_origin,yaw=0.)
        # Remove only nearby diagnostic-run obstacles in this owned PIE, never saved packages.
        for actor in self.actors(u.MCFoodActor):
            if actor.get_actor_location().distance(self.floor_origin)<900: actor.destroy_actor()
        location=self.hero.get_actor_location();camera=location+u.Vector(-230,-340,175);target=location+u.Vector(90,0,-10)
        self.frame_camera(camera,target);prop(self.camera,'camera_component').set_editor_property('field_of_view',55.)
        self.report['review_camera']=dict(location=xyz(camera),target=xyz(target),fov=55,
            policy='Actual game camera/view target and HUD; native Hero-forward fallback is used, no attack aim snapshot injected')
        self.enemy(108,-64);self.enemy(137,68)
        self.spawn_food_fixture(260,0);self.spawn_food_fixture(465,0);self.spawn_food_fixture(670,0)
        self.hp_before={path(actor):float(prop(actor,'health')) for actor in self.enemies+self.foods}
        self.grant('B2_MightyTooth','B2_EchoSwing');self.enter('cleave_echo')

    def tap(self):
        self.require(self.input_ready(),'Normal input/readiness gates remain released before a real tap')
        self.primary_input(False);self.primary_input(True);self.primary_input(False)
        self.report['input_requests'].append(dict(input='LMB',method='SetPrimaryInputHeld false/true/false',phase=self.phase,
            time=self.now(),wall_seconds=time.monotonic()-self.started,health_before=[float(prop(actor,'health')) for actor in self.enemies if valid(actor)]))
        self.pending_attack=len(self.report['input_requests'])-1
        self.taps+=1

    def collect_enemy_sample(self,actor):
        feedback=actor.get_component_by_class(u.MCCombatFeedbackComponent)
        hit=prop(feedback,'last_hit') if feedback is not None else None
        accepted_hit=None if hit is None else dict(serial=int(prop(hit,'serial')),server_time=float(prop(hit,'server_time')),
            source=path(prop(hit,'source')),health_damage=float(prop(hit,'health_damage')),shield_damage=float(prop(hit,'shield_damage')))
        return dict(id=path(actor),health=float(prop(actor,'health')),position=xyz(actor.get_actor_location()),last_accepted_hit=accepted_hit)

    def sample(self):
        cues=[]
        for cue in self.actors(self.cue_class):
            ident=path(cue);kind=str(prop(cue,'kind'));parts=[]
            for part in prop(cue,'tool_parts'):
                parts.append(dict(mesh=path(prop(part,'mesh')),captured_contact_pose=str(prop(part,'contact_pose'))))
            item=dict(id=ident,kind=kind,origin=xyz(cue.get_actor_location()),started=float(prop(cue,'started_at')),
                tool_parts=parts,ghost_components=[dict(id=path(mesh),mesh=path(prop(mesh,'static_mesh')),
                    transform=str(mesh.get_world_transform()),visible=bool(mesh.is_visible())) for mesh in prop(cue,'ghost_meshes')])
            cues.append(item);self.cues_seen[ident]=item
        row=dict(time=self.now(),world_delta_seconds=float(u.GameplayStatics.get_world_delta_seconds(self.world)),
            wall_seconds=time.monotonic()-self.started,phase=self.phase,hero=xyz(self.hero.get_actor_location()),
            hero_health=float(prop(prop(prop(self.hero,'status'),'state'),'health')),selected=str(prop(self.inventory,'selected')),
            hero_forward=xyz(self.hero.get_actor_forward_vector()),body_state=str(prop(self.hero,'tooth_physics').get_body_state()),
            tool_components=[dict(id=path(component),name=component.get_name(),mesh=path(prop(component,'static_mesh')),visible=bool(component.is_visible()),
                transform=str(component.get_world_transform())) for component in self.hero.get_components_by_class(u.StaticMeshComponent)
                if component.get_name() in ('InventoryTool','InventoryDetail','Brush')],
            enemies=[self.collect_enemy_sample(actor) for actor in self.enemies if valid(actor)],
            foods=[dict(id=path(actor),health=float(prop(actor,'health')),position=xyz(actor.get_actor_location())) for actor in self.foods if valid(actor)],cues=cues)
        current={item['id']:item['health'] for item in row['enemies']}
        changed=[item for item in row['enemies'] if item['health']<self.previous_enemy_health.get(item['id'],item['health'])-.001]
        if changed:
            # An accepted ordinary hit proves MCMeleeCombo::Supports registered
            # this existing diagnostic CVar. Never issue an unknown command first.
            if self.old_combo_debug is None:
                self.old_combo_debug=u.SystemLibrary.get_console_variable_int_value('mc.Combo.Debug');self.console('mc.Combo.Debug 1')
            if self.phase=='thunder':self.accepted+=1
            if self.pending_attack is not None:
                request=self.report['input_requests'][self.pending_attack]
                request['accepted_observation']=dict(time=row['time'],wall_seconds=row['wall_seconds'],enemies=changed,
                    input_to_observation_seconds=row['time']-request['time'])
                self.pending_attack=None;self.tap_at=time.monotonic()-self.started+.9
        self.previous_enemy_health=current
        self.report['samples'].append(row);return row

    def seen(self,needle):
        return [item for item in self.cues_seen.values() if needle in item['kind'].lower().replace('_','')]

    def finish(self,error=None):
        if self.done:return
        if self.old_combo_debug is not None:
            self.console('mc.Combo.Debug '+str(self.old_combo_debug));self.old_combo_debug=None
        self.report['cue_identities']=list(self.cues_seen.values());self.report['accepted_thunder_attacks']=self.accepted
        self.report['thunder_proc_observed']=bool(self.seen('lightning'))
        super().finish(error)
        if error is None and not self.report['thunder_proc_observed']:
            self.report.update(status='partial_inconclusive',passed=False,error=None,
                verdict='Real cleave/echo/billiards checked, but normal probabilistic thunder was not observed in the bounded attempts.')
            self.write()

    def tick(self,_delta):
        if self.done or self.busy:return
        self.busy=True
        try:
            wall=time.monotonic()-self.started;self.require(wall<115,'Probe remains below 120-second wall budget including cleanup')
            if self.pie_start is not None:self.require(not prop(self.pie_start,'Error'),'StartPIE: '+str(prop(self.pie_start,'Error')))
            worlds=list(u.EditorLevelLibrary.get_pie_worlds(True))
            if self.world is None:
                if not worlds:return
                self.require(len(worlds)==1 and u.SystemLibrary.is_server(worlds[0]),'Exactly one standalone authority PIE')
                pc=u.GameplayStatics.get_player_controller(worlds[0],0);hero=pc.get_controlled_pawn() if pc else None
                if hero is None:return
                self.require(isinstance(hero,u.MCToothCharacter),'Actual tooth pawn')
                self.world,self.pc,self.hero=worlds[0],pc,hero
                tongues=self.actors(u.MCTongue);self.require(len(tongues)==1,'One actual arena tongue');self.tongue=tongues[0]
                self.engine.set_editor_property('bUseFixedFrameRate',False);self.enter('readiness');return
            self.require(len(worlds)==1 and self.owned_pie_is_active(worlds),'Only owned PIE remains active')
            age=wall-self.mode_at
            if self.phase=='readiness':
                self.require(age<45,'Readiness is bounded')
                if age<2 or not self.pc.is_world_presentation_ready():return
                result=u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'level');self.report['review_result']=result
                self.require(not result.startswith('REJECT'),'Native level review fixture accepted')
                state=u.GameplayStatics.get_player_state(self.world,0);self.perks=state.get_component_by_class(u.MCPerkComponent)
                self.inventory=self.hero.get_component_by_class(u.MCInventoryComponent)
                self.require(self.perks is not None and self.inventory is not None,'Actual perk/inventory components')
                self.perks.reset_perks();self.enter('arrival_release');return
            if self.phase=='arrival_release':
                self.require(age<18,'Natural arrival/input release is bounded')
                if self.pc.is_reward_menu_open():self.pc.choose_reward_perk(0);return
                if age<1 or not self.input_ready():return
                self.inventory.server_select(u.MCToolSlot.KNIFE);self.enter('selection');return
            if self.phase=='selection':
                self.require(age<6,'Ordinary native tool selection is bounded')
                if prop(self.inventory,'selected')!=u.MCToolSlot.KNIFE:
                    if wall>=self.tap_at:self.inventory.server_select(u.MCToolSlot.KNIFE);self.tap_at=wall+.25
                    return
                if age>.5:self.setup()
                return
            row=self.sample()
            if wall>=self.capture_at:
                self.capture_at=wall+1./12
                self.screenshot('%05d.png'%len(self.report['frames']),sequence=True)
                self.report['clips'][self.phase].append(len(self.report['frames'])-1)
            if self.phase=='cleave_echo':
                if self.taps<1 and self.pending_attack is None:self.tap()
                if self.cleave_accepted_at is None and all(float(prop(actor,'health'))<self.hp_before[path(actor)] for actor in self.enemies):
                    self.cleave_accepted_at=self.now();self.report['cleave_acceptance_observed']=dict(time=self.now(),wall_seconds=wall,enemies=row['enemies'])
                self.require(age<12,'Bounded cleave/echo observation after actual accepted contact')
                # Solo03's first real hit arrived on the old fixed phase boundary.
                # Observe the full native .5s delay after actual HP acceptance.
                if self.cleave_accepted_at is not None and self.now()-self.cleave_accepted_at>=1.2:
                    self.check('Both diagnostic enemies accepted real cleaving swings',all(float(prop(actor,'health'))<self.hp_before[path(actor)] for actor in self.enemies))
                    self.check('Actual native ghost tool cue observed',bool(self.seen('echotool')) and any(item['tool_parts'] for item in self.seen('echotool')))
                    self.screenshot('01_cleave_echo.png');self.remove('B2_MightyTooth','B2_EchoSwing');self.grant('B2_ThunderGod')
                    self.last_health=float(prop(self.enemies[0],'health'));self.enter('thunder')
            elif self.phase=='thunder':
                if self.taps<24 and self.pending_attack is None and wall>=self.tap_at:self.tap()
                if self.pending_attack is not None:
                    request=self.report['input_requests'][self.pending_attack]
                    self.require(self.now()-request['time']<8,'One normal primary request must actually resolve before issuing another')
                if self.pending_attack is None and ((self.seen('lightning') and self.accepted>=6 and age>7) or age>=27):
                    self.check('Multiple normal LMB attacks accepted during probabilistic trial',self.accepted>=6)
                    self.report['thunder_trial']=dict(input_taps=self.taps,accepted_attacks=self.accepted,normal_catalog_chance=.2,
                        observed=bool(self.seen('lightning')),guarantee=False)
                    if self.seen('lightning'):self.check('Visible native lightning corresponds to real food HP loss',any(float(prop(food,'health'))<self.hp_before[path(food)] for food in self.foods))
                    self.screenshot('02_thunder_trial.png');self.remove('B2_ThunderGod')
                    for actor in self.fixtures:
                        if valid(actor):actor.destroy_actor()
                    self.fixtures=[];self.enemies=[];self.foods=[];self.grant('B2_Billiards')
                    self.move_to(self.floor_origin,yaw=0.);self.enemy(108,0,True);self.enemy(258,0,False)
                    self.hp_before={path(actor):float(prop(actor,'health')) for actor in self.enemies};self.enter('billiards')
            elif self.phase=='billiards':
                if self.taps<1:self.tap()
                if self.billiards_accepted_at is None and float(prop(self.enemies[0],'health'))<self.hp_before[path(self.enemies[0])]:
                    self.billiards_accepted_at=self.now();self.report['billiards_acceptance_observed']=dict(time=self.now(),enemies=row['enemies'])
                self.require(age<9,'Bounded native billiards contact observation')
                if self.billiards_accepted_at is not None and self.now()-self.billiards_accepted_at>1.2:
                    self.check('Real primary hit damaged pushed small enemy',float(prop(self.enemies[0],'health'))<self.hp_before[path(self.enemies[0])])
                    self.check('Native launch collision damaged the second enemy',float(prop(self.enemies[1],'health'))<self.hp_before[path(self.enemies[1])])
                    self.screenshot('03_billiards.png');self.finish()
        except Exception:self.finish(traceback.format_exc())
        finally:self.busy=False


if globals().get('MC_COMBAT_SOLO_RUN',True):
    probe=CombatPassivesSolo();setattr(builtins,KEY,probe)
    try:probe.start()
    except Exception:
        probe.finish(traceback.format_exc());raise
