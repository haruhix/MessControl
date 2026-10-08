#include "MCNutBossTypes.h"
#include "Engine/StaticMesh.h"

FMCNutBossSettings::FMCNutBossSettings()
{
    WholeMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole")));
    ShellMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell.SM_Walnut_HalfShell")));
    KernelMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Kernel.SM_Walnut_Kernel")));
}

void FMCNutBossSettings::Sanitize()
{
    auto Safe=[](float& V,float Default,float Low,float High){ V=FMath::IsFinite(V)?FMath::Clamp(V,Low,High):Default; };
    Safe(TankHealth,1400,200,10000); Safe(MageHealth,1100,200,10000); Safe(ExtraPlayerHealth,.65f,0,2);
    Safe(TankHeight,220,150,320); Safe(MageHeight,200,140,300); Safe(CreepHeight,70,40,100);
    Safe(EntranceSeconds,1.3f,.8f,3); Safe(EntranceHeight,800,300,1500);
    Safe(TankMoveSpeed,160,60,300); Safe(MageMoveSpeed,120,40,250);
    Safe(ShieldFrontDamageScale,.35f,.15f,1); Safe(ShieldHalfAngle,55,20,85);
    Safe(MeleeDamage,12,1,30); Safe(MeleeRange,190,140,280); Safe(MeleeWindup,.65f,.5f,2); Safe(MeleeCooldown,2.5f,1.5f,8);
    Safe(ChargeDamage,22,1,40); Safe(ChargeWindup,1.1f,.8f,3); Safe(ChargeCooldown,8,5,25);
    Safe(ChargeSpeed,650,300,900); Safe(ChargeDistance,1100,350,1600);
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
