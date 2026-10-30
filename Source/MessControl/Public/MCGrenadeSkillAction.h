#pragma once

#include "CoreMinimal.h"
#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCGrenadeSkillAction.generated.h"

class USphereComponent;
class UProjectileMovementComponent;

UENUM(BlueprintType)
enum class EMCGrenadeSkillPhase : uint8 { Prepared, Cooking, Thrown, Dropped, Detonated };

/** One committed charge. Its immutable fuse outlives the input, slot and source pawn. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCGrenadeSkillAction : public AMCActiveSkillAction
{
    GENERATED_BODY()
public:
    AMCGrenadeSkillAction();
    virtual bool CanStart() const override;
    virtual void StartCommitted() override;
    virtual void ReleaseInput() override;
    virtual void Cancel(EMCActiveSkillEndReason Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool GetPresentationHandTarget(bool bLeft,FTransform& WorldPose) const override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    /** Exact guide pose in native capsule space; HeldXYZ add deltas from legacy (50,44,25). Mesh bounds center is the physical projectile center. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill|Presentation") FTransform HeldMount=FTransform::Identity;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Grenade") TObjectPtr<USphereComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Grenade") TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Grenade") EMCGrenadeSkillPhase Phase=EMCGrenadeSkillPhase::Prepared;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Grenade") double ArmedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Grenade") double FuseEndsAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Grenade") int32 DetonationCount=0;
protected:
    virtual void BeginPlay() override;
    virtual void OnFinished(EMCActiveSkillEndReason Reason) override;
private:
    UFUNCTION() void OnRep_Phase();
    FVector HeldOffset() const;
    float FuseSeconds() const;
    void FollowHand();
    void PresentGroundWarning();
    void Drop();
    void Launch(FVector Velocity,EMCGrenadeSkillPhase Next);
    void Detonate();
    double DropAt=0;
    double FinishAt=0;
    double PresentationRecoveryEndsAt=-1;
    bool bWarningPresented=false;
};

/** Factory only; no lifecycle state lives in the depleted effect UObject. */
UCLASS()
class MESSCONTROL_API UMCGrenadeSkillPerkEffect : public UMCActiveSkillPerkEffect
{
    GENERATED_BODY()
public:
    UMCGrenadeSkillPerkEffect();
};
