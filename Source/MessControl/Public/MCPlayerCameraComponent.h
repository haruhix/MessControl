#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "MCPlayerCameraComponent.generated.h"

/** Player autofocus and short, additive physical impact feedback. */
UCLASS()
class MESSCONTROL_API UMCPlayerCameraComponent : public UCameraComponent
{
    GENERATED_BODY()
public:
    UMCPlayerCameraComponent();
    virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
    void AddImpact(float ImpactSpeed, const FVector& WorldDirection);
    void AddGroundImpact(float Strength,const FVector& Source);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Depth of Field") bool bAutoFocusPlayer=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Impact",meta=(ClampMin="0",ClampMax="2")) float ImpactShakeScale=1.f;
private:
    double ImpactStartedAt=-1.;
    float ImpactStrength=0.f;
    FVector ImpactDirection=FVector::UpVector;
    double GroundImpactStartedAt=-1.;
    float GroundImpactStrength=0.f;
    FVector GroundImpactSource=FVector::ZeroVector;
};
