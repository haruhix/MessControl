#pragma once

#include "CoreMinimal.h"
#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCActiveSkillMotion.h"
#include "MCDriverSkillAction.generated.h"

class UMCSuperCarVisualComponent;

/** Three-second CMC ride with a full visual footprint and native mask/contact results. */
UCLASS()
class MESSCONTROL_API AMCDriverSkillAction : public AMCActiveSkillAction
{
    GENERATED_BODY()
public:
    AMCDriverSkillAction();
    virtual bool CanStart() const override;
    virtual void StartCommitted() override;
    virtual void Tick(float DeltaSeconds) override;
    /** Reusable presentation; gameplay/motion authority remains in this action. */
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Skill|Presentation") TObjectPtr<UMCSuperCarVisualComponent> SuperCarVisual;
    /** Compatibility overrides mirrored into the reusable visual component. */
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation") TMap<FName,TSoftObjectPtr<UStaticMesh>> PartAssets;
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation") TMap<FName,FTransform> PartTransforms;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Skill|Presentation") TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    /** Pelvis registered from the supplied seated tooth guide; centimeters in the ride frame. */
    UPROPERTY(EditDefaultsOnly,Category="Skill|Presentation") FVector SeatLocal=FVector(-31.229538f,1.673586f,98.544419f);
    UFUNCTION(BlueprintPure,Category="Skill|Ride") FTransform GetRideFrame() const;
    UFUNCTION(BlueprintPure,Category="Skill|Ride") FTransform GetHandFrame(bool bLeft) const;
    UFUNCTION(BlueprintPure,Category="Skill|Ride") FTransform GetFootFrame(bool bLeft) const;
protected:
    virtual void BeginPlay() override;
    virtual void OnFinished(EMCActiveSkillEndReason Reason) override;
private:
    void RunOver(FVector From,FVector To,float Seconds);
    void UpdateParts(float Distance,float DeltaSeconds);
    void UpdateRideVFX();
    FMCActiveSkillMotionLease Motion;
    FVector Heading=FVector::ForwardVector,PreviousFoot=FVector::ZeroVector,PreviousClean=FVector::ZeroVector;
    FVector PreviousVisual=FVector::ZeroVector;
    FVector PreviousVisualHeading=FVector::ForwardVector;
    double EndsAt=0,LastSampleAt=0;
    TSet<TWeakObjectPtr<AMCToothCharacter>> ContactedAllies;
};

UCLASS()
class MESSCONTROL_API UMCDriverSkillEffect : public UMCActiveSkillPerkEffect
{
    GENERATED_BODY()
public:
    UMCDriverSkillEffect();
};
