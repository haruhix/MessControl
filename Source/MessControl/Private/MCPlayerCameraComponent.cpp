#include "MCPlayerCameraComponent.h"
#include "MCToothCharacter.h"

UMCPlayerCameraComponent::UMCPlayerCameraComponent()
{
    PostProcessBlendWeight=1.f;
    PostProcessSettings.bOverride_DepthOfFieldEnabled=true;
    PostProcessSettings.DepthOfFieldEnabled=true;
    PostProcessSettings.bOverride_DepthOfFieldFstop=true;
    PostProcessSettings.DepthOfFieldFstop=1.8f;
    PostProcessSettings.bOverride_DepthOfFieldSensorWidth=true;
    PostProcessSettings.DepthOfFieldSensorWidth=36.f;
    PostProcessSettings.bOverride_DepthOfFieldFocalDistance=true;
    PostProcessSettings.DepthOfFieldFocalDistance=900.f;
}

void UMCPlayerCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
    Super::GetCameraView(DeltaTime,DesiredView);
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    if (!Hero || !bAutoFocusPlayer || PostProcessBlendWeight<=0.f || !PostProcessSettings.bOverride_DepthOfFieldFocalDistance) return;
    const FVector Focus=Hero->GetCameraFocusLocation();
    // Use the final view after the spring arm's collision correction, and focus
    // on the avatar's depth plane even when the follow camera frames it off-center.
    const float Distance=FMath::Max(10.f,float(FVector::DotProduct(Focus-DesiredView.Location,DesiredView.Rotation.Vector())));
    PostProcessSettings.DepthOfFieldFocalDistance=Distance;
    DesiredView.PostProcessSettings.DepthOfFieldFocalDistance=Distance;
}
