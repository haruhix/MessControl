"""Bounded solo receiver check for the independent generic boss class.

Uses real ReceiveBossDamage, death and ResetForRun calls. This supplements the
Pickaxe/F3 gameplay check; it does not claim a simulated tool contact.
"""
import unreal as u,json
from pathlib import Path
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world();assert world
pc=u.GameplayStatics.get_player_controller(world,0);hero=pc.get_controlled_pawn();assert hero
mode=u.GameplayStatics.get_game_mode(world);assert not mode.actor_has_tag('MC.DevOneHitKill')
lib=u.get_default_object(u.GameplayStatics);pose=u.Transform(location=hero.get_actor_location()+u.Vector(0,900,400))
boss=lib.call_method('BeginDeferredActorSpawnFromClass',args=(world,u.MCBossCharacter,pose,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
lib.call_method('FinishSpawningActor',args=(boss,pose,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));boss.set_actor_tick_enabled(False)
checks=[]
def check(name,condition):
 checks.append(dict(name=name,passed=bool(condition)));assert condition,name
try:
 before=boss.get_editor_property('Runtime').get_editor_property('Health')
 normal=boss.receive_boss_damage(1.,hero)
 check('disabled mode retains ordinary generic boss damage',0<normal<before and boss.is_boss_alive())
 pc.request_dev_action(u.MCDevAction.ONE_HIT_KILL,-1)
 check('F3 action enables one-hit mode in the cold Editor build',mode.actor_has_tag('MC.DevOneHitKill'))
 environment=boss.receive_boss_damage(1.,None)
 check('enabled mode leaves environmental boss damage unchanged',environment==1. and boss.is_boss_alive())
 applied=boss.receive_boss_damage(1.,hero)
 check('enabled mode kills the generic boss through its normal receiver',not boss.is_boss_alive() and applied==before-normal-environment)
 boss.reset_for_run();pc.request_dev_action(u.MCDevAction.ONE_HIT_KILL,-1)
 check('disabled mode restores ordinary generic boss damage',boss.receive_boss_damage(1.,hero)==normal and boss.is_boss_alive() and not mode.actor_has_tag('MC.DevOneHitKill'))
 out=Path(u.Paths.project_dir())/'Artifacts/DevOneHitKill_20261017'
 (out/'GenericBossCheck.json').write_text(json.dumps(dict(passed=True,checks=checks,scope=__doc__),indent=2))
 print('DEV_ONE_HIT_GENERIC_BOSS_PASSED',checks)
finally:
 if mode.actor_has_tag('MC.DevOneHitKill'):pc.request_dev_action(u.MCDevAction.ONE_HIT_KILL,-1)
 boss.destroy_actor()
