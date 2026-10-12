#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCReactionVFX.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AMCFoodActor* AMCFoodActor::FindNearbySpoiledFood(UWorld* World,FVector Point,float Radius)
{
    if(!World || Point.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<=0) return nullptr;
    AMCFoodActor* Closest=nullptr;double Distance=FMath::Square(Radius);
    for(TActorIterator<AMCFoodActor> It(World);It;++It) {
        if(!It->bSpoiled || It->IsDisposed() || It->IsActorBeingDestroyed() || It->bBrushTool) continue;
        const double Candidate=FVector::DistSquared(Point,It->Visual->Bounds.Origin);
        if(Candidate<Distance) {Distance=Candidate;Closest=*It;}
    }
    return Closest;
}

void AMCFoodActor::UpdateSmell()
{
    if(!HasAuthority()) return;
    const bool Smelly=bSpoiled && !bBrushTool && FoodData.Kind==EMCFoodKind::Food && !IsDisposed();
    if(!Smelly) {
        if(IsValid(SmellVFX)) {SmellVFX->Destroy();SmellVFX=nullptr;}
        return;
    }
    if(!IsValid(SmellVFX)) {
        SmellVFX=AMCReactionVFX::Spawn(GetWorld(),Visual->Bounds.Origin,EMCReactionEffect::SpoiledSmell,0,
            FMath::Clamp(float(Visual->Bounds.SphereRadius)*.75f,40.f,150.f));
        if(IsValid(SmellVFX)) {
            SmellVFX->SetOwner(this);
            SmellVFX->AttachToComponent(Visual,FAttachmentTransformRules::KeepWorldTransform);
        }
    }
}

bool AMCFoodActor::SpawnFragments(FVector Direction)
{
    if(!HasAuthority() || bFragment || bBrushTool || FoodData.Kind!=EMCFoodKind::Food) return false;
    FRandomStream Random(HashCombine(GetTypeHash(GetActorLocation()),GetTypeHash(GetWorld()->GetTimeSeconds())));
    int32 Created=0;
    const FVector Center=GetActorLocation();
    const float Scatter=FMath::Clamp(float(Visual->Bounds.SphereRadius)*.3f,25.f,70.f);
    for(int32 I=0;I<FMath::Clamp(FoodData.Fragments,2,5);++I) {
        const float Angle=I*PI*2/FMath::Clamp(FoodData.Fragments,2,5);
        const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FTransform Pose(FRotator(0,Random.FRandRange(-180.f,180.f),0),Center+Ray*Scatter+FVector(0,0,20));
        auto* Piece=GetWorld()->SpawnActorDeferred<AMCFoodActor>(StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Piece) continue;
        Piece->ConfigureItem(ItemName,FoodData,Random,true);
        Piece->Batch=Batch;Piece->LastHandledBy=LastHandledBy;
        Piece->bSpoiled=bSpoiled;Piece->SpoilAt=SpoilAt;
        Piece->EntryRecoveryLanding=Center;
        Piece->FinishSpawning(Pose);
        Piece->Body->SetPhysicsLinearVelocity(Ray*90+Direction.GetSafeNormal()*35+FVector(0,0,110));
        Piece->ForceNetUpdate();++Created;
    }
    return Created>0;
}

void AMCFoodActor::RecoverUnreachableFood(float Dt)
{
    if(!HasAuthority() || IsDisposed() || bBrushTool || StackCarrier || EquippedBy || !Holders.IsEmpty()
        || Phase==EMCFoodPhase::Swallowing || Phase==EMCFoodPhase::Stuck || IsMouthEntryActive()) return;
    AccessCheckElapsed+=Dt;
    if(AccessCheckElapsed<.75f) return;
    const float Interval=AccessCheckElapsed;AccessCheckElapsed=0;
    // Wait for the throw to finish. A piece already below the arena can recover
    // immediately; otherwise fast airborne food keeps its real trajectory.
    const bool BelowArena=GetActorLocation().Z<OutOfArenaZ();
    if(!BelowArena && Body->GetPhysicsLinearVelocity().SizeSquared()>FMath::Square(80.f)) {UnreachableSeconds=0;return;}
    for(TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It)
        if(It->ContainsDeliveryPosition(GetActorLocation())) {UnreachableSeconds=0;return;}
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {Tongue=*It;break;}
    if(!Tongue) return;
    FHitResult Floor;
    const float Margin=FMath::Max(35.f,float(Body->GetScaledBoxExtent().Size2D())+15);
    const bool Accessible=Tongue->GameplaySpawnFootprint(GetActorLocation(),Margin,Floor)
        && FMath::Abs(GetActorLocation().Z-Floor.ImpactPoint.Z)<Body->Bounds.BoxExtent.Z+100;
    if(Accessible) {LastAccessiblePosition=Floor.ImpactPoint;UnreachableSeconds=0;return;}
    UnreachableSeconds=BelowArena?2.25f:UnreachableSeconds+Interval;
    if(UnreachableSeconds<2.25f) return;
    bool Found=false;
    for(const auto& Point:{LastAccessiblePosition,EntryRecoveryLanding})
        if(Point.IsSet() && Tongue->GameplaySpawnFootprint(Point.GetValue(),Margin,Floor)) {Found=true;break;}
    if(!Found) {
        FRandomStream Random(HashCombine(GetTypeHash(GetActorLocation()),GetTypeHash(GetWorld()->GetTimeSeconds())));
        Found=Tongue->RandomGameplaySpawnPoint(Random,Margin,Margin*2,TConstArrayView<FVector>(),Floor);
    }
    if(!Found) return;
    EndMouthEntry();
    SetActorLocation(Floor.ImpactPoint+FVector(0,0,Body->Bounds.BoxExtent.Z+8),false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    LastAccessiblePosition=Floor.ImpactPoint;UnreachableSeconds=0;ForceNetUpdate();
}
