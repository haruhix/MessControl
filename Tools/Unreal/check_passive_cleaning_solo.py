"""Prepared rerun. One owned solo native-input stain completion check.

Requires current ordinary Editor DLL, authored B2 table and idle saved L_Mouth.
Wait for the coordinated build exposing existing CleanSpeedUntil as read-only
HUD state. The initial090311Z run is retained as failed: real cleaning finished,
then its Python timer-property read failed. Do not reuse or overwrite that run.
Reuses the reviewed whole-stain native brush/movement recipe. Changes only an
initial PIE fixture: one80cm stain, initial40HP, five current passives. All timed
removal, completion, healing and speed activation remain native gameplay.
"""
import builtins
import hashlib
import json
import runpy
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path('E:/CodexWork/MessControl/Validation/PassivesCleaning_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
assert ROOT.as_posix().lower()=='e:/devgame/messcontrol' and OUT.drive.upper()=='E:'
assert not OUT.exists(),'Preserve existing evidence'
assert not u.GameUserSettings.get_game_user_settings().get_voice_enabled(),'Probe process must keep microphone disabled'
dll=ROOT/'Binaries/Win64/UnrealEditor-MessControl.dll'
for relative in ('Source/MessControl/Private/MCResultPassives.cpp','Source/MessControl/Private/MCMouthLiquid.cpp',
    'Source/MessControl/Private/MCArenaToothGrime.cpp','Source/MessControl/Private/MCRoguePerks.cpp',
    'Source/MessControl/Public/MCPerkComponent.h'):
    assert dll.stat().st_mtime_ns>=(ROOT/relative).stat().st_mtime_ns,'Rebuild current ordinary Editor DLL first'

scope=runpy.run_path(str(ROOT/'Tools/Unreal/check_brush_whole_stain_solo.py'),init_globals={
    'MC_BRUSH_OUTPUT':str(OUT),'MC_BRUSH_SIZES':(80,), 'MC_BRUSH_CASE_SECONDS':25,
    'MC_BRUSH_CASE_WALL_SECONDS':29,'MC_BRUSH_TOTAL_WALL_SECONDS':95,'MC_BRUSH_CAPTURE_HZ':6})
probe,prop=scope['probe'],scope['prop']
probe.cases=[(80.,60.)]
original_begin,original_end,original_write=probe.begin_case,probe.end_case,probe.write
probe.report.update(scope=__doc__,checks={},comparisons=[],
    comparison='One current80cm stain through ordinary60cm brush input; no baseline comparison',
    wrapper_recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    limitations=['Initial40HP, passive grants and one transient ordinary stain are disclosed PIE fixtures.',
        'Real primary input, movement, brush contact, mask removal and completion callbacks own all timed work.',
        'No earned full run, multiplayer, hardware input, microphone or packaged first-launch check.'],
    measured_goal='Native completion:40HP +3 stain recovery +20 true-mouth recovery =63HP;10percent speed timer')

def write():
    original_write()
    (probe.output/'README.md').write_text('# Results passives solo cleaning probe\n\n'+__doc__+
        '\nExpected one-time result:40HP +3HP CleanBreath +20HP RefreshingWork =63HP, '
        'with the native CleanAcceleration timer started for three seconds. '
        'The fixture uses current saved definitions and normal primary/movement input. '
        'No masks, HP, timers or contact readiness are written during the timed case.\n',encoding='utf-8')

def begin_case():
    if probe.case_index<0:
        probe.perks=prop(probe.pc.player_state,'perks')
        # This callback runs after Base's owned-world/loading readiness wait.
        # Read the legitimate HUD field before timed cleanup, so stale script
        # metadata fails at setup rather than losing a real completion result.
        probe.require(probe.pc.is_world_presentation_ready(),'Natural world presentation is ready before the timed case')
        probe.report['timer_field_readiness']=dict(method='Read-only BlueprintReadOnly HUD state',
            clean_speed_until=float(prop(probe.perks,'clean_speed_until')),
            game_time=float(u.GameplayStatics.get_time_seconds(probe.world)))
        probe.require(not any(a.is_available() and int(prop(prop(prop(a,'status'),'state'),'coffee_left'))>0
            for a in probe.actors(u.MCArenaTooth)), 'Existing dirty teeth must remain real outstanding mouth tasks; choose another disclosed fixture')
        for ident in ('B2_CleanBreath','B2_CleanAcceleration','B2_LastStroke','B2_RefreshingWork','B2_ReturnTrip'):
            probe.require(probe.perks.owns_perk(ident) or probe.perks.try_acquire_perk(ident),
                'Current saved passive acquisition failed: '+ident)
        state=prop(prop(probe.hero,'status'),'state');state.set_editor_property('health',40.)
        prop(probe.hero,'status').set_editor_property('State',state)
        probe.report['initial_player_health_fixture']=40.
    original_begin()

def end_case(outcome,now):
    hp=float(prop(prop(prop(probe.hero,'status'),'state'),'health'))
    expiry=float(prop(probe.perks,'clean_speed_until'))
    probe.report['results_passives']=dict(health_after=hp,clean_speed_until=expiry,game_time=now,
        actual_passive_ids=[str(x) for x in prop(probe.perks,'passive_perk_ids')])
    probe.report['checks'].update(native_whole_stain_clean=outcome=='native_clean',
        one_stain_and_one_true_mouth_recovery=abs(hp-63.)<.01,
        clean_speed_duration_started=now<expiry<=now+3.1)
    original_end(outcome,now)

def complete_report():
    checks=probe.report['checks']
    probe.report['checks']['exactly_one_native_completion']=len(probe.report['cases'])==1
    probe.write()
    probe.finish(None if checks and all(checks.values()) else 'Native Results cleaning check failed; inspect actual HP/timer and masks')

probe.begin_case,probe.end_case,probe.complete_report,probe.write=begin_case,end_case,complete_report,write
print(json.dumps(dict(prepared_probe_started=True,output=str(OUT),cancel="builtins.mc_brush_whole_stain_solo.finish('cancelled')")))
