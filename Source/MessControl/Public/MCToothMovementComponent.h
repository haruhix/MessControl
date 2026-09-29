#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MCLocomotionSurface.h"
#include "Engine/NetSerialization.h"
#include "MCToothMovementComponent.generated.h"
class AMCCoffeeFlood;

/** Predicted sprint and ground response, plus surface swimming. */
UCLASS()
class MESSCONTROL_API UMCToothMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UMCToothMovementComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual float GetMaxSpeed() const override;
    virtual float GetMaxAcceleration() const override;
    virtual float GetMaxBrakingDeceleration() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation,float DeltaTime,FRotator& DeltaRotation) const override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion",meta=(ClampMin="100",ClampMax="600")) float WalkSpeed=340;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion",meta=(ClampMin="200",ClampMax="900")) float SprintSpeed=560;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion",meta=(ClampMin="100",ClampMax="3000")) float GroundAcceleration=1050;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Locomotion") EMCGroundSurface GroundSurface=EMCGroundSurface::Normal;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Locomotion") FVector_NetQuantizeNormal MovementIntent=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Locomotion") bool bSprintActive=false;
    UFUNCTION(BlueprintCallable,Category="Locomotion") void SetSprinting(bool Enabled) { bWantsToSprint=Enabled; }
    bool WantsToSprint() const { return bWantsToSprint; }
    bool CanSprint() const;
    bool HasHeavyGrip() const;
    float Traction() const;
    FVector Intent() const;
    void RefreshGroundSurface();
    AMCCoffeeFlood* DeepWaterAt(FVector Position,bool Continuing=false) const;
    virtual void UpdateCharacterStateBeforeMovement(float Dt) override;
    virtual void CalcVelocity(float Dt,float Friction,bool bFluid,float BrakingDeceleration) override;
    virtual void TickCharacterPose(float Dt) override;
    virtual void PhysSwimming(float Dt,int32 Iterations) override;
private:
    bool bWantsToSprint=false;
};
