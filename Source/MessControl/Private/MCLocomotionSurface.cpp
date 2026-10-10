#include "MCLocomotionSurface.h"
#include "MCIceEvent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMCLocomotionSurface::AMCLocomotionSurface()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true);
    Bounds=CreateDefaultSubobject<UBoxComponent>(TEXT("SurfaceBounds")); SetRootComponent(Bounds);
    Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision); Bounds->SetGenerateOverlapEvents(false);
    Bounds->SetBoxExtent(HalfExtent); Bounds->ShapeColor=FColor(235,155,45);
}
void AMCLocomotionSurface::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); RefreshBounds(); }
void AMCLocomotionSurface::RefreshBounds() { Bounds->SetBoxExtent(HalfExtent.GetAbs().ComponentMax(FVector(1))); }
bool AMCLocomotionSurface::ContainsSole(FVector Point) const
{
    const FVector P=GetActorTransform().InverseTransformPosition(Point).GetAbs();
    const FVector E=HalfExtent.GetAbs().ComponentMax(FVector(1));
    if(P.X>E.X || P.Y>E.Y || P.Z>E.Z || Point.ContainsNaN()) return false;
    // Direct surface queries share the same warm-zone exclusion as predicted
    // character movement, including overlapping slippery patches.
    if(Surface==EMCGroundSurface::Slippery)
        for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It)
            if(It->IsSafePoint(Point)) return false;
    return true;
}
void AMCLocomotionSurface::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCLocomotionSurface,Surface);
    DOREPLIFETIME(AMCLocomotionSurface,Priority); DOREPLIFETIME(AMCLocomotionSurface,HalfExtent);
}
