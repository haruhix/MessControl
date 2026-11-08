#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCGripComponent.h"
#include "MCCataclysmRaft.h"
#include "MCRewardChest.h"
#include "MCThroat.h"
#include "MCToothMovementComponent.h"
#include "MCToothMeshComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CoreGlobals.h"
#include "Misc/ScopeExit.h"

namespace
{
FGuid WorldMotionEpoch(const UWorld* World)
{return MCNetworkFrames::WorldLoadEpoch(World);}
uint32 NextMotionRevision(uint32 Value) {++Value;return Value?Value:1;}
bool IsCMCOwner(EMCPresentationOwner Owner)
{return Owner==EMCPresentationOwner::CMC || Owner==EMCPresentationOwner::WorldAnchoredCMC;}
}

FMCMotionAuthorityState AMCToothCharacter::DesiredMotionAuthority() const
{
    FMCMotionAuthorityState State=MotionAuthority;
    const FGuid Epoch=WorldMotionEpoch(GetWorld());State.LoadEpoch=Epoch.IsValid()?Epoch:FallbackMotionEpoch;
    State.OwnerKind=uint8(EMCPresentationOwner::CMC);State.ClaimKind=uint8(EMCMotionClaimKind::None);State.SourceActor=nullptr;State.SubIndex=INDEX_NONE;State.ClaimOrigin=FVector::ZeroVector;
    if(IsValid(SwallowedBy)) {State.OwnerKind=uint8(EMCPresentationOwner::Transit);State.ClaimKind=uint8(EMCMotionClaimKind::Throat);State.SourceActor=SwallowedBy;State.ClaimOrigin=ThroatCaptureStart;}
    else if(IsValid(MimicCaptor)) {State.OwnerKind=uint8(EMCPresentationOwner::Transit);State.ClaimKind=uint8(EMCMotionClaimKind::Mimic);State.SourceActor=MimicCaptor;State.ClaimOrigin=MimicCaptureStart;}
    else if(IsSeatedOnRaft()) {State.OwnerKind=uint8(EMCPresentationOwner::Transit);State.ClaimKind=uint8(EMCMotionClaimKind::RaftSeat);State.SourceActor=SeatedRaft;State.SubIndex=RaftSeatIndex;}
    else if(ToothPhysics && ToothPhysics->GetBodyState()!=EMCBodyState::Standing) {
        State.OwnerKind=uint8(EMCPresentationOwner::BodyFrame);State.ClaimKind=uint8(EMCMotionClaimKind::Body);State.SubIndex=int32(ToothPhysics->GetBodyState());
    } else if(Grip && Grip->IsWorldAnchored()) {
        State.OwnerKind=uint8(EMCPresentationOwner::WorldAnchoredCMC);State.ClaimKind=uint8(EMCMotionClaimKind::WorldAnchor);State.SourceActor=Grip->BraceTarget();
    }
    return State;
}
void AMCToothCharacter::RefreshNetworkMotionAuthority()
{
    if(!HasAuthority() || !GetWorld()) return;
    if(!FallbackMotionEpoch.IsValid()) FallbackMotionEpoch=FGuid::NewGuid();
    auto Desired=DesiredMotionAuthority();
    const bool NewEpoch=Desired.LoadEpoch!=MotionAuthority.LoadEpoch || !MotionAuthority.Context().IsWellFormed();
    const bool NewOwner=NewEpoch || Desired.OwnerKind!=MotionAuthority.OwnerKind || Desired.ClaimKind!=MotionAuthority.ClaimKind
        || Desired.SourceActor!=MotionAuthority.SourceActor || Desired.SubIndex!=MotionAuthority.SubIndex;
    if(!NewOwner) return;
    Desired.OwnerRevision=NewEpoch?1:NextMotionRevision(MotionAuthority.OwnerRevision);
    Desired.ResetRevision=NewEpoch?1:MotionAuthority.ResetRevision;
    MotionAuthority=AcceptedMotionAuthority=Desired;ObservedMotionContext=Desired.Context();
    ReleaseNetworkPresentation(true);RefreshNetworkMovementContacts();ForceNetUpdate();
    if(HasActorBegunPlay()) if(auto* Move=Cast<UMCToothMovementComponent>(GetCharacterMovement())) Move->NotifyAuthorityMotionContextChanged();
}
FMCNetworkCorrectionContext AMCToothCharacter::CaptureNetworkCorrectionContext()
{
    if(HasAuthority()) RefreshNetworkMotionAuthority();
    return HasAuthority()?MotionAuthority.Context():ObservedMotionContext;
}
bool AMCToothCharacter::ObserveMotionContext(const FMCNetworkCorrectionContext& Context)
{
    const FGuid Expected=WorldMotionEpoch(GetWorld());
    if(!Context.CanAdvanceFrom(ObservedMotionContext,Expected)) return false;
    if(!Context.Equals(ObservedMotionContext)) {
        const bool Reset=ObservedMotionContext.IsWellFormed() && (Context.LoadEpoch!=ObservedMotionContext.LoadEpoch || Context.ResetRevision!=ObservedMotionContext.ResetRevision);
        const bool Transfer=!Reset && ObservedMotionContext.IsWellFormed() && Context.Owner==EMCPresentationOwner::BodyFrame
            && GetMesh() && !GetMesh()->GetComponentTransform().ContainsNaN();
        const FTransform Shown=GetMesh()->GetComponentTransform();
        ObservedMotionContext=Context;bHasBodyPresentationHandoff=false;
        ReleaseNetworkPresentation(true);
        if(Transfer) {
            // The selected body adapter takes this one shown frame. Releasing
            // the CMC offset must not pop the mesh before KeepWorld detaches it.
            if(auto* HandoffMesh=Cast<UMCToothMeshComponent>(GetMesh())) HandoffMesh->BeginNetworkPresentationCompensation();
            GetMesh()->SetWorldTransform(Shown,false,nullptr,ETeleportType::None);
            if(auto* HandoffMesh=Cast<UMCToothMeshComponent>(GetMesh())) HandoffMesh->EndNetworkPresentationCompensation();
            BodyPresentationHandoffContext=Context;BodyPresentationHandoffWorld=Shown;bHasBodyPresentationHandoff=true;
        }
    }
    return true;
}
EMCPresentationOwner AMCToothCharacter::GetNetworkPresentationOwner() const
{
    if(HasAuthority()) return EMCPresentationOwner(DesiredMotionAuthority().OwnerKind);
    return ObservedMotionContext.IsWellFormed()?ObservedMotionContext.Owner:EMCPresentationOwner::Transit;
}
bool AMCToothCharacter::IsNetworkMotionOwnerReady() const
{
    if(HasAuthority()) return true;
    if(!ObservedMotionContext.IsWellFormed()) return false;
    const FGuid Expected=WorldMotionEpoch(GetWorld());if(Expected.IsValid() && Expected!=ObservedMotionContext.LoadEpoch) return false;
    const auto PresentationOwner=ObservedMotionContext.Owner;
    if(PresentationOwner==EMCPresentationOwner::BodyFrame) return ToothPhysics && ToothPhysics->HasNetworkBodyFrameContext(ObservedMotionContext);
    if(!AcceptedMotionAuthority.IsWellFormed() || !AcceptedMotionAuthority.Context().Equals(ObservedMotionContext)) return false;
    const auto Claim=EMCMotionClaimKind(AcceptedMotionAuthority.ClaimKind);
    auto* Source=AcceptedMotionAuthority.SourceActor.Get();
    if(PresentationOwner==EMCPresentationOwner::Transit) {
        if(!IsValid(Source) || Source->GetWorld()!=GetWorld()) return false;
        if(Claim==EMCMotionClaimKind::Throat) return Cast<AMCThroat>(Source) && SwallowedBy==Source && bThroatCaptured;
        if(Claim==EMCMotionClaimKind::Mimic) return Cast<AMCRewardChest>(Source) && MimicCaptor==Source && bMimicCaptured;
        if(Claim==EMCMotionClaimKind::RaftSeat) return Cast<AMCCataclysmRaft>(Source) && SeatedRaft==Source && RaftSeatIndex==AcceptedMotionAuthority.SubIndex && bAppliedRaftSeat;
        return false;
    }
    if(!ToothPhysics || ToothPhysics->GetBodyState()!=EMCBodyState::Standing || SwallowedBy || IsMimicCaptured() || IsSeatedOnRaft()) return false;
    return PresentationOwner==EMCPresentationOwner::WorldAnchoredCMC
        ?Grip && Grip->IsWorldAnchored() && IsValid(Source) && Grip->BraceTarget()==Source
        :!Grip || !Grip->IsWorldAnchored();
}
bool AMCToothCharacter::AllowsMotionClaim(EMCMotionClaimKind Claim,const AActor* Source,int32 SubIndex) const
{
    if(HasAuthority() || bApplyingMotionAuthority) return true;
    return AcceptedMotionAuthority.IsWellFormed() && AcceptedMotionAuthority.Context().Equals(ObservedMotionContext)
        && EMCMotionClaimKind(AcceptedMotionAuthority.ClaimKind)==Claim && AcceptedMotionAuthority.SourceActor==Source
        && (SubIndex==INDEX_NONE || AcceptedMotionAuthority.SubIndex==SubIndex);
}
void AMCToothCharacter::OnRep_MotionAuthority()
{
    if(MotionAuthority.IsWellFormed() && ObserveMotionContext(MotionAuthority.Context())) AcceptedMotionAuthority=MotionAuthority;
    ApplyMotionAuthorityDescriptor();
}
void AMCToothCharacter::PostNetInit()
{
    Super::PostNetInit();
    // The initial actor bunch has already established this newly spawned
    // pawn's authoritative pose. Bind that exact descriptor once here; an
    // arbitrary first good ACK may never consume a later reset/handoff.
    if(!HasAuthority() && MotionAuthority.IsWellFormed() && ObserveMotionContext(MotionAuthority.Context())) {
        AcceptedMotionAuthority=MotionAuthority;LastAppliedNativeContext=MotionAuthority.Context();
        ApplyMotionAuthorityDescriptor();
    }
}
void AMCToothCharacter::ApplyMotionAuthorityDescriptor()
{
    if(HasAuthority() || bApplyingMotionAuthority) return;
    if(!bNetworkPresentationStarted || !ToothPhysics || !ToothPhysics->HasBegunPlay()) {bMotionAuthorityPending=true;return;}
    if(MotionAuthority.IsWellFormed() && ObserveMotionContext(MotionAuthority.Context())) AcceptedMotionAuthority=MotionAuthority;
    // A newer non-transit wire context already revokes every previous capture.
    // Its descriptor/body state may follow later; hold CMC until that is ready.
    if(ObservedMotionContext.IsWellFormed() && ObservedMotionContext.Owner!=EMCPresentationOwner::Transit) {
        TGuardValue<bool> Applying(bApplyingMotionAuthority,true);
        if(SwallowedBy || bThroatCaptured) {SwallowedBy=nullptr;OnRep_ThroatCapture();}
        if(MimicCaptor || bMimicCaptured) {MimicCaptor=nullptr;OnRep_MimicCapture();}
        if(SeatedRaft || bAppliedRaftSeat) {SeatedRaft=nullptr;RaftSeatIndex=INDEX_NONE;OnRep_RaftSeat();}
    }
    if(AcceptedMotionAuthority.IsWellFormed() && AcceptedMotionAuthority.Context().Equals(ObservedMotionContext)) {
        const auto Claim=EMCMotionClaimKind(AcceptedMotionAuthority.ClaimKind);
        AActor* Source=AcceptedMotionAuthority.SourceActor.Get();
        const bool SourceReady=Claim==EMCMotionClaimKind::None || Claim==EMCMotionClaimKind::Body || IsValid(Source);
        if(SourceReady) {
            TGuardValue<bool> Applying(bApplyingMotionAuthority,true);
            if(Claim!=EMCMotionClaimKind::Throat && (SwallowedBy || bThroatCaptured)) {SwallowedBy=nullptr;OnRep_ThroatCapture();}
            if(Claim!=EMCMotionClaimKind::Mimic && (MimicCaptor || bMimicCaptured)) {MimicCaptor=nullptr;OnRep_MimicCapture();}
            if(Claim!=EMCMotionClaimKind::RaftSeat && (SeatedRaft || bAppliedRaftSeat)) {SeatedRaft=nullptr;RaftSeatIndex=INDEX_NONE;OnRep_RaftSeat();}
            if(Claim==EMCMotionClaimKind::Throat) if(auto* Throat=Cast<AMCThroat>(Source))
                {ThroatCaptureStart=AcceptedMotionAuthority.ClaimOrigin;if(SwallowedBy!=Throat || !bThroatCaptured) {SwallowedBy=Throat;OnRep_ThroatCapture();}}
            if(Claim==EMCMotionClaimKind::Mimic) if(auto* Chest=Cast<AMCRewardChest>(Source))
                {MimicCaptureStart=AcceptedMotionAuthority.ClaimOrigin;if(MimicCaptor!=Chest || !bMimicCaptured) {MimicCaptor=Chest;OnRep_MimicCapture();}}
            if(Claim==EMCMotionClaimKind::RaftSeat) if(auto* Raft=Cast<AMCCataclysmRaft>(Source))
                if(SeatedRaft!=Raft || RaftSeatIndex!=AcceptedMotionAuthority.SubIndex || !bAppliedRaftSeat) {SeatedRaft=Raft;RaftSeatIndex=AcceptedMotionAuthority.SubIndex;OnRep_RaftSeat();}
        }
    }
    bMotionAuthorityPending=!IsNetworkMotionOwnerReady();
    if(bMotionAuthorityPending || !IsCMCOwner(GetNetworkPresentationOwner())) {
        GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->DisableMovement();
        bHeldForMotionAuthority=true;
    } else if(bHeldForMotionAuthority) {
        if(GetCharacterMovement()->MovementMode==MOVE_None && !HasFrozenLegs() && !IsArenaArrivalHeld()) GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        if(GetCharacterMovement()->MovementMode!=MOVE_None) bHeldForMotionAuthority=false;
    }
    RefreshNetworkMovementContacts();
}
void AMCToothCharacter::OnRep_ReplicatedMovement()
{
    ApplyMotionAuthorityDescriptor();
    if(!IsNetworkMotionOwnerReady() || !IsCMCOwner(GetNetworkPresentationOwner())) return;
    Super::OnRep_ReplicatedMovement();
}
void AMCToothCharacter::OnRep_ReplicatedBasedMovement()
{
    ApplyMotionAuthorityDescriptor();
    if(!IsNetworkMotionOwnerReady() || !IsCMCOwner(GetNetworkPresentationOwner())) return;
    Super::OnRep_ReplicatedBasedMovement();
}
EMCNetworkCorrectionDecision AMCToothCharacter::AdoptNetworkCorrectionContext(const FMCNetworkCorrectionContext& Context,bool bPositionResponse)
{
    if(GetLocalRole()!=ROLE_AutonomousProxy || !ObserveMotionContext(Context)) return EMCNetworkCorrectionDecision::Ignore;
    ApplyMotionAuthorityDescriptor();
    if(!IsCMCOwner(Context.Owner) || !IsNetworkMotionOwnerReady()) return EMCNetworkCorrectionDecision::Ignore;
    const auto Decision=LastAppliedNativeContext.Equals(Context)?EMCNetworkCorrectionDecision::Reconcile:EMCNetworkCorrectionDecision::SnapAndReset;
    if(bPositionResponse) {PendingNativeContext=Context;PendingNativeDecision=Decision;}
    return Decision;
}
EMCNetworkCorrectionDecision AMCToothCharacter::AcceptNetworkBodyFrameContext(const FMCNetworkCorrectionContext& Context)
{
    if(HasAuthority()) return EMCNetworkCorrectionDecision::Reconcile;
    const bool Changed=!Context.Equals(ObservedMotionContext);
    if(!ObserveMotionContext(Context)) return EMCNetworkCorrectionDecision::Ignore;
    if(Context.Owner!=EMCPresentationOwner::BodyFrame && !IsCMCOwner(Context.Owner)) {ApplyMotionAuthorityDescriptor();return EMCNetworkCorrectionDecision::Ignore;}
    ApplyMotionAuthorityDescriptor();
    return Changed?EMCNetworkCorrectionDecision::SnapAndReset:EMCNetworkCorrectionDecision::Reconcile;
}
bool AMCToothCharacter::ConsumeNetworkBodyPresentationHandoff(FTransform& Shown)
{
    if(!bHasBodyPresentationHandoff) return false;
    bHasBodyPresentationHandoff=false;
    if(!BodyPresentationHandoffContext.Equals(ObservedMotionContext)
        || !GetMesh()->GetComponentTransform().Equals(BodyPresentationHandoffWorld,.01f)) return false;
    Shown=BodyPresentationHandoffWorld;return true;
}
bool AMCToothCharacter::CanReconcileNetworkPresentation() const
{
    return bNetworkPresentationStarted && GetLocalRole()==ROLE_AutonomousProxy && IsLocallyControlled()
        && IsCMCOwner(GetNetworkPresentationOwner()) && IsNetworkMotionOwnerReady()
        && GetMesh()->GetAttachParent()==GetCapsuleComponent() && !GetMesh()->IsUsingAbsoluteLocation();
}
USkeletalMeshComponent* AMCToothCharacter::BeginNetworkPresentationCorrection()
{
    if(PendingNativeDecision==EMCNetworkCorrectionDecision::SnapAndReset) {ReleaseNetworkPresentation(true);return nullptr;}
    if(PendingNativeDecision!=EMCNetworkCorrectionDecision::Reconcile || !CanReconcileNetworkPresentation()) {ReleaseNetworkPresentation();return nullptr;}
    if(!NetworkPresentation.Begin(GetMesh(),PendingNativeContext)) return nullptr;
    return NetworkPresentation.BeginScope();
}
void AMCToothCharacter::EndNetworkPresentationCorrection(bool Awaiting,bool Applied)
{
    ON_SCOPE_EXIT {if(ToothPhysics) ToothPhysics->RefreshNetworkPresentationBalance();};
    NetworkPresentation.EndScope(Awaiting,GetWorld()->GetTimeSeconds());
    if(Applied) LastAppliedNativeContext=PendingNativeContext;
}
USkeletalMeshComponent* AMCToothCharacter::BeginNetworkPresentationReplay()
{
    if(!CanReconcileNetworkPresentation() || !NetworkPresentation.IsAwaitingReplay()) return nullptr;
    return NetworkPresentation.BeginScope();
}
void AMCToothCharacter::EndNetworkPresentationReplay()
{
    ON_SCOPE_EXIT {if(ToothPhysics) ToothPhysics->RefreshNetworkPresentationBalance();};
    NetworkPresentation.EndScope(false,GetWorld()->GetTimeSeconds());
}
void AMCToothCharacter::TickNetworkPresentation(float)
{
    ON_SCOPE_EXIT {if(ToothPhysics) ToothPhysics->RefreshNetworkPresentationBalance();};
    if(!HasAuthority()) ApplyMotionAuthorityDescriptor();
    if(!CanReconcileNetworkPresentation()) {NetworkPresentation.Release();return;}
    NetworkPresentation.Tick(GetWorld()->GetTimeSeconds());
}
void AMCToothCharacter::ReleaseNetworkPresentation(bool ResetCamera)
{
    NetworkPresentation.Release();if(ResetCamera) bMouthCameraInitialized=false;
    if(ToothPhysics) ToothPhysics->RefreshNetworkPresentationBalance();
}
FMCActorSimulationFrame AMCToothCharacter::GetSimulationFrame() const
{
    FMCActorSimulationFrame Frame;const auto C=HasAuthority()?MotionAuthority.Context():ObservedMotionContext;
    Frame.LoadEpoch=C.LoadEpoch;Frame.OwnerRevision=C.OwnerRevision;Frame.ResetRevision=C.ResetRevision;Frame.Owner=GetNetworkPresentationOwner();
    Frame.GameplayActor=GetActorTransform();Frame.SimulationTime=GetWorld()?GetWorld()->GetTimeSeconds():0;
    Frame.PhysicalCenter=GetActorLocation();Frame.CanonicalVelocity=GetCharacterMovement()->Velocity;
    Frame.bHasCanonicalPhysicsSample=IsCMCOwner(Frame.Owner) && IsNetworkMotionOwnerReady();
    if(HasAuthority() || GetLocalRole()==ROLE_AutonomousProxy) {
        const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
        Frame.SourceTime=State?State->GetServerWorldTimeSeconds():Frame.SimulationTime;Frame.bHasSourceTime=true;
    }
    if(!HasAuthority() && Frame.Owner==EMCPresentationOwner::BodyFrame && ToothPhysics && IsNetworkMotionOwnerReady()) Frame.GameplayActor=ToothPhysics->NetworkBodySimulationTransform();
    if(Frame.Owner==EMCPresentationOwner::BodyFrame && ToothPhysics) ToothPhysics->PopulateCanonicalPhysicsFrame(Frame);
    return Frame;
}
FMCActorPresentationFrame AMCToothCharacter::GetPresentationFrame() const
{
    const auto Simulation=GetSimulationFrame();FMCActorPresentationFrame Frame;
    Frame.LoadEpoch=Simulation.LoadEpoch;Frame.OwnerRevision=Simulation.OwnerRevision;Frame.ResetRevision=Simulation.ResetRevision;Frame.Owner=Simulation.Owner;
    Frame.GameplayActor=Simulation.GameplayActor;Frame.PresentedActor=GetActorTransform();Frame.FrameSerial=uint32(GFrameCounter);Frame.PresentationTime=Simulation.SimulationTime;
    // Native simulated-proxy smoothing and owner reconciliation both live on
    // this actual mesh. Never add another owner offset to an evaluated socket.
    if((IsCMCOwner(Frame.Owner) && GetMesh()->GetAttachParent()==GetCapsuleComponent() && !GetMesh()->IsUsingAbsoluteLocation())
        || Frame.Owner==EMCPresentationOwner::BodyFrame)
        Frame.PresentedActor=StandingMeshTransform().Inverse()*GetMesh()->GetComponentTransform();
    if(Frame.PresentedActor.ContainsNaN()) Frame.PresentedActor=GetActorTransform();
    return Frame;
}
FTransform AMCToothCharacter::GetPresentedActorTransform() const {return GetPresentationFrame().PresentedActor;}
FTransform AMCToothCharacter::GetPresentedMeshSocketTransform(FName Socket) const {return GetMesh()->GetSocketTransform(Socket);}
FTransform AMCToothCharacter::GetSimulationMeshSocketTransform(FName Socket) const {return GetPresentationFrame().ToCanonicalWorld(GetPresentedMeshSocketTransform(Socket));}
void AMCToothCharacter::NotifyNetworkPositionReset()
{
    if(!HasAuthority()) return;RefreshNetworkMotionAuthority();
    MotionAuthority.ResetRevision=NextMotionRevision(MotionAuthority.ResetRevision);
    AcceptedMotionAuthority=MotionAuthority;ObservedMotionContext=MotionAuthority.Context();ReleaseNetworkPresentation(true);ForceNetUpdate();
    if(HasActorBegunPlay()) if(auto* Move=Cast<UMCToothMovementComponent>(GetCharacterMovement())) Move->NotifyAuthorityMotionContextChanged();
}
bool AMCToothCharacter::TeleportTo(const FVector& Position,const FRotator& Rotation,bool IsTest,bool NoCheck)
{
    const bool Result=Super::TeleportTo(Position,Rotation,IsTest,NoCheck);if(Result && !IsTest) NotifyNetworkPositionReset();return Result;
}
void AMCToothCharacter::TeleportWithNetworkContext(FVector Position,FVector ExitVelocity)
{
    if(!HasAuthority() || Position.ContainsNaN() || ExitVelocity.ContainsNaN()) return;
    SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    if(!ExitVelocity.IsNearlyZero()) LaunchCharacter(ExitVelocity,true,true);
    NotifyNetworkPositionReset();
    if(!IsLocallyControlled()) ClientApplyNetworkTeleport(MotionAuthority,Position,ExitVelocity);
}
void AMCToothCharacter::ClientApplyNetworkTeleport_Implementation(FMCMotionAuthorityState Authority,FVector_NetQuantize10 Position,FVector_NetQuantize10 ExitVelocity)
{
    if(!Authority.IsWellFormed() || Position.ContainsNaN() || ExitVelocity.ContainsNaN() || !ObserveMotionContext(Authority.Context())) return;
    const bool Consumed=LastAppliedNativeContext.LoadEpoch==Authority.LoadEpoch
        && int32(LastAppliedNativeContext.OwnerRevision-Authority.OwnerRevision)>=0 && int32(LastAppliedNativeContext.ResetRevision-Authority.ResetRevision)>=0;
    AcceptedMotionAuthority=Authority;ApplyMotionAuthorityDescriptor();
    if(Consumed) return;
    TGuardValue<bool> ApplyingTeleport(bApplyingNetworkTeleport,true);
    ReleaseNetworkPresentation(true);
    if(auto* Move=Cast<UMCToothMovementComponent>(GetCharacterMovement())) Move->ResetClientMovementAfterNetworkTeleport();
    SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);bMouthCameraHeld=false;
    if(!ExitVelocity.IsNearlyZero()) LaunchCharacter(ExitVelocity,true,true);
    LastAppliedNativeContext=Authority.Context();
}
