#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MCNutBossAnimInstance.generated.h"

class UAnimSequence;

enum class EMCNutBossClip : uint8 { Idle, Walk, WalkLeft, WalkRight, Melee, Jump, Transform, Cast, HeavyCast, Summon, Rain, Hit, Death, ChargeTell, ChargeLoop, ChargeRecovery, Count };
enum class EMCNutPoseBone : uint8 { Torso, Head, LeftArm, RightArm, LeftForearm, RightForearm, LeftHand, RightHand, Count };

struct FMCNutBossClipSample
{
    UAnimSequence* Clip=nullptr;
    float Seconds=0,Weight=0;
    bool bLoop=false;
};

/** Written only on the game thread. Evaluation consumes values and pinned clip assets, never gameplay objects. */
struct FMCNutBossAnimationSnapshot
{
    TArray<FMCNutBossClipSample,TInlineAllocator<8>> Samples;
    int32 Bones[int32(EMCNutPoseBone::Count)]={INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE};
    int32 VisualKey=0;
    double ServerTime=0;
    float BlendSeconds=.18f,ForwardSpeed=0,SideSpeed=0,AimYaw=0,AimPitch=0;
    float Cast=0,CastProgress=0,Release=0,Channel=0,Guard=0,Hit=0,ShieldHit=0,Death=0,Airborne=0;
    FVector HitDirection=FVector::ZeroVector;
    FVector CastEmitter=FVector::ZeroVector;
    FVector HandTipOffset=FVector(0,8,0);
    FQuat ActorToComponent=FQuat::Identity;
    bool bMage=false,bHasDeathClip=false;
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
