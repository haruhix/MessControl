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
    // Screenshot reference: dark group 2+4+4, magenta group 1+2+1+2+2.
    // Width is relative to each reachable tongue cross-section, so the same
    // staggered layout follows the narrower throat and wider front of the arena.
    PatternA={{.37,.10},{.68,.10},
        {.04,.43},{.32,.43},{.67,.43},{.96,.43},
        {.07,.79},{.33,.83},{.66,.80},{.94,.77}};
    PatternB={{.52,.02},{.22,.25},{.79,.26},{.52,.46},
        {.20,.65},{.82,.63},{.52,.91},{.05,.98}};
    Entry.FlightSeconds=1.65f; Entry.EntryHeight=650; Entry.OutsideDistance=150;
    Enemy.MaxHealth=40; Enemy.MoveSpeed=135; Enemy.AttackDamage=8; Enemy.WindupSeconds=.75f; Enemy.AttackCooldown=2.3f; Enemy.BodyRadius=58;
}

void FMCNutRainSettings::Sanitize()
{
    const FMCNutRainSettings Defaults;
    const auto SanitizePattern=[](TArray<FVector2D>& Zones,const TArray<FVector2D>& Fallback) {
        TArray<FVector2D> Valid;
        for(const FVector2D& Zone:Zones) {
            if(!FMath::IsFinite(Zone.X) || !FMath::IsFinite(Zone.Y)) continue;
            const FVector2D Point(FMath::Clamp(Zone.X,0.,1.),FMath::Clamp(Zone.Y,0.,1.));
            if(!Valid.ContainsByPredicate([&](const FVector2D& Existing) {return Existing.Equals(Point,1.e-5);})) Valid.Add(Point);
            if(Valid.Num()==32) break;
        }
        Zones=Valid.IsEmpty()?Fallback:MoveTemp(Valid);
    };
    SanitizePattern(PatternA,Defaults.PatternA); SanitizePattern(PatternB,Defaults.PatternB);
    PatternDropSeconds=SafeNutValue(PatternDropSeconds,.5f,.1f,3.f);
    PatternSeriesGapSeconds=SafeNutValue(PatternSeriesGapSeconds,.9f,.1f,8.f);
    NutCount=FMath::Clamp(NutCount,1,120); NutsPerExtraPlayer=FMath::Clamp(NutsPerExtraPlayer,0,30);
    EnemyCount=FMath::Clamp(EnemyCount,0,16); EnemiesPerExtraPlayer=FMath::Clamp(EnemiesPerExtraPlayer,0,4);
    MinimumSeries=FMath::Clamp(MinimumSeries,4,8); MaximumSeries=FMath::Clamp(MaximumSeries,MinimumSeries,8);
    MinimumNutsPerSeries=FMath::Clamp(MinimumNutsPerSeries,4,12); MaximumNutsPerSeries=FMath::Clamp(MaximumNutsPerSeries,MinimumNutsPerSeries,12);
    MinimumDropSeconds=SafeNutValue(MinimumDropSeconds,1.5f,1.5f,3.f);
    MaximumDropSeconds=SafeNutValue(MaximumDropSeconds,3.f,MinimumDropSeconds,3.f);
    SeriesRestSeconds=SafeNutValue(SeriesRestSeconds,0,0,8.f);
    LargeNutHeight=SafeNutValue(LargeNutHeight,180,150,220); Boss.Sanitize();
    ImpactDamageLimit=SafeNutValue(ImpactDamageLimit,18,0,40);
    if(FMath::IsNearlyEqual(ImpactRadius,180.f)) ImpactRadius=300.f;
    ImpactRadius=SafeNutValue(ImpactRadius,300,80,480);
    ImpactPushSpeed=SafeNutValue(ImpactPushSpeed,260,0,500);
    RainSeconds=SafeNutValue(RainSeconds,22.5f,3,60); Entry.Sanitize(); Enemy.Sanitize();
    if(bBossEncounter) RainSeconds=(PatternA.Num()+PatternB.Num()-2)*PatternDropSeconds+PatternSeriesGapSeconds+Entry.FlightSeconds;
    ClusterRadius=SafeNutValue(ClusterRadius,260,60,650);
    LandingShadowOpacity=SafeNutValue(LandingShadowOpacity,.20f,.03f,.35f);
    SettleSeconds=SafeNutValue(SettleSeconds,2,.5f,6);
    SettleSeconds=FMath::Max(SettleSeconds,Entry.FlightSeconds+.25f);
}

int32 FMCNutRainSettings::RainCountForPlayers(int32 Players) const
{
    FMCNutRainSettings Copy=*this; Copy.Sanitize();
    if(Copy.bBossEncounter) return Copy.PatternA.Num()+Copy.PatternB.Num();
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
