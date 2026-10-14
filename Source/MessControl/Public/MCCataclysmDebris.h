#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "MCCataclysmDebris.generated.h"

class AMCCoffeeFlood;
class AMCTongue;
class AMCToothCharacter;
class UProceduralMeshComponent;
class UStaticMeshComponent;

/** A first-use graphics pipeline may wait, but must not hide a contact hazard. */
UCLASS()
class MESSCONTROL_API UMCCataclysmDebrisMeshComponent : public UStaticMeshComponent
{
    GENERATED_BODY()
protected:
    virtual bool UsePSOPrecacheRenderProxyDelay() const override { return false; }
};

/** A visible storm plank: authoritative swept contact, never a radial damage area. */
UCLASS()
class MESSCONTROL_API AMCCataclysmDebris : public AActor
{
    GENERATED_BODY()
public:
    AMCCataclysmDebris();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnRep_ReplicatedMovement() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    /** Event supplies an upstream spawn transform inside its actual water volume. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cataclysm|Debris")
    void ConfigureDebris(AMCCoffeeFlood* InFlood, AMCTongue* InTongue, int32 InSeed);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cataclysm|Debris")
    void StopDebris();

    UFUNCTION(BlueprintPure, Category="Cataclysm|Debris") bool IsWarning() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cataclysm|Debris")
    TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(ReplicatedUsing=OnRep_Config, BlueprintReadOnly, Category="Cataclysm|Debris")
    TObjectPtr<AMCCoffeeFlood> Flood;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Debris")
    TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(ReplicatedUsing=OnRep_Config, BlueprintReadOnly, Category="Cataclysm|Debris") int32 Seed = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Debris") double SpawnedAt = -1.;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Debris") FVector DriftVelocity = FVector::ZeroVector;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Debris") int32 ImpactCount = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Debris") int32 PlayerImpactCount = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Debris") int32 BracedContactCount = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Debris") int32 RaftImpactCount = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Cataclysm|Debris") double LastImpactAt = -1.;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cataclysm|Debris", meta=(ClampMin=".7", ClampMax="1"))
    float WarningSeconds = .85f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cataclysm|Debris", meta=(ClampMin="1", ClampMax="12"))
    float LifetimeSeconds = 10.f;

private:
    double Clock() const;
    void ConfigureBody();
    void UpdateCue(double Now);
    void Advance(float DeltaSeconds, double Now);
    bool SweepContact(FVector Start, FVector& End, double Now);
    bool ClearContact(const FHitResult& Hit, FVector Start) const;
    void MarkImpact(double Now, FVector ContactPoint);
    UFUNCTION() void OnRep_Config();

    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Cue;
    UPROPERTY(Replicated) double RetireAt = -1.;
    FVector HalfExtent = FVector(72.f, 28.f, 29.f);
    float SpinDegrees = 0.f;
    float SpeedOffset = 0.f;
    TWeakObjectPtr<AMCCoffeeFlood> TickFlood;
    bool bCueInitialized = false;
    bool bClientPoseInitialized = false;
    FVector ClientTarget = FVector::ZeroVector;
    FQuat ClientRotation = FQuat::Identity;
    double ClientSnapshotAt = 0.;
};
