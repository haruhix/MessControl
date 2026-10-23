#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCHubFishingZone.generated.h"

class APawn;
class UBoxComponent;
class UWidgetComponent;

/** Editable pier footprint. The authoritative pawn position grants fishing access. */
UCLASS()
class MESSCONTROL_API AMCHubFishingZone : public AActor
{
    GENERATED_BODY()
public:
    AMCHubFishingZone();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hub|Fishing") TObjectPtr<UBoxComponent> Zone;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub|Fishing") bool bEnabled = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hub|Fishing") TObjectPtr<UWidgetComponent> InteractionPrompt;

    UFUNCTION(BlueprintPure, Category="Hub|Fishing") bool ContainsPawn(const APawn* Pawn) const;
    UFUNCTION(BlueprintCallable, Category="Hub|Fishing") void SetZoneExtent(FVector HalfExtent);
};
