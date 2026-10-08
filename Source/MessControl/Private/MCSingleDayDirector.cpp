#include "MCSingleDayDirector.h"
#include "MCNutRainEvent.h"
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
}
UMCSingleDayProfile::UMCSingleDayProfile() { KeyEvents.AddDefaulted(); }
AMCSingleDayDirector::AMCSingleDayDirector()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.1f;
}
void AMCSingleDayDirector::Initialize(UMCDayPlan* Plan, UMCSingleDayProfile* Profile, UMCGameDirectorProfile* InterludeProfile)
{
    if(!HasAuthority()) return;
    Mechanics=Plan?Plan:NewObject<UMCDayPlan>(this);
    Settings=Profile?DuplicateObject<UMCSingleDayProfile>(Profile,this):NewObject<UMCSingleDayProfile>(this);
    DirectorProfile=InterludeProfile;
    Stage=EMCSingleDayStage::Training; bStopped=false;
    KeyEventIndex=0; bAuthoredFragmentComplete=false; InterludeEndsAt=StageEndsAt=0;
    FirstMealBatch=0; MealSpawned=MealAttempts=0;
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
bool AMCSingleDayDirector::ShouldFinishNutRain(int32 CompletedSeries,int32 MinimumSeries,float RainAge,float TargetSeconds) const
{
    const float Target=FMath::IsFinite(TargetSeconds)?FMath::Max(40.f,TargetSeconds):40.f;
    const int32 Required=FMath::Max(4,MinimumSeries);
    if (!FMath::IsFinite(RainAge) || CompletedSeries<Required || RainAge<Target) return false;
    const auto Team=MCSingleDayDirectorPrivate::ObserveTeam(GetWorld());
    if (MCSingleDayDirectorPrivate::NeedsRelief(Team)) return true;
    // A healthy team may receive one extra series, bounded to eight more seconds.
    return !MCSingleDayDirectorPrivate::IsHealthy(Team) || CompletedSeries>Required || RainAge>=Target+8.f;
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
    S.NextTitle=BeforeMeal?TEXT("ПРИЁМ ПИЩИ"):
        Stage==EMCSingleDayStage::FirstMeal || Stage==EMCSingleDayStage::MealRest?TEXT("ОРЕХОВОЕ СОБЫТИЕ"):
        Stage==EMCSingleDayStage::Nuts?TEXT("ПОДДЕРЖКА ДИРЕКТОРА"):
        IsLegacyTimedFinale() && Stage==EMCSingleDayStage::Director?TEXT("ФИНАЛЬНЫЙ БОСС"):TEXT("");
    if(Stage==EMCSingleDayStage::FirstMeal && DirectorProfile)
        S.Difficulty=DirectorProfile->GetScaledDaySettings(0).MinimumDifficulty;
    S.DecisionLog=GS->DirectorDecisionLog;
    S.LastDecision=S.DecisionLog.IsEmpty()?FString():S.DecisionLog.Last();
    S.SpawnedFood=Total; S.FinishedFood=FMath::Max(0,Total-Left);
    S.CompletionProgress=Total>0?1-float(Left)/Total:0;
    S.DayProgress=Stage==EMCSingleDayStage::Nuts?.25f:Stage==EMCSingleDayStage::Boss?.85f:.1f;
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
        TEXT("Обычная еда скоро появится. Подготовьте нож и место для переноски."));
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginFirstMeal()
{
    Stage=EMCSingleDayStage::FirstMeal; StageEndsAt=0;
    FirstMealBatch=2000000+int32(GetUniqueID()&0x000FFFFF);
    MealSpawned=MealAttempts=0;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    MealRandom.Initialize((GS?GS->RunSeed:41)^0x4D45414C);
    MealServices=GetWorld()->SpawnActor<AMCDayDirector>();
    if(!MealServices) {Fail(TEXT("Не удалось запустить обычный приём пищи."));return;}
    MealServices->SetOwner(this);
    MealServices->InitializeEventServices(Mechanics,GS?GS->RunSeed:41);
    NextMealDropAt=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    Record(TEXT("Обычный приём пищи: нарезка и доставка; ореховое событие ждёт завершения еды"));
    Publish(TEXT("ПРИЁМ ПИЩИ"),TEXT("Доставьте еду в горло и очистите оставшуюся грязь."),
        FMath::Clamp(Settings->OpeningMealItems,1,6),FMath::Clamp(Settings->OpeningMealItems,1,6));
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
    const int32 Target=FMath::Clamp(Settings->OpeningMealItems,1,6);
    const double Time=GS->GetServerWorldTimeSeconds();
    if(MealSpawned<Target && Time>=NextMealDropAt) {
        const FName Row=ChooseOpeningFood(); ++MealAttempts;
        if(auto* Food=MCSpawnDirectedFoodEntry(GetWorld(),Mechanics,Row,FirstMealBatch,MealRandom,MealServices)) {
            Food->SetOwner(this); ++MealSpawned;
        }
        else if(MealAttempts>=Target*6) {
            Fail(TEXT("Не удалось подать обычную еду. Проверьте меню, язык и коллизию продуктов.")); return;
        }
        const float Configured=Settings->OpeningMealDropSeconds;
        NextMealDropAt=Time+(FMath::IsFinite(Configured)?FMath::Clamp(Configured,.5f,5.f):1.5f);
    }
    // Fracture parents disappear before their fragments. The ordinary food pipeline
    // keeps carried, loose and swallowing pieces outstanding until their work ends.
    const auto Food=MCMeasureFoodPipeline(GetWorld(),FirstMealBatch);
    const int32 Dirt=MCCountDirectedFoodDirt(GetWorld(),FirstMealBatch);
    if(MealSpawned>=Target && Food.OutstandingActors==0 && Food.UnresolvedHazards==0 && Dirt==0) {
        if(IsValid(MealServices)) MealServices->Destroy(); MealServices=nullptr;
        BeginOpeningPause(true); return;
    }
    const int32 Left=Target-MealSpawned+Food.OutstandingActors+Dirt;
    Publish(TEXT("ПРИЁМ ПИЩИ"),TEXT("Доставьте еду в горло и очистите оставшуюся грязь."),
        Left,FMath::Max(GS->TasksTotal,Left));
}
void AMCSingleDayDirector::BeginNuts()
{
    StageEndsAt=0;
    if (!IsLegacyTimedFinale() && !Settings->KeyEvents.IsValidIndex(KeyEventIndex)) {
        BeginDirector(); return;
    }
    if (!IsLegacyTimedFinale() && Settings->KeyEvents[KeyEventIndex].Kind!=EMCSingleDayKeyEventKind::NutEncounter) {
        Fail(TEXT("Ключевое событие ещё не реализовано.")); return;
    }
    if (Interlude) {Interlude->Stop();Interlude->Destroy();Interlude=nullptr;}
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) {
        if (IsValid(Mode->GameDirector)) {Mode->GameDirector->Stop();Mode->GameDirector->Destroy();}
        Mode->GameDirector=nullptr;
    }
    if (IsValid(NutEvent)) {NutEvent->Stop();NutEvent->Destroy();NutEvent=nullptr;}
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
    Record(FString::Printf(TEXT("Ключевое событие %d: ореховые серии и боссы; обычные события приостановлены"),KeyEventIndex+1));
    NutEvent->Start(Mechanics,Variant);
    Publish(TEXT("ОРЕХОВЫЙ ДОЖДЬ"),TEXT("Продержитесь под дождём. Кирка разбивает твёрдые орехи; еду можно доставлять."));
    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY NUTS variant=%d"),VariantIndex);
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginDirector()
{
    Stage=EMCSingleDayStage::Director;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const bool Legacy=IsLegacyTimedFinale();
    const auto* CompletedSlot=Settings->KeyEvents.IsValidIndex(KeyEventIndex)?&Settings->KeyEvents[KeyEventIndex]:nullptr;
    if(GS->Progression && (Legacy || CompletedSlot)) GS->Progression->AddExperience(FMath::Max(0,Legacy?Settings->NutEventExperience:CompletedSlot->CompletionExperience));
    const bool HasNext=!Legacy && Settings->KeyEvents.IsValidIndex(KeyEventIndex+1);
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
    if(Stage==EMCSingleDayStage::FirstPerk && GS->Progression && !GS->Progression->HasPendingChoices()) {
        if(IsLegacyTimedFinale()) BeginNuts(); else BeginOpeningPause();
    }
    else if(Stage==EMCSingleDayStage::OpeningPause && GS->GetServerWorldTimeSeconds()>=StageEndsAt) BeginFirstMeal();
    else if(Stage==EMCSingleDayStage::FirstMeal) TickFirstMeal();
    else if(Stage==EMCSingleDayStage::MealRest && GS->GetServerWorldTimeSeconds()>=StageEndsAt) {
        if(!GS->Progression || !GS->Progression->HasPendingChoices()) BeginNuts();
        else Publish(TEXT("ПЕРВОЕ УСИЛЕНИЕ"),TEXT("Выберите личный перк перед ореховым событием: 1 / 2 / 3."));
    }
    else if(Stage==EMCSingleDayStage::Nuts && NutEvent) {
        if(NutEvent->bFailed) { Fail(TEXT("Не удалось запустить ореховый дождь. Проверьте язык и коллизию орехов.")); return; }
        if(NutEvent->IsComplete()) BeginDirector();
        else if(NutEvent->Stage==EMCNutRainStage::Enemies)
            Publish(TEXT("ОРЕХОВЫЙ БОЙ"),TEXT("Победите ореховых боссов и всех врагов. Кирка и нож наносят им урон."),NutEvent->EnemiesLeft,FMath::Max(GS->TasksTotal,NutEvent->EnemiesLeft));
        else Publish(TEXT("ОРЕХОВЫЙ ДОЖДЬ"),TEXT("Продержитесь до конца катаклизма. Следите за летящими орехами."));
    }
    else if(Stage==EMCSingleDayStage::Director && InterludeEndsAt>0 && GS->GetServerWorldTimeSeconds()>=InterludeEndsAt) {
        if (IsLegacyTimedFinale()) BeginBoss();
        else {++KeyEventIndex;BeginNuts();}
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
    if(FirstMealBatch>0) {
        MCDestroyDirectedFoodDirt(GetWorld(),FirstMealBatch);
        TArray<AMCFoodActor*> Meal;
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(It->Batch==FirstMealBatch) Meal.Add(*It);
        for(auto* Food:Meal) Food->Destroy();
        FirstMealBatch=0;
    }
    if(IsValid(MealServices)) MealServices->Destroy(); MealServices=nullptr;
    if(IsValid(NutEvent)) {NutEvent->Stop();NutEvent->Destroy();} NutEvent=nullptr;
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
    DOREPLIFETIME(AMCSingleDayDirector,FinalBoss); DOREPLIFETIME(AMCSingleDayDirector,VariantIndex);
    DOREPLIFETIME(AMCSingleDayDirector,KeyEventIndex); DOREPLIFETIME(AMCSingleDayDirector,bAuthoredFragmentComplete);
    DOREPLIFETIME(AMCSingleDayDirector,StageEndsAt);
}
