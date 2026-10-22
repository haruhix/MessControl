#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCHubFishingZone.generated.h"

class APawn;
class UBoxComponent;

/** Editable pier footprint. The authoritative pawn position grants fishing access. */
UCLASS()
class MESSCONTROL_API AMCHubFishingZone : public AActor
{
    GENERATED_BODY()
public:
    AMCHubFishingZone();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hub|Fishing") TObjectPtr<UBoxComponent> Zone;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub|Fishing") bool bEnabled = true;

    UFUNCTION(BlueprintPure, Category="Hub|Fishing") bool ContainsPawn(const APawn* Pawn) const;
    UFUNCTION(BlueprintCallable, Category="Hub|Fishing") void SetZoneExtent(FVector HalfExtent);
};
