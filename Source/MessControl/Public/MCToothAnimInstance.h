#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MCToothAnimInstance.generated.h"

struct FMCFootContactDebug
{
    FVector Animated=FVector::ZeroVector,Target=FVector::ZeroVector;
    bool bHit=false,bPlanted=false;
    FName Surface;
};

/** Procedural local-bone pose, evaluated through a thread-safe proxy. Editable settings live on the character. */
UCLASS(Transient,Blueprintable)
class MESSCONTROL_API UMCToothAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    // Populated only when the optional motion recorder requests pre-physics poses.
    bool bRecordMotion=false;
    TArray<FTransform> DiagnosticPose;
    FMCFootContactDebug FootContacts[2];
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
