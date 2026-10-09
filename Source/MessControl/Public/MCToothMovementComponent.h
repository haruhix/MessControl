#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "MCLocomotionSurface.h"
#include "Engine/NetSerialization.h"
#include "MCGripComponent.h"
#include "MCToothMovementComponent.generated.h"
class AMCCoffeeFlood;

struct FMCStaminaPredictionState
{
    float Value=100.f;
    float RecoveryDelay=0;
    bool Exhausted=false;
};

/** The stamina snapshot belongs to the acknowledged movement timestamp. */
struct FMCStaminaMoveResponse final : public FCharacterMoveResponseDataContainer
{
    FMCStaminaPredictionState Stamina;
    virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement,const FClientAdjustment& Adjustment) override;
    virtual bool Serialize(UCharacterMovementComponent& Movement,FArchive& Ar,UPackageMap* Map) override;
};

/** Force can change between moves; matching uses source identity, correction copies the force. */
USTRUCT()
struct MESSCONTROL_API FMCLocomotionRootMotionSource : public FRootMotionSource_ConstantForce
{
    GENERATED_BODY()
    virtual FRootMotionSource* Clone() const override;
    virtual bool Matches(const FRootMotionSource* Other) const override;
    virtual bool UpdateStateFrom(const FRootMotionSource* Other,bool bMarkForSimulatedCatchup=false) override;
    virtual bool NetSerialize(FArchive& Ar,UPackageMap* Map,bool& bOutSuccess) override;
    virtual UScriptStruct* GetScriptStruct() const override;
};
template<> struct TStructOpsTypeTraits<FMCLocomotionRootMotionSource> : public TStructOpsTypeTraitsBase2<FMCLocomotionRootMotionSource>
{
    enum { WithNetSerializer=true,WithCopy=true };
};

/** Predicted sprint, press dash, wind and river current, plus ground response and surface swimming. */
UCLASS()
class MESSCONTROL_API UMCToothMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UMCToothMovementComponent();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual float GetMaxSpeed() const override;
    virtual float GetMaxAcceleration() const override;
    virtual float GetMaxBrakingDeceleration() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual void PerformMovement(float Dt) override;
    virtual void SimulateMovement(float Dt) override;
    virtual void SetMovementMode(EMovementMode NewMode,uint8 NewCustomMode=0) override;
    // Owns MOVE_None only while the living standing character has leg ice.
    void SetFrozenLegs(bool Frozen);
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual bool ClientUpdatePositionAfterServerUpdate() override;
    virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response) override;
    virtual void ServerMoveHandleClientError(float TimeStamp,float Dt,const FVector& Accel,const FVector& ClientLocation,
        FMovementBaseInterfaceData* Base,FName Bone,uint8 Mode) override;
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
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion|Stamina",meta=(ClampMin="1")) float MaxStamina=100.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion|Stamina",meta=(ClampMin="0")) float SprintStaminaPerSecond=12.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion|Stamina",meta=(ClampMin="0")) float StaminaPerSecond=22.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion|Stamina",meta=(ClampMin="0")) float StaminaRecoveryDelay=.65f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion|Stamina",meta=(ClampMin="0")) float DashStaminaCost=22.f;
    float GetMaxStamina() const { return FMath::IsFinite(MaxStamina)?FMath::Max(1.f,MaxStamina):100.f; }
    float GetStamina() const { return FMath::Clamp(StaminaState.Value,0.f,GetMaxStamina()); }
    const FMCStaminaPredictionState& GetStaminaPrediction() const { return StaminaState; }
    const FMCStaminaPredictionState& GetStaminaResponseSnapshot() const { return ServerStaminaSnapshot; }
    void RestoreStaminaPrediction(const FMCStaminaPredictionState& State);
    void AdvanceStaminaPrediction(float Dt,bool SprintRequested,bool DashCharged);
    bool IsReplayingAuthoritativeStamina() const { return bReplayingAuthoritativeStamina; }
    bool LastMoveRequestedSprint() const { return bStaminaSprintRequested; }
    bool LastMoveChargedDash() const { return bStaminaDashCharged; }
    void RestoreReplayDashCharge(bool Charge) { bReplayDashCharge=Charge; }
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion|Dash",meta=(ClampMin="500",ClampMax="1600")) float DashSpeed=1000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion|Dash",meta=(ClampMin="0.2",ClampMax="0.65")) float DashDuration=.42f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion|Dash",meta=(ClampMin="0.5",ClampMax="3")) float DashCooldown=1.1f;
    UPROPERTY(ReplicatedUsing=OnRep_DashStartedAt,BlueprintReadOnly,Category="Locomotion|Dash") double DashStartedAt=-100;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Locomotion|Dash") FVector_NetQuantizeNormal DashDirection=FVector::ForwardVector;
    UFUNCTION(BlueprintCallable,Category="Locomotion|Dash") void RequestDash() { bWantsDash=true; }
    UFUNCTION(BlueprintPure,Category="Locomotion|Dash") bool IsDashing() const;
    UFUNCTION(BlueprintPure,Category="Locomotion|Dash") float GetDashProgress() const;
    float GetDashDuration() const { return FMath::Clamp(DashDuration,.2f,.65f); }
    bool CanDash() const;
    bool WantsDash() const { return bWantsDash; }
    void SetWantsDash(bool Enabled) { bWantsDash=Enabled; }
    void CancelDash();
    float GetDashCooldownRemaining() const { return DashCooldownRemaining; }
    void RestoreDashPrediction(float Cooldown) { DashCooldownRemaining=Cooldown; }
    FVector CaptureSuctionForMove();
    void RestoreSuctionForMove(FVector Sample);
    FVector CaptureRiverForMove();
    void RestoreRiverForMove(FVector Sample);
    FMCBraceMovementState CaptureBraceForMove();
    void RestoreBraceForMove(const FMCBraceMovementState& State);
    bool HasHeavyGrip() const;
    float Traction() const;
    FVector Intent() const;
    void RefreshGroundSurface();
    AMCCoffeeFlood* DeepWaterAt(FVector Position,bool Continuing=false) const;
    virtual void UpdateCharacterStateBeforeMovement(float Dt) override;
    virtual void CalcVelocity(float Dt,float Friction,bool bFluid,float BrakingDeceleration) override;
    virtual void ApplyRootMotionToVelocity(float Dt) override;
    virtual void TickCharacterPose(float Dt) override;
    virtual void PhysSwimming(float Dt,int32 Iterations) override;
    virtual void PhysCustom(float Dt,int32 Iterations) override;
    virtual bool CanAttemptJump() const override;
    virtual bool DoJump(bool bReplayingMoves,float DeltaTime) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Climbing") float ClimbSpeed=180;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Climbing") FVector_NetQuantizeNormal ClimbNormal=FVector::ForwardVector;
    bool IsClimbing() const { return MovementMode==MOVE_Custom && CustomMovementMode==1; }
    void SetWantsClimb(bool Active) { bWantsToClimb=Active; }
    bool WantsClimb() const { return bWantsToClimb; }
    void JumpFromWall();
private:
    bool bIceMovementLocked=false;
    EMovementMode IcePreviousMovementMode=MOVE_Walking;
    FMCStaminaPredictionState StaminaState,ServerStaminaSnapshot,PendingStaminaCorrection;
    FMCStaminaMoveResponse StaminaMoveResponse;
    bool bPendingStaminaCorrection=false,bReplayingAuthoritativeStamina=false;
    bool bStaminaSprintRequested=false,bStaminaDashCharged=false,bReplayDashCharge=false;
    bool CanSprintAction() const;
    bool bWantsToSprint=false;
    bool bWantsToClimb=false;
    bool bWantsDash=false;
    bool bHasSuctionSample=false;
    FVector PendingSuction=FVector::ZeroVector;
    bool bHasRiverSample=false;
    FVector PendingRiver=FVector::ZeroVector;
    FMCBraceMovementState BraceMovement;
    bool bHasBraceSample=false;
    float DashCooldownRemaining=0;
    mutable double DashPresentationStartedAt=-100;
    double LastPresentedDashStartedAt=-100;
    UFUNCTION() void OnRep_DashStartedAt();
    bool CanDashAction() const;
    void StartDash();
    void UpdateAmbientSuction();
    void UpdateRiverCurrent();
    float ClimbCooldown=0;
    bool FindClimbWall(FHitResult& Hit) const;
    bool TryMantle();
};
