"""Focused ReturnTrip rerun. Add an observer to the existing approved solo
manual-intake recipe's focused E/Q/held-Q path. Uses real collection, launch,
trajectory, aperture collision, swallow, AwardDelivery and native timer refresh.
End after the three real deliveries and the last two-second buff expires. The
separate legacy visual-replay/overboard/spasm suite is outside this passive check.
Check actual ambient suction getters; record the supported cosmetic intake
vortex separately. Preserve the failed 094008 legacy assertion report.
No direct delivery callbacks or timer writes. One owned standalone PIE only.
Wait for the coordinated build exposing existing DeliverySpeedUntil as read-only
HUD state; read visibility is checked in setup before the first real delivery.
Current startup waits for natural world/loading/input gates. It never waits for,
creates or chooses the retired initial-perk menu. The failed091921Z run remains
separate evidence. Review('level') is the disclosed native director-stop fixture.
"""
import builtins
import hashlib
import json
import math
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path('E:/CodexWork/MessControl/Validation/PassivesDelivery_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol' and OUT.drive.upper()=='E:'
assert not OUT.exists(),'Preserve existing evidence'
assert not u.GameUserSettings.get_game_user_settings().get_voice_enabled(),'Probe process must keep microphone disabled'
dll=ROOT/'Binaries/Win64/UnrealEditor-MessControl.dll'
assert dll.stat().st_mtime_ns>=max((ROOT/s).stat().st_mtime_ns for s in (
    'Source/MessControl/Private/MCRoguePerks.cpp','Source/MessControl/Public/MCPerkComponent.h',
    'Source/MessControl/Private/MCThroat.cpp','Source/MessControl/Public/MCThroat.h',
    'Source/MessControl/Private/MCFoodActor.cpp','Source/MessControl/Private/MCToothMovementComponent.cpp')),'Rebuild current ordinary Editor DLL first'
previous=getattr(builtins,'mc_manual_intake_solo',None)
assert previous is None or previous.done,'Preserve another active probe'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Preserve unsaved content'

# Load the reviewed class without its __main__ startup; route its managed output
# before class construction. Existing source files and packages stay unchanged.
recipe=ROOT/'Tools/Unreal/check_manual_intake_solo.py'
source=recipe.read_text(encoding='utf-8-sig')
old="OUTPUT = ROOT / 'Saved/ManualIntake/Capture'"
assert old in source,'Reviewed output anchor changed; inspect recipe'
source=source.replace(old,'OUTPUT = Path('+repr(str(OUT))+')',1)
# Adapt the startup gates and bounded endpoint of the reviewed recipe. Every
# collection, Q/held-Q, ballistic aim, trajectory, aperture and scoring phase
# up to all three physical deliveries stays intact.
def replace_once(before,after):
    global source
    assert source.count(before)==1,'Reviewed readiness anchor changed; inspect the current recipe'
    source=source.replace(before,after,1)

replace_once("                self.stage('first_perk')","                self.stage('results_readiness')")
replace_once('''            # Reward UI can pause the game clock while Slate is still ticking.
            if self.pc.is_reward_menu_open():
                if self.phase == 'first_perk':
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
                elif self.phase != 'perk_resolution':
                    self.report.setdefault('reward_ui_interruptions', []).append(dict(phase=self.phase, time=now))
                    self.pc.choose_reward_perk(0)
                return
''','''            # Startup gates are observed in wall time even if UI pauses game time.
            if self.pie_start is not None:
                self.require(not prop(self.pie_start,'Error'),'StartPIE: '+str(prop(self.pie_start,'Error')))
            if self.phase in ('results_readiness','results_arrival_release'):
                limit=45. if self.phase=='results_readiness' else 18.
                self.require(time.monotonic()-self.results_stage_wall<limit,'Natural Results delivery startup gate timed out')
            if self.pc.is_reward_menu_open():
                self.report.setdefault('unexpected_reward_ui',[]).append(dict(phase=self.phase,time=now))
                self.require(self.phase in ('results_readiness','results_arrival_release'),
                    'Unexpected reward UI during actual delivery; do not auto-choose a reward')
                return
''')
replace_once('''            if self.phase == 'first_perk':
                self.require(elapsed < 25., 'Normal first-perk UI never appeared')
                if self.pc.is_reward_menu_open():
                    self.pc.choose_reward_perk(0)
                    self.stage('perk_resolution')
            elif self.phase == 'perk_resolution' and not self.pc.is_reward_menu_open():
                self.report['checks']['normal_first_perk_resolved'] = True
                self.setup()
''','''            if self.phase == 'results_readiness':
                observed=self.results_input_snapshot()
                self.report['results_readiness_observed']=observed
                if time.monotonic()-self.results_stage_wall<1.5 or not observed['world_ready'] or observed['loading_visible']:
                    return
                result=u.MCRoguelikeEditorLibrary.review_rogue_gameplay(self.world,'level')
                self.report['native_director_stop_fixture']=result
                self.require(not result.startswith('REJECT'),'Native level review fixture accepted')
                self.stage('results_arrival_release')
            elif self.phase == 'results_arrival_release':
                observed=self.results_input_snapshot()
                self.report['results_arrival_observed']=observed
                if time.monotonic()-self.results_stage_wall<1. or not observed['world_ready'] or any(observed[key] for key in
                    ('loading_visible','reward_menu_open','mouse_cursor','move_ignored','look_ignored','boss_intro')):
                    return
                self.report['checks']['natural_current_world_and_input_gates_released']=True
                self.setup()
''')
# The current native aim point is the visible upper aperture when the artist's
# area center is below the tongue. Keep the measured morph/ring and the actual
# native target; only replace the obsolete assertion that those two centers are
# necessarily identical. This mirrors MCThroat::VacuumInlet, without moving or
# overriding the aperture, camera, collision or target.
replace_once('''        expected_center = u.MathLibrary.transform_location(art,local_center)
        self.require(math.dist(vec(expected_center),vec(self.target)) < .1,'Native aim target disagrees with actual fixed morph geometry')
''','''        expected_center = u.MathLibrary.transform_location(art,local_center)
        world_up = u.MathLibrary.transform_direction(art,u.Vector(*measured['up']))
        expected_boundary = [u.MathLibrary.transform_location(art,p) for p in self.aperture_points]
        native_floor_probe = self.throat.get_actor_transform().transform_location(prop(self.throat,'zone_center'))
        native_floor = self.floor(native_floor_probe)
        native_floor_z = native_floor.z if native_floor is not None else expected_center.z-100.
        expected_aim = expected_center
        if expected_center.z < native_floor_z+40. and world_up.z >= .1:
            top = max([expected_center.z]+[p.z for p in expected_boundary])
            visible_z = (max(native_floor_z+40.,expected_center.z)+top)*.5
            expected_aim = expected_center+world_up*((visible_z-expected_center.z)/world_up.z)
        self.report['current_native_aperture_consistency'] = dict(
            source='MCThroat::IntakeProfile + VacuumInlet visible-upper-opening policy',
            physical_area_center_cm=vec(expected_center),expected_visible_aim_cm=vec(expected_aim),
            actual_native_aim_cm=vec(self.target),actual_floor_cm=vec(native_floor) if native_floor is not None else None,
            floor_probe_cm=vec(native_floor_probe),difference_cm=math.dist(vec(expected_aim),vec(self.target)),
            policy='Unchanged measured artist morph and native target; no target or hit override')
        self.require(math.dist(vec(expected_aim),vec(self.target)) < .1,
            'Native visible aim target disagrees with current measured morph/floor policy')
''')
replace_once('''                    self.require(float(prop(prop(self.throat, 'suction_state'), 'duration')) == 0. and not self.actors(u.MCThroatVortex), 'A Q delivery started the retired arena suction')
                    self.report['checks']['manual_delivery_has_no_ambient_suction'] = True
                    self.prepare_visual_replay()
''','''                    self.results_record_state(now)
                    self.stage('results_buff_expiry')
            elif self.phase == 'results_buff_expiry':
                self.require(elapsed < 5.,'ReturnTrip focused expiry check timed out')
                self.results_check_expiry(now)
''')
replace_once("        required = ('intake_no_automatic_caps','manual_intake_reticle','manual_intake_throw')",
    "        required = ('intake_no_automatic_caps','manual_intake_reticle')")
assert '.choose_reward_perk(' not in source,'No invented or automatic initial menu choice is allowed'
namespace=dict(__name__='results_delivery_recipe',__file__=str(recipe),MC_MANUAL_INTAKE_FOCUS='throws',
    MC_MANUAL_INTAKE_SPASM=False,MC_MANUAL_INTAKE_OVERBOARD=False)
exec(compile(source,str(recipe),'exec'),namespace)
probe=namespace['ManualIntakeSolo']();prop=namespace['prop']
probe.report['scope']=__doc__
probe.report['not_run']+=['separate three-piece cinematic replay','legacy no-MCThroatVortex assertion',
    'extended legacy intake suite after three real Q deliveries']
probe.report['limitations']=[line for line in probe.report['limitations'] if 'visual replay' not in line
    and 'first perk' not in line]
probe.report['limitations']+=['Native Review(level) stops the unrelated director after natural readiness.',
    'The old 094008 intake-suite failure is retained; this focused passive result does not certify that entire suite.']
probe.report['prior_legacy_suite_failure']=dict(
    report='E:/CodexWork/MessControl/PassivesRemaining_20261028/Results/GameplayDelivery_20261028T094008Z/Report.json',
    assertion='A Q delivery started the retired arena suction',
    interpretation='Old assertion also rejects the supported cosmetic MCThroatVortex; old report did not capture actual ambient getters.',
    replacement='Observe actual ambient getters throughout this run; retain separate cosmetic vortex evidence and require zero actual ambient suction.')
setattr(builtins,'mc_manual_intake_solo',probe)
original_setup=probe.setup
original_stage=probe.stage
evidence=dict(status='running',scope=__doc__,events=[],samples=[],checks={},error=None,
    startup='Natural presentation/loading/input gates; no retired initial-perk menu or automatic choice',
    wrapper_recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    original_recipe_sha256=hashlib.sha256(recipe.read_bytes()).hexdigest(),
    effective_recipe_sha256=hashlib.sha256(source.encode('utf-8')).hexdigest(),
    native_callback_order='MCThroat accepted manual intake: AwardDelivery(); Dispose(); ++FoodSwallowed. The timer read follows the completed native award receipt.')
observer=dict(handle=None,last_count=None,last_expiry=-100.,perks=None,last_sample=-1.,active_empty_speed=None)

def write():
    probe.output.mkdir(parents=True,exist_ok=True)
    (probe.output/'ResultsDelivery.json').write_text(json.dumps(evidence,indent=2,allow_nan=False),encoding='utf-8')

def stage(name):
    probe.results_stage_wall=time.monotonic()
    original_stage(name)

def input_snapshot():
    loading=[widget for widget in u.ObjectIterator(u.MCLoadingWidget)
        if widget.get_owning_player()==probe.pc and widget.is_in_viewport()
        and widget.get_visibility() not in (u.SlateVisibility.HIDDEN,u.SlateVisibility.COLLAPSED)]
    return dict(world_ready=bool(probe.pc.is_world_presentation_ready()),loading_visible=bool(loading),
        reward_menu_open=bool(probe.pc.is_reward_menu_open()),mouse_cursor=bool(prop(probe.pc,'show_mouse_cursor')),
        move_ignored=bool(probe.pc.is_move_input_ignored()),look_ignored=bool(probe.pc.is_look_input_ignored()),
        boss_intro=bool(probe.pc.is_boss_intro_playing()))

def setup():
    original_setup()
    # The reviewed setup callback follows natural world/input readiness. The
    # observer reads actual existing HUD state; it never sets a timer deadline.
    observer['perks']=prop(probe.pc.player_state,'perks')
    rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(observer['perks'].get_perk_table()))
    row=next(row for row in rows if row['Name']=='B2_ReturnTrip')
    raw=row.get('Parameters');parameters={}
    probe.require(isinstance(raw,dict) and raw,'Saved ReturnTrip parameters must be present')
    for key,value in raw.items():
        probe.require(isinstance(value,dict) and set(value)=={'Parameters'},'Unexpected saved parameter encoding: '+repr((key,value)))
        number=value['Parameters']
        probe.require(type(number) in (int,float) and math.isfinite(number),'Nonfinite/nonnumeric saved ReturnTrip parameter')
        parameters[key]=float(number)
    probe.require('Duration' in parameters and 'SpeedBonus' in parameters,'ReturnTrip requires actual saved Duration and SpeedBonus')
    probe.require(abs(parameters['Duration']-2.)<.000001 and abs(parameters['SpeedBonus']-.15)<.000001,
        'Expected ordinary saved ReturnTrip two-second/fifteen-percent authoring')
    evidence['saved_returntrip_parameters']=dict(raw=raw,parsed=parameters,native_fallback_used=False)
    probe.require(observer['perks'].owns_perk('B2_ReturnTrip') or observer['perks'].try_acquire_perk('B2_ReturnTrip'),
        'Saved ReturnTrip acquisition failed')
    observer['last_count']=int(prop(probe.throat,'food_swallowed'))
    observer['last_expiry']=float(prop(observer['perks'],'delivery_speed_until'))
    evidence['timer_field_readiness']=dict(method='Read-only BlueprintReadOnly HUD state',
        delivery_speed_until=observer['last_expiry'],game_time=float(u.GameplayStatics.get_time_seconds(probe.world)))
    evidence['checks']['no_delivery_no_timer']=observer['last_expiry']<0
    probe.require(evidence['checks']['no_delivery_no_timer'],'ReturnTrip timer active before any native delivery')
    write()

def record_state(now):
    count=int(prop(probe.throat,'food_swallowed'));expiry=float(prop(observer['perks'],'delivery_speed_until'))
    if count>observer['last_count']:
        remaining=expiry-now;duration=evidence['saved_returntrip_parameters']['parsed']['Duration']
        evidence['events'].append(dict(game_time=now,food_swallowed=count,receipts=count-observer['last_count'],
            delivery_speed_until=expiry,previous_expiry=observer['last_expiry'],new_expiry=expiry>observer['last_expiry'],remaining=remaining))
        probe.require(expiry>observer['last_expiry'] and duration-.05<=remaining<=duration+.0001,
            'A real delivery did not refresh ReturnTrip for its saved duration')
    else:
        probe.require(count==observer['last_count'],'Swallowed receipt counter moved backward')
        probe.require(expiry<=observer['last_expiry']+.0001,'ReturnTrip refreshed without another swallowed food receipt')
    observer['last_count'],observer['last_expiry']=count,expiry
    active=bool(probe.throat.is_ambient_suction_active())
    duration=float(prop(prop(probe.throat,'suction_state'),'duration'))
    location=probe.hero.get_actor_location()
    strength=float(probe.throat.get_ambient_suction_strength_at(location))
    velocity=probe.throat.get_ambient_suction_velocity_at(location)
    velocity_size=math.sqrt(velocity.x**2+velocity.y**2+velocity.z**2)
    vortices=probe.actors(u.MCThroatVortex)
    ambient=dict(active=active,state_duration=duration,strength_at_hero=strength,velocity_at_hero_cm_s=velocity_size,
        cosmetic_vortices=[dict(path=v.get_path_name(),location_cm=namespace['vec'](v.get_actor_location())) for v in vortices])
    evidence['ambient_observation']=ambient
    evidence['maximum_cosmetic_vortex_count']=max(len(vortices),evidence.get('maximum_cosmetic_vortex_count',0))
    probe.require(not active and duration==0. and strength==0. and velocity_size==0.,
        'Manual delivery unexpectedly activated actual ambient suction: '+repr(ambient))
    if now>=observer['last_sample']+.1:
        movement=probe.hero.get_movement_component()
        evidence['samples'].append(dict(game_time=now,food_swallowed=count,delivery_speed_until=expiry,remaining=expiry-now,
            max_speed_cm_s=float(movement.get_max_speed()),movement_mode=str(prop(movement,'movement_mode')),
            ground_surface=str(prop(movement,'ground_surface')),held_pieces=len(prop(probe.collection,'pieces')),ambient=ambient))
        observer['last_sample']=now

def check_expiry(now):
    record_state(now)
    expiry=observer['last_expiry'];movement=probe.hero.get_movement_component()
    probe.require(observer['last_count']==probe.start_swallowed+3 and not prop(probe.collection,'pieces'),
        'Focused buff expiry must follow exactly three native deliveries with empty hands')
    if expiry-now>.25:
        observer['active_empty_speed']=dict(game_time=now,deadline=expiry,max_speed_cm_s=float(movement.get_max_speed()),
            movement_mode=str(prop(movement,'movement_mode')),ground_surface=str(prop(movement,'ground_surface')),
            hero_cm=namespace['vec'](probe.hero.get_actor_location()))
    if now<expiry+.15:return
    before=observer['active_empty_speed'];after=float(movement.get_max_speed())
    probe.require(before is not None and before['game_time']<expiry and now>expiry,'Both sides of actual server deadline must be observed')
    probe.require(str(prop(movement,'movement_mode'))==before['movement_mode'] and
        str(prop(movement,'ground_surface'))==before['ground_surface'],'Movement context changed across the passive expiry observation')
    expected=1.+evidence['saved_returntrip_parameters']['parsed']['SpeedBonus']
    ratio=before['max_speed_cm_s']/after if after>0 else 0.
    evidence['expiry_observation']=dict(active=before,expired_game_time=now,unchanged_deadline=expiry,
        expired_max_speed_cm_s=after,observed_speed_ratio=ratio,expected_speed_ratio=expected,
        method='Public inherited UMovementComponent::GetMaxSpeed virtual query; no speed/timer writes')
    probe.require(abs(ratio-expected)<.001,'Native normal speed failed to lose exactly the saved ReturnTrip bonus after expiry')
    probe.report['checks'].update(manual_delivery_has_no_actual_ambient_suction=True,
        three_real_deliveries_refresh_returntrip=True,returntrip_expires_after_saved_duration=True,
        native_speed_loses_fifteen_percent_bonus=True)
    probe.report['returntrip']=dict(events=evidence['events'],expiry=evidence['expiry_observation'])
    evidence['checks'].update(three_real_delivery_receipts=sum(event['receipts'] for event in evidence['events'])==3,
        actual_ambient_suction_inactive=True,deadline_passed_without_refresh=True,native_speed_bonus_expired=True)
    probe.stage('capture_flush')

def observe(_delta):
    try:
        if probe.done:
            evidence['checks']['at_least_two_real_delivery_events']=len(evidence['events'])>=2
            evidence['checks']['focused_intake_gameplay_passed']=bool(probe.report.get('passed'))
            evidence['status']='passed' if not evidence['error'] and evidence['checks'] and all(evidence['checks'].values()) else 'failed'
            write();u.unregister_slate_post_tick_callback(observer['handle']);return
        if observer['perks'] is None:return
        now=float(u.GameplayStatics.get_time_seconds(probe.world))
        record_state(now)
        write()
    except Exception:
        evidence.update(status='failed',error=traceback.format_exc());write()
        u.unregister_slate_post_tick_callback(observer['handle']);probe.finish(evidence['error'])

probe.setup,probe.stage,probe.results_input_snapshot=setup,stage,input_snapshot
probe.results_record_state,probe.results_check_expiry=record_state,check_expiry
observer['handle']=u.register_slate_post_tick_callback(observe)
try:probe.start()
except Exception:
    u.unregister_slate_post_tick_callback(observer['handle']);probe.finish(traceback.format_exc());raise
print(json.dumps(dict(prepared_probe_started=True,output=str(probe.output),cancel="builtins.mc_manual_intake_solo.finish('cancelled')")))
