#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCPhysicsTypes.h"
#include "MCToothPhysicsComponent.generated.h"
class AMCToothCharacter;
class UPhysicsControlComponent;

/** Server Chaos simulation; clients interpolate the compact skeleton, including the owning client. */
UCLASS(ClassGroup=(MessControl), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCToothPhysicsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCToothPhysicsComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Physics") TObjectPtr<UMCPhysicsProfile> Profile;
    UPROPERTY(ReplicatedUsing=OnRep_Settings,BlueprintReadOnly,Category="Physics") FMCPhysicsSettings Settings;
    UFUNCTION(BlueprintPure,Category="Physics") EMCBodyState GetBodyState() const { return LocalState; }
    bool CanAct() const { return LocalState == EMCBodyState::Standing; }
    void ApplyHit(FVector VelocityChange,FVector HitLocation);
    bool TryRecover();
    void SetTuning(FMCPhysicsSettings NewSettings);
    void SaveTuning() const;
    void ResetTuning();
    void BuildPresentationPose(TArray<FTransform>& InOutPose) const;
    float RecoveryAlpha() const;
    FVector PhysicalLocation() const;
    int32 KnockdownCount = 0;
    int32 RecoveryCount = 0;
private:
    void EnterRagdoll();
    void EnterRecovery();
    void EnterStanding();
    void SetState(EMCBodyState NewState);
    void CaptureFrame();
    void SetMuscles(bool bEnable);
    float ServerTime() const;
    UFUNCTION() void OnRep_Frame();
    UFUNCTION() void OnRep_Settings();
    UPROPERTY() TObjectPtr<AMCToothCharacter> Tooth;
    UPROPERTY() TObjectPtr<UPhysicsControlComponent> Muscles;
    UPROPERTY(ReplicatedUsing=OnRep_Frame) FMCRagdollFrame Frame;
    EMCBodyState LocalState = EMCBodyState::Standing;
    TArray<FTransform> DisplayPose;
    float SendAccumulator = 0.f;
    float LastHitTime = -10.f;
    float RecoveryInvulnerableUntil = 0.f;
};
