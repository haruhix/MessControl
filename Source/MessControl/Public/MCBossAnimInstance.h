#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MCBossAnimInstance.generated.h"

class UAnimSequence;
struct FMCBossAnimInstanceProxy;

/** A timestamped sequence contributing to the current crossfade. */
USTRUCT()
struct FMCBossAnimationLayer
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Sequence;
    double StartedAt=0.;
    float PlayRate=1.f;
    float StartWeight=0.f;
    float Weight=0.f;
    bool bLoop=false;
};

/** Native full-body crossfades; playback follows server time and never dispatches notifies. */
UCLASS(Transient)
class MESSCONTROL_API UMCBossAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** Attack clips start at full weight and finish before accepting another sequence. */
    void PresentSequence(UAnimSequence* Sequence,double StartedAt,double Now,float PlayRate,bool bLoop,float BlendSeconds,bool bPlayToEnd=false,bool bForceInterrupt=false);
    UPROPERTY(Transient, BlueprintReadOnly, Category="Boss|Animation") TObjectPtr<UAnimSequence> CurrentSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Boss|Animation") float SequencePosition=0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="Boss|Animation") float TransitionAlpha=1.f;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
private:
    friend struct FMCBossAnimInstanceProxy;
    void UpdateWeights(double Now);
    UPROPERTY(Transient) TArray<FMCBossAnimationLayer> Layers;
    double SampleTime=0.;
    double TransitionStartedAt=0.;
    float TransitionSeconds=0.f;
    bool bFinishCurrentSequence=false;
};
