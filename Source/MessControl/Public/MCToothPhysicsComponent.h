#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCPhysicsTypes.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "MCToothPhysicsComponent.generated.h"
class AMCToothCharacter;
class UPhysicsControlComponent;
struct FReferenceSkeleton;

/** Local active limbs; full knockdown uses server Chaos and replicated skeleton poses. */
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
    bool CanAct() const;
    void EnterDeath();
    void SetThroatCaptured(bool Captured);
    void ApplyHit(FVector VelocityChange,FVector HitLocation);
    bool TryRecover();
    void SetTuning(FMCPhysicsSettings NewSettings);
    void SaveTuning() const;
    void ResetTuning();
    void BuildPresentationPose(TArray<FTransform>& InOutPose) const;
    void SetGripArms(bool Left,bool Right,bool PhysicalLeft=false,bool PhysicalRight=false);
    bool IsPhysicalObjectGrip(bool Left) const { return Left?bPhysicalGripLeft:bPhysicalGripRight; }
    EMCActiveRagdollMode GetActiveRagdollMode() const { return ActiveRagdollMode; }
    bool SetActiveRagdollMode(EMCActiveRagdollMode Mode);
    void SubmitAnimationTargets(const TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt);
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
    void ConfigureStandingBody();
    bool UsesActiveMuscles() const { return ActiveRagdollMode!=EMCActiveRagdollMode::Off || bPhysicalGripLeft || bPhysicalGripRight; }
    void ConfigureGripConstraints();
    float ServerTime() const;
    UFUNCTION() void OnRep_Frame();
    UFUNCTION() void OnRep_Settings();
    UFUNCTION() void OnRep_ActiveRagdollMode();
    UPROPERTY(ReplicatedUsing=OnRep_ActiveRagdollMode) EMCActiveRagdollMode ActiveRagdollMode=EMCActiveRagdollMode::Soft;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Tooth;
    UPROPERTY() TObjectPtr<UPhysicsControlComponent> Muscles;
    UPROPERTY(ReplicatedUsing=OnRep_Frame) FMCRagdollFrame Frame;
    EMCBodyState LocalState = EMCBodyState::Standing;
    TArray<FTransform> DisplayPose;
    float SendAccumulator = 0.f;
    float LastHitTime = -10.f;
    float RecoveryInvulnerableUntil = 0.f;
    bool bGripLeft=false,bGripRight=false;
    bool bPhysicalGripLeft=false,bPhysicalGripRight=false;
    float StandingPhysicsWeight=1;
    float ArmPhysicsWeights[2]={1,1};
    float ArmSettleSeconds[2]={0,0};
    float GripReleaseSeconds[2]={0,0};
    FName BalanceControl;
    FTransform BalanceRest=FTransform::Identity;
    struct FJointTarget { FName Control,Role; int32 Parent=INDEX_NONE,Child=INDEX_NONE; };
    TArray<FJointTarget> JointTargets;
    struct FGripJoint { FName Name,Role; FConstraintProfileProperties Profile; };
    TArray<FGripJoint> GripJoints;
    FName HandControls[2];
};
