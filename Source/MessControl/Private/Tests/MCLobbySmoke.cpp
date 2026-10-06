#include "MCLobbySmoke.h"

#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCDayDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool UMCLobbySmoke::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
    return FParse::Param(FCommandLine::Get(),TEXT("MCLobbySmoke")) && Super::ShouldCreateSubsystem(Outer);
#else
    return false;
#endif
}

void UMCLobbySmoke::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    StartedAt=FPlatformTime::Seconds();
}

void UMCLobbySmoke::Fail(const TCHAR* Reason)
{
    bFinished=true;
    UE_LOG(LogTemp,Error,TEXT("MC_LOBBY_FAIL mode=%d reason=%s"),static_cast<int32>(GetWorld()->GetNetMode()),Reason);
    FPlatformMisc::RequestExitWithStatus(false,1);
}

void UMCLobbySmoke::Tick(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
    const double Now=FPlatformTime::Seconds();
    if (Now-StartedAt>90.) { Fail(TEXT("90 second timeout waiting for four-player lobby/day-one/pause validation")); return; }
    UWorld* World=GetWorld();
    auto* State=World->GetGameState<AMCGameState>();
    AMCPlayerController* Local=nullptr;
    for (FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
        if (auto* PC=Cast<AMCPlayerController>(It->Get()); PC && PC->IsLocalController()) { Local=PC; break; }
    if (!State || !Local || !Local->GetPawn()) return;
    const bool bHost=World->GetNetMode()==NM_ListenServer;
    auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    if (State->bTutorialActive) { Fail(TEXT("ordinary lobby accidentally entered tutorial")); return; }

    if (State->bLobbyWaiting)
    {
        if (!bSawWaiting) { bSawWaiting=true; WaitingSeenAt=Now; }
        if (State->Day!=0 || State->DayPlan || State->StepIndex!=INDEX_NONE || State->Phase!=EMCShiftPhase::Intermission)
        { Fail(TEXT("gameplay advanced while lobby was waiting")); return; }
        if (bHost && (!Mode || IsValid(Mode->DayDirector) || TActorIterator<AMCDayDirector>(World)))
        { Fail(TEXT("a native day director started before the host pressed Start")); return; }
        // Give the controller its first front-end refresh before asserting the input mode.
        if (Now-WaitingSeenAt>.5 && (!Local->ShouldShowMouseCursor() || !Local->IsMoveInputIgnored() || !Local->IsLookInputIgnored()))
        { Fail(TEXT("lobby input escaped into gameplay")); return; }
        if (State->PlayerArray.Num()==4 && State->LobbyLoadedPlayers==4) bSawFourLoaded=true;

        if (bHost)
        {
            AMCPlayerController* Remote=nullptr;
            bool bAllAcknowledged=State->PlayerArray.Num()==4;
            bool bHasUnloadedConnectedPlayer=false;
            int32 ControllerCount=0;
            for (FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            {
                auto* PC=Cast<AMCPlayerController>(It->Get());
                ++ControllerCount;
                const bool bLoaded=PC && PC->GetPawn() && PC->HasAcknowledgedGameplay();
                bAllAcknowledged&=bLoaded;
                bHasUnloadedConnectedPlayer|=!bLoaded;
                if (PC && !PC->IsLocalController()) Remote=PC;
            }
            bAllAcknowledged&=ControllerCount==4;
            if (bHasUnloadedConnectedPlayer && !bCheckedIncompleteGuard)
            {
                Mode->StartLobby(Local);
                if (!State->bLobbyWaiting) { Fail(TEXT("host started while a connected player's gameplay was not acknowledged")); return; }
                bCheckedIncompleteGuard=true;
            }
            if (bAllAcknowledged && bSawFourLoaded)
            {
                if (AllLoadedAt==0) AllLoadedAt=Now;
                // Observe beyond both five seconds of full readiness and the old intermission deadline.
                // This catches a lobby that merely delays the normal day-one timer instead of suspending it.
                if (Now-AllLoadedAt>=5. && State->GetServerWorldTimeSeconds()>State->PhaseEndsAt+.5)
                {
                    if (!Remote) { Fail(TEXT("four-player lobby has no remote requester")); return; }
                    Mode->StartLobby(Remote);
                    if (!State->bLobbyWaiting || State->Day!=0) { Fail(TEXT("a remote requester was allowed to start the host's lobby")); return; }
                    bRejectedRemote=true;
                    Mode->StartLobby(Local);
                    if (State->bLobbyWaiting) { Fail(TEXT("loaded listen host could not start its lobby")); return; }
                    bStarted=true;
                    UE_LOG(LogTemp,Display,TEXT("MC_LOBBY_START_GUARDS_PASS players=%d loaded=%d early_guard=%d"),ControllerCount,State->LobbyLoadedPlayers,bCheckedIncompleteGuard);
                }
            }
            else AllLoadedAt=0;
        }
        return;
    }

    if (!bSawWaiting) { Fail(TEXT("peer never observed the waiting lobby")); return; }
    if (State->Day==0) return; // Start and the following normal game-mode tick are distinct.
    if (State->Day!=1 || State->Phase!=EMCShiftPhase::Working || !State->DayPlan || State->StepIndex<0 || !State->bPhysicalBrushes)
    { Fail(TEXT("normal day-one plan did not resume after lobby Start")); return; }
    if (State->DayStartedAt>State->StepStartedAt+.1 || State->DayStartedAt>State->GetServerWorldTimeSeconds()+.1)
    { Fail(TEXT("day-one clocks are inconsistent after lobby Start")); return; }
    if (bHost && (!bStarted || !bRejectedRemote || !Mode || !IsValid(Mode->DayDirector) || !Mode->DayDirector->Settings))
    { Fail(TEXT("host did not start the real native day director through guarded lobby Start")); return; }
    bVerifiedDayOne=true;
    if (!bSawFourLoaded) { Fail(TEXT("peer did not receive four-player loaded lobby state before Start")); return; }

    if (!bVerifiedPause)
    {
        if (!bPauseOpen)
        {
            // The replicated lobby flag reaches clients before their next local UI refresh.
            if (Local->ShouldShowMouseCursor() || Local->IsMoveInputIgnored() || Local->IsLookInputIgnored()) return;
            Local->TogglePauseMenu();
            if (!Local->ShouldShowMouseCursor() || !Local->IsMoveInputIgnored() || !Local->IsLookInputIgnored())
            { Fail(TEXT("in-game menu did not isolate gameplay input")); return; }
#if UE_ENABLE_DEBUG_DRAWING
            if (Local->GetCurrentInputModeDebugString()!=FInputModeUIOnly().GetDebugDisplayName())
            { Fail(TEXT("in-game menu is not UI-only")); return; }
#endif
            if (World->IsPaused()) { Fail(TEXT("local menu paused the shared network game")); return; }
            PauseOpenedAt=Now; bPauseOpen=true;
        }
        else if (Now-PauseOpenedAt>.5)
        {
            Local->TogglePauseMenu();
            if (Local->ShouldShowMouseCursor() || Local->IsMoveInputIgnored() || Local->IsLookInputIgnored())
            { Fail(TEXT("Resume did not restore gameplay input")); return; }
#if UE_ENABLE_DEBUG_DRAWING
            if (Local->GetCurrentInputModeDebugString()!=FInputModeGameOnly().GetDebugDisplayName())
            { Fail(TEXT("Resume did not restore game-only input")); return; }
#endif
            bPauseOpen=false; bVerifiedPause=true; PassedAt=Now;
            UE_LOG(LogTemp,Display,TEXT("MC_LOBBY_PASS mode=%d players=%d day=%d native_director=%d pause_resume=1"),
                static_cast<int32>(World->GetNetMode()),State->PlayerArray.Num(),State->Day,bHost);
        }
    }
    // Keep the host alive while clients observe replication and finish their local menu checks.
    if (bVerifiedDayOne && bVerifiedPause && Now-PassedAt>(bHost?10.:2.))
    {
        bFinished=true;
        FPlatformMisc::RequestExitWithStatus(false,0);
    }
#endif
}
