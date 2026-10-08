#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCNutLandingShadow.generated.h"

class AMCFoodActor;
class AMCTongue;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** A faint, non-colliding landing warning supported by the current tongue. */
UCLASS()
class MESSCONTROL_API AMCNutLandingShadow : public AActor
{
    GENERATED_BODY()
public:
    AMCNutLandingShadow();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Configure(AMCTongue* OnTongue,AMCFoodActor* Food,FVector Landing,float InFlightSeconds,float InRadius,float InOpacity);
    UFUNCTION(BlueprintPure) bool IsWarningActive() const;
    UFUNCTION(BlueprintPure) float SecondsToImpact() const;
    UFUNCTION(BlueprintPure) float GetShadowStrength() const;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UDecalComponent> Shadow;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Landing Shadow") TSoftObjectPtr<UMaterialInterface> ShadowMaterial;
    UPROPERTY(ReplicatedUsing=RefreshShadow,BlueprintReadOnly) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(ReplicatedUsing=RefreshShadow,BlueprintReadOnly) FVector SurfaceAnchor=FVector::ZeroVector;
    UPROPERTY(ReplicatedUsing=RefreshShadow,BlueprintReadOnly) double ImpactAt=0;
    UPROPERTY(ReplicatedUsing=RefreshShadow,BlueprintReadOnly) float FlightSeconds=1.65f;
    UPROPERTY(ReplicatedUsing=RefreshShadow,BlueprintReadOnly) float Radius=70;
    UPROPERTY(ReplicatedUsing=RefreshShadow,BlueprintReadOnly) float MaxOpacity=.20f;
private:
    UFUNCTION() void RefreshShadow();
    double ServerTime() const;
    TWeakObjectPtr<AMCFoodActor> TrackedFood;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShadowMID;
};
