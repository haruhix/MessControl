"""Owned solo Hub fixture: open one existing chest through the real player RPC.
No offers/IDs are fabricated and no choice/skip is submitted. End this transient
PIE after review. Does not save assets, maps or the player's profile.
"""
import json
from pathlib import Path
import unreal as u
out=Path('E:/CodexWork/MessControl/Validation/UIHubReward_20261104')
worlds=u.EditorLevelLibrary.get_pie_worlds(True);assert len(worlds)==1
world=worlds[0];assert 'Hub' in world.get_path_name()
assert '-MCHubTransientProfile' in u.SystemLibrary.get_command_line()
pc=u.GameplayStatics.get_player_controller(world,0);hero=pc.get_controlled_pawn();assert isinstance(hero,u.MCToothCharacter)
hub=u.GameplayStatics.get_game_state(world);assert hub.get_editor_property('phase')==u.MCHubPhase.GATHERING
assert pc.is_world_presentation_ready() and hero.get_editor_property('RewardInteraction') is None
ps=pc.get_editor_property('player_state');perks=ps.get_editor_property('perks')
baseline=json.loads(u.ToolsetLibrary.get_object_properties(perks,['PassivePerkIDs','OwnedToolPerkIDs','ActiveSkillSlots']))
position=hero.get_actor_location();capsule=hero.get_editor_property('capsule_component');half=capsule.get_scaled_capsule_half_height()
for cls in (u.MCRewardChest,u.MCRewardDropZone):
    for previous in u.GameplayStatics.get_all_actors_of_class(world,cls):
        if previous.actor_has_tag('MC_UIV2_Reward_QA_20261104'):previous.destroy_actor()
pose=u.Transform(location=u.Vector(position.x+180,position.y,position.z-half+4),rotation=u.Rotator(pitch=0,yaw=180,roll=0))
statics=u.get_default_object(u.GameplayStatics)
def spawn(cls,transform,setup):
    actor=statics.call_method('BeginDeferredActorSpawnFromClass',args=(world,cls,transform,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    assert actor
    actor.tags=[u.Name('MC_UIV2_Reward_QA_20261104')];setup(actor)
    return statics.call_method('FinishSpawningActor',args=(actor,transform,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
zone=spawn(u.MCRewardDropZone.static_class(),pose,lambda actor:None)
cls=u.load_class(None,'/Game/Gameplay/Roguelike/BP_RewardChest.BP_RewardChest_C');assert cls
def setup(chest):
    chest.set_editor_property('bPlacedReward',True)
    chest.set_editor_property('PlacedDropZone',zone)
chest=spawn(cls,pose,setup);chest.force_mimic_for_test(False)
body=chest.get_editor_property('Body')
contact=body.get_socket_transform('Lockpick',u.RelativeTransformSpace.RTS_WORLD)
if not body.does_socket_exist('Lockpick'):
    contact.translation=u.MathLibrary.transform_location(contact,u.Vector(147.810,0,68.042))
front=u.MathLibrary.get_forward_vector(contact.rotation.rotator())
target=contact.translation+front*120
hero.character_movement.stop_movement_immediately()
hero.set_actor_location(u.Vector(target.x,target.y,position.z),False,True)
hero.call_method('ServerBeginRewardOpening',args=(chest,))
stage=str(chest.get_editor_property('stage'));assert 'LOCKPICKING' in stage,stage
report={'kind':'Real ordinary native chest opening; solo transient profile','chest':chest.get_path_name(),'zone':zone.get_path_name(),'baselinePerks':baseline,'personalChoicesTaken':ps.get_editor_property('PersonalChoicesTaken'),'stage':stage,'choiceSubmitted':False,'profileWritten':False,'cleanup':'Stop owned PIE; all fixture actors and pending offer are discarded.'}
out.mkdir(parents=True,exist_ok=True)
(out/'RewardFixture.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
