#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCRewardDropZone.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/** Only authored volumes may receive a reward. An unset surface allows the native tongue. */
UCLASS()
class MESSCONTROL_API AMCRewardDropZone : public AActor
{
    GENERATED_BODY()
public:
    AMCRewardDropZone();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rewards") TObjectPtr<UBoxComponent> Area;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Rewards") TObjectPtr<AActor> AllowedLandingSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rewards") bool bEnabled=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rewards") bool bAllowRewardDrops=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rewards", meta=(ClampMin="0")) float InwardMargin=80.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rewards", meta=(ClampMin="100")) float PlayerDensityRadius=700.f;
    /** Maximum deviation from the center tangent plane; a smooth slope is valid support. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rewards", meta=(ClampMin="0",ClampMax="30")) float MaxFloorVariation=12.f;
    bool AcceptsFloor(const FHitResult& Hit) const;
    bool ContainsFootprint(FVector Point, FVector HalfExtent) const;
    bool FindLanding(FRandomStream& Random, FVector HalfExtent, float FallHeight,
        TConstArrayView<AActor*> Ignored, FVector& OutFloor, FVector& OutNormal) const;
};
