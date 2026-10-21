#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "MCHubBasketball.generated.h"

class AMCToothCharacter;
class USphereComponent;
class UStaticMeshComponent;
class UPhysicalMaterial;
class UNiagaraSystem;

/** A freely simulated hub prop. Holding is explicit; flight always uses Chaos. */
UCLASS()
class MESSCONTROL_API AMCHubBasketball : public AActor
{
    GENERATED_BODY()
public:
    AMCHubBasketball();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Basketball") TObjectPtr<USphereComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Basketball") TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Basketball", meta=(ClampMin="10",ClampMax="80")) float Radius=50.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Basketball") float PickupReach=220.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Basketball") float ThrowSpeed=2100.f;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Basketball|Hoop") TObjectPtr<AActor> Hoop;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Basketball|Hoop") FVector LocalRimCenter=FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Basketball|Hoop") float RimAimRadius=100.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Basketball|Hoop") TObjectPtr<UNiagaraSystem> BasketConfetti;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Basketball|Hoop") int32 BasketsMade=0;
    UPROPERTY(ReplicatedUsing=OnRep_Holder, BlueprintReadOnly, Category="Basketball") TObjectPtr<AMCToothCharacter> Holder;

    static AMCHubBasketball* HeldBy(const AMCToothCharacter* Hero);
    static AMCHubBasketball* PickupCandidate(const AMCToothCharacter* Hero);
    bool TryPickup(AMCToothCharacter* Hero);
    UFUNCTION(BlueprintCallable, Category="Basketball") void Release(FVector Velocity);
    UFUNCTION(BlueprintPure, Category="Basketball") FVector LaunchVelocity() const;
    UFUNCTION(BlueprintPure, Category="Basketball") TArray<FVector> PredictThrowTrajectory() const;
    UFUNCTION(BlueprintPure, Category="Basketball") bool IsAiming() const;
    void SetAimHeld(bool Held);
    UFUNCTION(BlueprintCallable, Category="Basketball") void ThrowFromView();
    UFUNCTION(Server,Reliable) void ServerThrow(FVector_NetQuantize Velocity);

private:
    UFUNCTION() void OnRep_Holder();
    void UpdateHeldPosition();
    void CheckBasket();
    UFUNCTION(NetMulticast, Reliable) void CelebrateBasket(FVector_NetQuantize Location, FVector_NetQuantizeNormal Up);
    UPROPERTY(Transient) TObjectPtr<UPhysicalMaterial> Rubber;
    FVector Home=FVector::ZeroVector;
    bool bAimHeld=false;
    FVector PreviousFreePosition=FVector::ZeroVector;
    bool bHasPreviousFreePosition=false;
    bool bWasAboveRim=false;
    bool bPassingThroughRim=false;
    double LastBasketTime=-10.;
};
