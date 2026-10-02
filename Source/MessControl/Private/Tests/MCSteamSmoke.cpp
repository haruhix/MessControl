#if !UE_BUILD_SHIPPING
#include "MCSteamSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Uses the actual Steam backend on one logged-in account, never a mock session.
void MCTickSteamValidation(UWorld* World)
{
    static double Started=FPlatformTime::Seconds();
    static bool Requested=false;
    static double ReadyAt=0;
    auto* GI=World->GetGameInstance();
    auto* Steam=GI?GI->GetSubsystem<UMCSteamSessionSubsystem>():nullptr;
    const double Age=FPlatformTime::Seconds()-Started;
    const bool Find=FParse::Param(FCommandLine::Get(),TEXT("MCSteamFindTest"));
    if (!Requested && Steam && World->GetFirstPlayerController() && Age>2)
    {
        Requested=true;
        if (!Steam->CanUseSteam())
        {
            UE_LOG(LogTemp,Error,TEXT("MC_STEAM_TEST_FAIL Steam unavailable"));
            FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        if (Find) Steam->FindRooms(); else Steam->HostRoom();
    }
    if (Requested && Steam && !Steam->IsBusy())
    {
        if (Steam->HadFailure())
        {
            UE_LOG(LogTemp,Error,TEXT("MC_STEAM_TEST_FAIL %s"),*Steam->GetStatus());
            FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        UNetDriver* Driver=World->GetNetDriver();
        const bool HostReady=Steam->HasRoom() && World->GetNetMode()==NM_ListenServer && Driver &&
            Driver->GetClass()->GetName()==TEXT("SteamSocketsNetDriver");
        if ((Find && Steam->GetSearchRevision()>=2) || (!Find && HostReady))
        {
            if (ReadyAt==0) ReadyAt=FPlatformTime::Seconds();
            if (FPlatformTime::Seconds()-ReadyAt>3)
            {
                UE_LOG(LogTemp,Display,TEXT("MC_STEAM_TEST_PASS mode=%s driver=%s"),Find?TEXT("find"):TEXT("host"),Driver?*Driver->GetClass()->GetName():TEXT("none"));
                FPlatformMisc::RequestExitWithStatus(false,0); return;
            }
        }
    }
    if (Age>75)
    {
        UE_LOG(LogTemp,Error,TEXT("MC_STEAM_TEST_FAIL timeout"));
        FPlatformMisc::RequestExitWithStatus(false,1);
    }
}
#endif
