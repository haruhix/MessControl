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
    bCollecting=true; bHasHand=false; SwayAngle=SwayVelocity=FVector2D::ZeroVector; NextCollectAt=0; GetOwner()->ForceNetUpdate();
}
FQuat UMCFoodCollectionComponent::StackRotation() const
{ return GetOwner()->GetActorQuat()*FRotator(SwayAngle.X,0,SwayAngle.Y).Quaternion(); }
bool UMCFoodCollectionComponent::Collect(AMCFoodActor* Food)
{
    if(!GetOwner()->HasAuthority() || !bCollecting || Pieces.Num()>=FMath::Clamp(MaxPieces,1,8) || !CanCollect(Food)) return false;
    float Height=Food->Body->GetScaledBoxExtent().Z;
    for(const auto& Piece:Pieces) if(IsValid(Piece)) Height+=Piece->Body->GetScaledBoxExtent().Z*2+3;
    const FQuat Rotation=StackRotation();
    const FVector Goal=HandPoint()+Rotation.RotateVector(FVector(0,0,Height));
    // Reject occupied placement volumes instead of teleporting a piece through a wall.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCStackPlacement),false,GetOwner()); Q.AddIgnoredActor(Food);
    for(const auto& Piece:Pieces) Q.AddIgnoredActor(Piece);
    if(GetWorld()->OverlapBlockingTestByChannel(Goal,Rotation,ECC_PhysicsBody,FCollisionShape::MakeBox(Food->Body->GetScaledBoxExtent()*.9),Q)) return false;
    Food->SetStackCarrier(Cast<AMCToothCharacter>(GetOwner()));
    for(const auto& Piece:Pieces) if(IsValid(Piece)) {
        Food->Body->IgnoreActorWhenMoving(Piece,true); Piece->Body->IgnoreActorWhenMoving(Food,true);
    }
    Food->SetActorLocationAndRotation(Goal,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
    Pieces.Add(Food); PieceMotion.FindOrAdd(Food).Linear=GetOwner()->GetVelocity();
    Food->AttendFood(); GetOwner()->ForceNetUpdate(); return true;
}
void UMCFoodCollectionComponent::ReleaseFrom(int32 Index,bool Throw,FVector Impulse)
{
    auto* H=Cast<AMCToothCharacter>(GetOwner());
    for(int32 I=Pieces.Num()-1;I>=Index;--I) {
        if(auto* F=Pieces[I].Get();IsValid(F)) {
            const FPieceMotion Motion=PieceMotion.FindRef(F);
            for(const auto& Piece:Pieces) if(IsValid(Piece) && Piece!=F) {
                F->Body->IgnoreActorWhenMoving(Piece,false); Piece->Body->IgnoreActorWhenMoving(F,false);
            }
            F->SetStackCarrier(nullptr); DroppedAt.Add(F,GetWorld()->GetTimeSeconds()); PieceMotion.Remove(F);
            if(F->Body->IsSimulatingPhysics()) {
                F->Body->SetPhysicsLinearVelocity(Motion.Linear+Impulse);
                F->Body->SetPhysicsAngularVelocityInRadians(Motion.Angular);
                if(Throw) F->Body->AddImpulse(H->GetActorForwardVector()*500+FVector(0,0,180),NAME_None,true);
            }
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
void UMCFoodCollectionComponent::Spill(FVector Impulse)
{
    if(!GetOwner()->HasAuthority() || Pieces.IsEmpty() || Impulse.ContainsNaN()) return;
    FallenPieces+=Pieces.Num(); ReleaseFrom(0,false,Impulse.GetClampedToMaxSize(600));
    bCollecting=false; bHasHand=false; GetOwner()->ForceNetUpdate();
}
void UMCFoodCollectionComponent::HandleCarrierCollision(AActor* Other,const FHitResult& Hit)
{
    if(!GetOwner()->HasAuthority() || Pieces.IsEmpty() || !IsValid(Other) || Other==GetOwner() || Hit.ImpactNormal.Z>.55f) return;
    if(const auto* Food=Cast<AMCFoodActor>(Other); Food && (Contains(Food) || IsSettlingRelease(Food))) return;
    const float Speed=FMath::Max(-FVector::DotProduct(GetOwner()->GetVelocity()-Other->GetVelocity(),Hit.ImpactNormal),
        -FVector::DotProduct(PreviousCarrierVelocity-Other->GetVelocity(),Hit.ImpactNormal));
    if(Speed>=75) Spill(Hit.ImpactNormal*120+FVector(0,0,45));
}
void UMCFoodCollectionComponent::HandleStackCollision(AMCFoodActor* Food,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit)
{
    if(!GetOwner()->HasAuthority() || !Contains(Food) || !IsValid(Other) || Other==GetOwner()) return;
    if(const auto* Piece=Cast<AMCFoodActor>(Other); Piece && (Contains(Piece) || IsSettlingRelease(Piece))) return;
    const FVector OtherVelocity=OtherComponent && OtherComponent->IsSimulatingPhysics()?OtherComponent->GetPhysicsLinearVelocity():Other->GetVelocity();
    const float Speed=FMath::Max(-FVector::DotProduct(GetOwner()->GetVelocity()-OtherVelocity,Hit.ImpactNormal),
        float(Impulse.Size()/FMath::Max(1.f,Food->Settings.Mass)));
    if(Speed>=75) Spill(Hit.ImpactNormal*160+FVector(0,0,55));
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
    const FVector Acceleration=bHasHand?(HandVelocity-PreviousHandVelocity)/FMath::Max(Dt,.001f):FVector::ZeroVector;
    PreviousHand=Hand; PreviousHandVelocity=HandVelocity; PreviousCarrierVelocity=H->GetVelocity(); bHasHand=true;
    const float Walking=FMath::Clamp(float(H->GetVelocity().Size2D()/440),0.f,1.f);
    const FVector2D Target(
        FMath::Clamp(-FVector::DotProduct(Acceleration,H->GetActorForwardVector())*.004,-7.,7.)+FMath::Sin(Now*2.5)*.7,
        FMath::Clamp(FVector::DotProduct(Acceleration,H->GetActorRightVector())*.005,-10.,10.)+FMath::Sin(Now*4.6)*(1.2+Walking*3));
    // Bounded spring substeps keep a tall stack stable at different frame rates.
    for(float Left=FMath::Min(Dt,.1f);Left>SMALL_NUMBER;) {
        const float Step=FMath::Min(Left,1.f/120.f); Left-=Step;
        SwayVelocity+=((Target-SwayAngle)*70-SwayVelocity*12)*Step;
        SwayAngle=(SwayAngle+SwayVelocity*Step).GetClampedToMaxSize(12);
    }
    const FQuat Rotation=StackRotation(); float Height=0;
    const auto HeldPieces=Pieces;
    for(int32 I=0;I<HeldPieces.Num();++I) {
        auto* F=HeldPieces[I].Get();
        if(!IsValid(F) || F->IsDisposed() || F->StackCarrier!=H) {ReleaseFrom(I);break;}
        const float Extent=F->Body->GetScaledBoxExtent().Z;
        Height+=Extent;
        const FVector Goal=Hand+Rotation.RotateVector(FVector(0,0,Height)); Height+=Extent+3;
        const FVector Previous=F->GetActorLocation(); const FQuat PreviousRotation=F->GetActorQuat();
        FHitResult Hit;
        F->SetActorLocationAndRotation(Goal,Rotation,true,&Hit,ETeleportType::TeleportPhysics);
        // A collision callback can release the stack during this swept move.
        if(!bCollecting || F->StackCarrier!=H) return;
        if(Hit.bBlockingHit) {Spill(Hit.ImpactNormal*120+FVector(0,0,45));return;}
        auto& Motion=PieceMotion.FindOrAdd(F);
        Motion.Linear=((Goal-Previous)/FMath::Max(Dt,.001f)).GetClampedToMaxSize(1000);
        FQuat Delta=Rotation*PreviousRotation.Inverse(); Delta.EnforceShortestArcWith(FQuat::Identity);
        FVector Axis; double Angle; Delta.ToAxisAndAngle(Axis,Angle);
        Motion.Angular=(Axis*Angle/FMath::Max(Dt,.001f)).GetClampedToMaxSize(6);
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
