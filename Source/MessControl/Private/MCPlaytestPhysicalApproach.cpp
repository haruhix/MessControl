#include "MCPlaytestPhysicalApproach.h"
#include "MCToothCharacter.h"
#include "MCArenaTooth.h"
#include "MCFoodActor.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MCPlaytestPhysicalApproach
{
bool FindStandingPose(const AMCToothCharacter* Hero,FVector Candidate,FVector& PawnCenter)
{
    if(!IsValid(Hero) || Candidate.ContainsNaN()) return false;
    const auto* Capsule=Hero->GetCapsuleComponent();const auto* Movement=Hero->GetCharacterMovement();
    const float Half=Capsule->GetScaledCapsuleHalfHeight(),Radius=Capsule->GetScaledCapsuleRadius();
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCBotPhysicalStand),false,Hero);
    FHitResult Floor;
    if(!Hero->GetWorld()->LineTraceSingleByChannel(Floor,Candidate+FVector(0,0,Half+Movement->MaxStepHeight+40.f),
        Candidate-FVector(0,0,Half+Movement->MaxStepHeight+180.f),Capsule->GetCollisionObjectType(),Query,Responses)
        || !Floor.IsValidBlockingHit() || !Movement->IsWalkable(Floor) || !Floor.GetComponent()
        || Floor.GetComponent()->IsSimulatingPhysics() || Cast<AMCArenaTooth>(Floor.GetActor())
        || Cast<APawn>(Floor.GetActor()) || Cast<AMCFoodActor>(Floor.GetActor())) return false;
    // On a walkable incline the bottom hemisphere rests above the centre-ray
    // floor height. Predict the real capsule contact rather than placing its
    // centre at rayZ+Half, which would falsely overlap an ordinary slope.
    FHitResult CapsuleFloor;
    const FVector Base(Candidate.X,Candidate.Y,Floor.ImpactPoint.Z+Half);
    if(!Hero->GetWorld()->SweepSingleByChannel(CapsuleFloor,Base+FVector(0,0,Movement->MaxStepHeight+20.f),
        Base-FVector(0,0,Movement->MaxStepHeight+20.f),FQuat::Identity,Capsule->GetCollisionObjectType(),
        FCollisionShape::MakeCapsule(Radius+1.f,Half),Query,Responses)
        || !CapsuleFloor.IsValidBlockingHit() || !Movement->IsWalkable(CapsuleFloor) || !CapsuleFloor.GetComponent()
        || CapsuleFloor.GetComponent()->IsSimulatingPhysics() || Cast<AMCArenaTooth>(CapsuleFloor.GetActor())
        || Cast<APawn>(CapsuleFloor.GetActor()) || Cast<AMCFoodActor>(CapsuleFloor.GetActor())) return false;
    PawnCenter=CapsuleFloor.Location+FVector(0,0,4.f);
    // Require support under the capsule footprint, not just a centre ray across
    // a gap. The native CMC still owns its actual floor/step/slide simulation.
    for(int32 I=0;I<8;++I) {
        const float Angle=I*PI/4.f;
        const FVector Probe=Floor.ImpactPoint+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*(Radius*.8f);
        FHitResult Rim;
        if(!Hero->GetWorld()->LineTraceSingleByChannel(Rim,Probe+FVector(0,0,Movement->MaxStepHeight+12.f),
            Probe-FVector(0,0,Movement->MaxStepHeight+12.f),Capsule->GetCollisionObjectType(),Query,Responses)
            || !Rim.IsValidBlockingHit() || !Movement->IsWalkable(Rim) || !Rim.GetComponent()
            || Rim.GetComponent()->IsSimulatingPhysics() || Cast<AMCArenaTooth>(Rim.GetActor()) || Cast<APawn>(Rim.GetActor())
            || Cast<AMCFoodActor>(Rim.GetActor()) || FMath::Abs(Rim.ImpactPoint.Z-Floor.ImpactPoint.Z)>Movement->MaxStepHeight) return false;
    }
    return !Hero->GetWorld()->OverlapBlockingTestByChannel(PawnCenter,FQuat::Identity,Capsule->GetCollisionObjectType(),
        FCollisionShape::MakeCapsule(Radius+1.f,Half),Query,Responses);
}

bool CanWalkSegment(const AMCToothCharacter* Hero,FVector From,FVector To)
{
    if(!IsValid(Hero) || From.ContainsNaN() || To.ContainsNaN() || FVector::DistSquared(From,To)>FMath::Square(MaxDistance)) return false;
    const auto* Capsule=Hero->GetCapsuleComponent();const auto* Movement=Hero->GetCharacterMovement();
    FVector Previous;
    if(!FindStandingPose(Hero,From,Previous) || FMath::Abs(Previous.Z-From.Z)>15.f) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCBotPhysicalWalk),false,Hero);
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    const auto Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()+1.f,Capsule->GetScaledCapsuleHalfHeight());
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(From,To)/25.f));
    for(int32 I=1;I<=Steps;++I) {
        FVector Point;
        if(!FindStandingPose(Hero,FMath::Lerp(From,To,float(I)/Steps),Point)
            || FMath::Abs(Point.Z-Previous.Z)>Movement->MaxStepHeight) return false;
        FHitResult Hit;
        if(Hero->GetWorld()->SweepSingleByChannel(Hit,Previous,Point,FQuat::Identity,Capsule->GetCollisionObjectType(),Shape,Query,Responses)) return false;
        Previous=Point;
    }
    return FMath::Abs(Previous.Z-To.Z)<=15.f;
}

bool SubmitWalkInput(AMCToothCharacter* Hero,FVector To)
{
    if(!IsValid(Hero) || !Hero->CanWork() || Hero->HasFrozenLegs() || Hero->bInCoffee
        || !Hero->GetCharacterMovement()->IsMovingOnGround() || !CanWalkSegment(Hero,Hero->GetActorLocation(),To)) return false;
    const FVector Delta=To-Hero->GetActorLocation();
    const float Distance=Delta.Size2D();
    if(Distance>12.f) Hero->AddMovementInput(Delta.GetSafeNormal2D(),FMath::Clamp(Distance/65.f,.15f,1.f));
    return true;
}
}
