#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Read-only diagnostics for isolated network-fixture processes. Logging never
// acknowledges loading, changes input state or bypasses the readiness barrier.
void MCTickNetworkValidationTelemetry(UWorld* World)
{
    if(!World || !FParse::Param(FCommandLine::Get(),TEXT("MCNetTelemetry"))) return;
    auto* PC=Cast<AMCPlayerController>(World->GetFirstPlayerController());
    if(!PC || !PC->IsLocalController()) return;
    static TWeakObjectPtr<UWorld> LastWorld;
    static double NextSample=0;
    if(LastWorld!=World) {LastWorld=World;NextSample=0;}
    const double RealNow=World->GetRealTimeSeconds();
    if(RealNow<NextSample) return;
    NextSample=RealNow+.5;
    const auto* Player=PC->GetPlayerState<APlayerState>();
    const auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn());
    const float Ping=Player?Player->GetPingInMilliseconds():0.f;
    const bool MoveIgnored=PC->IsMoveInputIgnored(),ArrivalHeld=Hero && Hero->IsArenaArrivalHeld();
    UE_LOG(LogTemp,Display,TEXT("MC_NET_TELEMETRY net=%d world_t=%.3f dt_ms=%.3f ping_ms=%.1f ready=%d ack=%d move_blocked=%d move_ignored=%d look_ignored=%d arrival_held=%d has_pawn=%d"),
        int32(World->GetNetMode()),World->GetTimeSeconds(),World->GetDeltaSeconds()*1000.f,
        FMath::IsFinite(Ping)?Ping:0.f,PC->IsWorldPresentationReady(),PC->HasAcknowledgedGameplay(),
        !Hero || MoveIgnored || ArrivalHeld,MoveIgnored,PC->IsLookInputIgnored(),ArrivalHeld,Hero!=nullptr);
}
#endif
