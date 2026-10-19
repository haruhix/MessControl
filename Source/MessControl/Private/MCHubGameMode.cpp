#include "MCHubGameMode.h"
#include "MCHubGameState.h"
#include "MCHubReadyZone.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MCHub"

namespace
{
FString ExistingMapPackage(const TSoftObjectPtr<UWorld>& Map)
{
    FMCHubDestinationRow Probe;
    Probe.Map = Map;
    const FString Package = Map.ToSoftObjectPath().GetLongPackageName();
    return Probe.HasValidMapPath() && FPackageName::DoesPackageExist(Package) ? Package : FString();
}
}

AMCHubGameMode::AMCHubGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .2f;
    bUseSeamlessTravel = true;
    DefaultPawnClass = AMCToothCharacter::StaticClass();
    static ConstructorHelpers::FClassFinder<AMCToothCharacter> PlayerBlueprint(TEXT("/Game/Blueprints/BP_PlayerCharacter"));
    if (PlayerBlueprint.Succeeded()) DefaultPawnClass = PlayerBlueprint.Class;
    PlayerControllerClass = AMCPlayerController::StaticClass();
    PlayerStateClass = AMCPlayerState::StaticClass();
    GameStateClass = AMCHubGameState::StaticClass();
    HubMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/Hub.Hub")));
    SelectionMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_HubSelection.L_HubSelection")));
}

void AMCHubGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    bSelectionScene = UGameplayStatics::GetIntOption(Options, TEXT("MCHubSelection"), 0) != 0
        || FPackageName::GetShortName(MapName).EndsWith(TEXT("L_HubSelection"));
    InitialSelectedDay = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("MCHubDay"), 1), 1, 7);
    if (bSelectionScene) DefaultPawnClass = nullptr;
}

void AMCHubGameMode::InitGameState()
{
    Super::InitGameState();
    if (auto* State = GetGameState<AMCHubGameState>())
    {
        State->SetPhase(bSelectionScene ? EMCHubPhase::Selecting : EMCHubPhase::Gathering);
        State->SetSelectedDay(InitialSelectedDay);
    }
}

void AMCHubGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (auto* State = GetGameState<AMCHubGameState>())
    {
        State->SetPhase(bSelectionScene ? EMCHubPhase::Selecting : EMCHubPhase::Gathering);
        State->SetSelectedDay(InitialSelectedDay);
    }
    RemoveOrphanedStartupPawns();
    RefreshReadiness();
}

bool AMCHubGameMode::IsParticipant(const APlayerController* Player) const
{
    const auto* State = IsValid(Player) ? Player->GetPlayerState<AMCPlayerState>() : nullptr;
    return Player && Player->GetWorld() == GetWorld() && IsValid(State)
        && !State->IsOnlyASpectator() && !State->IsInactive() && !State->IsABot();
}

bool AMCHubGameMode::IsHostRequest(const APlayerController* Player) const
{
    const auto* State = GetGameState<AMCHubGameState>();
    return HasAuthority() && !bTravelPending && IsParticipant(Player) && State && State->IsHost(Player);
}

void AMCHubGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (!ErrorMessage.IsEmpty()) return;
    if (bTravelPending) ErrorMessage = LOCTEXT("TravelInProgress", "Команда уже переходит в другую локацию. Подключись после загрузки.").ToString();
    else if (GetNumPlayers() >= 4) ErrorMessage = LOCTEXT("HubFull", "В комнате уже четыре игрока.").ToString();
}

void AMCHubGameMode::InitializeHubPlayer(APlayerController* Player, bool bPreserveAppearance)
{
    if (!IsParticipant(Player)) return;
    LoadedPlayers.Remove(Player);
    if (auto* State = Player->GetPlayerState<AMCPlayerState>())
    {
        State->bSessionHost = Player->IsLocalController() && GetNetMode() != NM_DedicatedServer;
        if (!bPreserveAppearance)
        {
            static const FLinearColor Palette[] = {FLinearColor(.24f,.65f,1.f), FLinearColor(1.f,.35f,.25f),
                FLinearColor(.4f,.9f,.3f), FLinearColor(.8f,.4f,1.f)};
            State->PlayerColor = Palette[FMath::Max(0,GetNumPlayers()-1) % UE_ARRAY_COUNT(Palette)];
        }
        State->ForceNetUpdate();
        if (auto* Hero = Cast<AMCToothCharacter>(Player->GetPawn())) Hero->ApplyPlayerColor(State->PlayerColor);
    }
}

void AMCHubGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    InitializeHubPlayer(NewPlayer, false);
    RefreshReadiness();
}

void AMCHubGameMode::HandleSeamlessTravelPlayer(AController*& Controller)
{
    Super::HandleSeamlessTravelPlayer(Controller);
    InitializeHubPlayer(Cast<APlayerController>(Controller), true);
    RefreshReadiness();
}

void AMCHubGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    if (!bSelectionScene) Super::HandleStartingNewPlayer_Implementation(NewPlayer);
    else if (NewPlayer && NewPlayer->GetPawn())
    {
        APawn* Pawn = NewPlayer->GetPawn();
        NewPlayer->UnPossess();
        Pawn->Destroy();
    }
}

bool AMCHubGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
    return !bSelectionScene && !bTravelPending && Super::PlayerCanRestart_Implementation(Player);
}

void AMCHubGameMode::RestartPlayer(AController* Player)
{
    if (!bSelectionScene && !bTravelPending) Super::RestartPlayer(Player);
}

APawn* AMCHubGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* Player, const FTransform& SpawnTransform)
{
    if (bSelectionScene || bTravelPending) return nullptr;
    FTransform Safe = SpawnTransform;
    Safe.AddToTranslation(FVector(0, 0, 35.f));
    APawn* Pawn = Super::SpawnDefaultPawnAtTransform_Implementation(Player, Safe);
    if (Pawn) Pawn->Tags.AddUnique(TEXT("MC.Hub.PlayerPawn"));
    if (auto* Hero = Cast<AMCToothCharacter>(Pawn))
        if (const auto* State = Player ? Player->GetPlayerState<AMCPlayerState>() : nullptr) Hero->ApplyPlayerColor(State->PlayerColor);
    return Pawn;
}

void AMCHubGameMode::Logout(AController* Exiting)
{
    if (auto* Player = Cast<APlayerController>(Exiting))
    {
        LoadedPlayers.Remove(Player);
        LastSelectionAt.Remove(Player);
        RecoveryAt.Remove(Player);
        ForcedRecovery.Remove(Player);
    }
    Super::Logout(Exiting);
    RefreshReadiness();
}

void AMCHubGameMode::AcknowledgeSceneLoaded(APlayerController* Player)
{
    if (!HasAuthority() || bTravelPending || !IsParticipant(Player)) return;
    if (!Player->IsLocalController() && !Player->HasClientLoadedCurrentWorld()) return;
    if (!bSelectionScene && !Player->GetPawn()) return;
    LoadedPlayers.Add(Player);
    RefreshReadiness();
}

bool AMCHubGameMode::RecoverPlayer(AMCToothCharacter* Hero)
{
    auto* Player = IsValid(Hero) ? Cast<APlayerController>(Hero->GetController()) : nullptr;
    if (!HasAuthority() || bSelectionScene || bTravelPending || !IsParticipant(Player) || Player->GetPawn() != Hero) return false;
    ForcedRecovery.Add(Player);
    RecoveryAt.Add(Player, GetWorld()->GetTimeSeconds());
    return true;
}

void AMCHubGameMode::RefreshReadiness()
{
    auto* State = GetGameState<AMCHubGameState>();
    if (!HasAuthority() || !State || bTravelPending) return;
    TArray<FMCHubPlayerReadiness> Snapshot;
    TSet<TWeakObjectPtr<APlayerController>> Connected;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Player = It->Get();
        if (!IsParticipant(Player)) continue;
        Connected.Add(Player);
        FMCHubPlayerReadiness Entry;
        Entry.Player = Player->PlayerState;
        Entry.bLoaded = LoadedPlayers.Contains(Player);
        if (bSelectionScene) Entry.bReady = true;
        else if (const auto* Hero = Cast<AMCToothCharacter>(Player->GetPawn()); Hero && Hero->Status && Hero->Status->IsAlive())
        {
            for (TActorIterator<AMCHubReadyZone> Zone(GetWorld()); Zone; ++Zone)
                if (Zone->bEnabled && Zone->ContainsPawn(Hero)) { Entry.bReady = true; break; }
        }
        Snapshot.Add(Entry);
    }
    // Stable UI ordering across a disconnect/reconnect; no stale overlap leases are stored.
    Snapshot.Sort([](const auto& A, const auto& B) { return A.Player->GetPlayerId() < B.Player->GetPlayerId(); });
    for (auto It = LoadedPlayers.CreateIterator(); It; ++It) if (!Connected.Contains(*It)) It.RemoveCurrent();
    State->SetReadiness(Snapshot);
}

void AMCHubGameMode::RecoverPlayers()
{
    if (bSelectionScene || bTravelPending) return;
    float Floor = RecoveryFloorZ;
    bool bFoundStart = false;
    for (TActorIterator<APlayerStart> Start(GetWorld()); Start; ++Start)
    {
        const float Candidate = Start->GetActorLocation().Z - 1800.f;
        Floor = bFoundStart ? FMath::Min(Floor, Candidate) : Candidate;
        bFoundStart = true;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Player = It->Get();
        if (!IsParticipant(Player)) continue;
        auto* Hero = Cast<AMCToothCharacter>(Player->GetPawn());
        const bool bRecover = ForcedRecovery.Contains(Player) || !Hero || !Hero->Status || !Hero->Status->IsAlive()
            || !FMath::IsFinite(Hero->GetActorLocation().Z) || Hero->GetActorLocation().Z < Floor;
        if (!bRecover) { RecoveryAt.Remove(Player); continue; }
        double* Deadline = RecoveryAt.Find(Player);
        if (!Deadline) { RecoveryAt.Add(Player, Now + .6); continue; }
        if (Now < *Deadline) continue;
        *Deadline = Now + 1.;
        if (APawn* Previous = Player->GetPawn()) { Player->UnPossess(); Previous->Destroy(); }
        RestartPlayer(Player);
        ForcedRecovery.Remove(Player);
    }
}

void AMCHubGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || bTravelPending) return;
    RemoveOrphanedStartupPawns();
    RecoverPlayers();
    RefreshReadiness();
}

void AMCHubGameMode::RemoveOrphanedStartupPawns()
{
    for (TActorIterator<AMCToothCharacter> Hero(GetWorld()); Hero; ++Hero)
    {
        const bool bStartupPawn = Hero->IsNetStartupActor();
        AController* Controller = Hero->GetController();
        if ((Controller && Controller->IsPlayerController())
            || (!bStartupPawn && (!Hero->ActorHasTag(TEXT("MC.Hub.PlayerPawn"))
                || Hero->GetGameTimeSinceCreation() < 1.f))) continue;
        bool bOwnedByPlayer = false;
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (It->Get() && It->Get()->GetPawn() == *Hero) { bOwnedByPlayer = true; break; }
        if (!bOwnedByPlayer)
        {
            // Authored preview pawns can auto-create AI controllers. They are
            // not lobby participants and must not become runtime duplicates.
            if (Controller)
            {
                Controller->UnPossess();
                Controller->Destroy();
            }
            Hero->Destroy();
        }
    }
}

bool AMCHubGameMode::TravelTo(const FString& URL, EMCHubPhase RestorePhase)
{
    auto* State = GetGameState<AMCHubGameState>();
    if (!HasAuthority() || bTravelPending || !State || !GetWorld()) return false;
    bTravelPending = true;
    State->SetNotice(FText::GetEmpty());
    State->SetPhase(EMCHubPhase::Travelling);
    if (GetWorld()->ServerTravel(URL, true))
    {
        UE_LOG(LogTemp, Display, TEXT("MC_HUB_TRAVEL destination=%s participants=%d"), *URL, State->GetParticipantCount());
        return true;
    }
    bTravelPending = false;
    State->SetPhase(RestorePhase);
    State->SetNotice(LOCTEXT("TravelFailed", "Не удалось открыть локацию. Попробуй ещё раз."));
    return false;
}

bool AMCHubGameMode::RequestOpenSelection(APlayerController* Requester)
{
    auto* State = GetGameState<AMCHubGameState>();
    if (!IsHostRequest(Requester) || !State || State->Phase != EMCHubPhase::Gathering) return false;
    RefreshReadiness();
    if (!State->AreAllReady() || !State->AreAllLoaded())
    {
        State->SetNotice(LOCTEXT("NeedAllReady", "Вся команда должна загрузиться и встать в зону готовности."));
        return false;
    }
    const FString Package = ExistingMapPackage(SelectionMap);
    if (Package.IsEmpty()) { State->SetNotice(LOCTEXT("SelectionMapMissing", "Локация выбора пока недоступна.")); return false; }
    return TravelTo(FString::Printf(TEXT("%s?game=/Script/MessControl.MCHubGameMode?MCHubSelection=1?MCHubDay=%d"),
        *Package, State->SelectedDay), EMCHubPhase::Gathering);
}

bool AMCHubGameMode::RequestSelectDay(APlayerController* Requester, int32 Day)
{
    auto* State = GetGameState<AMCHubGameState>();
    if (!IsHostRequest(Requester) || !State || State->Phase != EMCHubPhase::Selecting || !State->FindDestination(Day)) return false;
    const double Now = GetWorld()->GetTimeSeconds();
    if (const auto* Previous = LastSelectionAt.Find(Requester); Previous && Now - *Previous < .06) return false;
    LastSelectionAt.Add(Requester, Now);
    State->SetSelectedDay(Day);
    State->SetNotice(FText::GetEmpty());
    return true;
}

bool AMCHubGameMode::RequestLaunch(APlayerController* Requester)
{
    auto* State = GetGameState<AMCHubGameState>();
    if (!IsHostRequest(Requester) || !State || State->Phase != EMCHubPhase::Selecting) return false;
    RefreshReadiness();
    if (!State->AreAllLoaded()) { State->SetNotice(LOCTEXT("WaitForSelectionLoad", "Ждём загрузки всей команды.")); return false; }
    const auto* Destination = State->FindDestination(State->SelectedDay);
    if (!Destination || !Destination->bEnabled || !Destination->HasValidMapPath())
    {
        State->SetNotice(LOCTEXT("DestinationUnavailable", "Эта локация пока недоступна."));
        return false;
    }
    const FString Package = ExistingMapPackage(Destination->Map);
    if (Package.IsEmpty() || Package == HubMap.ToSoftObjectPath().GetLongPackageName()
        || Package == SelectionMap.ToSoftObjectPath().GetLongPackageName())
    {
        State->SetNotice(LOCTEXT("DestinationMapMissing", "Игровая локация не найдена."));
        return false;
    }
    // The normal level owns its own ACK barrier. MCHubRun differentiates this
    // already approved team from the older manual Start / Play lobby flow.
    FString URL = FString::Printf(TEXT("%s?MCLobby=1?MCHubRun=1?MCHubDay=%d"), *Package, Destination->Day);
    if (Package == TEXT("/Game/Maps/L_Mouth")) URL += TEXT("?game=/Game/Blueprints/BP_MouthGameMode.BP_MouthGameMode_C");
    return TravelTo(URL, EMCHubPhase::Selecting);
}

bool AMCHubGameMode::RequestReturnToHub(APlayerController* Requester)
{
    auto* State = GetGameState<AMCHubGameState>();
    if (!IsHostRequest(Requester) || !State || State->Phase != EMCHubPhase::Selecting) return false;
    RefreshReadiness();
    // Back is also the recovery route for a failed selector UI. Require the
    // actual network world to be ready, not successful cosmetic/UI ACKs.
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Player = It->Get();
        if (IsParticipant(Player) && !Player->IsLocalController() && !Player->HasClientLoadedCurrentWorld())
        {
            State->SetNotice(LOCTEXT("WaitForNetworkLoad", "Ждём завершения загрузки игроков перед возвращением в хаб."));
            return false;
        }
    }
    const FString Package = ExistingMapPackage(HubMap);
    if (Package.IsEmpty()) { State->SetNotice(LOCTEXT("HubMapMissing", "Хаб не найден.")); return false; }
    return TravelTo(FString::Printf(TEXT("%s?game=/Script/MessControl.MCHubGameMode?MCHubDay=%d"),
        *Package, State->SelectedDay), EMCHubPhase::Selecting);
}

#undef LOCTEXT_NAMESPACE
