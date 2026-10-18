#pragma once

#include "CoreMinimal.h"
#include "MCNutEnemy.h"
#include "UObject/SoftObjectPtr.h"
#include "MCCataclysmJelly.generated.h"

class UAnimSequence;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EMCCataclysmJellySize : uint8 { Small, Medium, Large };

/** The encounter owns persistent, brush-cleanable patches and their finite budget. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMCCataclysmJellyDirtyPatch, FVector, float, bool);

/** An aimlessly wandering gel painter. Tool/chili damage uses the existing nut-enemy interface. */
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

    void ConfigureJelly(AMCTongue* OnTongue, float InMaxHealth=360.f, EMCCataclysmJellySize InSize=EMCCataclysmJellySize::Large);
    // A lethal hit starts a short escape cue; the encounter counts it until rupture.
    virtual bool IsEncounterAlive() const override;
    virtual bool CanReceiveToolHit() const override;
    virtual float ReceiveToolDamage(float Damage, AMCToothCharacter* Source) override;
    bool IsAlive() const { return IsEncounterAlive(); }
    float GetHealth() const { return Health; }
    FMCCataclysmJellyDirtyPatch OnDirtyPatchRequested;

    /** Floor-pivot coffee rig; collision and movement remain on the inherited Body. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Jelly|Presentation") TObjectPtr<USkeletalMeshComponent> JellyMesh;
    UPROPERTY(EditDefaultsOnly, Category="Jelly|Presentation") TSoftObjectPtr<USkeletalMesh> CoffeeMesh;
    UPROPERTY(EditDefaultsOnly, Category="Jelly|Presentation") TSoftObjectPtr<UAnimSequence> CoffeeIdle;
    UPROPERTY(EditDefaultsOnly, Category="Jelly|Presentation") TSoftObjectPtr<UAnimSequence> CoffeeHop;
    UPROPERTY(EditDefaultsOnly, Category="Jelly|Presentation") TSoftObjectPtr<UAnimSequence> CoffeeHurt;
    UPROPERTY(EditDefaultsOnly, Category="Jelly|Presentation") TSoftObjectPtr<UAnimSequence> CoffeePreBurst;
    UPROPERTY(EditDefaultsOnly, Category="Jelly|Presentation") TSoftObjectPtr<UAnimSequence> CoffeeBurst;
    UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Presentation") bool bCoffeePresentationReady=false;

    UPROPERTY(Replicated, EditDefaultsOnly, Category="Jelly|Grime", meta=(ClampMin="20", ClampMax="100")) float TrailRadius=38.f;
    UPROPERTY(Replicated, EditDefaultsOnly, Category="Jelly|Grime", meta=(ClampMin="100", ClampMax="500")) float RuptureRadius=280.f;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly, Category="Jelly") EMCCataclysmJellySize JellySize=EMCCataclysmJellySize::Large;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Jelly") double HopStartedAt=-100.;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly, Category="Jelly") double DeathStartedAt=-100.;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly, Category="Jelly") double RupturedAt=-100.;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly, Category="Jelly") bool bPendingRupture=false;

    // Server diagnostics distinguish actual travel/deposition requests from the cosmetic wobble.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") FVector WanderGoal=FVector::ZeroVector;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") float WanderDistance=0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") int32 WanderGoalChanges=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") int32 PaintStrokeRequests=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") int32 PaintFloorRejections=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") int32 TrailDiscontinuities=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") int32 RuptureCount=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") int32 ChildrenSpawned=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Jelly|Diagnostics") int32 RupturePlayersPushed=0;

protected:
    virtual void RefreshPresentation() override;
    virtual void Defeat() override;

private:
    static constexpr float HopSeconds=.90f;
    static constexpr float RuptureWarningSeconds=.60f;
    static constexpr float PaintInterval=.10f;
    static constexpr float MoveInterval=.05f;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GroundBurst;
    UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Droplets;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> GelMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GelMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SplatterMID;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> LoadedCoffeeIdle;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> LoadedCoffeeHop;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> LoadedCoffeeHurt;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> LoadedCoffeePreBurst;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> LoadedCoffeeBurst;
    double NextContactAt=0., NextWanderAt=0., NextTrailAt=0., NextMoveAt=0., LastMoveAt=0., LastTrailSampleAt=-100.;
    FVector LastTrailLocation=FVector::ZeroVector;
    FVector LastObservedLocation=FVector::ZeroVector;
    FRandomStream WanderRandom;
    bool bHasWanderGoal=false, bHasTrailAnchor=false;

    double ServerTime() const;
    bool IsPlayerTarget(const AMCToothCharacter* Hero) const;
    void ResetWandering(double Now);
    void PickWanderGoal(double Now);
    void WanderOnTongue(double Now);
    void PaintTrail(double Now, float DeltaSeconds, bool bFlush=false);
    void BreakTrail();
    void LoadCoffeePresentation();
    void TickCoffeePresentation(double Now);
    void TickGelPresentation(double Now);
    void ContaminateNearby(float Radius, float Amount);
    void PushRupturePlayers();
    void SpawnChildren();
    float PresentationScale() const;
    void RequestDirtyPatch(float Radius, bool bDeathBurst);
    void Rupture(double Now);
};
