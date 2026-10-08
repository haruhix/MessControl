#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCFogBrawlPresentationComponent.generated.h"

class AMCFogBrawlEvent;
class AMCTongue;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UProceduralMeshComponent;
class UMaterialInterface;

/** Local smoke, independent of authoritative strike resolution. */
UCLASS()
class MESSCONTROL_API UMCFogBrawlPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCFogBrawlPresentationComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    void ResetSmoke();
    UPROPERTY(EditAnywhere,Category="Smoke",meta=(ClampMin="0",ClampMax="1")) float Density=.12f;
private:
    UPROPERTY() TObjectPtr<AMCFogBrawlEvent> Event;
    UPROPERTY() TObjectPtr<AMCTongue> Tongue;
    UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
    UPROPERTY() TObjectPtr<UPostProcessComponent> Mood;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Billows;
    UPROPERTY() TObjectPtr<UMaterialInterface> SmokeMaterial;
    TArray<TWeakObjectPtr<UExponentialHeightFogComponent>> SuspendedFog;
    bool bPresenting=false;
    void BeginSmoke();
    void UpdateBillows(double Now,float Weight);
};
