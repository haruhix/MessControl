#include "MCNutRainProfile.h"

namespace
{
float SafeNutValue(float Value,float Default,float Min,float Max)
{ return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Default; }
}

void FMCNutEnemySettings::Sanitize()
{
    MaxHealth=SafeNutValue(MaxHealth,90,1,1000);
    MoveSpeed=SafeNutValue(MoveSpeed,165,40,500);
    AttackRange=SafeNutValue(AttackRange,125,50,250);
    AttackDamage=SafeNutValue(AttackDamage,12,0,100);
    WindupSeconds=SafeNutValue(WindupSeconds,.55f,.25f,3);
    AttackCooldown=SafeNutValue(AttackCooldown,1.8f,.5f,8);
    if(FMath::IsNearlyEqual(BodyRadius,35.f) || FMath::IsNearlyEqual(BodyRadius,42.f)) BodyRadius=58.f;
    BodyRadius=SafeNutValue(BodyRadius,58,20,260);
    HopHeight=SafeNutValue(HopHeight,14,0,40);
}

FMCNutRainSettings::FMCNutRainSettings()
{
    Entry.FlightSeconds=1.65f; Entry.EntryHeight=650; Entry.OutsideDistance=150;
    Enemy.MaxHealth=40; Enemy.MoveSpeed=135; Enemy.AttackDamage=8; Enemy.WindupSeconds=.75f; Enemy.AttackCooldown=2.3f; Enemy.BodyRadius=58;
}

void FMCNutRainSettings::Sanitize()
{
    NutCount=FMath::Clamp(NutCount,1,120); NutsPerExtraPlayer=FMath::Clamp(NutsPerExtraPlayer,0,30);
    EnemyCount=FMath::Clamp(EnemyCount,0,16); EnemiesPerExtraPlayer=FMath::Clamp(EnemiesPerExtraPlayer,0,4);
    MinimumSeries=FMath::Clamp(MinimumSeries,4,8); MaximumSeries=FMath::Clamp(MaximumSeries,MinimumSeries,8);
    // Migrate the old saved profile in memory. Legacy non-boss fixtures keep
    // their own count and duration; no editor package rewrite is required.
    if(bBossEncounter) {
        if(MinimumNutsPerSeries<=2) MinimumNutsPerSeries=6;
        if(MaximumNutsPerSeries<=4) MaximumNutsPerSeries=10;
        SeriesRestSeconds=0;
    }
    MinimumNutsPerSeries=FMath::Clamp(MinimumNutsPerSeries,4,12); MaximumNutsPerSeries=FMath::Clamp(MaximumNutsPerSeries,MinimumNutsPerSeries,12);
    MinimumDropSeconds=SafeNutValue(MinimumDropSeconds,1.5f,1.5f,3.f);
    MaximumDropSeconds=SafeNutValue(MaximumDropSeconds,3.f,MinimumDropSeconds,3.f);
    SeriesRestSeconds=SafeNutValue(SeriesRestSeconds,0,0,8.f);
    LargeNutHeight=SafeNutValue(LargeNutHeight,180,150,220); Boss.Sanitize();
    ImpactDamageLimit=SafeNutValue(ImpactDamageLimit,18,0,40);
    if(FMath::IsNearlyEqual(ImpactRadius,180.f)) ImpactRadius=300.f;
    ImpactRadius=SafeNutValue(ImpactRadius,300,80,480);
    ImpactPushSpeed=SafeNutValue(ImpactPushSpeed,260,0,500);
    RainSeconds=SafeNutValue(RainSeconds,22.5f,bBossEncounter?20:3,bBossEncounter?25:60); Entry.Sanitize(); Enemy.Sanitize();
    ClusterRadius=SafeNutValue(ClusterRadius,260,60,650);
    LandingShadowOpacity=SafeNutValue(LandingShadowOpacity,.20f,.03f,.35f);
    SettleSeconds=SafeNutValue(SettleSeconds,2,.5f,6);
    SettleSeconds=FMath::Max(SettleSeconds,Entry.FlightSeconds+.25f);
}

int32 FMCNutRainSettings::RainCountForPlayers(int32 Players) const
{
    FMCNutRainSettings Copy=*this; Copy.Sanitize();
    if(Copy.bBossEncounter) {
        const float Window=FMath::Max(0.f,Copy.RainSeconds-Copy.Entry.FlightSeconds);
        return FMath::Max(2,FMath::RoundToInt(Window/((Copy.MinimumDropSeconds+Copy.MaximumDropSeconds)*.5f))+1);
    }
    return FMath::Clamp(Copy.NutCount+Copy.NutsPerExtraPlayer*(FMath::Clamp(Players,1,8)-1),1,120);
}

int32 FMCNutRainSettings::EnemyCountForPlayers(int32 Players) const
{
    FMCNutRainSettings Copy=*this; Copy.Sanitize();
    return FMath::Clamp(Copy.EnemyCount+Copy.EnemiesPerExtraPlayer*(FMath::Clamp(Players,1,8)-1),0,16);
}

UMCNutRainProfile::UMCNutRainProfile()
{
    Menu=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/DT_NutRainMenu.DT_NutRainMenu")));
}
