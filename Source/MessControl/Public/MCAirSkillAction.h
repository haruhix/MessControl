#pragma once
#include "CoreMinimal.h"
#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCAirSkillAction.generated.h"
class AMCFoodActor;

/** Hold this active slot to suck; release the SAME press to blow retained fragments. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCAirSkillAction : public AMCActiveSkillAction
{
    GENERATED_BODY()
public:
    AMCAirSkillAction();
    virtual bool CanStart() const override;
    virtual void StartCommitted() override;
    virtual void ReleaseInput() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UFUNCTION(BlueprintPure,Category="Skill") float GetFillFraction() const;
    UFUNCTION(BlueprintPure,Category="Skill|Presentation") FVector GetIntakePoint() const;
    UFUNCTION(BlueprintPure,Category="Skill|Presentation") FVector GetIntakeForward() const;
    /** Artist guide in the native hero capsule frame. HeldXYZ add deltas from legacy (35,70,15). PropTransform owns the .55 mesh scale. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill|Presentation") FTransform HeldMount=FTransform::Identity;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill|Presentation") FVector IntakeForwardLocal=FVector::ForwardVector;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Skill") int32 StoredCount=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Skill") int32 BagCapacity=6;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Skill") bool bSucking=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Skill") bool bOverflowWarning=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Skill") bool bRuptured=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Skill") FVector_NetQuantizeNormal FlowDirection=FVector::ForwardVector;
protected:
    virtual void OnFinished(EMCActiveSkillEndReason Reason) override;
private:
    void UpdateHeldPose();
    void ScanFragments(float Step);
    void ReleaseContents(bool bBlow,bool bRupture);
    FVector IntakePoint() const;
    FVector SafeReleasePoint(AMCFoodActor* Food,FVector Proposed) const;
    UPROPERTY() TArray<TObjectPtr<AMCFoodActor>> Contents;
    double NextScanAt=0,FullSince=-1,HoldStartedAt=0,PresentationRecoveryEndsAt=-1;
    FVector LastAim=FVector::ForwardVector;
    bool bReleased=false;
};

UCLASS()
class MESSCONTROL_API UMCAirSkillEffect : public UMCActiveSkillPerkEffect
{
    GENERATED_BODY()
public:
    UMCAirSkillEffect();
};
