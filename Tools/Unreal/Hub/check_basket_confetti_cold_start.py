"""Confirm the saved native module owns the integration before the focused solo check.

Run in a freshly opened Editor, without a Live Coding compile in this session.
The base DLL must be newer than the basketball sources. This catches a successful
in-memory preview that has not yet been linked into the restartable editor module.
"""
import unreal as u
import json,os,hashlib,runpy
from pathlib import Path

root=Path(u.Paths.project_dir())
probe_out=Path(globals().get('MC_BASKET_CONFETTI_OUTPUT',str(root/'Saved/HubBasketballConfetti_20261021'))).resolve()
assert probe_out.drive.upper()=='E:','Store probe output on E:'
out=probe_out/'RestartFix';out.mkdir(parents=True,exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
dll=root/'Binaries/Win64/UnrealEditor-MessControl.dll'
sources=[root/'Source/MessControl/Public/MCHubBasketball.h',root/'Source/MessControl/Private/MCHubBasketball.cpp']
assert dll.stat().st_mtime>=max(p.stat().st_mtime for p in sources), 'Basketball changes are newer than the persistent editor DLL'
cls=u.load_class(None,'/Script/MessControl.MCHubBasketball')
effect=u.get_default_object(cls).get_editor_property('BasketConfetti')
assert effect is None,'Native CDO must defer its stateless system until Niagara has started'
assert u.get_default_object(cls).get_editor_property('BasketsMade')==0
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/Maps/Hub.Hub'
balls=[a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if a.get_class()==cls]
assert len(balls)==1
saved_effect=balls[0].get_editor_property('BasketConfetti')
assert saved_effect is None or isinstance(saved_effect,u.NiagaraSystem),'Unexpected serialized effect override'
assert balls[0].get_editor_property('Hoop')
report={'pid':os.getpid(),'base_dll':str(dll),'base_dll_sha256':hashlib.sha256(dll.read_bytes()).hexdigest(),
 'base_dll_newer_than_basketball_sources':True,'effect_default':None,'native_cdo_effect_unassigned':True,
 'effect_before_play':saved_effect.get_path_name() if saved_effect else None,
 'saved_hub_ball_effect_assigned':bool(saved_effect),'serialized_override_allowed':True,
 'baskets_counter_available':True,'world':world.get_path_name(),
 'validation':'Fresh normal Editor startup; no Live Coding compile requested. Starting one solo gameplay check.'}
(out/'cold_start.json').write_text(json.dumps(report,indent=2))
u.SystemLibrary.execute_console_command(world,'ModelContextProtocol.StartServer')
print('BASKET_CONFETTI_COLD_START_READY',json.dumps(report))
runpy.run_path(str(root/'Tools/Unreal/Hub/check_basket_confetti_solo.py'),init_globals={'MC_BASKET_CONFETTI_OUTPUT':str(probe_out)})
