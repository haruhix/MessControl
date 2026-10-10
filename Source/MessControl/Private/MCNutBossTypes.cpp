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
    // Migrate only the earlier generated dimensions. Repeated calls never double
    // a run copy again, and independently authored dimensions remain intact.
    if(FMath::IsNearlyEqual(TankHeight,220.f)) TankHeight=440.f;
    if(FMath::IsNearlyEqual(MageHeight,200.f)) {MageHeight=400.f; MageCastOffset*=2.f;}
    if(FMath::IsNearlyEqual(TankBallHeight,200.f)) TankBallHeight=400.f;
    if(FMath::IsNearlyEqual(CreepHeight,70.f)) CreepHeight=116.f;
    if(FMath::IsNearlyEqual(MeleeRange,190.f)) MeleeRange=380.f;
    if(FMath::IsNearlyEqual(JumpRadius,230.f)) JumpRadius=460.f;
    if(FMath::IsNearlyEqual(RainRadius,300.f)) RainRadius=703.5624f;
    if(FMath::IsNearlyEqual(RollPush,260.f)) RollPush=620.f;
    // Existing saved profiles retain the earlier generated balance values.
    // Migrate those defaults once while preserving independently authored tuning.
    if(FMath::IsNearlyEqual(MeleeCooldown,2.5f)) MeleeCooldown=3.5f;
    if(FMath::IsNearlyEqual(ChargeCooldown,8.f)) ChargeCooldown=14.f;
    if(FMath::IsNearlyEqual(RollCooldown,16.f)) RollCooldown=40.f;
    if(FMath::IsNearlyEqual(RollDuration,5.f)) RollDuration=8.f;
    if(FMath::IsNearlyEqual(JumpCooldown,13.f)) JumpCooldown=20.f;
    if(FMath::IsNearlyEqual(FireballCooldown,5.f)) FireballCooldown=8.f;
    if(FMath::IsNearlyEqual(SummonCooldown,17.f)) SummonCooldown=22.f;
    if(FMath::IsNearlyEqual(RainCooldown,20.f)) RainCooldown=28.f;
    if(FMath::IsNearlyEqual(MageTeleportCooldown,12.f)) MageTeleportCooldown=18.f;
    Safe(TankHealth,1400,200,10000); Safe(MageHealth,1100,200,10000); Safe(ExtraPlayerHealth,.65f,0,2);
    Safe(TankModelYaw,-90,-180,180); Safe(MageModelYaw,-90,-180,180);
    Safe(TankHeight,440,150,640); Safe(MageHeight,400,140,600); Safe(CreepHeight,116,40,240);
    Safe(TankBallHeight,400,120,600); Safe(TankMeleeImpactFraction,.5f,.05f,.95f);
    Safe(TankChargeLoopPlayRate,2.f,.1f,4.f); Safe(MageMeleeImpactFraction,.55f,.05f,.95f);
    Safe(TankJumpTakeoffFraction,.25f,.05f,.8f); Safe(TankJumpImpactFraction,FMath::Max(.7f,TankJumpTakeoffFraction+.05f),TankJumpTakeoffFraction+.05f,.95f);
    Safe(MageCastReleaseFraction,.55f,.2f,.8f); Safe(AnimationBlendSeconds,.18f,.08f,.4f); Safe(DeathSeconds,2.4f,1.6f,4);
    if(MageCastOffset.ContainsNaN()) MageCastOffset=FVector(240,-44,130);
    MageCastOffset.X=FMath::Clamp(MageCastOffset.X,60.,360.); MageCastOffset.Y=FMath::Clamp(MageCastOffset.Y,-180.,180.);
    MageCastOffset.Z=FMath::Clamp(MageCastOffset.Z,-40.,320.);
    Safe(EntranceSeconds,1.3f,.8f,3); Safe(EntranceHeight,800,300,1500);
    Safe(TankMoveSpeed,160,60,300); Safe(MageMoveSpeed,120,40,250);
    Safe(ShieldMaxHealth,500,1,10000); Safe(ShieldFrontDamageScale,.35f,.15f,1); Safe(ShieldHalfAngle,55,20,85);
    Safe(SpecialAttackGap,3,0,10); Safe(TankRecoverySeconds,2.5f,.5f,8); Safe(RollRecoverySeconds,3.5f,.5f,8);
    Safe(MeleeDamage,12,1,30); Safe(MeleeRange,380,140,560); Safe(MeleeWindup,.65f,.5f,2); Safe(MeleeCooldown,3.5f,1.5f,12);
    Safe(TankMeleePush,620,0,1500); Safe(TankChargePush,820,0,1500); Safe(TankJumpPush,900,0,1800); Safe(TankPushLift,210,0,500);
    Safe(ChargeDamage,22,1,40); Safe(ChargeWindup,1.1f,.8f,3); Safe(ChargeCooldown,14,5,60);
    Safe(ChargeSpeed,650,300,900); Safe(ChargeDistance,1100,350,1600);
    Safe(RollDamage,16,1,30); Safe(RollWindup,1.2f,.9f,3); Safe(RollCooldown,40,10,90);
    Safe(RollDuration,8,3,12); Safe(RollSpeed,520,300,750); Safe(RollTurnDegreesPerSecond,50,0,75);
    Safe(RollHitGap,1.1f,.8f,3); Safe(RollPush,620,50,1000); Safe(RollPainCooldown,1.25f,.4f,4);
    Safe(JumpDamage,25,1,40); Safe(JumpWindup,1.2f,.8f,3); Safe(JumpFlightSeconds,.8f,.6f,1.5f);
    Safe(JumpRadius,460,150,660); Safe(JumpCooldown,20,8,60);
    Safe(FireballDamage,18,1,35); Safe(FireballWindup,.9f,.7f,2); Safe(FireballCooldown,8,3,30);
    Safe(FireballSpeed,550,250,850); Safe(FireballRadius,28,15,45);
    Safe(SummonCooldown,22,10,60); SummonCount=FMath::Clamp(SummonCount,1,4); MaxLiveCreeps=FMath::Clamp(MaxLiveCreeps,1,12);
    Safe(CreepHealth,40,20,80);
    Safe(RainRadius,703.5624f,180,1000); Safe(RainImpactRadius,90,55,130); Safe(RainWindup,1.2f,1,3); Safe(RainActiveSeconds,4,3,6);
    RainDrops=FMath::Clamp(RainDrops,4,12); Safe(RainDamage,8,1,15); Safe(RainHitGap,.8f,.6f,2); Safe(RainCooldown,28,12,60);
    Safe(MageTeleportCooldown,18,3,60); Safe(MageTeleportWindup,.65f,.3f,2);
    Safe(MageTeleportMinDistance,600,250,1600); Safe(MageTeleportMaxDistance,1100,MageTeleportMinDistance,2200);
    Safe(MageFocusRadius,420,150,1200);
}

float FMCNutBossSettings::BodyRadiusForRole(EMCNutBossRole Role) const
{
    const float Radius=Role==EMCNutBossRole::Tank?TankHeight*(100.f/220.f):MageHeight*.45f;
    return FMath::IsFinite(Radius)?FMath::Clamp(Radius,60.f,260.f):(Role==EMCNutBossRole::Tank?200.f:180.f);
}

float FMCNutBossSettings::HealthForPlayers(EMCNutBossRole Role,int32 Players) const
{
    const float Base=Role==EMCNutBossRole::Tank?TankHealth:MageHealth;
    return FMath::Clamp(Base*(1+ExtraPlayerHealth*(FMath::Clamp(Players,1,8)-1)),200.f,40000.f);
}
