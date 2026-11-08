#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCGripComponent.h"
#include "MCFoodCollectionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"

void AMCFoodActor::RefreshAcceptedInteraction()
{
    if(!HasAuthority()) return;
    FMCFoodInteractionState Next;Next.Phase=Phase;Next.StackCarrier=StackCarrier;Next.Container=ActiveContainer;Next.Participants=Holders;
    Next.LoadEpoch=MCNetworkFrames::WorldLoadEpoch(GetWorld());
    AMCToothCharacter* Carrier=StackCarrier?StackCarrier.Get():Phase==EMCFoodPhase::Carried && Holders.Num()==1?Holders[0].Get():nullptr;
    if(!Next.LoadEpoch.IsValid()) {if(!FallbackFoodEpoch.IsValid()) FallbackFoodEpoch=FGuid::NewGuid();Next.LoadEpoch=FallbackFoodEpoch;}
    Next.Carry.Holder=Carrier;
    Next.bRequiresCarrier=IsValid(Carrier);Next.bRequiresStackCarrier=IsValid(StackCarrier);Next.bRequiresContainer=IsValid(ActiveContainer);
    if(IsValid(Carrier)) {
        const FTransform Pose=StackCarrier && IsStackPickupActive()?Carrier->FoodCollection->StackPose(this):GetActorTransform();
        const FTransform Relative=Pose.GetRelativeTransform(Carrier->GetActorTransform());
        Next.Carry.Location=Relative.GetLocation();Next.Carry.Rotation=Relative.Rotator();
    }
    Next.Pickup=StackPickup;
    const bool Changed=!InteractionState.Revision || Next.LoadEpoch!=InteractionState.LoadEpoch || Next.Phase!=InteractionState.Phase
        || Next.StackCarrier!=InteractionState.StackCarrier || Next.Container!=InteractionState.Container
        || Next.Participants!=InteractionState.Participants || Next.Carry.Holder!=InteractionState.Carry.Holder
        || Next.Pickup.StartedAt!=InteractionState.Pickup.StartedAt;
    Next.Revision=InteractionState.Revision;Next.AcceptedAt=InteractionState.AcceptedAt;
    if(Changed) {if(!++Next.Revision) ++Next.Revision;Next.AcceptedAt=HazardNow();}
    InteractionState=Next;PresentedInteraction=Next;
    // Existing public properties remain available to saved callers. Rendering
    // consumes the accepted descriptor rather than partial cross-actor gates.
    CarryPresentation=Next.Carry;
    if(Changed) {
        for(const auto& HolderEntry:Holders) if(auto* H=HolderEntry.Get();IsValid(H) && H->Grip) {
            for(auto* Frame:{&H->Grip->Frame,&H->Grip->Secondary}) if(Frame->Food==this) {
                Frame->FoodEpoch=Next.LoadEpoch;Frame->FoodRevision=Next.Revision;
            }
            H->ForceNetUpdate();
        }
        ForceNetUpdate();
    }
}
void AMCFoodActor::OnRep_InteractionState()
{
    const FGuid Expected=MCNetworkFrames::WorldLoadEpoch(GetWorld());
    if(InteractionState.CanAdvanceFrom(PresentedInteraction,Expected)) PresentedInteraction=InteractionState;
}
bool AMCFoodActor::MatchesInteractionGeneration(FGuid Epoch,uint32 Revision,const AMCToothCharacter* H) const
{
    const auto& State=HasAuthority()?InteractionState:PresentedInteraction;
    return State.Revision && State.LoadEpoch==Epoch && State.Revision==Revision && State.Participants.Contains(H);
}
FMCActorSimulationFrame AMCFoodActor::GetSimulationFrame() const
{
    FMCActorSimulationFrame F;const auto& State=HasAuthority()?InteractionState:PresentedInteraction;
    F.LoadEpoch=State.LoadEpoch;F.OwnerRevision=State.Revision;F.GameplayActor=GetActorTransform();
    F.Owner=State.StackCarrier || State.Phase==EMCFoodPhase::Swallowing || State.Container?EMCPresentationOwner::Transit:EMCPresentationOwner::BodyFrame;
    F.SimulationTime=GetWorld()?GetWorld()->GetTimeSeconds():0;F.PhysicalCenter=Body->Bounds.Origin;
    F.CanonicalVelocity=HasAuthority()?(Body->IsSimulatingPhysics()?Body->GetPhysicsLinearVelocity():GetVelocity()):GetReplicatedMovement().LinearVelocity;
    F.bHasCanonicalPhysicsSample=HasAuthority() && Body->IsSimulatingPhysics();
    if(HasAuthority()) {F.SourceTime=HazardNow();F.bHasSourceTime=true;}
    return F;
}
FMCActorPresentationFrame AMCFoodActor::GetPresentationFrame() const
{
    const auto S=GetSimulationFrame();FMCActorPresentationFrame F;
    F.LoadEpoch=S.LoadEpoch;F.OwnerRevision=S.OwnerRevision;F.Owner=S.Owner;F.GameplayActor=S.GameplayActor;
    F.PresentedActor=PresentationRoot?PresentationRoot->GetComponentTransform():GetActorTransform();
    F.PresentationTime=GetWorld()?GetWorld()->GetTimeSeconds():0;F.FrameSerial=FoodPresentationSerial;return F;
}
FTransform AMCFoodActor::GetPresentedActorTransform() const {return GetPresentationFrame().PresentedActor;}
FTransform AMCFoodActor::GetCanonicalModelTransform() const {return CanonicalModelLocal*GetActorTransform();}
FBox AMCFoodActor::GetCanonicalModelBounds() const
{
    return Visual && Visual->GetStaticMesh()?Visual->GetStaticMesh()->GetBoundingBox().TransformBy(GetCanonicalModelTransform()):Body->Bounds.GetBox();
}
bool AMCFoodActor::FindPresentedGripSurface(FVector From,FHitResult& Hit) const
{
    const FTransform Canonical=GetCanonicalModelTransform(),Visible=Visual->GetComponentTransform();
    if(!FindGripSurface(Canonical.TransformPosition(Visible.InverseTransformPosition(From)),Hit)) return false;
    const FVector Local=Canonical.InverseTransformPosition(Hit.ImpactPoint);
    // Transform a covector through the two model frames, including squash and
    // nonuniform authored scale, without moving the canonical query component.
    const FVector ModelNormal(Canonical.ToMatrixWithScale().GetTransposed().TransformVector(Hit.ImpactNormal));
    const FVector WorldNormal(Visible.ToMatrixWithScale().Inverse().GetTransposed().TransformVector(ModelNormal));
    Hit.ImpactPoint=Hit.Location=Visible.TransformPosition(Local);Hit.ImpactNormal=Hit.Normal=WorldNormal.GetSafeNormal();
    return !Hit.ImpactPoint.ContainsNaN() && !Hit.ImpactNormal.IsNearlyZero();
}
void AMCFoodActor::ApplyCanonicalProxyPose(FVector Location,FQuat Rotation)
{
    const FTransform Visible=PresentationRoot->GetComponentTransform();
    SetActorLocationAndRotation(Location,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
    if(bHasRenderedFoodPose) PresentationRoot->SetWorldTransform(Visible,false,nullptr,ETeleportType::None);
}
void AMCFoodActor::UpdateCarryPresentation(float Dt) {UpdateFoodPresentation(Dt);}
void AMCFoodActor::UpdateFoodPresentation(float Dt)
{
    if(!PresentationRoot || !GetWorld()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(LastFoodPresentationTick==Now) return;
    LastFoodPresentationTick=Now;
    FTransform Goal=GetActorTransform();const auto& State=PresentedInteraction;
    AMCToothCharacter* Carrier=State.Carry.Holder.Get();
    const bool Carried=IsValid(Carrier) && (State.StackCarrier==Carrier || State.Phase==EMCFoodPhase::Carried);
    if(HasAuthority()) {
        // Listen-server native character smoothing belongs to presentation too.
        // Food's authoritative body and query frame remain at the simulation pose.
        RenderedFoodPose=Carried?Carrier->GetPresentationFrame().FromCanonicalWorld(Goal):Goal;
        PresentationRoot->SetWorldTransform(RenderedFoodPose,false,nullptr,ETeleportType::None);
        bHasRenderedFoodPose=true;++FoodPresentationSerial;return;
    }
    if(Carried) {
        const FTransform Parent=Carrier->GetPresentedActorTransform();
        Goal=FTransform(State.Carry.Rotation,FVector(State.Carry.Location))*Parent;
        // The cue is evaluated from the same accepted generation. No late old
        // relative packet can move a released item back into an earlier carrier.
        const auto& Cue=State.Pickup;
        const float Age=FMath::Max(0.,HazardNow()-Cue.StartedAt)*FMCStackPickup::PlayRate;
        if(State.StackCarrier && Cue.StartedAt>=0 && Age<.075f+Cue.FlightSeconds+.18f) {
            if(Age<.075f) Goal=FTransform(Cue.StartRotation,FVector(Cue.StartLocation),GetActorScale3D());
            else {
                const float T=FMath::Clamp((Age-.075f)/FMath::Max(.01f,Cue.FlightSeconds),0.f,1.f);
                const float Ease=T*T*T*(T*(T*6-15)+10);
                FVector P=FMath::Lerp(FVector(Cue.StartLocation),Goal.GetLocation(),Ease);
                P.Z+=FMath::Square(FMath::Sin(PI*T))*Cue.ArcHeight;
                FQuat Q=FQuat::Slerp(Cue.StartRotation.Quaternion(),Goal.GetRotation(),Ease)*FRotator(-FMath::Sin(PI*T)*10,0,FMath::Sin(2*PI*T)*4).Quaternion();
                if(T>=1) {const float Settle=Age-.075f-Cue.FlightSeconds;const float E=FMath::Exp(-22*Settle)*(1-FMath::SmoothStep(.1f,.18f,Settle));P.Z+=FMath::Sin(26*Settle)*8*E;Q=Q*FRotator(FMath::Sin(30*Settle)*5*E,0,0).Quaternion();}
                Goal=FTransform(Q,P,GetActorScale3D());
            }
            RenderedFoodPose=Goal;
            PresentationCarrier=Carrier;SmoothedCarryRelative=Goal.GetRelativeTransform(Parent);
        } else {
            // Filter relative server physics, not the predicted carrier's motion.
            if(PresentationCarrier.Get()!=Carrier) {PresentationCarrier=Carrier;SmoothedCarryRelative=PresentationRoot->GetComponentTransform().GetRelativeTransform(Parent);}
            const float A=1-FMath::Exp(-25.f*FMath::Max(0.f,Dt));
            SmoothedCarryRelative.SetLocation(FMath::Lerp(SmoothedCarryRelative.GetLocation(),FVector(State.Carry.Location),A));
            SmoothedCarryRelative.SetRotation(FQuat::Slerp(SmoothedCarryRelative.GetRotation(),State.Carry.Rotation.Quaternion(),A));
            RenderedFoodPose=SmoothedCarryRelative*Parent;RenderedFoodPose.SetScale3D(GetActorScale3D());
        }
    } else {
        PresentationCarrier.Reset();
        if(!bHasRenderedFoodPose || State.Phase==EMCFoodPhase::Stuck || FVector::DistSquared(RenderedFoodPose.GetLocation(),Goal.GetLocation())>FMath::Square(600.f)) RenderedFoodPose=Goal;
        else {FTransform Blended;Blended.Blend(RenderedFoodPose,Goal,1-FMath::Exp(-25.f*FMath::Max(0.f,Dt)));RenderedFoodPose=Blended;}
    }
    PresentationRoot->SetWorldTransform(RenderedFoodPose,false,nullptr,ETeleportType::None);
    bHasRenderedFoodPose=true;++FoodPresentationSerial;
}
