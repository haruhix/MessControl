#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCDeliveryZoneVisualComponent.generated.h"

class AMCFoodDisposal;
class AMCTongue;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UWidgetComponent;
namespace MCDeliveryGuide {struct FSurfaceCache;}

/** Local floor guides use the same footprint as the server's delivery check. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCDeliveryZoneVisualComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCDeliveryZoneVisualComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    UPROPERTY(EditAnywhere,Category="Delivery|Guide") TObjectPtr<UMaterialInterface> GuideMaterial;
    UPROPERTY(EditAnywhere,Category="Delivery|Guide",meta=(ClampMin="0.5")) float ArrowPeriod=1.4f;
    UPROPERTY(EditAnywhere,Category="Delivery|Guide",meta=(ClampMin="0",ClampMax="1")) float FillOpacity=.26f;
private:
    void Rebuild();
    UProceduralMeshComponent* MakeMesh(FName Name);
    TWeakObjectPtr<AMCFoodDisposal> Zone;
    TWeakObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Floor;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Arrows;
    UPROPERTY(Transient) TObjectPtr<UWidgetComponent> Caption;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FloorMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ArrowMID;
    TSharedPtr<MCDeliveryGuide::FSurfaceCache> SurfaceCache;
    FTransform CachedGeometry,CachedMeshTransform;
    FVector CachedExtent=FVector::ZeroVector,CachedDirection=FVector::ZeroVector;
    bool bGuidesBuilt=false,bCachedCircular=false,bCachedCap=false;
};
