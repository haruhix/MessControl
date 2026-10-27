#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MCGlideAnimation.h"
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
    // Published by the game-thread proxy before the expression owner samples its curves.
    FMCGlideAnimationSample AuthoredGlide;
    UFUNCTION(BlueprintPure,Category="Animation|Glide") float GetAuthoredGlideAlpha() const { return AuthoredGlide.Alpha; }
    UFUNCTION(BlueprintPure,Category="Animation|Glide") float GetAuthoredGlideTime() const { return AuthoredGlide.Time; }
    UFUNCTION(BlueprintPure,Category="Animation|Glide") bool IsAuthoredGlideStrong() const { return AuthoredGlide.Alpha>0.f && AuthoredGlide.bStrong; }
    // Populated only when the optional motion recorder requests pre-physics poses.
    bool bRecordMotion=false;
    TArray<FTransform> DiagnosticPose;
    FMCFootContactDebug FootContacts[2];
    float DiagnosticDashProgress=-1;
    FVector DiagnosticMeshUp=FVector::UpVector;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
