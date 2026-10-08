#include "MCNutBossTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"

FMCNutBossSettings::FMCNutBossSettings()
{
    WholeMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole")));
    TankSkeletalMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/SK_NutTank.SK_NutTank")));
    TankBallMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/SM_NutTank_Ball.SM_NutTank_Ball")));
    TankIdleAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Idle.A_NutTank_Idle")));
    TankWalkAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Walk.A_NutTank_Walk")));
    TankWalkLeftAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_WalkLeft.A_NutTank_WalkLeft")));
    TankWalkRightAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_WalkRight.A_NutTank_WalkRight")));
    TankMeleeAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Melee.A_NutTank_Melee")));
    TankJumpAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Jump.A_NutTank_Jump")));
    TankTransformAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutTank/Animations/A_NutTank_Transform.A_NutTank_Transform")));
    MageSkeletalMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/SK_NutWizard.SK_NutWizard")));
    MageIdleAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Idle.A_NutWizard_Idle")));
    MageWalkAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Walk.A_NutWizard_Walk")));
    MageCastAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Cast.A_NutWizard_Cast")));
    MageHeavyCastAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_HeavyCast.A_NutWizard_HeavyCast")));
    MageSummonAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Summon.A_NutWizard_Summon")));
    MageRainAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Rain.A_NutWizard_Rain")));
    MageHitAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Hit.A_NutWizard_Hit")));
    MageDeathAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutWizard/Animations/A_NutWizard_Death.A_NutWizard_Death")));
    ShellMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell.SM_Walnut_HalfShell")));
    KernelMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Kernel.SM_Walnut_Kernel")));
}

void FMCNutBossSettings::Sanitize()
{
    auto Safe=[](float& V,float Default,float Low,float High){ V=FMath::IsFinite(V)?FMath::Clamp(V,Low,High):Default; };
    Safe(TankHealth,1400,200,10000); Safe(MageHealth,1100,200,10000); Safe(ExtraPlayerHealth,.65f,0,2);
    Safe(TankModelYaw,-90,-180,180); Safe(MageModelYaw,-90,-180,180);
    Safe(TankHeight,220,150,320); Safe(MageHeight,200,140,300); Safe(CreepHeight,70,40,100);
    Safe(TankBallHeight,200,120,300); Safe(TankMeleeImpactFraction,.5f,.05f,.95f);
    Safe(TankJumpTakeoffFraction,.25f,.05f,.8f); Safe(TankJumpImpactFraction,FMath::Max(.7f,TankJumpTakeoffFraction+.05f),TankJumpTakeoffFraction+.05f,.95f);
    Safe(MageCastReleaseFraction,.55f,.2f,.8f); Safe(AnimationBlendSeconds,.18f,.08f,.4f); Safe(DeathSeconds,2.4f,1.6f,4);
    if(MageCastOffset.ContainsNaN()) MageCastOffset=FVector(120,-22,65);
    MageCastOffset.X=FMath::Clamp(MageCastOffset.X,60.,180.); MageCastOffset.Y=FMath::Clamp(MageCastOffset.Y,-90.,90.);
    MageCastOffset.Z=FMath::Clamp(MageCastOffset.Z,-20.,160.);
    Safe(EntranceSeconds,1.3f,.8f,3); Safe(EntranceHeight,800,300,1500);
    Safe(TankMoveSpeed,160,60,300); Safe(MageMoveSpeed,120,40,250);
    Safe(ShieldFrontDamageScale,.35f,.15f,1); Safe(ShieldHalfAngle,55,20,85);
    Safe(MeleeDamage,12,1,30); Safe(MeleeRange,190,140,280); Safe(MeleeWindup,.65f,.5f,2); Safe(MeleeCooldown,2.5f,1.5f,8);
    Safe(ChargeDamage,22,1,40); Safe(ChargeWindup,1.1f,.8f,3); Safe(ChargeCooldown,8,5,25);
    Safe(ChargeSpeed,650,300,900); Safe(ChargeDistance,1100,350,1600);
    Safe(RollDamage,16,1,30); Safe(RollWindup,1.2f,.9f,3); Safe(RollCooldown,16,10,35);
    Safe(RollDuration,5,3,7); Safe(RollSpeed,520,300,750); Safe(RollTurnDegreesPerSecond,50,0,75);
    Safe(RollHitGap,1.1f,.8f,3); Safe(RollPush,260,50,400);
    Safe(JumpDamage,25,1,40); Safe(JumpWindup,1.2f,.8f,3); Safe(JumpFlightSeconds,.8f,.6f,1.5f);
    Safe(JumpRadius,230,150,330); Safe(JumpCooldown,13,8,35);
    Safe(FireballDamage,18,1,35); Safe(FireballWindup,.9f,.7f,2); Safe(FireballCooldown,5,3,15);
    Safe(FireballSpeed,550,250,850); Safe(FireballRadius,28,15,45);
    Safe(SummonCooldown,17,10,40); SummonCount=FMath::Clamp(SummonCount,1,4); MaxLiveCreeps=FMath::Clamp(MaxLiveCreeps,1,12);
    Safe(CreepHealth,40,20,80);
    Safe(RainRadius,300,180,420); Safe(RainImpactRadius,90,55,130); Safe(RainWindup,1.2f,1,3); Safe(RainActiveSeconds,4,3,6);
    RainDrops=FMath::Clamp(RainDrops,4,12); Safe(RainDamage,8,1,15); Safe(RainHitGap,.8f,.6f,2); Safe(RainCooldown,20,12,40);
}

float FMCNutBossSettings::HealthForPlayers(EMCNutBossRole Role,int32 Players) const
{
    const float Base=Role==EMCNutBossRole::Tank?TankHealth:MageHealth;
    return FMath::Clamp(Base*(1+ExtraPlayerHealth*(FMath::Clamp(Players,1,8)-1)),200.f,40000.f);
}
