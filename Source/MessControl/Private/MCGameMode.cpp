#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCArenaToothSocket.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "MCArenaDemo.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MCTaskActor.h"
#include "MCToothCharacter.h"
#include "MCPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AMCGameMode::AMCGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AMCToothCharacter::StaticClass();
    PlayerControllerClass = AMCPlayerController::StaticClass();
    GameStateClass = AMCGameState::StaticClass();
    TaskClass = AMCTaskActor::StaticClass();
    RunRulesProfile = TSoftObjectPtr<UMCRunRules>(FSoftObjectPath(TEXT("/Game/Data/DA_RunRules.DA_RunRules")));
    ArenaToothProfile = TSoftObjectPtr<UMCArenaToothProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_ArenaTooth.DA_ArenaTooth")));
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
#endif
}
void AMCGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    const AMCGameState* State = GetGameState<AMCGameState>();
    const int32 Limit = State ? State->RunSettings.MaxPlayers : FMCRunSettings().MaxPlayers;
    if (ErrorMessage.IsEmpty() && GetNumPlayers() >= Limit)
        ErrorMessage = FString::Printf(TEXT("This mouth is full (%d players)."), Limit);
}
void AMCGameMode::ClearTasks()
{
    for (AMCTaskActor* Task : ActiveTasks) if (IsValid(Task)) Task->Destroy();
    ActiveTasks.Empty();
}
void AMCGameMode::RestartShift()
{
    AMCGameState* State = GetGameState<AMCGameState>();
    if (!State) return;
    ClearTasks();
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
            Tooth->SetAppearance(Mesh,MeshTransform.GetScale3D());
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
    Random.Initialize(State->RunSeed); PreviousEvent = INDEX_NONE;
    State->PhaseEndsAt = State->GetServerWorldTimeSeconds() + 8.;
    State->ForceNetUpdate();
}
void AMCGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AMCGameState* State = GetGameState<AMCGameState>();
    if (!State || State->SecondsLeft() > 0.f) return;
    if (State->Phase == EMCShiftPhase::Intermission) StartDay();
    else if (State->Phase == EMCShiftPhase::Working) FinishDay(true);
}
void AMCGameMode::StartDay()
{
    AMCGameState* State = GetGameState<AMCGameState>();
    ClearTasks();
    ++State->Day;
    int32 Choice = Random.RandRange(0, EventPool.Num()-1);
    if (EventPool.Num()>1 && Choice == PreviousEvent) Choice = (Choice + Random.RandRange(1, EventPool.Num()-1)) % EventPool.Num();
    PreviousEvent = Choice;
    UMCDayEvent* Event = EventPool[Choice];
    State->CurrentEvent = Event; State->Phase = EMCShiftPhase::Working;
    State->PhaseEndsAt = State->GetServerWorldTimeSeconds() + FMath::Max(25.f, Event->Duration - (State->Day-1)*4.f);
    const int32 Count = FMath::Clamp(Event->BaseTaskCount + (State->Day-1)/2 + FMath::Max(0, GetNumPlayers()-1), 1, 16);
    TArray<FVector> Slots;
    for (int32 X=0; X<4; ++X) for (int32 Y=0; Y<4; ++Y) Slots.Add(FVector(-650+X*390, -570+Y*380, 40));
    for (int32 Index=Slots.Num()-1; Index>0; --Index) Slots.Swap(Index, Random.RandRange(0, Index));
    for (int32 Index=0; Index<Count; ++Index)
    {
        const FTransform SpawnTransform(FRotator::ZeroRotator, Slots[Index]);
        AMCTaskActor* Task = GetWorld()->SpawnActorDeferred<AMCTaskActor>(TaskClass ? TaskClass.Get() : AMCTaskActor::StaticClass(), SpawnTransform);
        if (Task) { Task->Initialize(Event); UGameplayStatics::FinishSpawningActor(Task, SpawnTransform); ActiveTasks.Add(Task); }
    }
    State->TasksTotal = ActiveTasks.Num(); State->TasksLeft = State->TasksTotal;
    State->ForceNetUpdate();
}
void AMCGameMode::ResolveTask(AMCTaskActor* Task)
{
    AMCGameState* State = GetGameState<AMCGameState>();
    if (!State || State->Phase != EMCShiftPhase::Working || !ActiveTasks.Contains(Task)) return;
    ActiveTasks.Remove(Task); State->TasksLeft = ActiveTasks.Num();
    State->MouthHealth = FMath::Min(State->RunSettings.MaxMouthHealth, State->MouthHealth + 1.f);
    State->ForceNetUpdate();
    if (ActiveTasks.IsEmpty()) FinishDay(false);
}
void AMCGameMode::FinishDay(bool bTimedOut)
{
    AMCGameState* State = GetGameState<AMCGameState>();
    if (!State || State->Phase != EMCShiftPhase::Working) return;
    if (bTimedOut) State->MouthHealth = FMath::Max(0.f, State->MouthHealth - State->TasksLeft * State->CurrentEvent->MissedTaskDamage);
    ClearTasks();
    State->TasksLeft = 0;
    State->Phase = State->MouthHealth <= 0 ? EMCShiftPhase::Lost : State->Day >= State->RunSettings.DaysToSurvive ? EMCShiftPhase::Won : EMCShiftPhase::Intermission;
    State->PhaseEndsAt = State->GetServerWorldTimeSeconds() + 7.;
    State->ForceNetUpdate();
}
