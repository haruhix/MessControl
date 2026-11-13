#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "MCDeathSpiritComponent.generated.h"

class UMaterialInstanceDynamic;
class UPoseableMeshComponent;
class AMCArenaTooth;

USTRUCT()
struct FMCDeathSpiritState
{
    GENERATED_BODY()

    UPROPERTY() FVector Origin=FVector::ZeroVector;
    UPROPERTY() FVector PullOrigin=FVector::ZeroVector;
    UPROPERTY() FVector PullTarget=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<AMCArenaTooth> PullReserve;
    UPROPERTY() FVector ReleasePosition=FVector::ZeroVector;
    UPROPERTY() double ReleasedAt=-1.;
    UPROPERTY() double DeathTime=-1.;
    UPROPERTY() double PullStartedAt=-1.;
    UPROPERTY() double PullReleaseAt=-1.;
    UPROPERTY() float FacingYaw=0.f;
    UPROPERTY() bool bRetired=false;
};

/** Presentation of the dead pawn while the game mode owns the respawn transaction. */
UCLASS()
class MESSCONTROL_API UMCDeathSpiritComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMCDeathSpiritComponent();
    void StartSpirit(double DeathTime,FVector Origin);
    void StartPull(double StartedAt,double ReleaseAt,FVector Target,AMCArenaTooth* Reserve,FVector ExitPosition);
    void CancelPull();
    void EndSpirit(TOptional<FVector> ReleasePosition={});
    void SetSteeringAxis(bool bRight,float Value);

    UFUNCTION(BlueprintPure,Category="Life") bool IsSpiritActive() const { return State.DeathTime>=0. && !State.bRetired; }
    UFUNCTION(BlueprintPure,Category="Life") bool IsPulling() const { return IsSpiritActive() && State.PullStartedAt>=0.; }
    UFUNCTION(BlueprintPure,Category="Life") FVector GetSpiritLocation() const;
    FVector GetPlannedExit() const { return State.ReleasePosition; }
    float GetDeathFilterStrength() const;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;

private:
    UFUNCTION() void OnRep_State();
    UFUNCTION(Server,Unreliable) void ServerSetSteering(FVector_NetQuantizeNormal Direction);
    void UpdateSteering(float DeltaSeconds);
    void AdvanceSteering(FVector Direction,float DeltaSeconds);
    FVector2D LocalSteering=FVector2D::ZeroVector;
    FVector RemoteSteering=FVector::ZeroVector;
    double RemoteSteeringExpiresAt=0.;
    double NextSteeringPacketAt=0.;
    double NextSteeringReplicationAt=0.;
    bool bSentSteering=false;
    double Clock() const;
    FVector HoverOffset(double Now) const;
    FVector PullTarget() const;
    float PullProgress(double Now) const;
    float ExitProgress(double Now) const;
    void UpdateBody(double Now);
    void UpdateVisual(double Now);
    void PresentAcceptedEvent(double Now);

    bool bHidBody=false;
    bool bHidNameLabel=false;
    bool bDisabledBodyCollision=false;
    bool bBodyWasSimulating=false;
    ECollisionEnabled::Type BodyCollision=ECollisionEnabled::NoCollision;
    double PresentedDeathTime=-1.;
    double PresentedMergeForRelease=-1.;
    bool bPresentedRelease=false;
    FTransform SpiritStandingTransform;

    UPROPERTY(ReplicatedUsing=OnRep_State) FMCDeathSpiritState State;
    UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> SpiritMesh;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SpiritMaterial;
};
