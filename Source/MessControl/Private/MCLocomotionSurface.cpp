#include "MCLocomotionSurface.h"
#include "Components/BoxComponent.h"
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
    return P.X<=E.X && P.Y<=E.Y && P.Z<=E.Z;
}
void AMCLocomotionSurface::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCLocomotionSurface,Surface);
    DOREPLIFETIME(AMCLocomotionSurface,Priority); DOREPLIFETIME(AMCLocomotionSurface,HalfExtent);
}
