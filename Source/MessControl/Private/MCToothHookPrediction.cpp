#include "MCToothMovementComponent.h"
#include "MCToothCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCHookPrediction,Log,All);

#if !UE_BUILD_SHIPPING
void UMCToothMovementComponent::BeginHookReplayDiagnostic(const FMCHookPullReceipt& R,float Ack,
    const FNetworkPredictionData_Client_Character* Data,int32 MoveIndex)
{
    static const bool Enabled=FParse::Param(FCommandLine::Get(),TEXT("MCNetTelemetry"));
    if(!Enabled || !Data || !UpdatedComponent || (HookReplayDiagnostic.Action==R.ActionID && HookReplayDiagnostic.Epoch==R.Epoch)) return;
    auto& D=HookReplayDiagnostic;D={};
    D.Action=R.ActionID;D.Epoch=R.Epoch;D.Revision=R.Revision;D.LocalID=ExternalLocalSourceID;
    D.Ack=Ack;D.CurrentMoveTime=Data->CurrentTimeStamp;D.SourceTime=R.SourceTime;
    // AckMove discards the acknowledged move and every earlier move. These
    // later deltas are the actual input history the native replay will visit.
    for(int32 I=MoveIndex+1;I<Data->SavedMoves.Num();++I) {
        const auto& Move=Data->SavedMoves[I];
        if(D.SavedCount==0) D.FirstMove=Move->TimeStamp;
        D.LastMove=Move->TimeStamp;D.SavedDelta+=Move->DeltaTime;++D.SavedCount;
    }
    D.BeforeResponse=UpdatedComponent->GetComponentLocation();D.bAwaitingReplay=true;
    LogHookReplayDiagnostic(TEXT("beforeResponse"),D.BeforeResponse);
}
void UMCToothMovementComponent::LogHookReplayDiagnostic(const TCHAR* Phase,const FVector& From,bool bReplayed)
{
    if(!UpdatedComponent) return;
    const auto& D=HookReplayDiagnostic;
    const FVector To=UpdatedComponent->GetComponentLocation(),Delta=To-From,Total=To-D.BeforeResponse;
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    const FTransform RenderedFrame=Hero?Hero->GetPresentedActorTransform():UpdatedComponent->GetComponentTransform();
    const FVector DisplayOffset=RenderedFrame.GetLocation()-To;
    const double CanonicalYaw=CharacterOwner?CharacterOwner->GetActorRotation().Yaw:UpdatedComponent->GetComponentRotation().Yaw;
    UE_LOG(LogMCHookPrediction,Log,
        TEXT("MC_HOOK_REPLAY event=%s pawn=%s action=%s epoch=%s revision=%u local_id=%u ack=%.6f current_move_ts=%.6f source_time=%.6f saved_count=%d saved_dt_ms=%.3f first_move_ts=%.6f last_move_ts=%.6f from_cm=(%s) to_cm=(%s) phase_delta_cm=%.3f phase_delta_xyz=(%s) total_delta_cm=%.3f total_delta_xyz=(%s) replayed=%d presented_cm=(%s) presented_yaw=%.3f canonical_yaw=%.3f display_offset_cm=%.3f display_offset_xyz=(%s)"),
        Phase,*GetNameSafe(CharacterOwner),*D.Action.ToString(EGuidFormats::Digits),*D.Epoch.ToString(EGuidFormats::Digits),
        D.Revision,uint32(D.LocalID),D.Ack,D.CurrentMoveTime,D.SourceTime,D.SavedCount,D.SavedDelta*1000.f,D.FirstMove,D.LastMove,
        *From.ToString(),*To.ToString(),Delta.Size(),*Delta.ToString(),Total.Size(),*Total.ToString(),bReplayed?1:0,
        *RenderedFrame.GetLocation().ToString(),RenderedFrame.Rotator().Yaw,CanonicalYaw,DisplayOffset.Size(),*DisplayOffset.ToString());
}
#endif

// Compatibility entry points: the accepted wire/history/lifecycle now belongs
// to the same external-motion bridge used by velocity/steered action leases.
bool UMCToothMovementComponent::BeginAcceptedHookPull(FGuid ID,FVector Goal,float Speed,float Stop,float Duration,bool FreeFlight)
{
    FMCAcceptedMotionSnapshot R;R.OperationID=ID;R.Policy=EMCAcceptedMotionPolicy::BoundedGoal;
    R.Goal=Goal;R.Speed=FMath::Clamp(Speed,100.f,1800.f);R.StopDistance=FMath::Clamp(Stop,60.f,300.f);
    R.Duration=FMath::Clamp(Duration,.1f,8.f);R.bFreeFlight=FreeFlight;return BeginAcceptedExternalMotion(R);
}
bool UMCToothMovementComponent::IsAcceptedHookPullOwned(FGuid ID) const
{return ExternalState.Policy==EMCAcceptedMotionPolicy::BoundedGoal && IsAcceptedExternalMotionOwned(ID);}
bool UMCToothMovementComponent::SampleAcceptedHookPullTime(FGuid ID,float& Time) const
{
    if(ExternalState.Policy!=EMCAcceptedMotionPolicy::BoundedGoal) {Time=0;return false;}
    FVector Heading;return SampleAcceptedExternalMotion(ID,Time,Heading);
}
bool UMCToothMovementComponent::UpdateAcceptedHookPull(FGuid ID,FVector Goal)
{return UpdateAcceptedExternalGoal(ID,Goal);}
void UMCToothMovementComponent::EndAcceptedHookPull(FGuid ID)
{if(ExternalState.Policy==EMCAcceptedMotionPolicy::BoundedGoal) EndAcceptedExternalMotion(ID);}
