#include "MCOrbitSpringArmComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"

namespace
{
    bool IntersectsCameraPath(const UPrimitiveComponent* Surface, const FVector& From, const FVector& To, float Radius)
    {
        if (!Surface || !Surface->IsRegistered() || !Surface->IsQueryCollisionEnabled()) return false;
        const FBox Bounds=Surface->Bounds.GetBox().ExpandBy(Radius);
        return From.Equals(To,KINDA_SMALL_NUMBER)?Bounds.IsInsideOrOn(From):FMath::LineBoxIntersection(Bounds,From,To,To-From);
    }
}

void UMCOrbitSpringArmComponent::SetRevealedWall(UMeshComponent* Wall,bool bReveal)
{
    if (bReveal) RevealedWalls.AddUnique(Wall);
    else RevealedWalls.Remove(Wall);
}

FVector UMCOrbitSpringArmComponent::BlendLocations(const FVector& Desired,const FVector& Hit,bool bBlocked,float Dt)
{
    const FVector Origin=GetComponentLocation()+TargetOffset;
    FVector Safe=bBlocked?Hit:Desired;
    const float Radius=FMath::Max(1.f,ProbeSize);
    if (bDoCollisionTest && IgnoredViewActor.IsValid() && GetWorld()) {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCMimicCamera),false,GetOwner());
        Query.AddIgnoredActor(IgnoredViewActor.Get());
        FHitResult Obstacle;
        Safe=GetWorld()->SweepSingleByChannel(Obstacle,Origin,Desired,FQuat::Identity,ProbeChannel,
            FCollisionShape::MakeSphere(Radius),Query)?Obstacle.Location:Desired;
    }
    float Limit=FVector::Distance(Origin,Safe);
    if(SafeArmLength<0 || FVector::DistSquared(Origin,LastOrigin)>FMath::Square(1400.f)) SafeArmLength=Limit;
    // Smooth only outward travel. A newly closer hit always wins this frame,
    // so the filtered camera stays on the validated part of the sphere sweep.
    SafeArmLength=FMath::Min(Limit,FMath::Lerp(SafeArmLength,Limit,1-FMath::Exp(-12.f*FMath::Max(0.f,Dt))));
    LastOrigin=Origin;
    FVector Result=Origin+(Safe-Origin).GetSafeNormal()*SafeArmLength;
    if (bDoCollisionTest && bWallProbeActive && GetWorld()) for (auto It=RevealedWalls.CreateIterator(); It; ++It) {
        auto* Wall=It->Get();
        if (!Wall) { It.RemoveCurrent(); continue; }
        if (!IntersectsCameraPath(Wall,Result,Result,Radius+2.f)) continue;
        FHitResult Touch;
        // An aperture can let the eye remain outside the shell. It must not let
        // outward interpolation or orbit place the eye in the shell itself.
        if (!Wall->SweepComponent(Touch,Result,Result+FVector(0,0,.1),FQuat::Identity,FCollisionShape::MakeSphere(Radius+2.f),true)) continue;
        FHitResult Contact;
        if (Wall->SweepComponent(Contact,Origin,Result,FQuat::Identity,FCollisionShape::MakeSphere(Radius+2.f),true)) {
            Limit=FMath::Max(0.f,float(FVector::Distance(Origin,Contact.Location))-2.f);
            SafeArmLength=FMath::Min(SafeArmLength,Limit);
            Result=Origin+(Result-Origin).GetSafeNormal()*SafeArmLength;
        }
    }
    return Result;
}
