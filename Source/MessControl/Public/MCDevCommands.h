#pragma once
#include "CoreMinimal.h"
#include "MCDevCommands.generated.h"

UENUM()
enum class EMCDevAction : uint8
{
    StartStep, DropFood, Infection, DropBrushes, CoffeeDirt, LooseTeeth,
    DamageSelf, Ragdoll, KillSelf, RestoreMouth, StopCoffee, RestartDay, TongueUlcer, TongueJolt,
    GazePractice, TongueMotion, TongueWeight, TongueWeightToggle, GripPractice,
    TonguePressurePreset, TonguePressureReload, TonguePressureClear, LocomotionGround,
    SpicyPepper, VomitMeal, ColdCola, SwimCoffee, ActiveRagdoll, Yawn, Fire, RewardChest, BossPractice,
    BossAI, BossStop, BossAnimation, BossRemove, BossIntro, CalculusPractice, CalculusClear,
    BossPhase3Spawn, BossPhase3Animation, BossPhase3Activate, BossPhase3Deactivate, BossPhase3Remove,
    OldFlood, BotsStart, BotsStop, BotsReport, MimicChest, MimicRemove,
    GrantToolBooster, GrantAllToolBoosters
};

// Reuse the existing typed dev-command argument for the 4 counts x 3 skills x 2 modes.
inline int32 MCDevBotSetup(int32 Count,int32 SkillIndex,bool bObserve)
{
    return Count-1+SkillIndex*4+(bObserve?12:0);
}
