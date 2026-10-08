#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCNutLandingShadow.generated.h"

class AMCFoodActor;
class AMCTongue;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** A tongue-supported landing telegraph; authority resolves one real radial impact. */
UCLASS()
class MESSCONTROL_API AMCNutLandingShadow : public AActor
{
    GENERATED_BODY()
public:
    AMCNutLandingShadow();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Configure(AMCTongue* OnTongue,AMCFoodActor* Food,FVector Landing,float InFlightSeconds,float InRadius,float InOpacity,
        float InDamage=18,float InPushSpeed=260);
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
    UPROPERTY(ReplicatedUsing=RefreshShadow,BlueprintReadOnly) bool bImpacted=false;
private:
    void Landed(const FHitResult& Hit);
    UFUNCTION() void RefreshShadow();
    double ServerTime() const;
    TWeakObjectPtr<AMCFoodActor> TrackedFood;
    float ImpactDamage=18;
    float ImpactPushSpeed=260;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShadowMID;
};
