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
    BodyRadius=SafeNutValue(BodyRadius,42,20,100);
    HopHeight=SafeNutValue(HopHeight,14,0,40);
}

FMCNutRainSettings::FMCNutRainSettings()
{
    Entry.FlightSeconds=1.65f; Entry.EntryHeight=650; Entry.OutsideDistance=150;
}

void FMCNutRainSettings::Sanitize()
{
    NutCount=FMath::Clamp(NutCount,1,120); NutsPerExtraPlayer=FMath::Clamp(NutsPerExtraPlayer,0,30);
    EnemyCount=FMath::Clamp(EnemyCount,0,16); EnemiesPerExtraPlayer=FMath::Clamp(EnemiesPerExtraPlayer,0,4);
    RainSeconds=SafeNutValue(RainSeconds,12,3,60); Entry.Sanitize(); Enemy.Sanitize();
    SettleSeconds=SafeNutValue(SettleSeconds,2,.5f,6);
    SettleSeconds=FMath::Max(SettleSeconds,Entry.FlightSeconds+.25f);
}

int32 FMCNutRainSettings::RainCountForPlayers(int32 Players) const
{
    FMCNutRainSettings Copy=*this; Copy.Sanitize();
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
