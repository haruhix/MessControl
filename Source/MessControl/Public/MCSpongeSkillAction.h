#pragma once

#include "CoreMinimal.h"
#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCActiveSkillMotion.h"
#include "MCSpongeSkillAction.generated.h"

/** Grounded CMC ride; the sponge is a visual/cleaning footprint, not a second physics vehicle. */
UCLASS()
class MESSCONTROL_API AMCSpongeSkillAction : public AMCActiveSkillAction
{
    GENERATED_BODY()
public:
    AMCSpongeSkillAction();
    virtual bool CanStart() const override;
    virtual void StartCommitted() override;
    virtual bool RepeatInput() override;
    virtual void Tick(float DeltaSeconds) override;
    /** Top-deck pivot; fitting the current player's feet is presentation-owned. */
    UFUNCTION(BlueprintPure,Category="Skill|Ride") FTransform GetRideFrame() const { return GetActorTransform(); }
    /** Authority diagnostics; the action transform remains the replicated visual heading. */
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Skill|Ride") int32 BounceCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Skill|Ride") FVector LastBounceNormal=FVector::ZeroVector;
    /** Reflected preload root; world-space history outlives this action's presentation. */
    UPROPERTY(EditDefaultsOnly,Category="Skill|VFX") TSoftObjectPtr<UNiagaraSystem> SoapTrailVFX;
    UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="Skill|VFX") TObjectPtr<UNiagaraComponent> SoapTrailComponent;
protected:
    virtual void OnFinished(EMCActiveSkillEndReason Reason) override;
private:
    void UpdateRideVFX();
    void UpdateSoapTrail();
    void EndSoapTrail(bool bImmediate);
    FMCActiveSkillMotionLease Motion;
    FVector Heading=FVector::ForwardVector,PreviousFoot=FVector::ZeroVector;
    double EndsAt=0,LastSampleAt=0,SteeringLockedUntil=0;
    FVector LastSoapPoint=FVector::ZeroVector;
    double LastSoapAt=0;
    bool bHasSoapPoint=false;
};

UCLASS()
class MESSCONTROL_API UMCSpongeSkillEffect : public UMCActiveSkillPerkEffect
{
    GENERATED_BODY()
public:
    UMCSpongeSkillEffect();
};
