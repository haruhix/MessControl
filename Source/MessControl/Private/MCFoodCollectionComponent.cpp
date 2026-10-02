#include "MCFoodCollectionComponent.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

UMCFoodCollectionComponent::UMCFoodCollectionComponent()
{
    SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
FVector UMCFoodCollectionComponent::HandPoint() const
{
    const auto* H=Cast<AMCToothCharacter>(GetOwner());
    return H?H->GetActorLocation()+H->GetActorForwardVector()*73+H->GetActorRightVector()*12+FVector(0,0,20):FVector::ZeroVector;
}
bool UMCFoodCollectionComponent::Contains(const AMCFoodActor* Food) const { return Pieces.Contains(Food); }
bool UMCFoodCollectionComponent::IsSettlingRelease(const AMCFoodActor* Food) const
{
    const double* At=DroppedAt.Find(Food);
    return At && GetWorld()->GetTimeSeconds()-*At<.75;
}
bool UMCFoodCollectionComponent::CanCollect(const AMCFoodActor* F) const
{
    const auto* H=Cast<AMCToothCharacter>(GetOwner());
    if(!H || !IsValid(F) || !H->CanWork() || F->UsesLegacyGrip() || F->StackCarrier || F->IsDisposed()
        || (F->Phase!=EMCFoodPhase::Free && F->Phase!=EMCFoodPhase::Falling) || !F->Holders.IsEmpty()) return false;
    // Large whole vegetables must be cut first. No shrinking of their physics body at pickup.
    if(F->Body->GetScaledBoxExtent().GetMax()>55 || F->Visual->Bounds.SphereRadius>85) return false;
    const double* Drop=DroppedAt.Find(F); if(Drop && GetWorld()->GetTimeSeconds()-*Drop<2) return false;
    if(FVector::DistSquared(F->GetActorLocation(),H->GetActorLocation())>FMath::Square(CollectionReach)) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(MCCollectFood),false,H); Q.AddIgnoredActor(F);
    for(const auto& Piece:Pieces) Q.AddIgnoredActor(Piece);
    return !GetWorld()->LineTraceSingleByChannel(Hit,H->GetActorLocation(),F->GetActorLocation(),ECC_Visibility,Q);
}
bool UMCFoodCollectionComponent::HasCandidate() const
{
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(CanCollect(*It)) return true;
    return false;
}
void UMCFoodCollectionComponent::Toggle()
{
    if(!GetOwner()->HasAuthority()) return;
    if(bCollecting) {Stop();return;}
    bCollecting=true; bHasHand=false; NextCollectAt=0; GetOwner()->ForceNetUpdate();
}
bool UMCFoodCollectionComponent::Collect(AMCFoodActor* Food)
{
    if(!GetOwner()->HasAuthority() || !bCollecting || Pieces.Num()>=FMath::Clamp(MaxPieces,1,8) || !CanCollect(Food)) return false;
    FVector Goal=HandPoint();
    // Place above the current physical top, including its spring motion and tilt.
    // Summing ideal heights could put a new piece inside a still-moving stack.
    if(!Pieces.IsEmpty()) Goal.Z=Pieces.Last()->Body->Bounds.GetBox().Max.Z+3;
    Goal.Z+=Food->Body->GetScaledBoxExtent().Z;
    // Reject occupied placement volumes instead of teleporting a piece through a wall.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCStackPlacement),false,GetOwner()); Q.AddIgnoredActor(Food);
    for(const auto& Piece:Pieces) Q.AddIgnoredActor(Piece);
    if(GetWorld()->OverlapBlockingTestByChannel(Goal,FQuat::Identity,ECC_PhysicsBody,FCollisionShape::MakeBox(Food->Body->GetScaledBoxExtent()*.9),Q)) return false;
    Food->SetStackCarrier(Cast<AMCToothCharacter>(GetOwner()));
    Food->SetActorLocationAndRotation(Goal,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
    Food->Body->SetPhysicsLinearVelocity(GetOwner()->GetVelocity()); Food->Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Pieces.Add(Food); PlacedAt.Add(Food,GetWorld()->GetTimeSeconds()); Food->AttendFood(); GetOwner()->ForceNetUpdate(); return true;
}
void UMCFoodCollectionComponent::ReleaseFrom(int32 Index,bool Throw)
{
    auto* H=Cast<AMCToothCharacter>(GetOwner());
    for(int32 I=Pieces.Num()-1;I>=Index;--I) {
        if(auto* F=Pieces[I].Get();IsValid(F)) {
            F->SetStackCarrier(nullptr); DroppedAt.Add(F,GetWorld()->GetTimeSeconds()); PlacedAt.Remove(F);
            if(Throw) F->Body->AddImpulse(H->GetActorForwardVector()*500+FVector(0,0,180),NAME_None,true);
        }
        Pieces.RemoveAt(I);
    }
    GetOwner()->ForceNetUpdate();
}
void UMCFoodCollectionComponent::Stop(bool Throw)
{
    if(!GetOwner()->HasAuthority()) return;
    ReleaseFrom(0,Throw); bCollecting=false; bHasHand=false; GetOwner()->ForceNetUpdate();
}
void UMCFoodCollectionComponent::EndPlay(const EEndPlayReason::Type Reason) {Stop();Super::EndPlay(Reason);}
void UMCFoodCollectionComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    auto* H=Cast<AMCToothCharacter>(GetOwner()); if(!H || !H->HasAuthority()) return;
    const auto* Move=Cast<UMCToothMovementComponent>(H->GetCharacterMovement());
    if(!H->CanWork() || H->bInCoffee || Move->IsClimbing()) {if(bCollecting || Pieces.Num()) Stop();return;}
    if(!bCollecting) return;
    const FVector Hand=HandPoint(); const double Now=GetWorld()->GetTimeSeconds();
    const FVector HandVelocity=bHasHand?(Hand-PreviousHand)/FMath::Max(Dt,.001f):H->GetVelocity();
    PreviousHand=Hand; bHasHand=true;
    for(int32 I=0;I<Pieces.Num();++I) {
        auto* F=Pieces[I].Get();
        if(!IsValid(F) || F->IsDisposed() || F->StackCarrier!=H) {ReleaseFrom(I);break;}
        const auto* Below=I>0?Pieces[I-1].Get():nullptr;
        const FVector Support=Below?Below->GetActorLocation()+FVector(0,0,Below->Body->Bounds.BoxExtent.Z):Hand;
        const float Width=Below?FMath::Min(Below->Body->Bounds.BoxExtent.X,F->Body->Bounds.BoxExtent.X):65;
        const FVector Offset=F->GetActorLocation()-Support;
        const float Age=Now-PlacedAt.FindRef(F);
        if(Age>.65f && (Offset.Size2D()>FMath::Max(18.f,Width*.85f) || Offset.Z<-F->Body->Bounds.BoxExtent.Z || F->GetActorUpVector().Z<.55f)) {
            FallenPieces+=Pieces.Num()-I; ReleaseFrom(I);break;
        }
        if(I==0) {
            const FVector Goal=Hand+FVector(0,0,F->Body->GetScaledBoxExtent().Z);
            const FVector Acceleration=((Goal-F->GetActorLocation())*150+(HandVelocity-F->Body->GetPhysicsLinearVelocity())*23).GetClampedToMaxSize(9000);
            float LoadMass=0;for(const auto& Piece:Pieces) if(IsValid(Piece)) LoadMass+=Piece->Body->GetMass();
            F->Body->AddForce(Acceleration*F->Body->GetMass()+FVector(0,0,-GetWorld()->GetGravityZ()*LoadMass));
            F->Body->AddTorqueInRadians((FVector::CrossProduct(F->GetActorUpVector(),FVector::UpVector)*18000-F->Body->GetPhysicsAngularVelocityInRadians()*2200)*F->Body->GetMass());
        }
        // Upper bodies have no hand force, position servo or attachment. Contact, gravity,
        // friction and inertia alone determine whether they remain on the lower piece.
    }
    if(Now>=NextCollectAt && Pieces.Num()<FMath::Clamp(MaxPieces,1,8)) {
        for(auto It=DroppedAt.CreateIterator();It;++It) if(!It.Key().IsValid() || Now-It.Value()>2) It.RemoveCurrent();
        NextCollectAt=Now+.55;
        AMCFoodActor* Best=nullptr; double Distance=DBL_MAX;
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(CanCollect(*It)) {
            const double D=FVector::DistSquared(It->GetActorLocation(),H->GetActorLocation()); if(D<Distance) {Best=*It;Distance=D;}
        }
        if(Best) Collect(Best);
    }
}
void UMCFoodCollectionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UMCFoodCollectionComponent,bCollecting);DOREPLIFETIME(UMCFoodCollectionComponent,Pieces);}
