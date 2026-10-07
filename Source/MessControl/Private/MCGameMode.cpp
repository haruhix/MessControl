#include "MCGameMode.h"
#include "MCColdCola.h"
#include "MCHazardWave.h"
#include "MCTongue.h"
#include "MCThroat.h"
#include "MCDayDirector.h"
#include "MCMouthSurface.h"
#include "MCFirePatch.h"
#include "MCReactionVFX.h"
#include "MCCoffeeFlood.h"
#include "MCGameState.h"
#include "MCRoguelikeDirector.h"
#include "MCRoguelikePreview.h"
#include "MCRewardChest.h"
#include "MCBossCharacter.h"
#include "MCInventoryComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCCoreScenario.h"
#include "MCDayOneScenario.h"
#include "GameFramework/PlayerController.h"
#include "MCArenaTooth.h"
#include "MCArenaToothSocket.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "MCArenaDemo.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MCTaskActor.h"
#include "MCToothCharacter.h"
#include "MCPlayerController.h"
#include "MCPlaytestBotController.h"
#include "MCPlaytestSession.h"
#include "MCTutorialDirector.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AMCGameMode::AMCGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AMCToothCharacter::StaticClass();
    // Camera/component defaults are authored in the current player Blueprint.
    static ConstructorHelpers::FClassFinder<AMCToothCharacter> PlayerBlueprint(TEXT("/Game/Blueprints/BP_PlayerCharacter"));
    if(PlayerBlueprint.Succeeded()) DefaultPawnClass=PlayerBlueprint.Class;
    PlayerControllerClass = AMCPlayerController::StaticClass();
    GameStateClass = AMCGameState::StaticClass();
    PlayerStateClass = AMCPlayerState::StaticClass();
    TaskClass = AMCTaskActor::StaticClass();
    RunRulesProfile = TSoftObjectPtr<UMCRunRules>(FSoftObjectPath(TEXT("/Game/Data/DA_RunRules.DA_RunRules")));
    ArenaToothProfile = TSoftObjectPtr<UMCArenaToothProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_ArenaTooth.DA_ArenaTooth")));
    FirstDayPlan=TSoftObjectPtr<UMCDayPlan>(FSoftObjectPath(TEXT("/Game/Data/DA_Day01.DA_Day01")));
    static ConstructorHelpers::FObjectFinder<UMCDayEvent> Coffee(TEXT("/Game/Data/DA_Coffee"));
    static ConstructorHelpers::FObjectFinder<UMCDayEvent> Food(TEXT("/Game/Data/DA_Food"));
    static ConstructorHelpers::FObjectFinder<UMCDayEvent> Tooth(TEXT("/Game/Data/DA_LooseTooth"));
    if (Coffee.Succeeded()) EventPool.Add(Coffee.Object);
    if (Food.Succeeded()) EventPool.Add(Food.Object);
    if (Tooth.Succeeded()) EventPool.Add(Tooth.Object);
}
void AMCGameMode::BeginPlay()
{
    Super::BeginPlay();
    bTutorialRequested=UGameplayStatics::HasOption(OptionsString,TEXT("MCTutorial"));
    bLobbyRequested=UGameplayStatics::HasOption(OptionsString,TEXT("MCLobby"));
    if (FParse::Param(FCommandLine::Get(),TEXT("MCLegacyDays"))) bUseDayOnePlan=false;
    EventPool.RemoveAll([](const TObjectPtr<UMCDayEvent>& Event) { return !IsValid(Event); });
    // Native fallbacks also make a blank test map playable before content generation.
    if (EventPool.IsEmpty())
    {
        for (int32 Index=0; Index<3; ++Index)
        {
            UMCDayEvent* Event = NewObject<UMCDayEvent>(this);
            Event->Kind = static_cast<EMCTaskKind>(Index);
            Event->Title = FText::FromString(Index == 0 ? TEXT("COFFEE BREAK") : Index == 1 ? TEXT("SNACK ATTACK") : TEXT("WOBBLY BUSINESS"));
            EventPool.Add(Event);
        }
    }
    RestartShift();
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(),TEXT("MCArenaDemo")) && GetNetMode()==NM_Standalone)
        GetWorld()->SpawnActor<AMCArenaDemo>();
    if (FParse::Param(FCommandLine::Get(),TEXT("MCCore"))) GetWorld()->SpawnActor<AMCCoreScenario>();
    if (FParse::Param(FCommandLine::Get(),TEXT("MCDayOne"))) GetWorld()->SpawnActor<AMCDayOneScenario>();
    if (FParse::Param(FCommandLine::Get(),TEXT("MCRoguelikePreview"))) GetWorld()->SpawnActor<AMCRoguelikePreview>();
#endif
}
void AMCGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    const AMCGameState* State = GetGameState<AMCGameState>();
    const int32 Limit = State ? State->RunSettings.MaxPlayers : FMCRunSettings().MaxPlayers;
    if (ErrorMessage.IsEmpty() && AMCPlaytestSession::Find(GetWorld()) && AMCPlaytestSession::Find(GetWorld())->IsActive())
        ErrorMessage=TEXT("A diagnostic playtest is active. Stop it with MC.Bots.Stop before joining.");
    if (ErrorMessage.IsEmpty() && GetGameplayParticipantCount() >= Limit)
        ErrorMessage = FString::Printf(TEXT("This mouth is full (%d players)."), Limit);
}
void AMCGameMode::ClearTasks()
{
    for (AMCTaskActor* Task : ActiveTasks) if (IsValid(Task)) Task->Destroy();
    ActiveTasks.Empty();
}
void AMCGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    if (auto* State=NewPlayer?NewPlayer->GetPlayerState<AMCPlayerState>():nullptr)
    {
        State->bSessionHost=NewPlayer->IsLocalController() && GetNetMode()!=NM_DedicatedServer;
        static const FLinearColor Palette[]={FLinearColor(.24f,.65f,1),FLinearColor(1,.35f,.25f),FLinearColor(.4f,.9f,.3f),FLinearColor(.8f,.4f,1)};
        const int32 Index=FMath::Max(0,GetNumPlayers()-1)%UE_ARRAY_COUNT(Palette);
        State->PlayerColor=Palette[Index]; State->ForceNetUpdate();
        if (auto* Hero=Cast<AMCToothCharacter>(NewPlayer->GetPawn())) Hero->ApplyPlayerColor(State->PlayerColor);
    }
}
void AMCGameMode::AwardTask(AMCToothCharacter* Worker, EMCScoreTask Kind)
{
    AwardTaskToPlayerState(IsValid(Worker)?Worker->GetPlayerState<AMCPlayerState>():nullptr,Kind);
}
void AMCGameMode::AwardTaskToPlayerState(AMCPlayerState* Worker, EMCScoreTask Kind)
{
    if (!HasAuthority() || !IsValid(Worker) || Worker->GetWorld()!=GetWorld()) return;
    if (const auto* GS=GetGameState<AMCGameState>(); GS && (GS->bTutorialActive || GS->bLobbyWaiting)) return;
    Worker->AddPoints(ScoreRewards.ForTask(Kind));
}
void AMCGameMode::NotifyObjectiveCompleted(FName CompletionId)
{
    if (!HasAuthority() || CompletionId.IsNone() || !IsValid(RoguelikeDirector)) return;
    if (const auto* GS=GetGameState<AMCGameState>(); GS && (GS->bTutorialActive || GS->bLobbyWaiting)) return;
    RoguelikeDirector->NotifyTaskCompleted(CompletionId);
}
void AMCGameMode::RestartShift()
{
    if (AMCPlaytestSession* Session=AMCPlaytestSession::Find(GetWorld()); Session && Session->IsActive())
    {
        UE_LOG(LogTemp,Display,TEXT("MC_BOTS RestartShift blocked during a playtest. Use MC.Bots.Stop before restarting."));
        return;
    }
    AMCGameState* State = GetGameState<AMCGameState>();
    if (!State) return;
    if (IsValid(TutorialDirector)) { TutorialDirector->OnTutorialFinished.RemoveAll(this); TutorialDirector->Stop(); TutorialDirector->Destroy(); }
    TutorialDirector=nullptr;
    State->bTutorialActive=bTutorialRequested;
    State->bLobbyWaiting=bLobbyRequested;
    State->LobbyLoadedPlayers=0;
    if (IsValid(DayDirector)) DayDirector->Destroy(); DayDirector=nullptr;
    for (TActorIterator<AMCTongue> It(GetWorld());It;++It) { It->ResetPain(); It->ResetPressure(); It->ResetYawn(); }
    TArray<AActor*> OldDayActors;
    for (TActorIterator<AActor> It(GetWorld());It;++It) if (Cast<AMCFirePatch>(*It) || Cast<AMCReactionVFX>(*It) || Cast<AMCMouthSurface>(*It) || Cast<AMCCoffeeFlood>(*It) || Cast<AMCColdColaEvent>(*It) || Cast<AMCIceBlock>(*It) || Cast<AMCHazardWave>(*It) || It->ActorHasTag(TEXT("DayOne"))) OldDayActors.Add(*It);
    for (auto* Actor:OldDayActors) Actor->Destroy();
    State->DayPlan=nullptr; State->StepIndex=INDEX_NONE; State->bPhysicalBrushes=false; State->bDayOneComplete=false; State->FailedEvents=0;
    State->bDevManualEvents=false;
    ClearTasks();
    Objectives.Empty(); PendingRespawns.Empty();
    TArray<AMCFoodActor*> OldFood;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) OldFood.Add(*It);
    for (auto* Food:OldFood) Food->Destroy();
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        auto* PC=It->Get(); if (!PC) continue;
        if (auto* Player=PC->GetPlayerState<AMCPlayerState>()) Player->ResetMatchScore();
        if (APawn* Pawn=PC->GetPawn()) { PC->UnPossess(); Pawn->Destroy(); }
        if (IsGameplayParticipant(PC)) RestartPlayer(PC);
    }
    if (!IsValid(Throat))
    {
        for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It) if (!It->bBrushBin) { Throat=*It; break; }
        if (!Throat) Throat=GetWorld()->SpawnActor<AMCThroat>(FVector(920,0,180),FRotator::ZeroRotator);
    }
    if (auto* LivingThroat=Cast<AMCThroat>(Throat)) LivingThroat->ResetSwallow();
    const UMCRunRules* Rules = RunRulesProfile.LoadSynchronous();
    State->RunSettings = Rules ? Rules->Settings : FMCRunSettings();
    State->RunSettings.Sanitize();
    for (AMCArenaTooth* Tooth:State->ArenaTeeth) if (IsValid(Tooth)) Tooth->Destroy();
    State->ArenaTeeth.Empty();
    const UMCArenaToothProfile* ToothProfile=ArenaToothProfile.LoadSynchronous();
    TArray<AMCArenaToothSocket*> Sockets;
    for (TActorIterator<AMCArenaToothSocket> It(GetWorld());It;++It) Sockets.Add(*It);
    Sockets.Sort([](const AMCArenaToothSocket& A,const AMCArenaToothSocket& B){return A.ToothId<B.ToothId;});
    // Blank test maps retain a native fallback. Authored maps never get an extra inner row.
    const int32 Count=Sockets.IsEmpty()?State->RunSettings.InitialArenaTeeth:FMath::Min(State->RunSettings.InitialArenaTeeth,Sockets.Num());
    const int32 PerSide=FMath::DivideAndRoundUp(Count,2);
    UStaticMesh* DefaultMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Art/Meshes/SM_ToothProp.SM_ToothProp"));
    for (int32 I=0;I<Count;++I)
    {
        const int32 Row=I/2;
        const float X=PerSide<=1?0.f:FMath::Lerp(-890.f,965.f,float(Row)/float(PerSide-1));
        const FTransform MeshTransform=Sockets.IsEmpty()?FTransform(FQuat::Identity,FVector(X,I%2?815.f:-815.f,0),FVector(2.15,2.15,2.45)):Sockets[I]->Preview->GetComponentTransform();
        UStaticMesh* Mesh=Sockets.IsEmpty()?DefaultMesh:Sockets[I]->Preview->GetStaticMesh().Get();
        if (!Mesh) continue;
        const FTransform Transform(MeshTransform.GetRotation(),MeshTransform.TransformPosition(Mesh->GetBounds().Origin));
        auto* Tooth=GetWorld()->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Tooth)
        {
            Tooth->SetAppearance(Mesh,MeshTransform.GetScale3D(),ToothProfile?ToothProfile->GameplayMaterial.LoadSynchronous():nullptr);
            Tooth->Initialize(Sockets.IsEmpty()?I+1:Sockets[I]->ToothId,ToothProfile?ToothProfile->Settings:FMCArenaToothSettings());
            UGameplayStatics::FinishSpawningActor(Tooth,Transform); State->ArenaTeeth.Add(Tooth);
        }
    }
    State->Day = 0; State->MouthHealth = State->RunSettings.MaxMouthHealth; State->TasksLeft = 0; State->TasksTotal = 0;
    State->CurrentEvent = nullptr; State->Phase = EMCShiftPhase::Intermission;
    State->RunSeed = FMath::Rand();
    // ?Seed=123 gives reproducible challenge selection for playtesting.
    const FString SeedOption = UGameplayStatics::ParseOption(OptionsString, TEXT("Seed"));
    if (!SeedOption.IsEmpty()) State->RunSeed = FCString::Atoi(*SeedOption);
    if (NextPlaytestSeed.IsSet()) { State->RunSeed=NextPlaytestSeed.GetValue(); NextPlaytestSeed.Reset(); }
    Random.Initialize(State->RunSeed); PreviousEvent = INDEX_NONE;
    if (!IsValid(RoguelikeDirector))
    {
        for (TActorIterator<AMCRoguelikeDirector> It(GetWorld());It;++It) { RoguelikeDirector=*It; break; }
        if (!RoguelikeDirector) RoguelikeDirector=GetWorld()->SpawnActor<AMCRoguelikeDirector>();
    }
    if (IsValid(RoguelikeDirector)) RoguelikeDirector->ResetRewards();
    for (TActorIterator<AMCRewardChest> It(GetWorld());It;++It) {
        if (It->ActorHasTag(TEXT("MC_DevMimic"))) It->Destroy();
        else if (It->bPlacedReward) It->ResetPlacedReward();
    }
    for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) {
        if (It->ActorHasTag(TEXT("MC_DevBoss"))) It->Destroy();
        else It->ResetForRun();
    }
    State->PhaseEndsAt = State->GetServerWorldTimeSeconds() + 8.;
    if (State->bTutorialActive)
    {
        State->Phase=EMCShiftPhase::Working;
        State->PhaseEndsAt=0;
        State->bPhysicalBrushes=false;
    }
    State->StepStartedAt=State->GetServerWorldTimeSeconds(); State->PreviousStepFailed=false;
    State->ForceNetUpdate();
}
void AMCGameMode::RestartShiftForPlaytest(int32 Seed)
{
    if (!HasAuthority()) return;
    if (const AMCPlaytestSession* Session=AMCPlaytestSession::Find(GetWorld()); Session && Session->IsActive()) return;
    NextPlaytestSeed=Seed;
    RestartShift();
}
void AMCGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AMCGameState* State = GetGameState<AMCGameState>();
    if (State && State->bLobbyWaiting)
    {
        int32 Loaded=0;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            if (const auto* PC=Cast<AMCPlayerController>(It->Get()); PC && PC->HasAcknowledgedGameplay() && PC->GetPawn()) ++Loaded;
        if (State->LobbyLoadedPlayers!=Loaded) { State->LobbyLoadedPlayers=Loaded; State->ForceNetUpdate(); }
        return;
    }
    if (State && State->bTutorialActive)
    {
        if (!IsValid(TutorialDirector))
        {
            TutorialDirector=GetWorld()->SpawnActor<AMCTutorialDirector>();
            if (TutorialDirector)
            {
                TutorialDirector->OnTutorialFinished.AddUObject(this,&AMCGameMode::FinishTutorial);
                TutorialDirector->Start(FirstDayPlan.LoadSynchronous());
            }
        }
        return;
    }
    if (!State || State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost) return;
    ProcessRespawns();
    if (State->bDayOneComplete) return;
    if (State->MouthHealth<=0 || (GetGameplayParticipantCount()>0 && !HasLivingPlayers() && State->AvailableArenaTeeth()==0))
    { State->Phase=EMCShiftPhase::Lost; State->ForceNetUpdate(); return; }
    if (State->Phase==EMCShiftPhase::Working && IsValid(DayDirector)) return;
    if (State->Phase==EMCShiftPhase::Working) UpdateObjectives();
    if (State->Phase==EMCShiftPhase::Intermission && State->Day>=State->RunSettings.DaysToSurvive)
    { if (GetGameplayParticipantCount()==0 || HasLivingPlayers()) { State->Phase=EMCShiftPhase::Won; State->ForceNetUpdate(); } return; }
    if (State->SecondsLeft() > 0.f) return;
    if (State->Phase == EMCShiftPhase::Intermission) StartDay();
    else if (State->Phase == EMCShiftPhase::Working) FinishDay(true);
}
void AMCGameMode::StartLobby(APlayerController* Requester)
{
    auto* State=GetGameState<AMCGameState>();
    if (!State || !State->bLobbyWaiting || !Requester || !Requester->IsLocalController() || GetNetMode()==NM_DedicatedServer) return;
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        const auto* PC=Cast<AMCPlayerController>(It->Get());
        if (!PC || !PC->HasAcknowledgedGameplay() || !PC->GetPawn()) return;
    }
    bLobbyRequested=false;
    State->bLobbyWaiting=false;
    State->PhaseEndsAt=State->GetServerWorldTimeSeconds();
    State->ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_LOBBY_STARTED players=%d"),GetNumPlayers());
}
void AMCGameMode::FinishTutorial()
{
    if (!HasAuthority() || !IsValid(TutorialDirector)) return;
    TutorialDirector->OnTutorialFinished.RemoveAll(this);
    TutorialDirector->Stop();
    bTutorialRequested=false;
    RestartShift();
    UE_LOG(LogTemp,Display,TEXT("MC_TUTORIAL_FINISHED_NORMAL_LOOP"));
}
void AMCGameMode::StartDay()
{
    AMCGameState* State = GetGameState<AMCGameState>();
    ClearTasks();
    ++State->Day;
    if (State->Day==1 && bUseDayOnePlan)
    {
        UMCDayPlan* Plan=FirstDayPlan.LoadSynchronous(); if (!Plan) Plan=NewObject<UMCDayPlan>(this);
        DayDirector=GetWorld()->SpawnActor<AMCDayDirector>(); DayDirector->Start(Plan); return;
    }
    int32 Choice = Random.RandRange(0, EventPool.Num()-1);
    if (EventPool.Num()>1 && Choice == PreviousEvent) Choice = (Choice + Random.RandRange(1, EventPool.Num()-1)) % EventPool.Num();
    PreviousEvent = Choice;
    UMCDayEvent* Event = EventPool[Choice];
    State->CurrentEvent = Event; State->Phase = EMCShiftPhase::Working;
    State->StepStartedAt=State->GetServerWorldTimeSeconds();
    State->PhaseEndsAt = State->GetServerWorldTimeSeconds() + FMath::Max(25.f, Event->Duration - (State->Day-1)*4.f);
    const int32 Count = FMath::Clamp(Event->BaseTaskCount + (State->Day-1)/2 + FMath::Max(0, GetGameplayParticipantCount()-1), 1, 16);
    Objectives.Empty();
    auto AddObjective=[&](AActor* Target) { FMCEventObjective O; O.Target=Target; O.Kind=Event->Kind; Objectives.Add(O); };
    if (Event->Kind==EMCTaskKind::Coffee)
    {
        for (AMCArenaTooth* Tooth:State->ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable()) { Tooth->SetCoffee(1); AddObjective(Tooth); }
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->Status->IsAlive()) { It->Status->ApplyCoffee(); AddObjective(*It); }
    }
    else if (Event->Kind==EMCTaskKind::LooseTooth)
    {
        TArray<AActor*> Targets;
        for (AMCArenaTooth* Tooth:State->ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable()) Targets.Add(Tooth);
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->Status->IsAlive()) Targets.Add(*It);
        for (int32 I=Targets.Num()-1;I>0;--I) Targets.Swap(I,Random.RandRange(0,I));
        for (int32 I=0;I<FMath::Min(Count,Targets.Num());++I)
        { Targets[I]->FindComponentByClass<UMCToothStatusComponent>()->Loosen(); AddObjective(Targets[I]); }
    }
    else
    {
        const auto* Profile=LoadObject<UMCFoodProfile>(nullptr,TEXT("/Game/Data/DA_FoodPhysics.DA_FoodPhysics"));
        FMCFoodSettings FoodSettings=Profile?Profile->Settings:FMCFoodSettings(); FoodSettings.Sanitize();
        for (int32 I=0;I<Count;++I)
        {
            const bool bJam=I%2==0; const float Side=I%4<2?1.f:-1.f;
            FTransform Transform(FRotator::ZeroRotator,FVector(0,0,FoodSettings.DropHeight+I*60));
            auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
            if (Food)
            {
                // Legacy food uses the prototype visual; BeginPlay derives its collision from this mesh.
                FVector Extent=Food->Body->GetScaledBoxExtent();
                if (const auto* Mesh=Food->Visual->GetStaticMesh().Get())
                    Extent=Mesh->GetBounds().BoxExtent*Food->Visual->GetRelativeScale3D().GetAbs()*Food->GetActorScale3D().GetAbs();
                const float Margin=Extent.Size2D()+20;
                FHitResult Floor; bool Found=false;
                for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
                    if (It->RandomGameplaySpawnPoint(Random,Margin,Margin*2+40,TConstArrayView<FVector>(),Floor)) { Found=true; break; }
                if (!Found)
                {
                    UE_LOG(LogTemp,Warning,TEXT("MC_FOOD_DROP no gameplay zone footprint for legacy food (radius %.1f)"),Margin);
                    Food->Destroy(); continue;
                }
                Transform.SetLocation(FVector(Floor.ImpactPoint.X,Floor.ImpactPoint.Y,FoodSettings.DropHeight+I*60));
                Food->Initialize(bJam,FVector(0,-Side,0));
                UGameplayStatics::FinishSpawningActor(Food,Transform); AddObjective(Food);
            }
        }
    }
    State->TasksTotal = Objectives.Num(); State->TasksLeft = State->TasksTotal;
    State->ForceNetUpdate();
}
void AMCGameMode::ResolveTask(AMCTaskActor* Task, AMCToothCharacter* Worker)
{
    AMCGameState* State = GetGameState<AMCGameState>();
    if (!State || State->Phase != EMCShiftPhase::Working || !ActiveTasks.Contains(Task)) return;
    ActiveTasks.Remove(Task); State->TasksLeft = ActiveTasks.Num();
    AwardTask(Worker,Task->Kind==EMCTaskKind::Coffee?EMCScoreTask::Coffee:Task->Kind==EMCTaskKind::Food?EMCScoreTask::Food:EMCScoreTask::Repair);
    State->MouthHealth = FMath::Min(State->RunSettings.MaxMouthHealth, State->MouthHealth + 1.f);
    State->ForceNetUpdate();
    if (ActiveTasks.IsEmpty()) FinishDay(false);
}
void AMCGameMode::FinishDay(bool bTimedOut)
{
    AMCGameState* State = GetGameState<AMCGameState>();
    if (!State || State->Phase != EMCShiftPhase::Working) return;
    if (bTimedOut) State->MouthHealth = FMath::Max(0.f, State->MouthHealth - State->TasksLeft * State->CurrentEvent->MissedTaskDamage);
    else NotifyObjectiveCompleted(FName(*FString::Printf(TEXT("LegacyDay_%d"),State->Day)));
    State->PreviousStepFailed=bTimedOut;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->Status->IsAlive()) It->NotifyTaskFeedback(!bTimedOut);
    // Unfinished coffee, damage and food persist into the following day.
    State->TasksLeft = 0;
    State->Phase = State->MouthHealth <= 0 ? EMCShiftPhase::Lost : State->Day >= State->RunSettings.DaysToSurvive && (GetGameplayParticipantCount()==0 || HasLivingPlayers()) ? EMCShiftPhase::Won : EMCShiftPhase::Intermission;
    State->PhaseEndsAt = State->GetServerWorldTimeSeconds() + 7.;
    State->StepStartedAt=State->GetServerWorldTimeSeconds();
    State->ForceNetUpdate();
}

void AMCGameMode::UpdateObjectives()
{
    if (IsValid(DayDirector)) return;
    auto* GS=GetGameState<AMCGameState>(); if (!GS || GS->Phase!=EMCShiftPhase::Working) return;
    for (auto& O:Objectives)
    {
        if (O.bCompleted) continue;
        bool bDone=false;
        if (O.Kind==EMCTaskKind::Food) {
            const auto* Food=Cast<AMCFoodActor>(O.Target);
            bDone=!IsValid(Food) || (Food->IsDisposed() && Food->IsHazardResolved() && (!Food->bAbsorbed || !IsValid(Food->AbsorbedUlcer) || Food->AbsorbedUlcer->IsHealed()));
        }
        else if (IsValid(O.Target))
        {
            auto* Status=O.Target->FindComponentByClass<UMCToothStatusComponent>();
            const auto* Arena=Cast<AMCArenaTooth>(O.Target);
            bDone=Status && Status->IsAlive() && (!Arena || Arena->IsAvailable()) && !Status->NeedsCare(O.Kind==EMCTaskKind::Coffee);
        }
        if (bDone) { O.bCompleted=true; GS->MouthHealth=FMath::Min(GS->RunSettings.MaxMouthHealth,GS->MouthHealth+1.f); }
    }
    GS->TasksLeft=0; for (const auto& O:Objectives) if (!O.bCompleted) ++GS->TasksLeft;
    GS->ForceNetUpdate();
    if (GS->TasksLeft==0) FinishDay(false);
}
bool AMCGameMode::HasLivingPlayers() const
{
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if (IsGameplayParticipant(It->GetController()) && IsValid(It->Status) && It->Status->IsAlive()) return true;
    return false;
}
bool AMCGameMode::IsGameplayParticipant(const AController* Controller)
{
    if (!IsValid(Controller) || Controller->IsActorBeingDestroyed()) return false;
    if (!Cast<APlayerController>(Controller) && !Cast<AMCPlaytestBotController>(Controller)) return false;
    const APlayerState* Identity=Controller->GetPlayerState<APlayerState>();
    return Identity && !Identity->IsSpectator() && !Identity->IsOnlyASpectator();
}
int32 AMCGameMode::GetGameplayParticipantCount() const
{
    int32 Count=0;
    for (TActorIterator<AController> It(GetWorld());It;++It) if (IsGameplayParticipant(*It)) ++Count;
    return Count;
}
void AMCGameMode::PlayerDied(AMCToothCharacter* Hero)
{
    auto* GS=GetGameState<AMCGameState>();
    if (!IsValid(Hero) || !Hero->GetController() || !GS || GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost) return;
    if (PendingRespawns.Contains(Hero)) return;
    if (AMCPlaytestSession* Session=AMCPlaytestSession::Find(GetWorld())) Session->RecordDeath(Hero);
    Hero->RespawnAt=GS->GetServerWorldTimeSeconds()+FMath::Max(.1f,RespawnDelay);
    PendingRespawns.Add(Hero); Hero->ForceNetUpdate();
}
void AMCGameMode::ProcessRespawns()
{
    auto* GS=GetGameState<AMCGameState>(); if (!GS) return;
    for (int32 I=0;I<PendingRespawns.Num();)
    {
        AMCToothCharacter* Old=PendingRespawns[I];
        if (!IsValid(Old) || !IsValid(Old->GetController())) { PendingRespawns.RemoveAt(I); continue; }
        if (GS->GetServerWorldTimeSeconds()<Old->RespawnAt) { ++I; continue; }
        AMCArenaTooth* Reserve=nullptr;
        for (AMCArenaTooth* Tooth:GS->ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable() && (!Reserve || Tooth->State.ToothId<Reserve->State.ToothId)) Reserve=Tooth;
        if (!Reserve) { ++I; continue; }
        const FMCToothStatus Inherited=Reserve->Status->State;
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
        AMCTongue* Tongue=nullptr; for(TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
        FVector Center=Tongue?Tongue->GetActorLocation():FVector::ZeroVector;
        const FVector Inward=(Center-Reserve->GetActorLocation()).GetSafeNormal2D();
        const FVector Extent=Reserve->Visual->Bounds.BoxExtent;
        const float Clearance=FMath::Abs(Inward.X)*Extent.X+FMath::Abs(Inward.Y)*Extent.Y+Old->GetCapsuleComponent()->GetScaledCapsuleRadius()+40;
        AMCToothCharacter* NewHero=nullptr;
        for(int32 Attempt=0;Attempt<8 && !NewHero;++Attempt)
        {
            FVector Location=Reserve->GetActorLocation()+Inward*(Clearance+Attempt*60);
            if(Tongue) {
                FHitResult Floor; if(!Tongue->SurfacePoint(Location,Floor) || Floor.ImpactNormal.Z<.4f) continue;
                Location=Floor.ImpactPoint+FVector(0,0,Old->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4);
            } else { Location.Y=FMath::Sign(Location.Y)*510; Location.Z=120; }
            NewHero=GetWorld()->SpawnActor<AMCToothCharacter>(Old->GetClass(),Location,Inward.Rotation(),Params);
        }
        if (!NewHero) { ++I; continue; }
        if (!Reserve->ConsumeForRespawn()) { NewHero->Destroy(); ++I; continue; }
        if (Old->Status->Settings.bInheritReserveStatus) NewHero->Status->Restore(Inherited);
        NewHero->RespawnSourceId=Reserve->State.ToothId;
        NewHero->Inventory->bWaterJetUnlocked=Old->Inventory->bWaterJetUnlocked;
        NewHero->Inventory->SprayReadyAt=Old->Inventory->SprayReadyAt;
        for (auto& O:Objectives) if (!O.bCompleted && (O.Target==Old || O.Target==Reserve)) O.Target=NewHero;
        AController* Controller=Old->GetController(); Controller->Possess(NewHero);
        Old->RespawnAt=0; Old->SetLifeSpan(3); NewHero->ForceNetUpdate();
        PendingRespawns.RemoveAt(I); GS->ForceNetUpdate();
    }
}
