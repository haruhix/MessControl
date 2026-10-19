#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCHubReadyZone.generated.h"

class AMCHubGameState;
class APawn;
class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

/** Move/rotate/scale this actor in the hub. Only the server turns its occupants into readiness. */
UCLASS()
class MESSCONTROL_API AMCHubReadyZone : public AActor
{
    GENERATED_BODY()
public:
    AMCHubReadyZone();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hub") TObjectPtr<UBoxComponent> Zone;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hub") TObjectPtr<UStaticMeshComponent> FloorMarker;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hub") TArray<TObjectPtr<UStaticMeshComponent>> BorderMarkers;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hub") TObjectPtr<UStaticMeshComponent> MouthVisual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Hub") TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub") bool bShowFloorMarker = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub") FLinearColor WaitingColor = FLinearColor(.08f,.75f,.65f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub") FLinearColor ReadyColor = FLinearColor(.45f,1.f,.2f);

    UFUNCTION(BlueprintPure, Category="Hub") bool ContainsPawn(const APawn* Pawn) const;
    UFUNCTION(BlueprintCallable, Category="Hub") void SetZoneExtent(FVector HalfExtent);

private:
    UPROPERTY(Transient) TObjectPtr<AMCHubGameState> BoundState;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> MarkerMaterials;
    void RefreshGeometry();
    UFUNCTION() void RefreshLabel();
};
