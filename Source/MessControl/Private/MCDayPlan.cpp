#include "MCDayPlan.h"
void FMCFoodRow::Sanitize()
{
    auto Safe=[](float V,float D,float Lo,float Hi){return FMath::IsFinite(V)?FMath::Clamp(V,Lo,Hi):D;};
    SelectionWeight=Safe(SelectionWeight,1,0,100); Health=Safe(Health,75,1,1000);
    Mass=Safe(Mass,9,1,50); SpoilSeconds=Safe(SpoilSeconds,35,3,600); Fragments=FMath::Clamp(Fragments,2,5);
    if (HalfExtent.ContainsNaN()) HalfExtent=FVector(45,35,35);
    HalfExtent=HalfExtent.GetAbs().BoundToBox(FVector(10),FVector(100));
}
UMCDayPlan::UMCDayPlan()
{
    CoffeeProfile=TSoftObjectPtr<UMCCoffeeProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_CoffeeWater.DA_CoffeeWater")));
    Menu=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu")));
    auto Add=[&](EMCDayStep Step,float Seconds,const TCHAR* Title,const TCHAR* Hint)
    { FMCDayStepSettings S; S.Step=Step; S.Seconds=Seconds; S.Title=FText::FromString(Title); S.Instruction=FText::FromString(Hint); Steps.Add(S); };
    Add(EMCDayStep::BrushLesson,0,TEXT("01 / LEARN TO BRUSH"),TEXT("E: pick up a falling brush. Hold LMB: clean teeth AND floor stains. No event timer."));
    Add(EMCDayStep::DiscardBrushes,0,TEXT("PUT BRUSHES OVERBOARD"),TEXT("Carry brushes to the front tray (away from throat). Q: throw. Brushes never go down the throat."));
    Add(EMCDayStep::BreakfastRain,2,TEXT("02 / BREAKFAST IS FALLING"),TEXT("Dodge the food! Broccoli, egg, bacon and carrot are chosen from the menu."));
    Add(EMCDayStep::BreakfastCleanup,20,TEXT("BREAKFAST / CLEAN UP"),TEXT("RMB: break food. Hold E: drag to THROAT. Q: throw held food. Protect red ulcers."));
    Add(EMCDayStep::CoffeeWaves,10,TEXT("COFFEE / SURVIVE FOUR WAVES"),TEXT("WASD: paddle. Hold E near an arena tooth: cling. Release E: let go."));
    Add(EMCDayStep::CoffeeCleanup,20,TEXT("COFFEE / BRUSH EVERYTHING"),TEXT("Fresh brushes fall in. E: pick up. LMB: teeth and floor. C: clean yourself."));
    Add(EMCDayStep::StuckFood,35,TEXT("03 / BETWEEN THE TEETH"),TEXT("Hold E + move towards centre to pull food free. Then drag it to THROAT."));
}
void UMCDayPlan::Sanitize()
{
    auto Safe=[](float V,float D,float Lo,float Hi){return FMath::IsFinite(V)?FMath::Clamp(V,Lo,Hi):D;};
    TargetDaySeconds=Safe(TargetDaySeconds,240,30,600);
    for (auto& S:Steps) { S.Seconds=Safe(S.Seconds,30,0,120); S.FailureDamage=Safe(S.FailureDamage,8,0,50); }
    BreakfastCount=FMath::Clamp(BreakfastCount,1,12); StuckCount=FMath::Clamp(StuckCount,1,8);
    SurfacePatches=FMath::Clamp(SurfacePatches,1,24); WaveCount=FMath::Clamp(WaveCount,1,8);
    FloodHeight=Safe(FloodHeight,155,60,240); FlowAcceleration=Safe(FlowAcceleration,320,0,800);
    PaddleAcceleration=Safe(PaddleAcceleration,400,0,800); AnchorReach=Safe(AnchorReach,160,60,250);
    UlcerHealSeconds=Safe(UlcerHealSeconds,15,1,120); UlcerDamagePerSecond=Safe(UlcerDamagePerSecond,.35,0,5);
    UlcerDisturbDamage=Safe(UlcerDisturbDamage,1,0,10);
    if (ArenaHalfSize.ContainsNaN()) ArenaHalfSize=FVector(1050,740,220);
    ArenaHalfSize=ArenaHalfSize.GetAbs().BoundToBox(FVector(500,300,100),FVector(3000,2000,600));
}
