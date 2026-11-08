#include "MCToothMovementComponent.h"
#include "MCActiveSkillMotion.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCAcceptedMotion,Log,All);
namespace
{
bool ExternalPawnAvailable(const AMCToothCharacter* H)
{
    return H && H->Status && H->Status->IsAlive() && H->ToothPhysics && H->ToothPhysics->CanAct()
        && H->IsNetworkMotionOwnerReady() && H->GetNetworkPresentationOwner()==EMCPresentationOwner::CMC
        && !H->HasFrozenLegs() && !H->SwallowedBy && !H->IsMimicCaptured() && !H->IsSeatedOnRaft();
}
FGuid ExternalEpoch(const AMCToothCharacter* H)
{return H?H->GetSimulationFrame().LoadEpoch:FGuid();}
void ExternalDiagnostic(const TCHAR* Event,const ACharacter* H,const FMCAcceptedMotionSnapshot& R,uint16 LocalID,float Ack=0)
{
#if !UE_BUILD_SHIPPING
    static const bool Enabled=FParse::Param(FCommandLine::Get(),TEXT("MCNetTelemetry"));
    if(!Enabled) return;
    UE_LOG(LogMCAcceptedMotion,Log,TEXT("MC_EXTERNAL_MOTION event=%s pawn=%s role=%d operation=%s epoch=%s revision=%u kind=%d local_id=%u source_time=%.4f ack=%.4f"),
        Event,*GetNameSafe(H),H?int32(H->GetLocalRole()):-1,*R.OperationID.ToString(EGuidFormats::Digits),
        *R.Epoch.ToString(EGuidFormats::Digits),R.Revision,int32(R.Policy),uint32(LocalID),R.SourceTime,Ack);
    // Preserve existing diagnostic recipes without a second Hook state machine.
    if(R.Policy==EMCAcceptedMotionPolicy::BoundedGoal) UE_LOG(LogMCAcceptedMotion,Log,
        TEXT("MC_HOOK_PREDICTION event=%s pawn=%s role=%d action=%s epoch=%s revision=%u local_id=%u source_time=%.4f ack=%.4f"),
        Event,*GetNameSafe(H),H?int32(H->GetLocalRole()):-1,*R.OperationID.ToString(EGuidFormats::Digits),
        *R.Epoch.ToString(EGuidFormats::Digits),R.Revision,uint32(LocalID),R.SourceTime,Ack);
#endif
}
}

bool UMCToothMovementComponent::BeginAcceptedExternalMotion(FMCAcceptedMotionSnapshot R)
{
    auto* H=Cast<AMCToothCharacter>(CharacterOwner);
    if(!H || !H->HasAuthority()) return false;
    H->RefreshNetworkMotionAuthority();
    if(!R.OperationID.IsValid() || !ExternalPawnAvailable(H)
        || !MCActiveSkillMotion::CanAcquire(H) || (R.bFreeFlight && MovementMode==MOVE_Flying)) return false;
    R.Epoch=ExternalEpoch(H);R.Revision=ExternalRevision+1;if(!R.Revision) ++R.Revision;
    R.bActive=true;R.PreviousMode=uint8(MovementMode);R.PreviousCustomMode=CustomMovementMode;R.SourceTime=0;
    R.StartMoveTime=FRootMotionSource_ConstantForce().GetStartTime();
    R.ServerSourceID=1; // validate the accepted policy before allocating its native ID
    if(!R.IsWellFormed()) return false;
    auto Source=MCAcceptedExternalMotion::MakeSource(R);
    ExternalLocalSourceID=ApplyRootMotionSource(Source);R.ServerSourceID=ExternalLocalSourceID;
    if(ExternalLocalSourceID==uint16(ERootMotionSourceID::Invalid)) return false;
    ExternalRevision=R.Revision;ExternalState=R;PendingExternalParameters.Reset();
    if(R.bFreeFlight) SetMovementMode(MOVE_Flying);
    ExternalDiagnostic(TEXT("authority_begin"),CharacterOwner,ExternalState,ExternalLocalSourceID);
    GetPredictionData_Server_Character()->bForceClientUpdate=true;ForceClientAdjustment();return true;
}
bool UMCToothMovementComponent::IsAcceptedExternalMotionOwned(FGuid ID) const
{
    const auto S=const_cast<UMCToothMovementComponent*>(this)->GetRootMotionSourceByID(ExternalLocalSourceID);
    return ExternalState.bActive && ExternalState.OperationID==ID && MCAcceptedExternalMotion::MatchesSource(S,ExternalState)
        && MCAcceptedExternalMotion::IsRunning(S);
}
bool UMCToothMovementComponent::SampleAcceptedExternalMotion(FGuid ID,float& Time,FVector& Heading) const
{
    Time=0;Heading=FVector::ZeroVector;
    if(!CharacterOwner || CharacterOwner->GetLocalRole()==ROLE_SimulatedProxy || ExternalState.Epoch!=ExternalEpoch(Cast<AMCToothCharacter>(CharacterOwner))
        || !IsAcceptedExternalMotionOwned(ID)) return false;
    const auto S=const_cast<UMCToothMovementComponent*>(this)->GetRootMotionSourceByID(ExternalLocalSourceID);
    Time=S->GetTime();
    if(S->GetScriptStruct()==FMCLocomotionRootMotionSource::StaticStruct())
        Heading=static_cast<const FMCLocomotionRootMotionSource*>(S.Get())->Force.GetSafeNormal2D();
    return FMath::IsFinite(Time) && Time>=0 && (ExternalState.Duration<0 || Time<=ExternalState.Duration);
}
bool UMCToothMovementComponent::UpdateAcceptedExternalVelocity(FGuid ID,FVector V)
{
    if(!CharacterOwner || !CharacterOwner->HasAuthority() || V.ContainsNaN() || !IsAcceptedExternalMotionOwned(ID)
        || ExternalState.Policy==EMCAcceptedMotionPolicy::BoundedGoal) return false;
    V=V.GetClampedToMaxSize(2000.f);
    auto R=PendingExternalParameters.Get(ExternalState);
    if(!R.Velocity.Equals(V,.01f)) {R.Velocity=V;PendingExternalParameters=R;}
    return true;
}
bool UMCToothMovementComponent::UpdateAcceptedExternalGoal(FGuid ID,FVector Goal)
{
    if(!CharacterOwner || !CharacterOwner->HasAuthority() || Goal.ContainsNaN() || !IsAcceptedExternalMotionOwned(ID)
        || ExternalState.Policy!=EMCAcceptedMotionPolicy::BoundedGoal) return false;
    auto R=PendingExternalParameters.Get(ExternalState);
    if(!R.Goal.Equals(Goal,.01f)) {R.Goal=Goal;PendingExternalParameters=R;}return true;
}
bool UMCToothMovementComponent::UpdateAcceptedSteeringHeading(FGuid ID,FVector Heading,float LockSeconds)
{
    if(!CharacterOwner || !CharacterOwner->HasAuthority() || Heading.ContainsNaN() || Heading.IsNearlyZero()
        || !FMath::IsFinite(LockSeconds) || !IsAcceptedExternalMotionOwned(ID)
        || ExternalState.Policy!=EMCAcceptedMotionPolicy::SteeredVelocity) return false;
    auto R=PendingExternalParameters.Get(ExternalState);
    R.Velocity=Heading.GetSafeNormal2D()*R.Speed;
    R.SteeringLockedUntil=GetRootMotionSourceByID(ExternalLocalSourceID)->GetTime()+FMath::Clamp(LockSeconds,0.f,1.f);
    PendingExternalParameters=R;return true;
}
void UMCToothMovementComponent::ConsumeExternalParametersForMove()
{
    if(!CharacterOwner || !CharacterOwner->HasAuthority() || !PendingExternalParameters.IsSet()) return;
    const auto S=GetRootMotionSourceByID(ExternalLocalSourceID);
    if(!MCAcceptedExternalMotion::MatchesSource(S,ExternalState) || !MCAcceptedExternalMotion::IsRunning(S)) {PendingExternalParameters.Reset();return;}
    auto R=PendingExternalParameters.GetValue();PendingExternalParameters.Reset();
    R.Revision=++ExternalRevision;if(!R.Revision) R.Revision=++ExternalRevision;
    R.SourceTime=S->GetTime();R.StartMoveTime=S->GetStartTime();R.ServerSourceID=S->LocalID;
    if(R.Policy==EMCAcceptedMotionPolicy::BoundedGoal) {
        auto* Goal=static_cast<FMCHookPullRootMotionSource*>(S.Get());
        Goal->Goal=R.Goal;Goal->Speed=R.Speed;Goal->StopDistance=R.StopDistance;Goal->Revision=R.Revision;
    } else static_cast<FMCLocomotionRootMotionSource*>(S.Get())->Force=R.Velocity;
    ExternalState=R;
    // A changed authority parameter needs position/mode plus policy state at
    // the same ACK. Never place an old ACK heading into the current move.
    GetPredictionData_Server_Character()->bForceClientUpdate=true;ForceClientAdjustment();
}
bool UMCToothMovementComponent::ReadAcceptedSteeringParameters(const FRootMotionSource& S,float& Speed,float& Rate,bool& Locked) const
{
    if(!ExternalState.bActive || ExternalState.Policy!=EMCAcceptedMotionPolicy::SteeredVelocity
        || ExternalState.Epoch!=ExternalEpoch(Cast<AMCToothCharacter>(CharacterOwner)) || S.LocalID!=ExternalLocalSourceID
        || S.InstanceName!=MCAcceptedExternalMotion::SourceName(ExternalState)
        || S.GetScriptStruct()!=FMCLocomotionRootMotionSource::StaticStruct()) return false;
    Speed=ExternalState.Speed;Rate=ExternalState.TurnRate;Locked=S.GetTime()<ExternalState.SteeringLockedUntil;return true;
}
void UMCToothMovementComponent::EndAcceptedExternalMotion(FGuid ID)
{
    if(!CharacterOwner || !CharacterOwner->HasAuthority() || ExternalState.OperationID!=ID || !ExternalState.bActive) return;
    const auto S=GetRootMotionSourceByID(ExternalLocalSourceID);
    if(MCAcceptedExternalMotion::MatchesSource(S,ExternalState)) {
        ExternalState.SourceTime=ExternalState.Duration<0?S->GetTime():FMath::Min(S->GetTime(),ExternalState.Duration);
        ExternalState.StartMoveTime=S->GetStartTime();
        if(ExternalState.Policy==EMCAcceptedMotionPolicy::BoundedGoal) {
            S->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::SetVelocity;
            S->FinishVelocityParams.SetVelocity=FVector(0,0,Velocity.Z);
        }
        RemoveRootMotionSourceByID(ExternalLocalSourceID);
    }
    PendingExternalParameters.Reset();ExternalState.bActive=false;ExternalState.Revision=++ExternalRevision;
    if(!ExternalState.Revision) ExternalState.Revision=++ExternalRevision;
    ExternalDiagnostic(TEXT("authority_stop"),CharacterOwner,ExternalState,ExternalLocalSourceID);
    if(ExternalState.bFreeFlight && ExternalState.Epoch==ExternalEpoch(Cast<AMCToothCharacter>(CharacterOwner)) && MovementMode==MOVE_Flying
        && ExternalPawnAvailable(Cast<AMCToothCharacter>(CharacterOwner)) && MCActiveSkillMotion::CanAcquire(Cast<AMCToothCharacter>(CharacterOwner))) {
        if(ExternalState.PreviousMode==MOVE_Walking || ExternalState.PreviousMode==MOVE_NavWalking) {
            FFindFloorResult Floor;FindFloor(CharacterOwner->GetActorLocation(),Floor,false);
            SetMovementMode(Floor.IsWalkableFloor() && Floor.FloorDist<=MaxStepHeight?EMovementMode(ExternalState.PreviousMode):MOVE_Falling);
        } else SetMovementMode(EMovementMode(ExternalState.PreviousMode),ExternalState.PreviousCustomMode);
    }
    if(!CharacterOwner->IsActorBeingDestroyed()) {GetPredictionData_Server_Character()->bForceClientUpdate=true;ForceClientAdjustment();}
}
void UMCToothMovementComponent::CaptureExternalResponseSnapshot()
{
    ServerExternalSnapshot=ExternalState;
    if(const auto S=GetRootMotionSourceByID(ExternalLocalSourceID);MCAcceptedExternalMotion::MatchesSource(S,ExternalState)) {
        ServerExternalSnapshot.SourceTime=ExternalState.Duration<0?S->GetTime():FMath::Min(S->GetTime(),ExternalState.Duration);
        ServerExternalSnapshot.StartMoveTime=S->GetStartTime();ServerExternalSnapshot.ServerSourceID=S->LocalID;
        ServerExternalSnapshot.bActive=ExternalState.bActive && MCAcceptedExternalMotion::IsRunning(S);
        if(S->GetScriptStruct()==FMCLocomotionRootMotionSource::StaticStruct())
            ServerExternalSnapshot.Velocity=static_cast<const FMCLocomotionRootMotionSource*>(S.Get())->Force;
    }
    ServerHookSnapshot=ServerExternalSnapshot.Policy==EMCAcceptedMotionPolicy::BoundedGoal?ServerExternalSnapshot.AsHookReceipt():FMCHookPullReceipt();
}
void UMCToothMovementComponent::CheckAcceptedExternalValidity()
{
    if(!ExternalState.bActive || !CharacterOwner) return;
    if(ExternalState.Epoch==ExternalEpoch(Cast<AMCToothCharacter>(CharacterOwner)) && ExternalPawnAvailable(Cast<AMCToothCharacter>(CharacterOwner))) return;
    if(CharacterOwner->HasAuthority()) {EndAcceptedExternalMotion(ExternalState.OperationID);return;}
    LocallyInterruptedExternal=ExternalState.OperationID;LocallyInterruptedExternalEpoch=ExternalState.Epoch;
    ExternalDiagnostic(TEXT("owner_interrupt"),CharacterOwner,ExternalState,ExternalLocalSourceID);
    RemoveLocalExternalHistory(ExternalState);ExternalState.bActive=false;
}
void UMCToothMovementComponent::NotifyAuthorityMotionContextChanged()
{
    if(!CharacterOwner || !CharacterOwner->HasAuthority()) return;
    if(ExternalState.bActive) EndAcceptedExternalMotion(ExternalState.OperationID);
    PendingExternalParameters.Reset();CancelDash();
    GetPredictionData_Server_Character()->bForceClientUpdate=true;ForceClientAdjustment();
}
void UMCToothMovementComponent::ResetClientMovementAfterNetworkTeleport()
{
    if(!CharacterOwner || CharacterOwner->GetLocalRole()!=ROLE_AutonomousProxy) return;
    if(ExternalState.bActive) {
        LocallyInterruptedExternal=ExternalState.OperationID;LocallyInterruptedExternalEpoch=ExternalState.Epoch;
        RemoveLocalExternalHistory(ExternalState);ExternalState.bActive=false;
    }
    PendingExternalParameters.Reset();CancelDash();
    CharacterOwner->SavedRootMotion.Clear();CharacterOwner->bClientResimulateRootMotionSources=false;CharacterOwner->bClientResimulateRootMotion=false;
    if(HasPredictionData_Client()) {
        auto* Data=GetPredictionData_Client_Character();
        Data->SavedMoves.Reset();Data->PendingMove.Reset();Data->LastAckedMove.Reset();Data->bUpdatePosition=false;
    }
    bPendingStaminaCorrection=bReplayingAuthoritativeStamina=bPendingSwimCorrection=bReplayingAuthoritativeSwim=false;
    bHasSuctionSample=bHasRiverSample=bHasBraceSample=bHasHandlingSample=false;
#if !UE_BUILD_SHIPPING
    HookReplayDiagnostic.bAwaitingReplay=false;
#endif
}
void UMCToothMovementComponent::RemoveLocalExternalHistory(const FMCAcceptedMotionSnapshot& R)
{
    if(const auto S=GetRootMotionSourceByID(ExternalLocalSourceID);MCAcceptedExternalMotion::MatchesSource(S,R))
        RemoveRootMotionSourceByID(S->LocalID);
    MCAcceptedExternalMotion::ReplaceHistory(CharacterOwner->SavedRootMotion,R,0,false);
    if(CharacterOwner->GetLocalRole()!=ROLE_AutonomousProxy) return;
    auto* Data=GetPredictionData_Client_Character();
    for(auto& Move:Data->SavedMoves) MCAcceptedExternalMotion::ReplaceHistory(Move->SavedRootMotion,R,0,false);
    if(Data->LastAckedMove.IsValid()) MCAcceptedExternalMotion::ReplaceHistory(Data->LastAckedMove->SavedRootMotion,R,0,false);
    if(Data->PendingMove.IsValid()) MCAcceptedExternalMotion::ReplaceHistory(Data->PendingMove->SavedRootMotion,R,0,false);
}
void UMCToothMovementComponent::PrepareExternalMoveResponse(FMCStaminaMoveResponse& Response,FNetworkPredictionData_Client_Character* Data,int32 MoveIndex)
{
    if(!CharacterOwner || CharacterOwner->GetLocalRole()!=ROLE_AutonomousProxy) return;
    CheckAcceptedExternalValidity();
    const bool HadGroup=Response.bRootMotionSourceCorrection;
    auto* Group=HadGroup?Response.GetRootMotionSourceGroup(*this):nullptr;
    if(Group) MCAcceptedExternalMotion::RemoveUnstampedSources(*Group);
    if(!Data || MoveIndex==INDEX_NONE) return;
    const auto& R=Response.ExternalMotion;
    if(!R.Revision || !R.IsWellFormed() || R.Epoch!=ExternalEpoch(Cast<AMCToothCharacter>(CharacterOwner))) return;
    if(ExternalState.Epoch==R.Epoch && (int32(R.Revision-ExternalState.Revision)<0
        || (R.Revision==ExternalState.Revision && !ExternalState.bActive && R.bActive))) return;
    auto S=GetRootMotionSourceByID(ExternalLocalSourceID);
    const bool Interrupted=LocallyInterruptedExternal==R.OperationID && LocallyInterruptedExternalEpoch==R.Epoch;
    const bool Active=R.bActive && !Interrupted && ExternalPawnAvailable(Cast<AMCToothCharacter>(CharacterOwner));
    // No initial source, no old heading/time applied to the current move, and
    // no replay from a good ACK that has no correction position/mode.
    if(Active && Response.IsGoodMove()) return;
    if(ExternalState.OperationID.IsValid() && (ExternalState.OperationID!=R.OperationID || ExternalState.Epoch!=R.Epoch))
        RemoveLocalExternalHistory(ExternalState);
    const auto Previous=ExternalState;
    const bool WasActive=Previous.bActive && Previous.OperationID==R.OperationID && Previous.Epoch==R.Epoch;
    ExternalState=R;ExternalState.bActive=Active;
    if(!Active) {
        if(MCAcceptedExternalMotion::MatchesSource(S,R)) {
            if(S->GetScriptStruct()==FMCHookPullRootMotionSource::StaticStruct()) {
                S->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::SetVelocity;S->FinishVelocityParams.SetVelocity=FVector(0,0,Velocity.Z);
            }
            RemoveRootMotionSourceByID(S->LocalID);
        }
        RemoveLocalExternalHistory(R);
        if(WasActive) {auto Diagnostic=R;Diagnostic.Policy=Previous.Policy;ExternalDiagnostic(TEXT("owner_stop"),CharacterOwner,Diagnostic,ExternalLocalSourceID,Response.ClientAdjustment.TimeStamp);}
        return;
    }
    if(!(MCAcceptedExternalMotion::MatchesSource(S,R) && MCAcceptedExternalMotion::IsRunning(S))) {
        S=MCAcceptedExternalMotion::MakeSource(R);ExternalLocalSourceID=ApplyRootMotionSource(S);
        ExternalDiagnostic(TEXT("owner_install"),CharacterOwner,R,ExternalLocalSourceID,Response.ClientAdjustment.TimeStamp);
#if !UE_BUILD_SHIPPING
        if(R.Policy==EMCAcceptedMotionPolicy::BoundedGoal) BeginHookReplayDiagnostic(R.AsHookReceipt(),Response.ClientAdjustment.TimeStamp,Data,MoveIndex);
#endif
    } else ExternalLocalSourceID=S->LocalID;
    for(int32 I=MoveIndex;I<Data->SavedMoves.Num();++I)
        MCAcceptedExternalMotion::ReplaceHistory(Data->SavedMoves[I]->SavedRootMotion,R,ExternalLocalSourceID,true);
    if(!HadGroup) {
        Response.bRootMotionSourceCorrection=true;Group=Response.GetRootMotionSourceGroup(*this);*Group=FRootMotionSourceGroup();
        const FRotator Rotation=(Response.bHasRotation?Response.ClientAdjustment.NewRot:CharacterOwner->GetActorRotation()).GetNormalized();
        Response.RootMotionRotation=FVector_NetQuantizeNormal(Rotation.Pitch/180.f,Rotation.Yaw/180.f,Rotation.Roll/180.f);
    }
    auto ServerSource=MCAcceptedExternalMotion::MakeSource(R);ServerSource->LocalID=R.ServerSourceID;Group->RootMotionSources.Add(ServerSource);
    CharacterOwner->bClientResimulateRootMotionSources=true;
}
