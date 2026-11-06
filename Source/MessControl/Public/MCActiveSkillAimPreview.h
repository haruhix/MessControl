#pragma once

#include "CoreMinimal.h"

enum class EMCActiveSkillAimState : uint8
{
    Clear,
    Contact,
    Blocked,
    Ally,
    Boss
};

/** Local HUD prediction. Gameplay validates the actual launch independently. */
struct FMCActiveSkillAimPreview
{
    TArray<FVector> Path;
    FVector ContactPoint=FVector::ZeroVector;
    FVector ContactNormal=FVector::UpVector;
    EMCActiveSkillAimState State=EMCActiveSkillAimState::Clear;
    float FuseSeconds=-1.f;
    bool bHasContact=false;
    bool bFuseWarning=false;
};

struct FMCActiveSkillAimPose
{
    FTransform PropLocal=FTransform::Identity;
    FTransform HandsLocal[2]={FTransform::Identity,FTransform::Identity};
    bool bHasLeftHand=false;
};
