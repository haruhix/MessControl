#pragma once

#include "CoreMinimal.h"
#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCActiveSkillMotion.h"
#include "MCHookSkillAction.generated.h"

UENUM(BlueprintType)
enum class EMCHookCastPhase : uint8 { Inactive, Windup, Outbound, Pulling, Returning, Recovery };

/** A visible, swept cast. Only contact starts a pull; misses reel back without moving a pawn. */
UCLASS()
class MESSCONTROL_API AMCHookSkillAction : public AMCActiveSkillAction
{
    GENERATED_BODY()
public:
    AMCHookSkillAction();
    virtual bool CanStart() const override;
    virtual void StartCommitted() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FVector GetTetherStart() const;
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FVector GetTetherEnd() const;
    /** Rendered cable attachment at the measured tail; gameplay contact stays GetTetherEnd. */
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FVector GetTetherAttachmentLocation() const;
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FVector GetHookShaftWorldDirection() const;
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FVector GetHookCurlWorldDirection() const;
    UFUNCTION(BlueprintPure,Category="Skill|Hook") EMCHookCastPhase GetCastPhase() const { return CastPhase; }
    UFUNCTION(BlueprintPure,Category="Skill|Hook") float GetPhaseAlpha() const;
    /** Independent caster frame: never parent the glove to the travelling hook head. Game-thread only. */
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FTransform GetCasterHandFrame(bool bLeft=false) const;
    /** Additive actor-facing body pose, sampled by the native anim proxy on the game thread. */
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FTransform GetCastBodyOffset() const;
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FVector GetHookHeadLocation() const;
    UFUNCTION(BlueprintPure,Category="Skill|Hook") bool DidContactTarget() const { return bContactedTarget; }
    UFUNCTION(BlueprintPure,Category="Skill|Hook") double GetPhaseStartedAt() const { return PhaseStartedAt; }
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FRotator GetCastFacing() const { return CastFacing; }
    UFUNCTION(BlueprintPure,Category="Skill|Hook") FName GetPullStopReason() const { return PullStopReason; }
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Skill|Hook") TObjectPtr<AActor> HookTarget;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Skill|Hook") TObjectPtr<AMCToothCharacter> PulledPawn;
    /** Socket fitting may replace this approximate grip without changing gameplay aim. */
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation") FVector GripOffset=FVector(45,35,25);
protected:
    virtual void OnFinished(EMCActiveSkillEndReason Reason) override;
private:
    bool TraceFlight(FVector From,FVector To,FHitResult& Hit) const;
    bool BeginContact(const FHitResult& Hit);
    void Launch();
    void BeginReturn(EMCActiveSkillEndReason Reason);
    void StopPull(FName Gate,const FHitResult* Blocker=nullptr);
    void SetCastPhase(EMCHookCastPhase Phase,float Duration);
    void UpdatePresentation();
    FTransform HandLocal(bool bLeft) const;
    bool TargetStillValid() const;
    FMCActiveSkillMotionLease Motion;
    UPROPERTY(Replicated) FVector TargetLocalContact=FVector::ZeroVector;
    UPROPERTY(Replicated) EMCHookCastPhase CastPhase=EMCHookCastPhase::Inactive;
    UPROPERTY(Replicated) double PhaseStartedAt=0;
    UPROPERTY(Replicated) float PhaseDuration=0;
    UPROPERTY(Replicated) double ReleasedAt=0;
    UPROPERTY(Replicated) FVector FlightOrigin=FVector::ZeroVector;
    UPROPERTY(Replicated) FVector FlightDirection=FVector::ForwardVector;
    UPROPERTY(Replicated) FVector AimPoint=FVector::ZeroVector;
    UPROPERTY(Replicated) FRotator CastFacing=FRotator::ZeroRotator;
    UPROPERTY(Replicated) FVector ImpactPoint=FVector::ZeroVector;
    UPROPERTY(Replicated) bool bContactedTarget=false;
    UPROPERTY(Replicated) FName PullStopReason;
    UPROPERTY(Replicated) FTransform PhaseRightHand=FTransform::Identity;
    UPROPERTY(Replicated) FTransform PhaseLeftHand=FTransform::Identity;
    UPROPERTY(Replicated) FTransform PhaseBody=FTransform::Identity;
    UPROPERTY(Replicated) FTransform RestRightHand=FTransform::Identity;
    UPROPERTY(Replicated) FTransform RestLeftHand=FTransform::Identity;
    UPROPERTY(Replicated) FQuat PhaseVisualRotation=FQuat::Identity;
    FVector PreviousMovedPosition=FVector::ZeroVector;
    EMCActiveSkillEndReason ReturnReason=EMCActiveSkillEndReason::Completed;
    double EndsAt=0,LastSampleAt=0;
};

UCLASS()
class MESSCONTROL_API UMCHookSkillEffect : public UMCActiveSkillPerkEffect
{
    GENERATED_BODY()
public:
    UMCHookSkillEffect();
};
