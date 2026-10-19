#include "MCHubGameState.h"
#include "MCPlayerState.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "MCHub"

namespace
{
const TCHAR* DestinationTablePath = TEXT("/Game/Gameplay/Hub/DT_HubDestinations.DT_HubDestinations");

bool IsParticipant(const APlayerState* Player)
{
    return IsValid(Player) && !Player->IsOnlyASpectator() && !Player->IsInactive() && !Player->IsABot();
}

FMCHubDestinationRow FallbackDestination(int32 Day)
{
    FMCHubDestinationRow Row;
    Row.Day = Day;
    Row.DisplayNameKey = FName(*FString::Printf(TEXT("UI.Hub.Destination.Day%02d.Name"), Day));
    Row.DescriptionKey = FName(*FString::Printf(TEXT("UI.Hub.Destination.Day%02d.Description"), Day));
    Row.DisplayName = FText::Format(LOCTEXT("DayTitle", "День {0}"), FText::AsNumber(Day));
    Row.Description = LOCTEXT("DestinationUnavailable", "Эта локация пока недоступна.");
    if (Day == 1)
    {
        Row.bEnabled = true;
        Row.Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_Mouth.L_Mouth")));
        Row.Description = LOCTEXT("FirstDestinationDescription", "Обычная смена во рту.");
    }
    return Row;
}

void BuildDestinations(const UDataTable* Table, TMap<int32, FMCHubDestinationRow>& Result)
{
    Result.Reset();
    for (int32 Day = 1; Day <= 7; ++Day) Result.Add(Day, FallbackDestination(Day));
    if (!Table) return;
    if (Table->GetRowStruct() != FMCHubDestinationRow::StaticStruct())
    {
        UE_LOG(LogTemp, Warning, TEXT("MC_HUB_DESTINATION_SCHEMA_MISMATCH table=%s"), *Table->GetPathName());
        return;
    }
    TArray<FName> Names = Table->GetRowNames();
    Names.Sort(FNameLexicalLess());
    TSet<int32> Seen;
    for (const FName Name : Names)
    {
        const auto* Row = Table->FindRow<FMCHubDestinationRow>(Name, TEXT("Hub destinations"), false);
        if (!Row || Row->Day < 1 || Row->Day > 7) continue;
        if (Seen.Contains(Row->Day))
        {
            Result.FindChecked(Row->Day).bEnabled = false;
            UE_LOG(LogTemp, Warning, TEXT("MC_HUB_DUPLICATE_DAY day=%d table=%s"), Row->Day, *Table->GetPathName());
            continue;
        }
        Seen.Add(Row->Day);
        Result.FindChecked(Row->Day) = *Row;
    }
}
}

bool FMCHubDestinationRow::HasValidMapPath() const
{
    const FString Package = Map.ToSoftObjectPath().GetLongPackageName();
    return Day >= 1 && Day <= 7 && !Map.IsNull() && Package.StartsWith(TEXT("/Game/"))
        && FPackageName::IsValidLongPackageName(Package)
        && !Package.Contains(TEXT("?")) && !Package.Contains(TEXT("#"));
}

AMCHubGameState::AMCHubGameState()
{
    DestinationTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(DestinationTablePath));
    SetNetUpdateFrequency(10.f);
}

void AMCHubGameState::BeginPlay()
{
    Super::BeginPlay();
    ReloadDestinations();
}

void AMCHubGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCHubGameState, Phase);
    DOREPLIFETIME(AMCHubGameState, SelectedDay);
    DOREPLIFETIME(AMCHubGameState, Players);
    DOREPLIFETIME(AMCHubGameState, Notice);
    DOREPLIFETIME(AMCHubGameState, DestinationTable);
}

void AMCHubGameState::ReloadDestinations()
{
    LoadedDestinationTable = DestinationTable.LoadSynchronous();
    BuildDestinations(LoadedDestinationTable, Destinations);
    bDefinitionsLoaded = true;
    OnHubStateChanged.Broadcast();
}

bool AMCHubGameState::ResolveAuthoredDestination(int32 Day, FMCHubDestinationRow& Destination)
{
    if (Day < 1 || Day > 7) { Destination = FMCHubDestinationRow(); return false; }
    TMap<int32, FMCHubDestinationRow> Definitions;
    BuildDestinations(LoadObject<UDataTable>(nullptr, DestinationTablePath), Definitions);
    Destination = Definitions.FindChecked(Day);
    return true;
}

const FMCHubDestinationRow* AMCHubGameState::FindDestination(int32 Day) const
{
    if (!bDefinitionsLoaded) const_cast<AMCHubGameState*>(this)->ReloadDestinations();
    return Destinations.Find(Day);
}

bool AMCHubGameState::GetDestination(int32 Day, FMCHubDestinationRow& Destination) const
{
    if (const auto* Row = FindDestination(Day)) { Destination = *Row; return true; }
    Destination = FMCHubDestinationRow();
    Destination.bEnabled = false;
    return false;
}

TArray<FMCHubDestinationRow> AMCHubGameState::GetDestinations() const
{
    TArray<FMCHubDestinationRow> Result;
    for (int32 Day = 1; Day <= 7; ++Day) if (const auto* Row = FindDestination(Day)) Result.Add(*Row);
    return Result;
}

int32 AMCHubGameState::GetParticipantCount() const
{
    int32 Count = 0;
    for (const auto& Entry : Players) Count += IsParticipant(Entry.Player) ? 1 : 0;
    return Count;
}

int32 AMCHubGameState::GetReadyCount() const
{
    int32 Count = 0;
    for (const auto& Entry : Players) Count += IsParticipant(Entry.Player) && Entry.bReady && Entry.bLoaded ? 1 : 0;
    return Count;
}

bool AMCHubGameState::AreAllReady() const
{
    const int32 Count = GetParticipantCount();
    return Count > 0 && GetReadyCount() == Count;
}

bool AMCHubGameState::AreAllLoaded() const
{
    bool bAny = false;
    for (const auto& Entry : Players)
    {
        if (!IsParticipant(Entry.Player)) continue;
        bAny = true;
        if (!Entry.bLoaded) return false;
    }
    return bAny;
}

bool AMCHubGameState::IsHost(const APlayerController* Player) const
{
    const auto* State = IsValid(Player) ? Player->GetPlayerState<AMCPlayerState>() : nullptr;
    return HasAuthority() && GetNetMode() != NM_DedicatedServer && Player && Player->GetWorld() == GetWorld()
        && Player->IsLocalController() && IsParticipant(State) && State->bSessionHost;
}

bool AMCHubGameState::HasHost() const
{
    if (!HasAuthority() || !GetWorld()) return false;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (IsHost(It->Get())) return true;
    return false;
}

void AMCHubGameState::NotifyChanged()
{
    ForceNetUpdate();
    OnHubStateChanged.Broadcast();
}

void AMCHubGameState::SetPhase(EMCHubPhase NewPhase)
{
    if (!HasAuthority() || Phase == NewPhase) return;
    Phase = NewPhase;
    NotifyChanged();
}

void AMCHubGameState::SetSelectedDay(int32 Day)
{
    if (!HasAuthority() || Day < 1 || Day > 7 || SelectedDay == Day) return;
    SelectedDay = Day;
    NotifyChanged();
}

void AMCHubGameState::SetReadiness(const TArray<FMCHubPlayerReadiness>& NewPlayers)
{
    if (!HasAuthority() || Players == NewPlayers) return;
    Players = NewPlayers;
    NotifyChanged();
}

void AMCHubGameState::SetNotice(const FText& NewNotice)
{
    if (!HasAuthority() || Notice.EqualTo(NewNotice)) return;
    Notice = NewNotice;
    NotifyChanged();
}

void AMCHubGameState::OnRep_HubState() { OnHubStateChanged.Broadcast(); }
void AMCHubGameState::OnRep_DestinationTable() { ReloadDestinations(); }

#undef LOCTEXT_NAMESPACE
