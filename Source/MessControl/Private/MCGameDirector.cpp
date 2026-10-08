#include "MCGameDirector.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCFoodDirectorHooks.h"
#include "MCFoodActor.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCMouthSurface.h"
#include "MCFirePatch.h"
#include "MCColdCola.h"
#include "MCCoffeeFlood.h"
#include "MCTongue.h"
#include "MCThroat.h"
#include "MCRewardChest.h"
#include "MCRoguelikeDirector.h"
#include "MCBossCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace {
bool FoodKind(EMCGameDirectorEvent K) { return K==EMCGameDirectorEvent::Food || K==EMCGameDirectorEvent::Pepper || K==EMCGameDirectorEvent::StuckFood; }
bool MovementKind(EMCGameDirectorEvent K) { return K==EMCGameDirectorEvent::Yawn || K==EMCGameDirectorEvent::CoffeeFlood || K==EMCGameDirectorEvent::ColdCola || K==EMCGameDirectorEvent::Boss; }
const TCHAR* ShortEventName(EMCGameDirectorEvent K)
{
    static const TCHAR* Names[]={TEXT("Еда"),TEXT("Кофе"),TEXT("Река"),TEXT("Кола"),TEXT("Зевание"),TEXT("Перец"),TEXT("Застр.еда"),TEXT("Зуб"),TEXT("Сундук"),TEXT("Босс")};
    return Names[FMath::Clamp(int32(K),0,9)];
}
}
AMCGameDirector::AMCGameDirector()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.25f;
}
AMCGameDirector* AMCGameDirector::Find(UWorld* World)
{
    const auto* Mode=World?World->GetAuthGameMode<AMCGameMode>():nullptr;
    return Mode && IsValid(Mode->GameDirector)?Mode->GameDirector.Get():nullptr;
}
bool AMCGameDirector::IsManagingEvents() const
{
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    return HasAuthority() && bManaging && GS && !GS->bTutorialActive && !GS->bLobbyWaiting && !GS->bDevManualEvents;
}
bool AMCGameDirector::IsLaunchingEvent(EMCGameDirectorEvent Kind) const
{
    return LaunchGrant.IsSet() && (LaunchGrant.GetValue()==Kind ||
        (LaunchGrant.GetValue()==EMCGameDirectorEvent::ColdCola && Kind==EMCGameDirectorEvent::CoffeeFlood));
}
FString AMCGameDirector::EventName(EMCGameDirectorEvent Kind)
{
    switch(Kind) {
    case EMCGameDirectorEvent::Food:return TEXT("ПОРЦИЯ ЕДЫ");
    case EMCGameDirectorEvent::Coffee:return TEXT("КОФЕЙНЫЕ ПЯТНА");
    case EMCGameDirectorEvent::CoffeeFlood:return TEXT("КОФЕЙНАЯ РЕКА");
    case EMCGameDirectorEvent::ColdCola:return TEXT("ХОЛОДНАЯ КОЛА");
    case EMCGameDirectorEvent::Yawn:return TEXT("ЗЕВАНИЕ");
    case EMCGameDirectorEvent::Pepper:return TEXT("ПЕРЕЦ · УБЕРИТЕ ЗА 10 С");
    case EMCGameDirectorEvent::StuckFood:return TEXT("ЗАСТРЯВШАЯ ЕДА");
    case EMCGameDirectorEvent::LooseTooth:return TEXT("РАСШАТАННЫЕ ЗУБЫ");
    case EMCGameDirectorEvent::Reward:return TEXT("СУНДУК С ПЕРКАМИ");
    default:return TEXT("БОСС");
    }
}
void AMCGameDirector::Record(const FString& Decision)
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const int32 Seconds=FMath::Max(0,int32(GetWorld()->GetTimeSeconds()-DayStartedAt));
    const FString Entry=FString::Printf(TEXT("Д%d %02d:%02d  %s"),GS?GS->Day:0,Seconds/60,Seconds%60,*Decision);
    Log.Add(Entry); if(Log.Num()>12) Log.RemoveAt(0,Log.Num()-12);
    if (GS) GS->RecordDirectorDecision(Entry);
    UE_LOG(LogTemp,Display,TEXT("MC_DIRECTOR %s"),*Entry);
}
void AMCGameDirector::InitializeRun(UMCDayPlan* Plan,UMCGameDirectorProfile* Profile)
{
    if(!HasAuthority()) return;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    if(IsValid(Services)) Services->Destroy(); Services=nullptr; LaunchGrant.Reset();
    Mechanics=Plan?DuplicateObject<UMCDayPlan>(Plan,this):NewObject<UMCDayPlan>(this); Mechanics->Sanitize();
    Settings=Profile?DuplicateObject<UMCGameDirectorProfile>(Profile,this):NewObject<UMCGameDirectorProfile>(this); Settings->Sanitize();
    Random.Initialize(GS->RunSeed); Tickets.Reset(); Log.Reset(); NextId=0;
    LastEventAt.Reset(); DayEventCounts.Reset(); RecentKinds.Reset(); CandidateScores.Reset();
    Throughput=1; PreviousWork=AddedWork=0; CandidateTitle.Empty(); WaitWeight=WaitProbability=0;
    PlannedFoodTotal=SpawnedFoodTotal=FinishedFoodTotal=0;
    GS->TasksLeft=GS->TasksTotal=0; DayStartedAt=GS->GetServerWorldTimeSeconds(); DayEndsAt=0;
    if(!GS->bSingleDayLoop) GS->RunSettings.DaysToSurvive=7;
    // Clients resolve saved assets by package path; the server-only run copy has
    // no network identity. Keep it private to event services and consequences.
    UMCDayPlan* SharedPlan=Plan && Plan->IsAsset()?Plan:nullptr;
    if(!SharedPlan) {
        const auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
        SharedPlan=Mode?Mode->FirstDayPlan.LoadSynchronous():nullptr;
    }
    GS->DayPlan=SharedPlan; GS->StepIndex=INDEX_NONE; GS->bDayOneComplete=false;
    Services=GetWorld()->SpawnActor<AMCDayDirector>();
    if(Services) { Services->SetOwner(this); Services->InitializeEventServices(Mechanics,GS->RunSeed); }
    bManaging=true; bInterlude=false; bSupportMode=false; Pacing=EMCGameDirectorPacing::Intermission;
    GS->DirectorState=FMCGameDirectorState(); GS->DirectorState.bEnabled=true;
    Record(TEXT("Director включён: выбор по состоянию команды, без квот событий")); GS->ForceNetUpdate();
}
void AMCGameDirector::Stop()
{
    bManaging=false; LaunchGrant.Reset();
    if(auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr) { GS->DirectorState.bEnabled=false; GS->ForceNetUpdate(); }
}
void AMCGameDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    bManaging=false;
    if(IsValid(Services)) Services->Destroy(); Services=nullptr;
    Super::EndPlay(Reason);
}
void AMCGameDirector::BeginDay(int32 Day)
{
    if(!IsManagingEvents() || !Settings || !Settings->Days.IsValidIndex(Day-1)) return;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    DaySettings=Settings->GetScaledDaySettings(Day-1); DayStartedAt=GS->GetServerWorldTimeSeconds(); DayEndsAt=DayStartedAt+DaySettings.DaySeconds;
    Pacing=EMCGameDirectorPacing::Build; PhaseStartedAt=DayStartedAt; RestUntil=0; NextFoodAt=DayStartedAt; LastSpecialAt=DayStartedAt-100;
    LastReason.Empty(); CurrentTitle=TEXT("ВРЕМЯ ЧИСТИТЬ"); CandidateTitle.Empty();
    DayEventCounts.Reset(); NextDecisionAt=DayStartedAt+Settings->InitialCleaningSeconds; SampleAt=DayStartedAt;
    Difficulty=FMath::Clamp(Day==1?.7f*Settings->DifficultyMultiplier:Difficulty,DaySettings.MinimumDifficulty,DaySettings.MaximumDifficulty);
    TargetPressure=FMath::Lerp(DaySettings.TargetPressureMin,DaySettings.TargetPressureMax,.5f);
    GS->Day=Day; GS->Phase=EMCShiftPhase::Working; GS->StepStartedAt=GS->DayStartedAt=DayStartedAt; GS->PhaseEndsAt=DayEndsAt;
    GS->bDayOneComplete=false; GS->PreviousStepFailed=false;
    if(Day==1 && Services && DaySettings.InitialPatches>0) Services->AddDirt(false,DaySettings.InitialPatches,100000,0);
    PreviousWork=Observe().WorkSeconds; AddedWork=0;
    Record(FString::Printf(TEXT("Начат день %d · %.0f с · события выбираются в процессе"),Day,DaySettings.DaySeconds));
    Publish(Observe(),TEXT("Начальная работа и подготовка подачи"),DayStartedAt); GS->ForceNetUpdate();
}
void AMCGameDirector::BeginInterlude(float Seconds)
{
    if(!HasAuthority() || !Settings) return;
    bSupportMode=false;
    Settings->Days[0].InitialPatches=0;
    for(auto& Rule:Settings->Events) if(Rule.Kind==EMCGameDirectorEvent::Boss || Rule.Kind==EMCGameDirectorEvent::Reward) Rule.Weight=0;
    BeginDay(1); bInterlude=true;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    DayEndsAt=GS->GetServerWorldTimeSeconds()+FMath::Clamp(Seconds,5.f,300.f);
    DaySettings.DaySeconds=FMath::Clamp(Seconds,5.f,300.f);
    DaySettings.FinalCleanupSeconds=FMath::Min(8.f,DaySettings.DaySeconds*.25f);
    GS->PhaseEndsAt=DayEndsAt;
    GS->ForceNetUpdate();
}
void AMCGameDirector::BeginSupport(float Seconds)
{
    if (!HasAuthority() || !Settings) return;
    bSupportMode=true;
    Settings->Days[0].InitialPatches=0;
    for (auto& Rule:Settings->Events)
        if (Rule.Kind==EMCGameDirectorEvent::Boss || Rule.Kind==EMCGameDirectorEvent::Reward) Rule.Weight=0;
    BeginDay(1);
    bInterlude=true;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const float Duration=FMath::IsFinite(Seconds)?FMath::Max(0.f,Seconds):0.f;
    DayEndsAt=Duration>0?GS->GetServerWorldTimeSeconds()+Duration:0;
    DaySettings.FinalCleanupSeconds=0;
    GS->PhaseEndsAt=DayEndsAt;
    Record(Duration>0?TEXT("Поддержка арены до следующего ключевого события"):TEXT("Поддержка арены: авторенный фрагмент завершён, дедлайна нет"));
    Publish(Observe(),TEXT("Обычные задачи доступны без дневных гейтов и оценки дедлайна"),GS->GetServerWorldTimeSeconds());
}
FName AMCGameDirector::ChooseFoodRow(EMCGameDirectorEvent Kind,const FMCGameDirectorState& Seen) const
{
    if(Kind==EMCGameDirectorEvent::StuckFood) return FName(TEXT("Fibre"));
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    FRandomStream Content((GS?GS->RunSeed:0)+NextId*7919+int32(Kind)*101);
    UDataTable* Menu=Mechanics?Mechanics->Menu.LoadSynchronous():nullptr;
    TArray<FName> MenuNames=Menu?Menu->GetRowNames():TArray<FName>(); MenuNames.Sort(FNameLexicalLess());
    TArray<FName> Names; TArray<float> Weights; float Total=0;
    for(const auto Name:MenuNames) if(const auto* Row=Menu->FindRow<FMCFoodRow>(Name,TEXT("Director menu"))) {
        const bool Match=Kind==EMCGameDirectorEvent::Pepper?Row->Kind==EMCFoodKind::Spicy:Row->Kind==EMCFoodKind::Food;
        const float Added=MCForecastDirectedFoodWork(GetWorld(),Mechanics,Name)/(FMath::Max(1,Seen.AvailablePlayers)*40)+(Kind==EMCGameDirectorEvent::Pepper?.35f:0);
        if(Match && Seen.Pressure+Added<=DaySettings.PressureLimit && Row->SelectionWeight>0 && FMath::IsFinite(Row->SelectionWeight))
        {Names.Add(Name);Weights.Add(Row->SelectionWeight);Total+=Row->SelectionWeight;}
    }
    if(Names.IsEmpty()) return NAME_None;
    float Pick=Content.FRand()*Total;
    for(int32 I=0;I<Names.Num();++I) {Pick-=Weights[I];if(Pick<=0) return Names[I];}
    return Names.Last();
}
AMCGameDirector::FTicket AMCGameDirector::MakeTicket(EMCGameDirectorEvent Kind,const FMCGameDirectorState& Seen) const
{
    FTicket T; T.Kind=Kind; T.Id=NextId; T.Batch=100001+NextId;
    if(FoodKind(Kind)) T.FoodRow=ChooseFoodRow(Kind,Seen);
    // A drink scales its payload before admission; its saved asset remains unchanged.
    const float Room=FMath::Max(0.f,DaySettings.PressureLimit-Seen.Pressure);
    const float WorkBudget=FMath::Max(0.f,FMath::Min(Room,TargetPressure-Seen.Pressure+.2f))*FMath::Max(1,Seen.AvailablePlayers)*40;
    T.Patches=FMath::Clamp(FMath::FloorToInt(WorkBudget/FMath::Max(1.f,Settings->CleaningWorkerSeconds)),1,DaySettings.CoffeePatches);
    T.ToothCount=FMath::Clamp(FMath::FloorToInt((WorkBudget-T.Patches*Settings->CleaningWorkerSeconds)/(Settings->CleaningWorkerSeconds*.5f)),0,4);
    const auto* Cola=Mechanics?Mechanics->ColdColaProfile.LoadSynchronous():nullptr;
    T.IceCount=FMath::Clamp(FMath::FloorToInt(FMath::Max(0.f,Room-.3f)*FMath::Max(1,Seen.AvailablePlayers)*40/8),1,Cola?FMath::Clamp(Cola->IceCount,1,12):5);
    return T;
}
int32 AMCGameDirector::PendingFoodCount() const
{
    int32 Count=0; for(const auto& T:Tickets) Count+=FoodKind(T.Kind) && !T.bStarted; return Count;
}
FMCGameDirectorState AMCGameDirector::Observe() const
{
    FMCGameDirectorState S; S.bEnabled=bManaging; S.Pacing=Pacing; S.DayEndAt=DayEndsAt;
    S.PressureLimit=DaySettings.PressureLimit;
    S.PlannedFood=PlannedFoodTotal; S.SpawnedFood=SpawnedFoodTotal; S.FinishedFood=FinishedFoodTotal;
    for(const auto& T:Tickets) S.QueuedEvents+=!T.bStarted && !T.bDone;
    const auto Pipeline=MCMeasureFoodPipeline(GetWorld());
    S.WholeFood=Pipeline.Whole; S.Fragments=Pipeline.Fragments; S.CarriedFood=Pipeline.Carried; S.ThroatQueued=Pipeline.ThroatQueued;
    float Work=Pipeline.EstimatedWorkerSeconds;
    int32 Occupied=0,Players=0; float HealthSum=0,StaminaSum=0;
    const float CleaningSeconds=Settings?Settings->CleaningWorkerSeconds:8.f;
    const float RepairSeconds=Settings?Settings->RepairWorkerSeconds:10.f;
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        if(It->IsActorBeingDestroyed()) continue;
        if(const auto* Patch=Cast<AMCMouthSurface>(*It)) {
            if(Patch->bUlcer) { if(!Patch->IsHealed()) {++S.Ulcers;Work+=(1-Patch->Healing)*Patch->HealSeconds;} }
            else if(!Patch->IsClean()) {++S.CleaningTasks;Work+=Patch->RemainingLiquid()*CleaningSeconds;}
            continue;
        }
        if(const auto* Fire=Cast<AMCFirePatch>(*It);Fire && Fire->IsBurning()) {++S.Fires;Work+=Fire->Heat*Fire->ExtinguishSeconds;continue;}
        if(const auto* Ice=Cast<AMCIceBlock>(*It);Ice && !Ice->bBroken) {++S.Ice; Work+=8.f*Ice->Health/FMath::Max(1.f,Ice->MaxHealth);continue;}
        if(const auto* Hero=Cast<AMCToothCharacter>(*It)) {
            ++Players;
            if(!Hero->Status || !Hero->Status->IsAlive()) continue;
            ++S.LivingPlayers;
            const float Health=Hero->Status->State.Health/FMath::Max(1.f,Hero->Status->State.MaxHealth);
            HealthSum+=FMath::IsFinite(Health)?FMath::Clamp(Health,0.f,1.f):0;
            const float Stamina=Hero->GetStaminaNormalized();
            StaminaSum+=FMath::IsFinite(Stamina)?FMath::Clamp(Stamina,0.f,1.f):0;
            Occupied+=Hero->RewardInteraction!=nullptr || Hero->MimicCaptor!=nullptr || (Hero->ToothPhysics && !Hero->ToothPhysics->CanAct());
            if(Hero->MimicCaptor) S.bUrgent=true;
        }
        if(const auto* Tooth=Cast<AMCArenaTooth>(*It);Tooth && !Tooth->IsAvailable()) continue;
        if(const auto* Care=It->FindComponentByClass<UMCToothStatusComponent>();Care && Care->IsAlive()) {
            if(Care->State.CoffeeLeft>0) {++S.CleaningTasks; Work+=Care->CoffeeAmount()*CleaningSeconds;}
            if(Care->State.RepairLeft>0) {++S.CleaningTasks; Work+=RepairSeconds;}
        }
    }
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(!It->bBrushTool && !It->IsDisposed() && It->FoodData.Kind==EMCFoodKind::Spicy && !It->bFusePaused) S.bUrgent=true;
    S.bUrgent|=S.Fires>0 || S.Ulcers>0;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) S.bGlobalMovement|=It->IsYawnActive() || It->IsMotionActive();
    for(TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) S.bGlobalMovement|=It->IsActive();
    for(TActorIterator<AMCColdColaEvent> It(GetWorld());It;++It) S.bGlobalMovement|=!It->IsComplete();
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) S.bGlobalMovement|=It->ThroatPhase!=EMCThroatPhase::Collecting || It->QueuedFoodCount>0;
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) {
        const bool Active=It->IsBossAlive() && It->Runtime.State!=EMCBossState::Dormant;
        S.bGlobalMovement|=Active; S.bUrgent|=Active;
    }
    for(TActorIterator<AMCRewardChest> It(GetWorld());It;++It) S.bUrgent|=It->IsHoldingCaptive();
    S.AvailablePlayers=FMath::Max(0,S.LivingPlayers-Occupied);
    S.WorkSeconds=FMath::IsFinite(Work)?FMath::Max(0.f,Work):400;
    S.TeamHealth=Players>0?HealthSum/Players:0; S.TeamStamina=Players>0?StaminaSum/Players:0;
    const int32 Workers=FMath::Max(1,S.AvailablePlayers);
    S.Pressure=Work/(Workers*40.f)+(S.bGlobalMovement?.3f:0)+(S.bUrgent?.35f:0);
    if(!FMath::IsFinite(S.Pressure)) S.Pressure=10;
    S.Stress=FMath::Clamp((1-S.TeamHealth)*.65f+(1-S.TeamStamina)*.2f+(S.bUrgent?.25f:0)+
        (S.LivingPlayers>0?float(Occupied)/S.LivingPlayers*.25f:.5f)+FMath::Max(0.f,S.Pressure-DaySettings.PressureLimit)*.5f,0.f,1.f);
    S.Difficulty=Difficulty; S.Throughput=Throughput; S.TargetPressure=TargetPressure;
    const auto* GameState=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GameState?GameState->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    S.DayProgress=bSupportMode?0.f:FMath::Clamp(float((Now-DayStartedAt)/FMath::Max(1.f,DaySettings.DaySeconds)),0.f,1.f);
    S.CompletionProgress=SpawnedFoodTotal>0?FMath::Clamp(float(FinishedFoodTotal)/SpawnedFoodTotal,0.f,1.f):0;
    return S;
}
void AMCGameDirector::UpdateAdaptation(const FMCGameDirectorState& Seen,double Now,float Dt)
{
    const float Elapsed=float(Now-SampleAt);
    if(Elapsed>=2) {
        const float Cleared=FMath::Max(0.f,PreviousWork+AddedWork-Seen.WorkSeconds);
        // Drinks add obstacles over several frames. Do not mistake that deferred
        // generation (or a reactive emergency) for a change in player throughput.
        if(PreviousWork+AddedWork>1 && Seen.AvailablePlayers>0 && !Seen.bGlobalMovement && !Seen.bUrgent) {
            const float Rate=FMath::Clamp(Cleared/(Elapsed*Seen.AvailablePlayers),0.f,2.f);
            Throughput=FMath::Lerp(Throughput,Rate,1-FMath::Exp(-Elapsed/Settings->AdaptationSeconds));
        }
        PreviousWork=Seen.WorkSeconds; AddedWork=0; SampleAt=Now;
    }
    const float Performance=FMath::Clamp((Throughput-.25f)/.75f,0.f,1.f)*(1-Seen.Stress);
    float Desired=FMath::Lerp(DaySettings.MinimumDifficulty,DaySettings.MaximumDifficulty,Performance);
    if(Seen.Pressure>=DaySettings.PressureLimit || Seen.TeamHealth<.45f) Desired=DaySettings.MinimumDifficulty;
    const float Response=Settings->AdaptationSeconds*(Desired<Difficulty?.5f:2.f);
    Difficulty=FMath::Lerp(Difficulty,Desired,1-FMath::Exp(-FMath::Max(0.f,Dt)/Response));
    const float Fraction=(Difficulty-DaySettings.MinimumDifficulty)/FMath::Max(.01f,DaySettings.MaximumDifficulty-DaySettings.MinimumDifficulty);
    const float DesiredPressure=FMath::Clamp(FMath::Lerp(DaySettings.TargetPressureMin,DaySettings.TargetPressureMax,FMath::Clamp(Fraction,0.f,1.f))*(1-.35f*Seen.Stress),DaySettings.TargetPressureMin,DaySettings.TargetPressureMax);
    TargetPressure=FMath::Lerp(TargetPressure,DesiredPressure,1-FMath::Exp(-FMath::Max(0.f,Dt)/Settings->AdaptationSeconds));
}
float AMCGameDirector::ComputeWaitWeight(const FMCGameDirectorState& Seen) const
{
    const float Ratio=Seen.Pressure/FMath::Max(.05f,TargetPressure);
    return 1+FMath::Clamp(Ratio*Ratio*4,0.f,20.f)+Seen.Stress*8;
}
TArray<FMCGameDirectorCandidate> AMCGameDirector::EvaluateCandidates(const FMCGameDirectorState& Seen,double Now) const
{
    TArray<FMCGameDirectorCandidate> Out; if(!Settings || !Mechanics) return Out;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    const bool Reserved=Tickets.ContainsByPredicate([](const FTicket& T){return !T.bStarted && T.WarningAt>=0;});
    const float Deficit=FMath::Clamp((TargetPressure-Seen.Pressure)/FMath::Max(.05f,TargetPressure),0.f,1.f);
    for(const auto& Rule:Settings->Events) {
        auto& C=Out.AddDefaulted_GetRef(); C.Kind=Rule.Kind; C.BaseWeight=Rule.Weight;
        const auto T=MakeTicket(Rule.Kind,Seen); C.ForecastPressure=ForecastPressure(T,Seen);
        if(Rule.Weight<=0) C.BlockReason=TEXT("Выключено");
        else if(!bSupportMode && GS && GS->Day<Rule.FirstDay) C.BlockReason=TEXT("Откроется в день ")+FString::FromInt(Rule.FirstDay);
        else if(Difficulty+.001f<Rule.MinimumDifficulty) C.BlockReason=TEXT("Сложность ниже порога");
        else if(Rule.MaxPerDay>0 && DayEventCounts.FindRef(Rule.Kind)>=Rule.MaxPerDay) C.BlockReason=TEXT("Достигнут безопасный предел");
        else if(const auto* Last=LastEventAt.Find(Rule.Kind);Last && Now<*Last+Rule.CooldownSeconds) C.BlockReason=FString::Printf(TEXT("Cooldown %.0f с"),*Last+Rule.CooldownSeconds-Now);
        else if(Pacing!=EMCGameDirectorPacing::Build) C.BlockReason=Pacing==EMCGameDirectorPacing::FinalCleanup?TEXT("Финальная уборка"):TEXT("Разгрузка / передышка");
        else if(Reserved) C.BlockReason=TEXT("Окно уже зарезервировано");
        else if(Now<DayStartedAt+Settings->InitialCleaningSeconds) C.BlockReason=TEXT("Начальная уборка");
        else if(Seen.AvailablePlayers<=0) C.BlockReason=TEXT("Нет доступных игроков");
        else if(Seen.bUrgent) C.BlockReason=TEXT("Срочная угроза");
        else if(Seen.bGlobalMovement) C.BlockReason=TEXT("Движение / глотание");
        else if(C.ForecastPressure>DaySettings.PressureLimit) C.BlockReason=TEXT("Прогноз выше лимита");
        else if(Rule.Kind!=EMCGameDirectorEvent::Reward && Seen.Pressure>=TargetPressure) C.BlockReason=TEXT("Целевая нагрузка достигнута");
        else if(FoodKind(Rule.Kind) && (Seen.WholeFood>=DaySettings.MaxWholeFood || Seen.Fragments>=DaySettings.MaxFragments ||
            MCMeasureFoodPipeline(GetWorld()).EstimatedWorkerSeconds>=DaySettings.FoodWorkPerPlayer*FMath::Max(1,Seen.AvailablePlayers))) C.BlockReason=TEXT("Очередь еды заполнена");
        else if(FoodKind(Rule.Kind) && Now<NextFoodAt) C.BlockReason=TEXT("Интервал порций");
        else if(!FoodKind(Rule.Kind) && Now<LastSpecialAt+DaySettings.EventGap) C.BlockReason=TEXT("Интервал событий");
        else if(FoodKind(Rule.Kind) && (T.FoodRow.IsNone() || !Mechanics->Menu.LoadSynchronous() ||
            !Mechanics->Menu.LoadSynchronous()->FindRow<FMCFoodRow>(T.FoodRow,TEXT("Director availability")))) C.BlockReason=TEXT("Нет подходящей еды в меню");
        else if(Rule.Kind==EMCGameDirectorEvent::Coffee && Seen.CleaningTasks>FMath::Max(2,Seen.AvailablePlayers*2)) C.BlockReason=TEXT("Сначала текущие пятна");
        else if((Rule.Kind==EMCGameDirectorEvent::Pepper || Rule.Kind==EMCGameDirectorEvent::Boss) && Seen.Stress>.25f) C.BlockReason=TEXT("Команде нужна разгрузка");
        if(C.BlockReason.IsEmpty() && Rule.Kind==EMCGameDirectorEvent::Reward) {
            bool Chest=false; for(TActorIterator<AMCRewardChest> It(GetWorld());It;++It) if(!It->IsActorBeingDestroyed()) {Chest=true;break;}
            if(!Mode || !Mode->RoguelikeDirector || Mode->RoguelikeDirector->PendingRewards<=0) C.BlockReason=TEXT("Нет заработанной награды");
            else if(Chest) C.BlockReason=TEXT("Сундук уже на арене");
        }
        if(C.BlockReason.IsEmpty() && Rule.Kind==EMCGameDirectorEvent::LooseTooth) {
            bool Tooth=false; if(GS) for(const AMCArenaTooth* A:GS->ArenaTeeth) if(IsValid(A) && A->IsAvailable() && A->Status && !A->Status->IsLoose()) {Tooth=true;break;}
            if(!Tooth) C.BlockReason=TEXT("Нет доступного зуба");
        }
        if(C.BlockReason.IsEmpty() && Rule.Kind==EMCGameDirectorEvent::Boss) {
            bool Boss=false; for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) if(It->IsBossAlive() && It->Runtime.State==EMCBossState::Dormant) {Boss=true;break;}
            if(!Boss) C.BlockReason=TEXT("Нет готового босса");
        }
        if(C.BlockReason.IsEmpty() && (Rule.Kind==EMCGameDirectorEvent::Yawn || Rule.Kind==EMCGameDirectorEvent::CoffeeFlood || FoodKind(Rule.Kind))) {
            bool Tongue=false; for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {Tongue=true;break;}
            if(!Tongue) C.BlockReason=TEXT("Нет языка / арены");
        }
        float Duration=FoodKind(Rule.Kind)?25.f:Rule.Kind==EMCGameDirectorEvent::Boss?90.f:15.f;
        if(Rule.Kind==EMCGameDirectorEvent::ColdCola) {const auto* Cola=Mechanics->ColdColaProfile.LoadSynchronous();Duration=Cola?Cola->ColdSeconds+Cola->ThawSeconds+15:55;}
        if(DayEndsAt>0 && C.BlockReason.IsEmpty() && Now+Duration+Settings->WarningSeconds>DayEndsAt)
            C.BlockReason=bSupportMode?TEXT("Не успеть до следующего ключевого события"):TEXT("Не успеть до конца дня");
        if(!C.BlockReason.IsEmpty()) continue;
        const float Cost=FMath::Max(0.f,C.ForecastPressure-Seen.Pressure);
        const float Fit=FMath::Clamp((TargetPressure-Seen.Pressure+.15f)/FMath::Max(.05f,Cost),.15f,1.5f);
        C.EffectiveWeight=Rule.Weight*(.25f+Deficit*1.5f)*Fit;
        if(Rule.Kind==EMCGameDirectorEvent::Food) C.EffectiveWeight*=Seen.WholeFood==0 && Seen.Fragments==0?1.8f:1.f;
        else if(Rule.Kind==EMCGameDirectorEvent::Reward) C.EffectiveWeight=Rule.Weight*(1+Seen.Stress*2);
        else C.EffectiveWeight*=FMath::Clamp(Difficulty,.25f,1.5f)*(1-Seen.Stress);
        for(int32 I=RecentKinds.Num()-1;I>=0;--I) if(RecentKinds[I]==Rule.Kind) C.EffectiveWeight*=I==RecentKinds.Num()-1?.35f:.7f;
    }
    float Sum=ComputeWaitWeight(Seen); for(const auto& C:Out) Sum+=C.EffectiveWeight;
    for(auto& C:Out) C.Probability=Sum>0?C.EffectiveWeight/Sum:0;
    return Out;
}
void AMCGameDirector::CancelReservation(const FString& Reason,double Now)
{
    const int32 Removed=Tickets.RemoveAll([](const FTicket& T){return !T.bStarted;});
    if(Removed>0) {PlannedFoodTotal=SpawnedFoodTotal;CurrentTitle=TEXT("ТЕКУЩАЯ РАБОТА");NextDecisionAt=Now+Settings->DecisionInterval;Record(TEXT("Выбор отменён: ")+Reason);}
}
bool AMCGameDirector::SelectEvent(const FMCGameDirectorState& Seen,double Now)
{
    CandidateScores=EvaluateCandidates(Seen,Now); WaitWeight=ComputeWaitWeight(Seen);
    float Sum=WaitWeight; for(const auto& C:CandidateScores) Sum+=C.EffectiveWeight;
    WaitProbability=WaitWeight/Sum; CandidateTitle=TEXT("ПЕРЕДЫШКА");
    float Best=WaitProbability; for(const auto& C:CandidateScores) if(C.Probability>Best) {Best=C.Probability;CandidateTitle=EventName(C.Kind);}
    float Pick=Random.FRand()*Sum;
    NextDecisionAt=Now+Settings->DecisionInterval*Random.FRandRange(.8f,1.3f);
    if(Pick<WaitWeight) {Record(FString::Printf(TEXT("Выбор: ждать %.0f%% · нагрузка %.2f / цель %.2f · темп %.2f"),WaitProbability*100,Seen.Pressure,TargetPressure,Throughput));return false;}
    Pick-=WaitWeight;
    for(const auto& C:CandidateScores) {
        if(C.EffectiveWeight<=0) continue; Pick-=C.EffectiveWeight; if(Pick>0) continue;
        FTicket T=MakeTicket(C.Kind,Seen); T.Id=NextId++; T.Day=GetWorld()->GetGameState<AMCGameState>()->Day; T.RequestedAt=Now;
        Record(FString::Printf(TEXT("Выбор %s %.1f→%.1f %.0f%%"),ShortEventName(C.Kind),C.BaseWeight,C.EffectiveWeight,C.Probability*100));
        Tickets.Add(T); if(FoodKind(C.Kind)) ++PlannedFoodTotal;
        auto& Reserved=Tickets.Last();
        if(TryStart(Reserved,Seen,Now)) {
            // Extra items are admitted one by one, never an unconditional batch.
            if(C.Kind==EMCGameDirectorEvent::Food) for(int32 I=1;I<DaySettings.MaxFoodBatch;++I) {
                const auto Fresh=Observe();
                const auto* Rule=Settings->Events.FindByPredicate([&](const auto& R){return R.Kind==C.Kind;});
                if(Fresh.bUrgent || Fresh.bGlobalMovement || Fresh.WholeFood>=DaySettings.MaxWholeFood || Fresh.Fragments>=DaySettings.MaxFragments ||
                    Fresh.Pressure>=TargetPressure*.65f || (Rule && Rule->MaxPerDay>0 && DayEventCounts.FindRef(C.Kind)>=Rule->MaxPerDay)) break;
                auto Extra=MakeTicket(C.Kind,Fresh);Extra.Id=NextId++;Extra.Day=T.Day;Extra.RequestedAt=Now;
                if(ForecastPressure(Extra,Fresh)>TargetPressure || !TryStart(Extra,Fresh,Now)) break;
                Tickets.Add(Extra);++PlannedFoodTotal;
            }
            return true;
        }
        if(Reserved.WarningAt<0) {Tickets.Pop();PlannedFoodTotal=SpawnedFoodTotal;LastEventAt.Add(C.Kind,Now);Record(TEXT("Место или ресурс недоступны: пересчёт выбора"));}
        return false;
    }
    return false;
}
bool AMCGameDirector::CanStartSwallow() const
{
    if(!IsManagingEvents()) return true;
    // Intake and accepted spicy food keep their agency: urgency is not a swallow veto.
    for(const auto& T:Tickets) if(!T.bStarted && T.WarningAt>=0 && MovementKind(T.Kind)) return false;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(It->IsYawnActive() || It->IsMotionActive()) return false;
    for(TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) if(It->IsActive()) return false;
    for(TActorIterator<AMCColdColaEvent> It(GetWorld());It;++It) if(!It->IsComplete()) return false;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It)
        if(It->ThroatPhase!=EMCThroatPhase::Collecting && It->ThroatPhase!=EMCThroatPhase::Anticipation) return false;
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) if(It->IsBossAlive() && It->Runtime.State!=EMCBossState::Dormant) return false;
    return true;
}
bool AMCGameDirector::CanStartRewardInteraction() const
{
    if(!IsManagingEvents()) return true;
    const auto S=Observe();
    if(S.bUrgent || S.bGlobalMovement || S.AvailablePlayers<=0) return false;
    for(const auto& T:Tickets) if(!T.bStarted && T.WarningAt>=0) return false;
    return S.Pressure<DaySettings.PressureLimit;
}
float AMCGameDirector::ForecastPressure(const FTicket& T,const FMCGameDirectorState& S) const
{
    const int32 Workers=FMath::Max(1,S.AvailablePlayers);
    float Work=0,Intensity=0;
    if(FoodKind(T.Kind)) {
        const auto* Menu=Mechanics->Menu.LoadSynchronous();
        const auto* Row=Menu && !T.FoodRow.IsNone()?Menu->FindRow<FMCFoodRow>(T.FoodRow,TEXT("Director forecast"),false):nullptr;
        if(Row) Work=MCForecastDirectedFoodWork(GetWorld(),Mechanics,T.FoodRow,T.Kind==EMCGameDirectorEvent::StuckFood);
        if(T.Kind==EMCGameDirectorEvent::Pepper) Intensity=.35f;
    } else if(T.Kind==EMCGameDirectorEvent::Coffee) {
        Work=(T.Patches+T.ToothCount*.5f)*Settings->CleaningWorkerSeconds;
    } else if(T.Kind==EMCGameDirectorEvent::LooseTooth) Work=Settings->RepairWorkerSeconds;
    else if(T.Kind==EMCGameDirectorEvent::ColdCola) {
        Work=T.IceCount*8.f; Intensity=.3f;
    } else if(T.Kind==EMCGameDirectorEvent::Boss) Intensity=.65f;
    else if(MovementKind(T.Kind)) Intensity=.3f;
    else if(T.Kind==EMCGameDirectorEvent::Reward) Work=5.f;
    return S.Pressure+Work/(Workers*40.f)+Intensity;
}
bool AMCGameDirector::TryDispatchReward(AMCRoguelikeDirector* Rewards)
{
    if(!IsManagingEvents() || !Rewards || !IsLaunchingEvent(EMCGameDirectorEvent::Reward)) return false;
    const auto Seen=Observe(); if(Seen.bUrgent || Seen.bGlobalMovement || Seen.Pressure>DaySettings.PressureLimit || Pacing==EMCGameDirectorPacing::FinalCleanup) return false;
    return Rewards->TryReleaseQueuedReward();
}
bool AMCGameDirector::TryStart(FTicket& T,const FMCGameDirectorState& S,double Now)
{
    if(T.bDone || T.bStarted || T.RequestedAt>Now) return false;
    if(S.bUrgent || S.bGlobalMovement || S.AvailablePlayers<=0) {T.WarningAt=-1; return false;}
    if(S.Pressure>=DaySettings.PressureLimit) return false;
    const float Forecast=ForecastPressure(T,S);
    if(Forecast>DaySettings.PressureLimit || (T.Kind!=EMCGameDirectorEvent::Reward && S.Pressure>=TargetPressure)) {T.WarningAt=-1; return false;}
    if(MovementKind(T.Kind) || T.Kind==EMCGameDirectorEvent::Pepper) {
        if(T.WarningAt<0) {T.WarningAt=Now;CurrentTitle=TEXT("СКОРО: ")+EventName(T.Kind);Record(TEXT("Предупреждение: ")+EventName(T.Kind));return false;}
        if(Now<T.WarningAt+Settings->WarningSeconds) return false;
    }
    LaunchGrant=T.Kind;
    bool Success=false;
    if(FoodKind(T.Kind)) {
        auto* Food=T.Kind==EMCGameDirectorEvent::StuckFood
            ?MCSpawnDirectedStuckFood(GetWorld(),Mechanics,T.FoodRow,T.Batch,Random)
            :MCSpawnDirectedFoodEntry(GetWorld(),Mechanics,T.FoodRow,T.Batch,Random);
        if(Food) {
            // Mixed stacks and exact geometry can differ from the conservative
            // preflight estimate. Reject this synchronous attempt before it is
            // committed to the ledger or replicated on the next network tick.
            if(Observe().Pressure>DaySettings.PressureLimit) {Food->Destroy();LaunchGrant.Reset();return false;}
            T.Actor=Food; ++SpawnedFoodTotal; Success=true;
        }
    } else if(T.Kind==EMCGameDirectorEvent::Coffee && Services) {
        Services->AddDirt(true,T.Patches,T.Batch,T.ToothCount,.5f); Success=Observe().WorkSeconds>S.WorkSeconds;
    } else if(T.Kind==EMCGameDirectorEvent::CoffeeFlood) {
        auto* Flood=GetWorld()->SpawnActor<AMCCoffeeFlood>(); if(Flood) {Flood->SetOwner(this);Flood->Start(Mechanics);T.Actor=Flood;Success=Flood->IsActive();}
    } else if(T.Kind==EMCGameDirectorEvent::ColdCola) {
        auto* Cola=GetWorld()->SpawnActor<AMCColdColaEvent>(); if(Cola) {Cola->SetOwner(this);Cola->Start(Mechanics,T.IceCount);T.Actor=Cola;Success=Cola->bActive;}
    } else if(T.Kind==EMCGameDirectorEvent::Yawn) {
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(It->StartYawn(4)) {T.Actor=*It;Success=true;break;}
    } else if(T.Kind==EMCGameDirectorEvent::LooseTooth) {
        const auto* GS=GetWorld()->GetGameState<AMCGameState>();
        for(AMCArenaTooth* Tooth:GS->ArenaTeeth) if(IsValid(Tooth) && Tooth->IsAvailable() && !Tooth->Status->IsLoose()) {Tooth->Status->Loosen();T.Actor=Tooth;Success=true;break;}
    } else if(T.Kind==EMCGameDirectorEvent::Reward) {
        if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();Mode && Mode->RoguelikeDirector) {
            auto* Rewards=Mode->RoguelikeDirector.Get();
            Success=Rewards->TryReleaseQueuedReward();
        }
    } else if(T.Kind==EMCGameDirectorEvent::Boss) {
        for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) if(It->Runtime.State==EMCBossState::Dormant && It->IsBossAlive()) {It->ActivateBoss();T.Actor=*It;Success=It->Runtime.State!=EMCBossState::Dormant;break;}
    }
    LaunchGrant.Reset();
    if(!Success) return false;
    T.bStarted=true; T.StartedAt=Now; T.WarningAt=-1; CurrentTitle=EventName(T.Kind);
    // Regular meals have their own cadence. They must not keep postponing the
    // coffee/other-special window while that event still fits the workload.
    if(T.Kind!=EMCGameDirectorEvent::Food) LastSpecialAt=Now;
    LastEventAt.Add(T.Kind,Now); ++DayEventCounts.FindOrAdd(T.Kind); RecentKinds.Add(T.Kind);
    if(RecentKinds.Num()>4) RecentKinds.RemoveAt(0);
    if(FoodKind(T.Kind)) NextFoodAt=Now+DaySettings.FoodInterval;
    AddedWork+=FMath::Max(0.f,Observe().WorkSeconds-S.WorkSeconds);
    if(T.Kind==EMCGameDirectorEvent::Reward) T.bDone=true;
    Record(FString::Printf(TEXT("Запуск: %s%s"),*EventName(T.Kind),FoodKind(T.Kind)?TEXT(" · 1 продукт"):TEXT("")));
    Record(FString::Printf(TEXT("Бюджет: %.2f → прогноз %.2f / %.2f"),S.Pressure,Forecast,DaySettings.PressureLimit));
    return true;
}
void AMCGameDirector::ResolveTickets(double Now)
{
    const auto Seen=Observe();
    for(auto& T:Tickets) {
        if(!T.bStarted || T.bDone) continue;
        bool Done=false;
        if(FoodKind(T.Kind)) {
            const auto Food=MCMeasureFoodPipeline(GetWorld(),T.Batch);
            Done=Food.OutstandingActors==0 && Food.UnresolvedHazards==0;
        } else if(T.Kind==EMCGameDirectorEvent::Coffee) Done=Seen.CleaningTasks==0;
        else if(T.Kind==EMCGameDirectorEvent::CoffeeFlood) {const auto* A=Cast<AMCCoffeeFlood>(T.Actor.Get());Done=!A || !A->IsActive();}
        else if(T.Kind==EMCGameDirectorEvent::ColdCola) {const auto* A=Cast<AMCColdColaEvent>(T.Actor.Get());Done=!A || A->IsComplete();}
        else if(T.Kind==EMCGameDirectorEvent::Yawn) {const auto* A=Cast<AMCTongue>(T.Actor.Get());Done=!A || !A->IsYawnActive();}
        else if(T.Kind==EMCGameDirectorEvent::LooseTooth) {const auto* A=T.Actor.Get();const auto* Care=A?A->FindComponentByClass<UMCToothStatusComponent>():nullptr;Done=!Care || !Care->IsLoose();}
        else if(T.Kind==EMCGameDirectorEvent::Boss) {const auto* A=Cast<AMCBossCharacter>(T.Actor.Get());Done=!A || !A->IsBossAlive();}
        if(Done) {T.bDone=true;if(FoodKind(T.Kind)) ++FinishedFoodTotal;Record(TEXT("Завершено: ")+EventName(T.Kind));}
    }
}
bool AMCGameDirector::HasOutstandingWork() const
{
    for(const auto& T:Tickets) if(T.bStarted && !T.bDone && T.Kind!=EMCGameDirectorEvent::Reward) return true;
    const auto Seen=Observe(); const auto Food=MCMeasureFoodPipeline(GetWorld());
    return Food.OutstandingActors>0 || Food.UnresolvedHazards>0 || Seen.CleaningTasks>0 || Seen.Ice>0 || Seen.bGlobalMovement || Seen.bUrgent;
}
void AMCGameDirector::Publish(const FMCGameDirectorState& Seen,const FString& Reason,double Now)
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    GS->DirectorState=Seen; auto& S=GS->DirectorState; S.CurrentTitle=CurrentTitle; S.DecisionReason=Reason;
    S.Instruction=Pacing==EMCGameDirectorPacing::FinalCleanup?TEXT("Закончите сбор, доставку и уборку"):TEXT("Режьте, собирайте и доставляйте еду. Следите за предупреждениями.");
    if(Seen.Fires>0) S.Instruction=TEXT("Сначала потушите огонь. Новые события ждут.");
    else if(Seen.Ulcers>0) S.Instruction=TEXT("Вылечите язвы: они остаются задачей после удаления еды.");
    else if(Seen.bUrgent) S.Instruction=TEXT("Сначала решите срочную угрозу: уберите перец, помогите пленнику или закончите бой.");
    else if(Seen.Ice>0) S.Instruction=TEXT("Разбейте лёд киркой, затем возвращайтесь к доставке и уборке.");
    else if(Seen.bGlobalMovement) S.Instruction=TEXT("Заканчивается движение или глотание. Новые помехи ждут.");
    else if(Seen.CleaningTasks>0) S.Instruction=TEXT("Очистите зубы и пятна. Еду режьте, собирайте и доставляйте к выходу.");
    S.Candidates=EvaluateCandidates(Seen,Now); S.WaitWeight=ComputeWaitWeight(Seen);
    float Sum=S.WaitWeight;for(const auto& C:S.Candidates) Sum+=C.EffectiveWeight;
    S.WaitProbability=S.WaitWeight/Sum; S.NextTitle=TEXT("ПЕРЕДЫШКА");
    float Best=S.WaitProbability; for(const auto& C:S.Candidates) if(C.Probability>Best) {Best=C.Probability;S.NextTitle=EventName(C.Kind);}
    for(const auto& T:Tickets) if(!T.bStarted && T.WarningAt>=0) {S.bNextReserved=true;S.NextTitle=EventName(T.Kind);break;}
    S.DecisionLog=GS->DirectorDecisionLog;
    S.LastDecision=S.DecisionLog.IsEmpty()?FString():S.DecisionLog.Last();
    int32 Outstanding=0; for(const auto& T:Tickets) Outstanding+=T.bStarted && !T.bDone && FoodKind(T.Kind);
    const auto Pipeline=MCMeasureFoodPipeline(GetWorld());
    GS->TasksLeft=FMath::Max(Outstanding,Pipeline.OutstandingActors>0?1:0)+Seen.CleaningTasks+Seen.Fires+Seen.Ulcers+Seen.Ice+(Seen.bGlobalMovement?1:0);
    GS->TasksTotal=FMath::Max(GS->TasksTotal,GS->TasksLeft);
    GS->ForceNetUpdate();
}
void AMCGameDirector::FinishDay(const FMCGameDirectorState& Seen,double Now)
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS || GS->Phase!=EMCShiftPhase::Working) return;
    const bool Failed=HasOutstandingWork();
    if(Failed) {++GS->FailedEvents;GS->MouthHealth=FMath::Max(0.f,GS->MouthHealth-GS->TasksLeft*Settings->MissedTaskDamage);}
    CancelReservation(TEXT("конец дня — невыбранные события не являются долгом"),Now);
    // Only work that exists in the world can survive the day boundary.
    GS->PreviousStepFailed=Failed;
    GS->Phase=GS->MouthHealth<=0 || (GS->Day>=7 && Failed)?EMCShiftPhase::Lost:GS->Day>=7?EMCShiftPhase::Won:EMCShiftPhase::Intermission;
    GS->PhaseEndsAt=Now+Settings->IntermissionSeconds; GS->StepStartedAt=Now;
    Pacing=EMCGameDirectorPacing::Intermission;
    Record(Failed?TEXT("Смена закончена: текущая работа переносится дальше"):TEXT("Смена завершена: выданная работа выполнена"));
    Publish(Observe(),Failed?TEXT("Остаток задач и последствия сохранены"):TEXT("Следующий день после передышки"),Now);
    GS->ForceNetUpdate();
}
void AMCGameDirector::Tick(float Dt)
{
    Super::Tick(Dt); if(!IsManagingEvents() || !Settings || !Mechanics) return;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(GS->Phase!=EMCShiftPhase::Working) return;
    const double Now=GS->GetServerWorldTimeSeconds(); ResolveTickets(Now); auto Seen=Observe();
    UpdateAdaptation(Seen,Now,Dt); Seen=Observe();
    if(DayEndsAt>0 && Now>=DayEndsAt) {
        if(bInterlude) {CancelReservation(TEXT("следующий основной эвент"),Now);Stop();return;}
        CancelReservation(TEXT("конец смены"),Now);Publish(Seen,TEXT("Окончание смены"),Now);FinishDay(Seen,Now);return;
    }
    if(!bSupportMode && Now>=DayEndsAt-DaySettings.FinalCleanupSeconds) Pacing=EMCGameDirectorPacing::FinalCleanup;
    else if(Pacing==EMCGameDirectorPacing::Build && (Seen.Pressure>=TargetPressure*1.12f || Seen.Stress>.4f || Seen.bUrgent)) {
        Pacing=EMCGameDirectorPacing::Drain; PhaseStartedAt=Now;
    } else if(Pacing==EMCGameDirectorPacing::Drain && !Seen.bUrgent && !Seen.bGlobalMovement && Seen.Pressure<=TargetPressure*.45f) {
        Pacing=EMCGameDirectorPacing::Rest; RestUntil=Now+DaySettings.RestSeconds*Random.FRandRange(.75f,1.25f);
        Record(TEXT("Передышка: команда разгрузила работу"));
    } else if(Pacing==EMCGameDirectorPacing::Rest && Now>=RestUntil) {Pacing=EMCGameDirectorPacing::Build;PhaseStartedAt=Now;}
    Seen.Pacing=Pacing;
    if(Seen.bGlobalMovement || Seen.bUrgent || Seen.AvailablePlayers<=0 || Pacing!=EMCGameDirectorPacing::Build)
        CancelReservation(TEXT("приоритет доставке и текущей работе"),Now);
    FString Reason;
    if(Seen.bUrgent) Reason=TEXT("Пауза: команда решает срочную угрозу");
    else if(Seen.AvailablePlayers<=0) Reason=TEXT("Жду доступных игроков");
    else if(Seen.bGlobalMovement) Reason=TEXT("Жду завершения движения, напитка или глотания");
    else if(Pacing==EMCGameDirectorPacing::FinalCleanup) Reason=TEXT("Финальная уборка: завершаем только выданную работу");
    else if(Pacing==EMCGameDirectorPacing::Drain) Reason=TEXT("Снижаю темп: накопилась работа или команда устала");
    else if(Pacing==EMCGameDirectorPacing::Rest) Reason=TEXT("Передышка после разгрузки");
    else if(Now<DayStartedAt+Settings->InitialCleaningSeconds) Reason=TEXT("Начальная уборка, затем оценка ситуации");
    else {
        bool Reserved=false;
        for(auto& T:Tickets) if(!T.bStarted) {
            Reserved=true;
            if(TryStart(T,Seen,Now)) {Seen=Observe();Reason=TEXT("Запустил выбранное событие");}
            else if(T.WarningAt>=0) Reason=TEXT("Предупреждение: событие выбрано, окно зарезервировано");
            else Reason=TEXT("Выбор потерял актуальность: пересчитываю");
            break;
        }
        if(Reserved && Reason==TEXT("Выбор потерял актуальность: пересчитываю")) CancelReservation(Reason,Now);
        if(!Reserved && Now>=NextDecisionAt) {
            const bool Started=SelectEvent(Seen,Now);Seen=Observe();
            Reason=Started?TEXT("Выбор по нагрузке, темпу и весам событий"):PendingFoodCount()>0?TEXT("Выбрано предупреждение перед событием"):TEXT("Взвесил события: жду или готовлю выбранное окно");
        }
        if(Reason.IsEmpty()) Reason=Seen.Pressure>=TargetPressure?TEXT("Целевая нагрузка достигнута: жду разгрузки"):TEXT("Наблюдаю темп до следующего решения");
    }
    // Compact reason changes live; full log records decisions, cancellations and phase changes.
    LastReason=Reason; Publish(Observe(),Reason,Now);
}
