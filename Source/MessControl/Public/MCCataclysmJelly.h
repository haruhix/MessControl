#pragma once

#include "CoreMinimal.h"
#include "MCNutEnemy.h"
#include "MCCataclysmJelly.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/** The encounter owns persistent, brush-cleanable patches and their finite budget. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMCCataclysmJellyDirtyPatch, FVector, float, bool);

/** A hopping grime carrier. Tool/chili damage uses the existing nut-enemy interface. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCCataclysmJelly : public AMCNutEnemy
{
    GENERATED_BODY()
public:
    AMCCataclysmJelly();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void ConfigureJelly(AMCTongue* OnTongue, float InMaxHealth=360.f);
    // A lethal hit starts a short escape cue; the encounter counts it until rupture.
    virtual bool IsEncounterAlive() const override;
    virtual bool CanReceiveToolHit() const override;
    bool IsAlive() const { return IsEncounterAlive(); }
    float GetHealth() const { return Health; }
    FMCCataclysmJellyDirtyPatch OnDirtyPatchRequested;

    UPROPERTY(EditDefaultsOnly, Category="Jelly|Grime", meta=(ClampMin="40", ClampMax="200")) float TrailRadius=100.f;
    UPROPERTY(EditDefaultsOnly, Category="Jelly|Grime", meta=(ClampMin="100", ClampMax="500")) float RuptureRadius=280.f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Jelly") double HopStartedAt=-100.;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly, Category="Jelly") double DeathStartedAt=-100.;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly, Category="Jelly") double RupturedAt=-100.;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly, Category="Jelly") bool bPendingRupture=false;

protected:
    virtual void RefreshPresentation() override;
    virtual void Defeat() override;

private:
    static constexpr float HopSeconds=.90f;
    static constexpr float RuptureWarningSeconds=.60f;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GroundBurst;
    UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Droplets;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> GelMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GelMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SplatterMID;
    double NextContactAt=0., NextRetargetAt=0., NextTrailAt=0.;
    FVector LastTrailLocation=FVector::ZeroVector;

    double ServerTime() const;
    bool IsPlayerTarget(const AMCToothCharacter* Hero) const;
    void RetargetJelly();
    void TickGelPresentation(double Now);
    void ContaminateNearby(float Radius, float Amount);
    void RequestDirtyPatch(float Radius, bool bDeathBurst);
    void Rupture(double Now);
};
