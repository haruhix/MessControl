#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "MCPlayerCameraComponent.generated.h"

/** Player camera with post-process depth of field focused on the avatar. */
UCLASS()
class MESSCONTROL_API UMCPlayerCameraComponent : public UCameraComponent
{
    GENERATED_BODY()
public:
    UMCPlayerCameraComponent();
    virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Depth of Field") bool bAutoFocusPlayer=true;
};
