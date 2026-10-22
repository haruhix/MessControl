#include "MCHubFishingZone.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"

AMCHubFishingZone::AMCHubFishingZone()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = false;
    Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("PierFishingVolume"));
    SetRootComponent(Zone);
    Zone->SetBoxExtent(FVector(250.f, 140.f, 140.f));
    // Position containment handles initial overlaps, teleporting and recovery.
    // This volume neither blocks movement nor generates physics/overlap work.
    Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Zone->SetGenerateOverlapEvents(false);
    Zone->SetCanEverAffectNavigation(false);
    Zone->SetHiddenInGame(true);
    Zone->ShapeColor = FColor(48, 210, 215);
}

bool AMCHubFishingZone::ContainsPawn(const APawn* Pawn) const
{
    if (!bEnabled || !Zone || !IsValid(Pawn) || Pawn->GetWorld() != GetWorld()) return false;
    const FVector Local = Zone->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
    const FVector Extent = Zone->GetUnscaledBoxExtent();
    return !Local.ContainsNaN() && FMath::Abs(Local.X) <= Extent.X
        && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void AMCHubFishingZone::SetZoneExtent(FVector HalfExtent)
{
    if (!Zone || HalfExtent.ContainsNaN()) return;
    Zone->SetBoxExtent(HalfExtent.GetAbs().ComponentMax(FVector(25.f)));
}
