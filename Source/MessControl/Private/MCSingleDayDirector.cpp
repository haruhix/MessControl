#include "MCSingleDayDirector.h"
#include "MCCoffeeFlood.h"
#include "MCCataclysmEvent.h"
#include "MCNutRainEvent.h"
#include "MCNutBoss.h"
#include "MCIceEvent.h"
#include "MCFogBrawlEvent.h"
#include "MCNutRainProfile.h"
#include "MCGameDirector.h"
#include "MCGameDirectorProfile.h"
#include "MCFoodActor.h"
#include "MCFoodDirectorHooks.h"
#include "MCDayDirector.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCProgressionComponent.h"
#include "MCBossCharacter.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCDayPlan.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace MCSingleDayDirectorPrivate
{
struct FTeamCondition { float Health=.5f; float Stamina=.5f; };
FTeamCondition ObserveTeam(UWorld* World)
{
    FTeamCondition Result; int32 Count=0; float Health=0,Stamina=0;
    if (!World) return Result;
    for (TActorIterator<AMCToothCharacter> It(World);It;++It) {
        if (It->IsActorBeingDestroyed() || !It->Status) continue;
        ++Count;
        if (!It->Status->IsAlive()) continue;
        Health+=FMath::Clamp(It->Status->State.Health/FMath::Max(1.f,It->Status->State.MaxHealth),0.f,1.f);
        Stamina+=FMath::Clamp(It->GetStaminaNormalized(),0.f,1.f);
    }
    if (Count>0) { Result.Health=Health/Count; Result.Stamina=Stamina/Count; }
    return Result;
}
bool NeedsRelief(const FTeamCondition& Team) { return Team.Health<.5f || Team.Stamina<.25f; }
bool IsHealthy(const FTeamCondition& Team) { return Team.Health>.8f && Team.Stamina>.55f; }
int32 CountDirtyAvailableArenaTeeth(UWorld* World)
{
    if(!World) return 0;
    int32 Count=0;
    // Observe physical teeth, just like the adaptive Director; the HUD's icon
    // count and the portion's Batch are not limits on which teeth need care.
    for(TActorIterator<AMCArenaTooth> It(World);It;++It)
        if(!It->IsActorBeingDestroyed() && It->IsAvailable() && It->Status && It->Status->NeedsCare(true)) ++Count;
    return Count;
}
int32 CountDirtyFloorSurfaces(UWorld* World)
{
    if(!World) return 0;
    int32 Count=0;
    // Both ordinary stains and painted coffee tiles use this cleanable actor.
    // No Batch/tag filter: old or unrelated visible floor dirt also needs care.
    for(TActorIterator<AMCMouthSurface> It(World);It;++It)
        if(!It->IsActorBeingDestroyed() && !It->bUlcer && !It->IsClean()) ++Count;
    return Count;
}
}
UMCSingleDayProfile::UMCSingleDayProfile()
{
    KeyEvents.AddDefaulted();
    auto& Jellies=KeyEvents.AddDefaulted_GetRef();
    Jellies.EventId=TEXT("Jellies");Jellies.Kind=EMCSingleDayKeyEventKind::Jellies;
    auto& Ice=KeyEvents.AddDefaulted_GetRef();
    Ice.EventId=TEXT("IceEvent");
    Ice.Kind=EMCSingleDayKeyEventKind::IceEvent;
    auto& Fog=KeyEvents.AddDefaulted_GetRef();
    Fog.EventId=TEXT("FogBrawl");
    Fog.Kind=EMCSingleDayKeyEventKind::FogBrawl;
}
AMCSingleDayDirector::AMCSingleDayDirector()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.1f;
}
void AMCSingleDayDirector::Initialize(UMCDayPlan* Plan, UMCSingleDayProfile* Profile, UMCGameDirectorProfile* InterludeProfile)
{
    if(!HasAuthority()) return;
    ResetStageRewards();
    Mechanics=Plan?Plan:NewObject<UMCDayPlan>(this);
    Settings=Profile?DuplicateObject<UMCSingleDayProfile>(Profile,this):NewObject<UMCSingleDayProfile>(this);
    if(Settings->OpeningMealPortions.IsEmpty()) Settings->OpeningMealPortions={3,5,8};
    if(Settings->OpeningMealPortions.Num()>30) Settings->OpeningMealPortions.SetNum(30);
    for(int32& Count:Settings->OpeningMealPortions) Count=FMath::Clamp(Count,1,30);
    Settings->BetweenMealPauseMinSeconds=FMath::IsFinite(Settings->BetweenMealPauseMinSeconds)
        ?FMath::Clamp(Settings->BetweenMealPauseMinSeconds,0.f,30.f):5.f;
    Settings->BetweenMealPauseMaxSeconds=FMath::IsFinite(Settings->BetweenMealPauseMaxSeconds)
        ?FMath::Clamp(Settings->BetweenMealPauseMaxSeconds,Settings->BetweenMealPauseMinSeconds,30.f)
        :FMath::Max(Settings->BetweenMealPauseMinSeconds,10.f);
    if(Settings->AfterNutMealPortions.IsEmpty()) Settings->AfterNutMealPortions={5,8,9};
    if(Settings->AfterNutMealPortions.Num()>30) Settings->AfterNutMealPortions.SetNum(30);
    for(int32& Count:Settings->AfterNutMealPortions) Count=FMath::Clamp(Count,1,30);
    Settings->AfterNutMealDropSeconds=FMath::IsFinite(Settings->AfterNutMealDropSeconds)?FMath::Clamp(Settings->AfterNutMealDropSeconds,.5f,5.f):1.f;
    Settings->SmallMint.Sanitize();
    // Extend the previous prototype shapes in the runtime copy only, preserving
    // saved identities, variants and support intervals. Custom sequences stay authored.
    if(!Settings->bLegacyTimedFinale && Settings->KeyEvents.Num()==1
        && Settings->KeyEvents[0].Kind==EMCSingleDayKeyEventKind::NutEncounter) {
        auto& Ice=Settings->KeyEvents.AddDefaulted_GetRef();
        Ice.EventId=TEXT("IceEvent");
        Ice.Kind=EMCSingleDayKeyEventKind::IceEvent;
    }
    if(!Settings->bLegacyTimedFinale && Settings->KeyEvents.Num()==2
        && Settings->KeyEvents[0].Kind==EMCSingleDayKeyEventKind::NutEncounter
        && Settings->KeyEvents[1].Kind==EMCSingleDayKeyEventKind::IceEvent) {
        auto& Fog=Settings->KeyEvents.AddDefaulted_GetRef();
        Fog.EventId=TEXT("FogBrawl");
        Fog.Kind=EMCSingleDayKeyEventKind::FogBrawl;
    }
    DirectorProfile=InterludeProfile;
    Stage=EMCSingleDayStage::Training; bStopped=false;
    KeyEventIndex=0; bAuthoredFragmentComplete=false; InterludeEndsAt=StageEndsAt=0;
    FirstMealBatch=PostNutMealBatch=0; MealAttempts=0;
    MealPortionNumber=MealPortionTarget=MealPortionSpawned=0;
    MealPortionCount=Settings->OpeningMealPortions.Num();
    LastMealGateState.Reset(); NextMealDiagnosticAt=0;
    RunStartedAt=GetWorld()->GetTimeSeconds();
    if(auto* GS=GetWorld()->GetGameState<AMCGameState>()) {
        GS->RunSettings.DaysToSurvive=1; GS->Day=1;
        FRandomStream Roll(GS->RunSeed);
        VariantIndex=Settings->NutRainVariants.IsEmpty()?0:Roll.RandRange(0,Settings->NutRainVariants.Num()-1);
    }
    Record(FString::Printf(TEXT("Начат новый забег: ориентир %.0f–%.0f минут, авторено ключевых событий %d"),
        Settings->RunTargetMinMinutes,Settings->RunTargetMaxMinutes,Settings->KeyEvents.Num()));
    ForceNetUpdate();
}
int32 AMCSingleDayDirector::ChooseNutSeriesSize(int32 Minimum,int32 Maximum,int32 CompletedSeries) const
{
    const int32 Low=FMath::Clamp(Minimum,1,32),High=FMath::Clamp(Maximum,Low,32);
    const auto Team=MCSingleDayDirectorPrivate::ObserveTeam(GetWorld());
    if (MCSingleDayDirectorPrivate::NeedsRelief(Team)) return Low;
    int32 Size=MCSingleDayDirectorPrivate::IsHealthy(Team)?High:(Low+High)/2;
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    FRandomStream Choice((GS?GS->RunSeed:0)+FMath::Max(0,CompletedSeries)*104729+KeyEventIndex*7919);
    const float Variation=Choice.FRand();
    if (Variation<.1f) --Size; else if (Variation>.9f) ++Size;
    return FMath::Clamp(Size,Low,High);
}
bool AMCSingleDayDirector::ShouldFinishNutRain(int32 /*CompletedSeries*/,int32 /*MinimumSeries*/,float RainAge,float TargetSeconds) const
{
    const float Target=FMath::IsFinite(TargetSeconds)?FMath::Clamp(TargetSeconds,20.f,25.f):22.5f;
    if (!FMath::IsFinite(RainAge) || RainAge<Target) return false;
    // Team pressure already chose storm density and duration at the start.
    // Once the final nut lands, do not add a breather or wait for debris cleanup.
    return true;
}
void AMCSingleDayDirector::Record(const FString& Decision)
{
    if (auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr) {
        const int32 Seconds=FMath::Max(0,int32(GetWorld()->GetTimeSeconds()-RunStartedAt));
        GS->RecordDirectorDecision(FString::Printf(TEXT("Забег %02d:%02d · %s"),Seconds/60,Seconds%60,*Decision));
    }
    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY %s"),*Decision);
}
void AMCSingleDayDirector::BeginFirstPerk()
{
    if(!HasAuthority() || Stage!=EMCSingleDayStage::Training || bStopped) return;
    Stage=EMCSingleDayStage::FirstPerk;
    Record(TEXT("Обучение завершено: каждый участник выбирает личный перк"));
    Publish(TEXT("ПЕРВОЕ УСИЛЕНИЕ"),TEXT("Задания дают общий опыт. Каждый выбирает свой перк: 1 / 2 / 3."));
    ForceNetUpdate();
}
void AMCSingleDayDirector::Publish(const FString& Title,const FString& Instruction,int32 Left,int32 Total)
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=StageEndsAt;
    GS->TasksLeft=Left; GS->TasksTotal=Total;
    auto& S=GS->DirectorState; S=FMCGameDirectorState(); S.bEnabled=true;
    S.CurrentTitle=Title; S.Instruction=Instruction;
    const bool BeforeMeal=(!IsLegacyTimedFinale() && Stage==EMCSingleDayStage::FirstPerk) || Stage==EMCSingleDayStage::OpeningPause;
    S.NextTitle=BeforeMeal || Stage==EMCSingleDayStage::BetweenMealsPause?TEXT("ПРИЁМ ПИЩИ"):
        Stage==EMCSingleDayStage::PostNutMeal?(MealPortionNumber<MealPortionCount?TEXT("ПРИЁМ ПИЩИ"):TEXT("МЯТНАЯ КОНФЕТА")):
        Stage==EMCSingleDayStage::PostNutMint?TEXT("ГРЯЗНОЕ ЖЕЛЕ"):
        Stage==EMCSingleDayStage::MealCoffeeWave?TEXT("ПЕРЕДЫШКА"):
        Stage==EMCSingleDayStage::FirstMeal && MealPortionNumber<MealPortionCount?TEXT("КОФЕЙНАЯ ВОЛНА"):
        Stage==EMCSingleDayStage::FirstMeal || Stage==EMCSingleDayStage::MealRest?TEXT("ОРЕХОВОЕ СОБЫТИЕ"):
        Stage==EMCSingleDayStage::Nuts && Settings && Settings->KeyEvents.IsValidIndex(KeyEventIndex+1)
            && Settings->KeyEvents[KeyEventIndex+1].Kind==EMCSingleDayKeyEventKind::Jellies?TEXT("ПЕРЕДЫШКА ПОСЛЕ ОРЕХОВ"):
        Stage==EMCSingleDayStage::Nuts || Stage==EMCSingleDayStage::Ice || Stage==EMCSingleDayStage::FogBrawl || Stage==EMCSingleDayStage::Jellies?TEXT("ПОДДЕРЖКА ДИРЕКТОРА"):
        IsLegacyTimedFinale() && Stage==EMCSingleDayStage::Director?TEXT("ФИНАЛЬНЫЙ БОСС"):TEXT("");
    if((Stage==EMCSingleDayStage::FirstMeal || Stage==EMCSingleDayStage::MealCoffeeWave
        || Stage==EMCSingleDayStage::BetweenMealsPause || Stage==EMCSingleDayStage::PostNutMeal) && DirectorProfile)
        S.Difficulty=DirectorProfile->GetScaledDaySettings(0).MinimumDifficulty;
    S.DecisionLog=GS->DirectorDecisionLog;
    S.LastDecision=S.DecisionLog.IsEmpty()?FString():S.DecisionLog.Last();
    S.SpawnedFood=Total; S.FinishedFood=FMath::Max(0,Total-Left);
    S.CompletionProgress=Total>0?1-float(Left)/Total:0;
    S.DayProgress=Stage==EMCSingleDayStage::Nuts?.25f:Stage==EMCSingleDayStage::PostNutMeal?.35f:Stage==EMCSingleDayStage::PostNutMint?.4f:
        Stage==EMCSingleDayStage::Jellies?.45f:Stage==EMCSingleDayStage::Ice?.55f:Stage==EMCSingleDayStage::FogBrawl?.75f:Stage==EMCSingleDayStage::Boss?.85f:.1f;
    GS->ForceNetUpdate();
}
void AMCSingleDayDirector::BeginOpeningPause(bool bAfterMeal)
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    Stage=bAfterMeal?EMCSingleDayStage::MealRest:EMCSingleDayStage::OpeningPause;
    const float Configured=bAfterMeal?Settings->BeforeNutsPauseSeconds:Settings->AfterTrainingPauseSeconds;
    const float Seconds=FMath::IsFinite(Configured)?FMath::Clamp(Configured,0.f,30.f):(bAfterMeal?4.f:5.f);
    StageEndsAt=GS->GetServerWorldTimeSeconds()+Seconds;
    Record(bAfterMeal?TEXT("Приём пищи завершён: короткая передышка перед орехами"):
        TEXT("Первый перк выбран: короткая передышка после обучения"));
    Publish(TEXT("ПЕРЕДЫШКА"),bAfterMeal?TEXT("Приготовьтесь уворачиваться от падающих орехов."):
        TEXT("Обычная еда скоро появится. Подготовьте нож, кирку и щётку."));
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginFirstMeal()
{
    FirstMealBatch=2000000+int32(GetUniqueID()&0x000FFFFF);
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    MealRandom.Initialize((GS?GS->RunSeed:41)^0x4D45414C);
    MealPortionCount=Settings->OpeningMealPortions.Num();
    MealServices=GetWorld()->SpawnActor<AMCDayDirector>();
    if(!MealServices) {Fail(TEXT("Не удалось запустить обычный приём пищи."));return;}
    MealServices->SetOwner(this);
    MealServices->InitializeEventServices(Mechanics,GS?GS->RunSeed:41);
    Record(FString::Printf(TEXT("Начало первого Relax: %d порции еды для всей команды, без начальной кофейной волны"),MealPortionCount));
    BeginMealPortion(0);
}
void AMCSingleDayDirector::BeginMealPortion(int32 PortionIndex)
{
    if(!Settings || !Settings->OpeningMealPortions.IsValidIndex(PortionIndex)) {
        Fail(TEXT("Не найдены настройки следующей порции еды."));return;
    }
    Stage=EMCSingleDayStage::FirstMeal; StageEndsAt=0;
    MealPortionNumber=PortionIndex+1;
    MealPortionTarget=Settings->OpeningMealPortions[PortionIndex];
    MealPortionSpawned=MealAttempts=0;
    LastMealGateState.Reset(); NextMealDiagnosticAt=0;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    NextMealDropAt=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    Record(FString::Printf(TEXT("Первый Relax: порция %d/%d, целых продуктов %d; предыдущая грязь остаётся"),
        MealPortionNumber,MealPortionCount,MealPortionTarget));
    Publish(TEXT("ПРИЁМ ПИЩИ"),FString::Printf(TEXT("Порция %d/%d: обработайте %d продуктов. Q — закинуть фрагмент в глотку. Щётка — очистить язык и зубы."),
        MealPortionNumber,MealPortionCount,MealPortionTarget),MealPortionTarget,MealPortionTarget);
    ForceNetUpdate();
}
FName AMCSingleDayDirector::ChooseOpeningFood()
{
    UDataTable* Menu=Mechanics?Mechanics->Menu.LoadSynchronous():nullptr;
    TArray<FName> Names=Menu?Menu->GetRowNames():TArray<FName>(); Names.Sort(FNameLexicalLess());
    TArray<FName> Eligible; TArray<float> Weights; float Total=0;
    for(FName Name:Names) if(const auto* Row=Menu->FindRow<FMCFoodRow>(Name,TEXT("Opening meal"),false))
        if(Row->Kind==EMCFoodKind::Food && !Row->WholeMeshes.IsEmpty() &&
            FMath::IsFinite(Row->SelectionWeight) && Row->SelectionWeight>0) {
            Eligible.Add(Name); Weights.Add(Row->SelectionWeight); Total+=Row->SelectionWeight;
        }
    if(Eligible.IsEmpty() || !FMath::IsFinite(Total)) return NAME_None;
    float Pick=MealRandom.FRand()*Total;
    for(int32 Index=0;Index<Eligible.Num();++Index) {Pick-=Weights[Index];if(Pick<=0) return Eligible[Index];}
    return Eligible.Last();
}
void AMCSingleDayDirector::TickFirstMeal()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    const int32 Target=MealPortionTarget;
    const double Time=GS->GetServerWorldTimeSeconds();
    const auto CurrentFood=MCMeasureFoodPipeline(GetWorld());
    int32 Workers=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->GetController() && It->Status && It->Status->IsAlive()) ++Workers;
    const int32 WholeLimit=FMath::Clamp(6+2*(FMath::Max(1,Workers)-1),6,12);
    if(MealPortionSpawned<Target && Time>=NextMealDropAt && CurrentFood.Whole<WholeLimit && CurrentFood.Fragments<48
        && !CurrentFood.bThroatBusy) {
        const FName Row=ChooseOpeningFood(); ++MealAttempts;
        if(auto* Food=MCSpawnDirectedFoodEntry(GetWorld(),Mechanics,Row,FirstMealBatch,MealRandom,MealServices)) {
            Food->SetOwner(this); ++MealPortionSpawned; ForceNetUpdate();
        }
        else if(MealAttempts>=Target*6) {
            Fail(TEXT("Не удалось подать обычную еду. Проверьте меню, язык и коллизию продуктов.")); return;
        }
        const float Configured=Settings->OpeningMealDropSeconds;
        NextMealDropAt=Time+(FMath::IsFinite(Configured)?FMath::Clamp(Configured,.5f,5.f):1.f);
    }
    // Each ordinary item remains outstanding until destroyed. Retained transport
    // and swallowing also stay outstanding until their existing work ends.
    const bool bFinalPortion=MealPortionNumber>=MealPortionCount;
    const auto Food=MCMeasureFoodPipeline(GetWorld(),bFinalPortion?INDEX_NONE:FirstMealBatch);
    const int32 Dirt=MCSingleDayDirectorPrivate::CountDirtyFloorSurfaces(GetWorld());
    const int32 DirtyTeeth=MCSingleDayDirectorPrivate::CountDirtyAvailableArenaTeeth(GetWorld());
    if(MealPortionSpawned>=Target && Food.OutstandingActors==0 && Food.UnresolvedHazards==0) {
        if(Dirt==0 && DirtyTeeth==0) {
            BeginStageReward(FName(*FString::Printf(TEXT("OpeningPortion_%d"),MealPortionNumber)),
                bFinalPortion?EMCSingleDayRewardContinuation::OpeningRest:EMCSingleDayRewardContinuation::OpeningCoffee);
            return;
        }
    }
    const int32 Pending=FMath::Max(0,Target-MealPortionSpawned);
    ReportMealGate(Food,Dirt,DirtyTeeth,Pending,Time);
    const int32 Left=Pending+Food.OutstandingActors+Food.UnresolvedHazards+Dirt+DirtyTeeth;
    const FString Instruction=FString::Printf(
        TEXT("Порция %d/%d · подано %d/%d. Осталось: еда %d · фрагменты %d (в руках %d) · пятна %d · грязные зубы %d · угрозы %d.\nQ — закинуть фрагмент в глотку. Щётка — очистить язык и зубы.%s"),
        MealPortionNumber,MealPortionCount,MealPortionSpawned,Target,Food.Whole,Food.Fragments,Food.Carried,Dirt,DirtyTeeth,
        FMath::Max(0,Food.OutstandingActors-Food.Whole-Food.Fragments)+Food.UnresolvedHazards,
        TEXT(" Очистите язык и зубы: после завершённой порции появится общий сундук с бустером."));
    Publish(TEXT("ПРИЁМ ПИЩИ"),Instruction,
        Left,FMath::Max(GS->TasksTotal,Left));
}
void AMCSingleDayDirector::BeginMealCoffeeWave()
{
    Stage=EMCSingleDayStage::MealCoffeeWave; StageEndsAt=0;
    Publish(TEXT("КОФЕЙНАЯ ВОЛНА"),FString::Printf(TEXT("Порция %d/%d обработана. Держитесь за опору; следующая порция появится после кофе и короткой передышки."),
        MealPortionNumber,MealPortionCount));
    OpeningCoffee=GetWorld()->SpawnActor<AMCCoffeeFlood>();
    if(!OpeningCoffee) {Fail(TEXT("Не удалось создать волну кофе между порциями."));return;}
    OpeningCoffee->SetOwner(this); OpeningCoffee->ResidueBatch=FirstMealBatch;
    OpeningCoffee->Start(Mechanics);
    if(!OpeningCoffee->IsActive()) {Fail(TEXT("Не удалось запустить волну кофе между порциями."));return;}
    // Start copies authored settings before initializing residue. Keep that
    // saved Profile reference for client visuals; limit only this owned drink.
    OpeningCoffee->WaterSettings.Cycles=1;
    OpeningCoffee->Waves=1;
    OpeningCoffee->Seconds=OpeningCoffee->WaterSettings.CycleSeconds();
    OpeningCoffee->ForceNetUpdate();
    Record(FString::Printf(TEXT("Порция %d/%d обработана: кофейная волна, накопленная грязь сохраняется"),MealPortionNumber,MealPortionCount));
    ForceNetUpdate();
}
void AMCSingleDayDirector::TickMealCoffeeWave()
{
    if(!IsValid(OpeningCoffee)) {Fail(TEXT("Кофейная волна исчезла до завершения."));return;}
    if(OpeningCoffee->IsActive()) return;
    OpeningCoffee->Destroy(); OpeningCoffee=nullptr;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Pause=MealRandom.FRandRange(Settings->BetweenMealPauseMinSeconds,Settings->BetweenMealPauseMaxSeconds);
    Stage=EMCSingleDayStage::BetweenMealsPause; StageEndsAt=Now+Pause;
    Record(FString::Printf(TEXT("Кофе после порции %d/%d завершён: передышка %.2f с отсчитывается от конца волны"),MealPortionNumber,MealPortionCount,Pause));
    Publish(TEXT("ПЕРЕДЫШКА"),FString::Printf(TEXT("Кофе закончился. Порция %d/%d скоро появится; можно продолжать уборку."),MealPortionNumber+1,MealPortionCount));
    ForceNetUpdate();
}
void AMCSingleDayDirector::TickBetweenMealsPause()
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(Now>=StageEndsAt) BeginMealPortion(MealPortionNumber);
}
void AMCSingleDayDirector::ReportMealGate(const FMCFoodPipelineLoad& Food,int32 Dirt,int32 DirtyTeeth,int32 Pending,double Now)
{
    // The global cleanliness also includes players. Report the actual portion
    // and available arena-tooth gate instead of using a rounded percentage.
    const bool bFinalPortion=MealPortionNumber>=MealPortionCount;
    const FString State=FString::Printf(TEXT("portion=%d/%d served=%d/%d pending=%d whole=%d fragments=%d carried=%d swallowing=%d outstanding=%d hazards=%d dirt=%d teeth=%d cleanup_gate=%d"),
        MealPortionNumber,MealPortionCount,MealPortionSpawned,MealPortionTarget,
        Pending,Food.Whole,Food.Fragments,Food.Carried,Food.Swallowing,Food.OutstandingActors,Food.UnresolvedHazards,Dirt,DirtyTeeth,true);
    const bool Changed=State!=LastMealGateState;
    if(Changed) {
        LastMealGateState=State;
        if(Pending==0) Record(FString::Printf(TEXT("Порция %d/%d: еда %d · пятна %d · грязные зубы %d · последствия %d. %s"),
            MealPortionNumber,MealPortionCount,Food.OutstandingActors,Dirt,DirtyTeeth,Food.UnresolvedHazards,
            TEXT("Очистите язык и зубы: затем команда получит общий сундук с бустером.")));
    }
    if(!Changed && Now<NextMealDiagnosticAt) return;
    UE_LOG(LogTemp,Display,TEXT("MC_MEAL_GATE batch=%d %s"),FirstMealBatch,*State);
    if(Now<NextMealDiagnosticAt) return;
    NextMealDiagnosticAt=Now+15.;
    int32 Logged=0;
    for(TActorIterator<AMCFoodActor> It(GetWorld());It && Logged<6;++It) {
        if(It->IsActorBeingDestroyed() || (!bFinalPortion && It->Batch!=FirstMealBatch) || It->bBrushTool || It->IsDisposed()) continue;
        ++Logged;
        UE_LOG(LogTemp,Display,TEXT("MC_MEAL_BLOCKER food=%s item=%s fragment=%d phase=%d location=%s"),
            *It->GetName(),*It->ItemName.ToString(),It->bFragment,int32(It->Phase),*It->GetActorLocation().ToString());
    }
    Logged=0;
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It && Logged<6;++It) {
        if(It->IsActorBeingDestroyed() || It->bUlcer || It->IsClean()) continue;
        ++Logged;
        UE_LOG(LogTemp,Display,TEXT("MC_MEAL_BLOCKER stain=%s tile=%d layers=%d remaining=%.6f location=%s"),
            *It->GetName(),It->bPaintTile,It->Status->State.CoffeeLeft,It->RemainingLiquid(),*It->GetActorLocation().ToString());
    }
    Logged=0;
    for(TActorIterator<AMCArenaTooth> It(GetWorld());It && Logged<6;++It) {
        if(It->IsActorBeingDestroyed() || !It->IsAvailable() || !It->Status || !It->Status->NeedsCare(true)) continue;
        ++Logged;
        UE_LOG(LogTemp,Display,TEXT("MC_MEAL_BLOCKER tooth=%s id=%d layers=%d health=%.1f location=%s"),
            *It->GetName(),It->State.ToothId,It->Status->State.CoffeeLeft,It->Status->State.Health,*It->GetActorLocation().ToString());
    }
}
void AMCSingleDayDirector::BeginKeyEvent()
{
    StageEndsAt=0;
    if (!IsLegacyTimedFinale() && !Settings->KeyEvents.IsValidIndex(KeyEventIndex)) {
        BeginDirector(); return;
    }
    if(IsLegacyTimedFinale()) {BeginNuts();return;}
    switch(Settings->KeyEvents[KeyEventIndex].Kind) {
        case EMCSingleDayKeyEventKind::NutEncounter: BeginNuts(); break;
        case EMCSingleDayKeyEventKind::IceEvent: BeginIce(); break;
        case EMCSingleDayKeyEventKind::FogBrawl: BeginFogBrawl(); break;
        case EMCSingleDayKeyEventKind::Jellies: BeginJellies(); break;
        default: Fail(TEXT("Ключевое событие ещё не реализовано.")); break;
    }
}
void AMCSingleDayDirector::BeginNuts()
{
    if (Interlude) {Interlude->Stop();Interlude->Destroy();Interlude=nullptr;}
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) {
        if (IsValid(Mode->GameDirector)) {Mode->GameDirector->Stop();Mode->GameDirector->Destroy();}
        Mode->GameDirector=nullptr;
    }
    if (IsValid(NutEvent)) {NutEvent->Stop();NutEvent->Destroy();NutEvent=nullptr;}
    if (IsValid(IceEvent)) {IceEvent->Stop();IceEvent->Destroy();IceEvent=nullptr;}
    if (IsValid(FogEvent)) {FogEvent->Stop();FogEvent->Destroy();FogEvent=nullptr;}
    Stage=EMCSingleDayStage::Nuts;
    NutEvent=GetWorld()->SpawnActor<AMCNutRainEvent>();
    if(!NutEvent) { Fail(TEXT("Не удалось создать ореховое событие.")); return; }
    NutEvent->SetOwner(this);
    const auto& SlotVariants=!IsLegacyTimedFinale()?Settings->KeyEvents[KeyEventIndex].NutRainVariants:Settings->NutRainVariants;
    const auto& Variants=SlotVariants.IsEmpty()?Settings->NutRainVariants:SlotVariants;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    FRandomStream Roll((GS?GS->RunSeed:0)+KeyEventIndex*7919);
    VariantIndex=Variants.IsEmpty()?0:Roll.RandRange(0,Variants.Num()-1);
    UMCNutRainProfile* Variant=Variants.IsValidIndex(VariantIndex)?Variants[VariantIndex].LoadSynchronous():nullptr;
    Record(FString::Printf(TEXT("Ключевое событие %d: ореховый катаклизм, Рыцарь и его превращение в Колдуна; обычные события приостановлены"),KeyEventIndex+1));
    NutEvent->Start(Mechanics,Variant);
    Publish(TEXT("ОРЕХОГЕДДОН"),TEXT("Уклоняйтесь от красных зон падения, затем победите Рыцаря и Колдуна. Разбитые орехи уменьшают будущий призыв; XP за них не начисляется."));
    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY NUTS variant=%d"),VariantIndex);
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginIce()
{
    if (IsValid(Interlude)) {Interlude->Stop();Interlude->Destroy();Interlude=nullptr;}
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) {
        if (IsValid(Mode->GameDirector)) {Mode->GameDirector->Stop();Mode->GameDirector->Destroy();}
        Mode->GameDirector=nullptr;
    }
    if (IsValid(NutEvent)) {NutEvent->Stop();NutEvent->Destroy();NutEvent=nullptr;}
    if (IsValid(IceEvent)) {IceEvent->Stop();IceEvent->Destroy();IceEvent=nullptr;}
    if (IsValid(FogEvent)) {FogEvent->Stop();FogEvent->Destroy();FogEvent=nullptr;}
    Stage=EMCSingleDayStage::Ice;
    IceEvent=GetWorld()->SpawnActor<AMCIceEvent>();
    if(!IceEvent) {Fail(TEXT("Не удалось создать ледяное событие."));return;}
    IceEvent->SetOwner(this);
    Record(FString::Printf(TEXT("Ключевое событие %d: ледяной кристалл замораживает арену; обычные события приостановлены"),KeyEventIndex+1));
    IceEvent->Start();
    Publish(TEXT("ЗИМА БЛИЗКО"),TEXT("Разбейте центральный кристалл киркой. Перебегайте между тёплыми зонами, разбивайте мешающие кристаллы и лёд на ногах; уклоняйтесь от трёх сосулек и конуса бури."),
        FMath::CeilToInt(IceEvent->CandyHealth),FMath::CeilToInt(IceEvent->MaxCandyHealth));
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginFogBrawl()
{
    if (IsValid(Interlude)) {Interlude->Stop();Interlude->Destroy();Interlude=nullptr;}
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) {
        if (IsValid(Mode->GameDirector)) {Mode->GameDirector->Stop();Mode->GameDirector->Destroy();}
        Mode->GameDirector=nullptr;
    }
    if (IsValid(NutEvent)) {NutEvent->Stop();NutEvent->Destroy();NutEvent=nullptr;}
    if (IsValid(IceEvent)) {IceEvent->Stop();IceEvent->Destroy();IceEvent=nullptr;}
    if (IsValid(FogEvent)) {FogEvent->Stop();FogEvent->Destroy();FogEvent=nullptr;}
    Stage=EMCSingleDayStage::FogBrawl;
    FogEvent=GetWorld()->SpawnActor<AMCFogBrawlEvent>();
    if(!FogEvent) {Fail(TEXT("Не удалось создать событие тумана."));return;}
    FogEvent->SetOwner(this);
    Record(FString::Printf(TEXT("Ключевое событие %d: туман скрывает арену; вместе защищайте отмеченные зубы от ударов, обычные события приостановлены"),KeyEventIndex+1));
    FogEvent->Start();
    Publish(TEXT("ТУМАННАЯ ДРАКА"),TEXT("Все зубы мигают с ускорением. Целей столько, сколько игроков. Удары справа, затем слева; у цели держи ПКМ."),
        FMath::Max(0,FogEvent->TotalStrikes-FogEvent->StrikesResolved),FMath::Max(0,FogEvent->TotalStrikes));
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginDirector()
{
    if(Settings && Settings->KeyEvents.IsValidIndex(KeyEventIndex)
        && BeginStageReward(FName(*FString::Printf(TEXT("KeyEvent_%d"),KeyEventIndex)),EMCSingleDayRewardContinuation::EventSupport)) return;
    Stage=EMCSingleDayStage::Director;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const bool Legacy=IsLegacyTimedFinale();
    const auto* CompletedSlot=Settings->KeyEvents.IsValidIndex(KeyEventIndex)?&Settings->KeyEvents[KeyEventIndex]:nullptr;
    if(GS->Progression && (Legacy || CompletedSlot)) GS->Progression->AddExperience(FMath::Max(0,Legacy?Settings->NutEventExperience:CompletedSlot->CompletionExperience));
    if(IsValid(IceEvent)) {
        // A completed event has already cleared gameplay. Let its short local
        // shatter finish while removing the replicated event for the next slot.
        if(!IceEvent->IsComplete()) IceEvent->Stop();
        IceEvent->Destroy();
    }
    IceEvent=nullptr;
    if(IsValid(FogEvent)) {FogEvent->Stop();FogEvent->Destroy();} FogEvent=nullptr;
    const bool HasNext=!Legacy && Settings->KeyEvents.IsValidIndex(KeyEventIndex+1);
    if(!Legacy && CompletedSlot && CompletedSlot->Kind==EMCSingleDayKeyEventKind::NutEncounter
        && HasNext && Settings->KeyEvents[KeyEventIndex+1].Kind==EMCSingleDayKeyEventKind::Jellies) {
        BeginPostNutMeal();return;
    }
    bAuthoredFragmentComplete=!Legacy && !HasNext;
    const float Configured=Legacy?Settings->DirectorSeconds:HasNext?CompletedSlot->DirectorSupportSeconds:0.f;
    const float Seconds=Legacy?(FMath::IsFinite(Configured)?FMath::Clamp(Configured,5.f,300.f):30.f):
        HasNext?(FMath::IsFinite(Configured)?FMath::Clamp(Configured,5.f,600.f):120.f):0.f;
    InterludeEndsAt=Seconds>0?GS->GetServerWorldTimeSeconds()+Seconds:0;
    Record(Legacy?TEXT("Ореховое событие завершено: legacy интервал перед боссом"):
        HasNext?TEXT("Ключевое событие завершено: директор поддерживает арену до следующего слота"):
        TEXT("Авторенный фрагмент завершён: директор поддерживает арену без таймера; следующие события ещё не авторены"));
    Interlude=GetWorld()->SpawnActor<AMCGameDirector>();
    if(Interlude) {
        Interlude->SetOwner(this);
        Interlude->InitializeRun(Mechanics,DirectorProfile);
        if (Legacy) Interlude->BeginInterlude(Seconds); else Interlude->BeginSupport(Seconds);
        if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->GameDirector=Interlude;
    }
    else {Fail(TEXT("Не удалось создать директор поддержки арены."));return;}
    GS->RunSettings.DaysToSurvive=1;
    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY DIRECTOR seconds=%.1f"),Seconds);
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginBoss()
{
    if(Interlude) { Interlude->Stop(); Interlude->Destroy(); Interlude=nullptr; }
    if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->GameDirector=nullptr;
    Stage=EMCSingleDayStage::Boss;
    Record(TEXT("Legacy прототип: активирован финальный босс"));
    TSubclassOf<AMCBossCharacter> BossClass=Settings->FinalBossClass.LoadSynchronous();
    if(!BossClass) BossClass=LoadClass<AMCBossCharacter>(nullptr,TEXT("/Game/Gameplay/Boss/BP_ZombieBoss.BP_ZombieBoss_C"));
    if(!BossClass) BossClass=AMCBossCharacter::StaticClass();
    FVector Point(650,0,300); FHitResult Floor;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(It->SurfacePoint(FVector(650,0,0),Floor)) {Point=Floor.ImpactPoint+FVector(0,0,115);break;}
    FActorSpawnParameters Params; Params.Owner=this; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    FinalBoss=GetWorld()->SpawnActor<AMCBossCharacter>(BossClass,Point,FRotator(0,180,0),Params);
    if(FinalBoss) FinalBoss->ActivateBoss();
    else { Fail(TEXT("Не удалось создать финального босса.")); return; }
    Publish(TEXT("ФИНАЛЬНЫЙ БОСС"),TEXT("Победите босса. Ваши личные перки и общий уровень сохраняются."),1,1);
    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY BOSS")); ForceNetUpdate();
}
void AMCSingleDayDirector::Tick(float Dt)
{
    Super::Tick(Dt); if(!HasAuthority() || bStopped) return;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    if(GS->Phase==EMCShiftPhase::Lost) {Stop();return;}
    if(bStageRewardPending) {TickStageReward();return;}
    if(Stage==EMCSingleDayStage::FirstPerk && GS->Progression && !GS->Progression->HasPendingChoices()) {
        if(IsLegacyTimedFinale()) BeginKeyEvent(); else BeginOpeningPause();
    }
    else if(Stage==EMCSingleDayStage::OpeningPause && GS->GetServerWorldTimeSeconds()>=StageEndsAt) BeginFirstMeal();
    else if(Stage==EMCSingleDayStage::FirstMeal) TickFirstMeal();
    else if(Stage==EMCSingleDayStage::MealCoffeeWave) TickMealCoffeeWave();
    else if(Stage==EMCSingleDayStage::BetweenMealsPause) TickBetweenMealsPause();
    else if(Stage==EMCSingleDayStage::PostNutMeal) TickPostNutMeal();
    else if(Stage==EMCSingleDayStage::PostNutMint) TickSmallMint();
    else if(Stage==EMCSingleDayStage::Jellies) TickJellies();
    else if(Stage==EMCSingleDayStage::MealRest && GS->GetServerWorldTimeSeconds()>=StageEndsAt) {
        if(!GS->Progression || !GS->Progression->HasPendingChoices()) BeginKeyEvent();
        else Publish(TEXT("ПЕРВОЕ УСИЛЕНИЕ"),TEXT("Выберите личный перк перед ореховым событием: 1 / 2 / 3."));
    }
    else if(Stage==EMCSingleDayStage::Ice && IsValid(IceEvent)) {
        if(IceEvent->bFailed) {Fail(TEXT("Не удалось запустить ледяное событие. Проверьте поверхность языка."));return;}
        if(IceEvent->IsComplete()) {
            Record(TEXT("Центральный ледяной кристалл разбит: событие завершено, арена оттаивает"));
            BeginDirector();
        }
        else Publish(TEXT("ЗИМА БЛИЗКО"),TEXT("Разбейте центральный кристалл киркой. Перебегайте между тёплыми зонами, разбивайте мешающие кристаллы и лёд на ногах; уклоняйтесь от трёх сосулек и конуса бури."),
            FMath::CeilToInt(IceEvent->CandyHealth),FMath::CeilToInt(IceEvent->MaxCandyHealth));
    }
    else if(Stage==EMCSingleDayStage::FogBrawl && IsValid(FogEvent)) {
        if(FogEvent->bFailed) {Fail(TEXT("Не удалось запустить событие тумана. Проверьте доступные зубы и участников."));return;}
        if(FogEvent->IsComplete()) {
            Record(TEXT("Все удары в тумане пережиты: событие завершено, арена снова видна"));
            BeginDirector();
        }
        else Publish(TEXT("ТУМАННАЯ ДРАКА"),TEXT("Рулетка перебирает все доступные зубы. После выбора целей: справа, затем слева. У своего зуба держи ПКМ."),
            FMath::Max(0,FogEvent->TotalStrikes-FogEvent->StrikesResolved),FMath::Max(0,FogEvent->TotalStrikes));
    }
    else if(Stage==EMCSingleDayStage::Nuts && NutEvent) {
        if(NutEvent->bFailed) { Fail(TEXT("Не удалось запустить ореховый дождь. Проверьте язык и коллизию орехов.")); return; }
        if(NutEvent->IsComplete()) BeginDirector();
        else if(NutEvent->Stage==EMCNutRainStage::Enemies) {
            const AMCNutBoss* Boss=NutEvent->Bosses.IsEmpty()?nullptr:NutEvent->Bosses[0].Get();
            const bool Mage=IsValid(Boss) && Boss->BossRole==EMCNutBossRole::Mage;
            const bool Transforming=IsValid(Boss) && Boss->State==EMCNutBossState::Transforming;
            Publish(Transforming?TEXT("ГОЛОВА РАСКАЛЫВАЕТСЯ"):Mage?TEXT("ОРЕХОВЫЙ БОЙ · ФАЗА 2/2"):TEXT("ОРЕХОВЫЙ БОЙ · ФАЗА 1/2"),
                Transforming?TEXT("Рыцарь повержен и превращается в Орехового Колдуна. Приготовьтесь ко второй фазе."):
                Mage?TEXT("Победите Орехового Колдуна. Разбивайте орехи до призыва и убивайте малых саммонов до взрыва."):
                    TEXT("Победите Орехового Рыцаря. После раскола головы он превратится в Колдуна."),
                NutEvent->BossesLeft>0?(Mage?1:2):0,2);
        }
        else Publish(TEXT("ОРЕХОГЕДДОН"),TEXT("Уклоняйтесь от красных зон падения. После дождя появится Рыцарь; после его поражения — Колдун."));
    }
    else if(Stage==EMCSingleDayStage::Director && InterludeEndsAt>0 && GS->GetServerWorldTimeSeconds()>=InterludeEndsAt) {
        if (IsLegacyTimedFinale()) BeginBoss();
        else {++KeyEventIndex;BeginKeyEvent();}
    }
    else if(IsLegacyTimedFinale() && Stage==EMCSingleDayStage::Boss && IsValid(FinalBoss) && !FinalBoss->IsBossAlive()) {
        Stage=EMCSingleDayStage::Complete; bStopped=true;
        GS->Phase=EMCShiftPhase::Won; GS->PhaseEndsAt=0; GS->TasksLeft=0;
        GS->DirectorState.CurrentTitle=TEXT("БОСС ПОБЕЖДЁН"); GS->DirectorState.DayProgress=GS->DirectorState.CompletionProgress=1;
        GS->DirectorState.Instruction.Empty(); GS->DirectorState.NextTitle.Empty();
        GS->DirectorState.FinishedFood=GS->DirectorState.SpawnedFood;
        Record(TEXT("Legacy финальный босс побеждён: прототип завершён"));
        GS->ForceNetUpdate(); ForceNetUpdate(); UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY COMPLETE"));
    }
}
void AMCSingleDayDirector::Stop()
{
    bStopped=true;
    if(!HasAuthority()) return;
    ResetStageRewards();
    if(IsValid(OpeningCoffee)) {OpeningCoffee->Stop();OpeningCoffee->Destroy();} OpeningCoffee=nullptr;
    if(FirstMealBatch>0) {
        MCDestroyDirectedFoodDirt(GetWorld(),FirstMealBatch);
        TArray<AMCFoodActor*> Meal;
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(It->Batch==FirstMealBatch) Meal.Add(*It);
        for(auto* Food:Meal) Food->Destroy();
        FirstMealBatch=0;
    }
    if(IsValid(MealServices)) MealServices->Destroy(); MealServices=nullptr;
    if(PostNutMealBatch>0) {
        MCDestroyDirectedFoodDirt(GetWorld(),PostNutMealBatch);
        TArray<AMCFoodActor*> Meal;
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(It->Batch==PostNutMealBatch) Meal.Add(*It);
        for(auto* Food:Meal) Food->Destroy();
        PostNutMealBatch=0;
    }
    if(IsValid(SmallMint)) {SmallMint->Stop();SmallMint->Destroy();} SmallMint=nullptr;
    if(IsValid(JellyEvent)) {JellyEvent->Stop(true);JellyEvent->Destroy();} JellyEvent=nullptr;
    StageEndsAt=0;
    if(IsValid(NutEvent)) {NutEvent->Stop();NutEvent->Destroy();} NutEvent=nullptr;
    if(IsValid(IceEvent)) {IceEvent->Stop();IceEvent->Destroy();} IceEvent=nullptr;
    if(IsValid(FogEvent)) {FogEvent->Stop();FogEvent->Destroy();} FogEvent=nullptr;
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>(); Mode && Mode->GameDirector==Interlude) Mode->GameDirector=nullptr;
    if(IsValid(Interlude)) {Interlude->Stop();Interlude->Destroy();} Interlude=nullptr;
    if(IsValid(FinalBoss)) FinalBoss->Destroy(); FinalBoss=nullptr;
}
void AMCSingleDayDirector::Fail(const FString& Reason)
{
    Record(TEXT("Сбой ключевого события: ")+Reason);
    Publish(TEXT("СОБЫТИЕ НЕ ЗАПУСТИЛОСЬ"),Reason);
    Stop();
    if(auto* GS=GetWorld()->GetGameState<AMCGameState>()) { GS->Phase=EMCShiftPhase::Lost; GS->ForceNetUpdate(); }
    UE_LOG(LogTemp,Error,TEXT("MC_SINGLE_DAY FAILED: %s"),*Reason);
}
void AMCSingleDayDirector::EndPlay(const EEndPlayReason::Type Reason) {Stop();Super::EndPlay(Reason);}
void AMCSingleDayDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCSingleDayDirector,Stage); DOREPLIFETIME(AMCSingleDayDirector,NutEvent);
    DOREPLIFETIME(AMCSingleDayDirector,IceEvent);
    DOREPLIFETIME(AMCSingleDayDirector,FogEvent);
    DOREPLIFETIME(AMCSingleDayDirector,JellyEvent);DOREPLIFETIME(AMCSingleDayDirector,SmallMint);
    DOREPLIFETIME(AMCSingleDayDirector,FinalBoss); DOREPLIFETIME(AMCSingleDayDirector,VariantIndex);
    DOREPLIFETIME(AMCSingleDayDirector,KeyEventIndex); DOREPLIFETIME(AMCSingleDayDirector,bAuthoredFragmentComplete);
    DOREPLIFETIME(AMCSingleDayDirector,bStageRewardPending);DOREPLIFETIME(AMCSingleDayDirector,StageRewardChest);
    DOREPLIFETIME(AMCSingleDayDirector,StageRewardCompletionId);
    DOREPLIFETIME(AMCSingleDayDirector,StageEndsAt);
    DOREPLIFETIME(AMCSingleDayDirector,MealPortionNumber);
    DOREPLIFETIME(AMCSingleDayDirector,MealPortionCount);
    DOREPLIFETIME(AMCSingleDayDirector,MealPortionTarget);
    DOREPLIFETIME(AMCSingleDayDirector,MealPortionSpawned);
}
