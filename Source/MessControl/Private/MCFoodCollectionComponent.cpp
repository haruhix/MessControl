#include "MCFoodCollectionComponent.h"
#include "MCFoodActor.h"
#include "MCFoodStackSettings.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Net/UnrealNetwork.h"

UMCFoodCollectionComponent::UMCFoodCollectionComponent()
{
    SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
const FMCFoodStackSettings* UMCFoodCollectionComponent::LayoutSettings() const
{
    for(const auto& Food:Pieces) if(IsValid(Food)) return &Food->FoodData.Stack;
    static const FMCFoodStackSettings Defaults;
    return &Defaults;
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
float UMCFoodCollectionComponent::ContactThreshold() const
{ return FMath::IsFinite(SpillContactImpulse)?FMath::Max(1.f,SpillContactImpulse):600.f; }
bool UMCFoodCollectionComponent::IsLoosePileFood(const AActor* Actor) const
{
    const auto* F=Cast<AMCFoodActor>(Actor);
    return IsValid(F) && !F->UsesLegacyGrip() && !F->StackCarrier && F->Holders.IsEmpty()
        && (F->Phase==EMCFoodPhase::Free || F->Phase==EMCFoodPhase::Falling)
        && F->Body->GetScaledBoxExtent().GetMax()<=55
        && F->Body->GetPhysicsLinearVelocity().Size()*F->Settings.Mass<ContactThreshold();
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
    // Nearby loose ingredients can hide one another in a heap. They do not
    // obstruct pickup visibility; solid scenery and active hazards still do.
    for(int32 I=0;I<64;++I) {
        if(!GetWorld()->LineTraceSingleByChannel(Hit,H->GetActorLocation(),F->GetActorLocation(),ECC_Visibility,Q)) return true;
        if(!IsLoosePileFood(Hit.GetActor())) return false;
        Q.AddIgnoredActor(Hit.GetActor());
    }
    return false;
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
FTransform UMCFoodCollectionComponent::StackPose(float SlotHeight) const
{ const FQuat Rotation=StackRotation();return FTransform(Rotation,HandPoint()+Rotation.RotateVector(FVector(0,0,SlotHeight))); }
FTransform UMCFoodCollectionComponent::StackPose(const AMCFoodActor* Food) const
{
    if(!Food) return StackPose(0.f);
    const FQuat Rotation=StackRotation();
    return FTransform(Food->StackRestRotation(Rotation),HandPoint()+Rotation.RotateVector(FVector(Food->StackPickup.SlotOffset)+FVector(0,0,Food->StackPickup.SlotHeight)));
}
void UMCFoodCollectionComponent::RebuildStackLayout()
{
    float Height=0;
    for(int32 I=0;I<Pieces.Num();++I) if(auto* Food=Pieces[I].Get();IsValid(Food)) {
        const float Extent=Food->PrepareHorizontalStackPose(&Food->FoodData.Stack,I);
        Height+=Extent;Food->StackPickup.SlotHeight=Height;Height+=Extent+Food->FoodData.Stack.SafeLayerGap();
    }
}
bool UMCFoodCollectionComponent::Collect(AMCFoodActor* Food)
{
    if(!GetOwner()->HasAuthority() || !bCollecting || Pieces.Num()>=FMath::Clamp(MaxPieces,1,8) || !CanCollect(Food)) return false;
    RebuildStackLayout();
    float Height=Food->PrepareHorizontalStackPose(&Food->FoodData.Stack,Pieces.Num());
    for(const auto& Piece:Pieces) if(IsValid(Piece)) Height+=Piece->StackHalfHeight()*2+Piece->FoodData.Stack.SafeLayerGap();
    Food->StackPickup.SlotHeight=Height;
    const FTransform Pose=StackPose(Food);
    const FQuat Rotation=Pose.GetRotation();const FVector Goal=Pose.GetLocation();
    // Reject occupied placement volumes instead of teleporting a piece through a wall.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCStackPlacement),false,GetOwner()); Q.AddIgnoredActor(Food);
    for(const auto& Piece:Pieces) Q.AddIgnoredActor(Piece);
    TArray<FOverlapResult> Occupants;
    GetWorld()->OverlapMultiByChannel(Occupants,Goal,Rotation,ECC_PhysicsBody,FCollisionShape::MakeBox(Food->Body->GetScaledBoxExtent()*.9),Q);
    for(const auto& Occupant:Occupants) if(Occupant.bBlockingHit && !IsLoosePileFood(Occupant.GetActor())) return false;
    Food->BeginStackPickup(Cast<AMCToothCharacter>(GetOwner()),Height);
    for(const auto& Piece:Pieces) if(IsValid(Piece)) {
        Food->Body->IgnoreActorWhenMoving(Piece,true); Piece->Body->IgnoreActorWhenMoving(Food,true);
    }
    Pieces.Add(Food); PieceMotion.FindOrAdd(Food).Linear=GetOwner()->GetVelocity();
    Food->AttendFood(); GetOwner()->ForceNetUpdate(); return true;
}
bool UMCFoodCollectionComponent::DetachForDelivery(AMCFoodActor* Food)
{
    if(!GetOwner()->HasAuthority() || !IsValid(Food) || Food->Phase!=EMCFoodPhase::Swallowing || !Pieces.Contains(Food)) return false;
    for(const auto& Piece:Pieces) if(IsValid(Piece) && Piece!=Food) {
        Food->Body->IgnoreActorWhenMoving(Piece,false);Piece->Body->IgnoreActorWhenMoving(Food,false);
    }
    Pieces.Remove(Food);PieceMotion.Remove(Food);DroppedAt.Remove(Food);
    Food->SetStackCarrier(nullptr); // Swallowing is already set: no transient return to Chaos.
    RebuildStackLayout();
    if(Pieces.IsEmpty()) {
        bCollecting=false;bHasHand=false;SwayAngle=SwayVelocity=FVector2D::ZeroVector;NextCollectAt=0;
    }
    GetOwner()->ForceNetUpdate();return true;
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
            F->ProtectPlayersOnStackRelease(Throw);
            F->SetStackCarrier(nullptr); DroppedAt.Add(F,GetWorld()->GetTimeSeconds()); PieceMotion.Remove(F);
            if(F->Body->IsSimulatingPhysics()) {
                // The pickup arc is an animation, not stored projectile energy.
                // Ordinary drops inherit the carrier's motion; Q still throws forwards.
                F->Body->SetPhysicsLinearVelocity((Throw?Motion.Linear:H->GetVelocity())+Impulse);
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
    UE_LOG(LogTemp,Log,TEXT("MC_STACK_SPILL carrier=%s pieces=%d impulse=%s"),*GetNameSafe(GetOwner()),Pieces.Num(),*Impulse.ToString());
    FallenPieces+=Pieces.Num(); ReleaseFrom(0,false,Impulse.GetClampedToMaxSize(600));
    bCollecting=false; bHasHand=false; GetOwner()->ForceNetUpdate();
}
float UMCFoodCollectionComponent::ContactStrength(float HeldMass,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,FVector Normal) const
{
    if(!IsValid(Other) || Impulse.ContainsNaN() || Normal.ContainsNaN()) return 0;
    Normal=Normal.GetSafeNormal();if(Normal.IsNearlyZero()) return 0;
    const bool Simulating=OtherComponent && OtherComponent->IsSimulatingPhysics();
    const FVector OtherVelocity=Simulating?OtherComponent->GetPhysicsLinearVelocityAtPoint(OtherComponent->GetComponentLocation()):Other->GetVelocity();
    // Exclude our scripted hop/sway velocity. Only an external incoming body or
    // the player's motion into a solid obstacle produces an impact.
    const float ClosingSpeed=FMath::Max(0.f,float(FMath::Max(-FVector::DotProduct(GetOwner()->GetVelocity()-OtherVelocity,Normal),
        -FVector::DotProduct(PreviousCarrierVelocity-OtherVelocity,Normal))));
    const float Mass=Simulating?FMath::Max(.01f,OtherComponent->GetMass()):FMath::Max(.01f,HeldMass);
    return FMath::Max(float(FMath::Abs(FVector::DotProduct(Impulse,Normal))),Mass*ClosingSpeed);
}
void UMCFoodCollectionComponent::HandleCarrierCollision(AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit)
{
    if(!GetOwner()->HasAuthority() || Pieces.IsEmpty() || !IsValid(Other) || Other==GetOwner()) return;
    if(auto* Player=Cast<AMCToothCharacter>(Other)) {SpillOnPlayerContact(Player,Hit.ImpactNormal,Hit.ImpactPoint);return;}
    if(Hit.ImpactNormal.Z>.55f) return;
    if(const auto* Food=Cast<AMCFoodActor>(Other); Food && (Contains(Food) || IsSettlingRelease(Food))) return;
    const auto* H=Cast<AMCToothCharacter>(GetOwner());
    const float Strength=ContactStrength(H->ToothPhysics->Settings.Mass,Other,OtherComponent,Impulse,Hit.ImpactNormal);
    if(Strength>=ContactThreshold()) {
        UE_LOG(LogTemp,Log,TEXT("MC_STACK_CARRIER_CONTACT carrier=%s other=%s impulse=%.1f threshold=%.1f"),*GetNameSafe(H),*GetNameSafe(Other),Strength,ContactThreshold());
        Spill(Hit.ImpactNormal*120+FVector(0,0,45));
    }
}
void UMCFoodCollectionComponent::HandleStackCollision(AMCFoodActor* Food,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit)
{
    if(!GetOwner()->HasAuthority() || !Contains(Food) || !IsValid(Other) || Other==GetOwner()) return;
    if(auto* Player=Cast<AMCToothCharacter>(Other)) {SpillOnPlayerContact(Player,Hit.ImpactNormal,Hit.ImpactPoint);return;}
    if(const auto* Piece=Cast<AMCFoodActor>(Other); Piece && (Contains(Piece) || IsSettlingRelease(Piece))) return;
    const float Strength=ContactStrength(Food->Settings.Mass,Other,OtherComponent,Impulse,Hit.ImpactNormal);
    if(Strength>=ContactThreshold()) {
        UE_LOG(LogTemp,Log,TEXT("MC_STACK_CONTACT carrier=%s food=%s other=%s impulse=%.1f threshold=%.1f hop=%d normal=%s"),*GetNameSafe(GetOwner()),*GetNameSafe(Food),*GetNameSafe(Other),Strength,ContactThreshold(),Food->IsStackPickupActive(),*Hit.ImpactNormal.ToString());
        Spill(Hit.ImpactNormal*160+FVector(0,0,55));
    }
}
void UMCFoodCollectionComponent::SpillOnPlayerContact(AMCToothCharacter* Other,FVector Normal,FVector ContactPoint)
{
    auto* Carrier=CastChecked<AMCToothCharacter>(GetOwner());
    FVector Direction=Normal.GetSafeNormal2D();
    if(Direction.IsNearlyZero()) Direction=(Carrier->GetActorLocation()-Other->GetActorLocation()).GetSafeNormal2D();
    if(Direction.IsNearlyZero()) Direction=-Carrier->GetActorForwardVector();
    // Empty the load first: ApplyHit also spills and can synchronously change collision.
    Spill(Direction*120+FVector(0,0,45));
    Carrier->ToothPhysics->ApplyHit(Direction*Carrier->ToothPhysics->Settings.Knockback+FVector(0,0,Carrier->ToothPhysics->Settings.Lift),ContactPoint);
}
void UMCFoodCollectionComponent::CheckIncomingContacts(AMCFoodActor* Food)
{
    // Cheap broad phase, then exact authored shapes only for strong candidates.
    // PhysicsBody overlap response prevents pickup animation from pushing a heap.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCStackIncomingContact),false,GetOwner());
    for(const auto& Piece:Pieces) Q.AddIgnoredActor(Piece);
    // Only query actor bounds here. Hundreds of authored hulls are evaluated
    // by the exact probe only after an incoming body passes the impulse gate.
    Q.bSkipNarrowPhase=true;
    FCollisionQueryParams ExactQ=Q;ExactQ.bSkipNarrowPhase=false;
    FCollisionObjectQueryParams Objects(ECC_PhysicsBody);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FOverlapResult> Contacts;
    GetWorld()->OverlapMultiByObjectType(Contacts,Food->GetActorLocation(),Food->GetActorQuat(),Objects,
        FCollisionShape::MakeBox(Food->Body->GetScaledBoxExtent()),Q);
    for(const auto& Contact:Contacts) {
        auto* Other=Contact.GetActor();auto* Body=Contact.GetComponent();
        if(!IsValid(Other) || !Body || !Body->IsSimulatingPhysics()) continue;
        if(const auto* F=Cast<AMCFoodActor>(Other); F && IsSettlingRelease(F)) continue;
        const FVector Normal=(Food->GetActorLocation()-Body->GetComponentLocation()).GetSafeNormal();
        if(ContactStrength(Food->Settings.Mass,Other,Body,FVector::ZeroVector,Normal)<ContactThreshold()) continue;
        if(!Body->ComponentOverlapComponent(Food->Body,Food->GetActorLocation(),Food->GetActorQuat(),ExactQ)) continue;
        FHitResult Hit;Hit.ImpactNormal=Normal;
        HandleStackCollision(Food,Other,Body,FVector::ZeroVector,Hit);
        if(!bCollecting) return;
    }
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
        SwayAngle=(SwayAngle+SwayVelocity*Step).GetClampedToMaxSize(LayoutSettings()->SafeMaxSwayDegrees());
    }
    RebuildStackLayout();
    const auto HeldPieces=Pieces;
    for(int32 I=0;I<HeldPieces.Num();++I) {
        auto* F=HeldPieces[I].Get();
        if(!IsValid(F) || F->IsDisposed() || F->StackCarrier!=H) {ReleaseFrom(I);break;}
        const FTransform RestPose=StackPose(F);
        const FTransform Pose=F->StackPickupPose(RestPose);
        const FVector Goal=Pose.GetLocation();const FQuat PieceRotation=Pose.GetRotation();
        const FVector Previous=F->GetActorLocation(); const FQuat PreviousRotation=F->GetActorQuat();
        FHitResult Hit;
        F->SetActorLocationAndRotation(Goal,PieceRotation,true,&Hit,ETeleportType::TeleportPhysics);
        // A collision callback can release the stack during this swept move.
        if(!bCollecting || F->StackCarrier!=H) return;
        if(Hit.bBlockingHit && Hit.bStartPenetrating && Hit.ImpactNormal.Z>.55f && FVector::DotProduct(Goal-Previous,Hit.ImpactNormal)>0) {
            // A rotated piece can start slightly inside its supporting floor.
            // Permit a clean upward exit, after checking the actual target hulls.
            FComponentQueryParams Q(SCENE_QUERY_STAT(MCStackFloorExit),H);Q.AddIgnoredActor(F);
            for(const auto& Piece:Pieces) Q.AddIgnoredActor(Piece);
            FCollisionObjectQueryParams Objects(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
            TArray<FOverlapResult> Obstacles;
            GetWorld()->ComponentOverlapMulti(Obstacles,F->Body,Goal,PieceRotation,Q,Objects);
            if(Obstacles.IsEmpty()) {
                F->SetActorLocationAndRotation(Goal,PieceRotation,false,nullptr,ETeleportType::TeleportPhysics);
                Hit.bBlockingHit=false;
            }
        }
        if(Hit.bBlockingHit) {
            HandleStackCollision(F,Hit.GetActor(),Hit.GetComponent(),FVector::ZeroVector,Hit);
            if(!bCollecting) return;
            // A weak touch is a movement constraint, not an impact. A blocked
            // pickup is released at the contact point without spilling the load.
            if(F->IsStackPickupActive()) ReleaseFrom(I);
            return;
        }
        CheckIncomingContacts(F);if(!bCollecting || F->StackCarrier!=H) return;
        auto& Motion=PieceMotion.FindOrAdd(F);
        if(!Motion.bLanded && !F->IsStackPickupActive()) {
            Motion.bLanded=true;SwayVelocity+=FVector2D(7,I%2?-9:9);
        }
        Motion.Linear=((Goal-Previous)/FMath::Max(Dt,.001f)).GetClampedToMaxSize(1000);
        FQuat Delta=PieceRotation*PreviousRotation.Inverse(); Delta.EnforceShortestArcWith(FQuat::Identity);
        FVector Axis; double Angle; Delta.ToAxisAndAngle(Axis,Angle);
        Motion.Angular=(Axis*Angle/FMath::Max(Dt,.001f)).GetClampedToMaxSize(6);
    }
    if(Now>=NextCollectAt && Pieces.Num()<FMath::Clamp(MaxPieces,1,8)) {
        for(auto It=DroppedAt.CreateIterator();It;++It) if(!It.Key().IsValid() || Now-It.Value()>2) It.RemoveCurrent();
        NextCollectAt=Now+.55/FMCStackPickup::PlayRate;
        AMCFoodActor* Best=nullptr; double Distance=DBL_MAX;
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(CanCollect(*It)) {
            const double D=FVector::DistSquared(It->GetActorLocation(),H->GetActorLocation()); if(D<Distance) {Best=*It;Distance=D;}
        }
        if(Best) Collect(Best);
    }
}
void UMCFoodCollectionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UMCFoodCollectionComponent,bCollecting);DOREPLIFETIME(UMCFoodCollectionComponent,Pieces);}
