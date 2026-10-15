#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MCNutBossAnimInstance.generated.h"

class UAnimSequence;

enum class EMCNutBossClip : uint8 { Idle, Walk, WalkLeft, WalkRight, Melee, Jump, Transform, Cast, HeavyCast, Summon, Rain, Hit, Death, ChargeTell, ChargeLoop, ChargeRecovery, ShieldBreak, UnshieldedIdle, UnshieldedWalk, Melee2, MeleeCombo, Block, StunHit, BallEnd, HeadLeft, HeadRight, Count };
enum class EMCNutPoseBone : uint8 { Torso, Head, LeftArm, RightArm, LeftForearm, RightForearm, LeftHand, RightHand, Count };

struct FMCNutBossClipSample
{
    UAnimSequence* Clip=nullptr;
    float Seconds=0,Weight=0;
    bool bLoop=false;
    /** 0: action, 1: idle, 2: walking; locomotion crossfades on the animation thread. */
    uint8 LocomotionMode=0;
};

/** Written only on the game thread. Evaluation consumes values and pinned clip assets, never gameplay objects. */
struct FMCNutBossAnimationSnapshot
{
    TArray<FMCNutBossClipSample,TInlineAllocator<8>> Samples;
    int32 Bones[int32(EMCNutPoseBone::Count)]={INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE};
    int32 VisualKey=0;
    double ServerTime=0;
    float BlendSeconds=.25f,ForwardSpeed=0,SideSpeed=0,AimYaw=0,AimPitch=0;
    bool bMoving=false;
    float Cast=0,CastProgress=0,Release=0,Channel=0,Guard=0,Hit=0,ShieldHit=0,Death=0,Airborne=0,Melee=0,MeleePhase=0;
    float CastReleaseFraction=.55f;
    FVector HitDirection=FVector::ZeroVector;
    FVector CastEmitter=FVector::ZeroVector;
    FVector HandTipOffset=FVector(0,8,0);
    FQuat ActorToComponent=FQuat::Identity;
    bool bMage=false,bHasDeathClip=false,bHasMeleeClip=false;
    bool bAuthoredSpellGestures=false;
    float AuthoredTankMotionWeight=0,AuthoredTankHitWeight=0;

    /** Correct the palm at release/channel contact, leaving authored gathering and recovery gestures free. */
    float CastHandAlignmentWeight() const
    {
        if(bAuthoredSpellGestures) return 0.f;
        if(Channel>.5f) return Cast*.85f;
        const float ContactStart=FMath::Max(0.f,CastReleaseFraction-.15f);
        const float HoldEnd=FMath::Min(.95f,CastReleaseFraction+.07f);
        const float ContactEnd=FMath::Min(1.f,HoldEnd+.16f);
        return Cast*.85f*FMath::SmoothStep(ContactStart,CastReleaseFraction,CastProgress)
            *(1-FMath::SmoothStep(HoldEnd,ContactEnd,CastProgress));
    }
};

/** Native pose blending and small bounded overlays for both authored nut rigs. No Blueprint animation graph is required. */
UCLASS(Transient, Blueprintable)
class MESSCONTROL_API UMCNutBossAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UMCNutBossAnimInstance();
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
