#include "MCGameDirectorProfile.h"

namespace
{
float Finite(float Value,float Default,float Minimum,float Maximum)
{
    return FMath::Clamp(FMath::IsFinite(Value)?Value:Default,Minimum,Maximum);
}
TArray<FMCGameDirectorEventRule> DefaultEventRules()
{
    TArray<FMCGameDirectorEventRule> Rules;
    auto Add=[&](EMCGameDirectorEvent Kind,float Weight,float Cooldown,int32 FirstDay,float MinimumDifficulty,int32 MaxPerDay=0) {
        auto& Rule=Rules.AddDefaulted_GetRef();
        Rule.Kind=Kind; Rule.Weight=Weight; Rule.CooldownSeconds=Cooldown; Rule.FirstDay=FirstDay;
        Rule.MinimumDifficulty=MinimumDifficulty; Rule.MaxPerDay=MaxPerDay;
    };
    Add(EMCGameDirectorEvent::Food,5,8,1,0);
    Add(EMCGameDirectorEvent::Coffee,4.5f,10,1,0);
    Add(EMCGameDirectorEvent::CoffeeFlood,.6f,75,2,.8f);
    Add(EMCGameDirectorEvent::ColdCola,.7f,85,1,.7f);
    Add(EMCGameDirectorEvent::Yawn,1,45,1,.65f);
    Add(EMCGameDirectorEvent::Pepper,.6f,60,3,.8f);
    Add(EMCGameDirectorEvent::StuckFood,.8f,40,1,0);
    Add(EMCGameDirectorEvent::LooseTooth,.7f,55,2,0);
    Add(EMCGameDirectorEvent::Reward,1.2f,65,1,0);
    Add(EMCGameDirectorEvent::Boss,0,180,7,1,1);
    return Rules;
}
}

void FMCGameDirectorEventRule::Sanitize()
{
    // Invalid weights fail closed, while a designer-authored zero stays zero.
    Weight=Finite(Weight,0,0,1000);
    CooldownSeconds=Finite(CooldownSeconds,60,0,3600);
    FirstDay=FMath::Clamp(FirstDay,1,7);
    MinimumDifficulty=Finite(MinimumDifficulty,0,0,3);
    MaxPerDay=FMath::Clamp(MaxPerDay,0,1000);
}

void FMCGameDirectorDaySettings::Sanitize()
{
    DaySeconds=Finite(DaySeconds,360,60,1200);
    CoffeePatches=FMath::Clamp(CoffeePatches,1,20); InitialPatches=FMath::Clamp(InitialPatches,0,20);
    MaxFoodBatch=FMath::Clamp(MaxFoodBatch,1,4); MaxWholeFood=FMath::Clamp(MaxWholeFood,1,12); MaxFragments=FMath::Clamp(MaxFragments,4,80);
    FoodInterval=Finite(FoodInterval,6,1,30); FoodWorkPerPlayer=Finite(FoodWorkPerPlayer,30,5,120); PressureLimit=Finite(PressureLimit,.9f,.2f,3);
    TargetPressureMin=Finite(TargetPressureMin,.18f,0,PressureLimit);
    TargetPressureMax=Finite(TargetPressureMax,.48f,TargetPressureMin,PressureLimit);
    MinimumDifficulty=Finite(MinimumDifficulty,.5f,0,3);
    MaximumDifficulty=Finite(MaximumDifficulty,.85f,MinimumDifficulty,3);
    EventGap=Finite(EventGap,12,1,60); RestSeconds=Finite(RestSeconds,12,0,30); BuildSeconds=Finite(BuildSeconds,35,1,120);
    FinalCleanupSeconds=Finite(FinalCleanupSeconds,60,10,FMath::Min(180.f,DaySeconds*.5f));
}
UMCGameDirectorProfile::UMCGameDirectorProfile()
{
    Events=DefaultEventRules();
    for(int32 I=0;I<7;++I) {
        auto& D=Days.AddDefaulted_GetRef(); D.DaySeconds=360+I*20; D.CoffeePatches=3+I/2;
        D.TargetPressureMin=.18f+I*.025f; D.TargetPressureMax=.48f+I*.045f;
        D.MinimumDifficulty=.5f; D.MaximumDifficulty=.85f+I*.075f; D.InitialPatches=2;
        D.MaxWholeFood=3+I/3; D.MaxFragments=18+I*2; D.FoodWorkPerPlayer=30+I*3;
        D.PressureLimit=.9f+I*.06f; D.EventGap=12-I*.5f; D.RestSeconds=12-I;
        D.FinalCleanupSeconds=60+I*5;
    }
}
FMCGameDirectorDaySettings UMCGameDirectorProfile::GetScaledDaySettings(int32 DayIndex) const
{
    auto Day=Days.IsValidIndex(DayIndex)?Days[DayIndex]:FMCGameDirectorDaySettings();
    const float Scale=Finite(DifficultyMultiplier,1,.25f,10);
    // InitializeRun sanitizes the authored base before this is called. Its 0..3 bounds must not
    // silently flatten later days when the whole run is made harder.
    Day.MinimumDifficulty*=Scale; Day.MaximumDifficulty*=Scale;
    Day.TargetPressureMin*=Scale; Day.TargetPressureMax*=Scale; Day.PressureLimit*=Scale;
    Day.FoodWorkPerPlayer*=Scale;
    // These remain physical arena safety caps, rather than difficulty targets.
    Day.MaxWholeFood=FMath::Clamp(FMath::CeilToInt(Day.MaxWholeFood*Scale),1,12);
    Day.MaxFragments=FMath::Clamp(FMath::CeilToInt(Day.MaxFragments*Scale),4,80);
    return Day;
}
void UMCGameDirectorProfile::Sanitize()
{
    const auto* Defaults=GetDefault<UMCGameDirectorProfile>();
    while(Days.Num()<7) Days.Add(Defaults->Days[Days.Num()]);
    Days.SetNum(7); for(auto& Day:Days) Day.Sanitize();
    TArray<FMCGameDirectorEventRule> UniqueRules;
    // Canonical order makes missing/unknown/duplicate kinds unambiguous. Keep
    // the first authored rule, including zero, and default only missing kinds.
    for(const auto& Default:DefaultEventRules()) {
        const auto* Authored=Events.FindByPredicate([&](const auto& Rule) {return Rule.Kind==Default.Kind;});
        auto Rule=Authored?*Authored:Default; Rule.Sanitize(); UniqueRules.Add(Rule);
    }
    Events=MoveTemp(UniqueRules);
    DifficultyMultiplier=Finite(DifficultyMultiplier,1,.25f,10);
    DecisionInterval=Finite(DecisionInterval,3,.25f,30); AdaptationSeconds=Finite(AdaptationSeconds,20,1,120);
    InitialCleaningSeconds=Finite(InitialCleaningSeconds,8,1,30); TeamScaling=Finite(TeamScaling,.35f,0,.75f);
    WarningSeconds=Finite(WarningSeconds,2,1,5); IntermissionSeconds=Finite(IntermissionSeconds,7,1,30);
    MissedTaskDamage=Finite(MissedTaskDamage,1,0,10); CleaningWorkerSeconds=Finite(CleaningWorkerSeconds,8,1,30); RepairWorkerSeconds=Finite(RepairWorkerSeconds,10,1,60);
}
