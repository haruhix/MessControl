#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

void AMCToothCharacter::UpdateMouthCamera(float Dt)
{
    if(!IsLocallyControlled()) return;
    float CenterY=0;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {CenterY=It->GetActorLocation().Y;break;}
    const FVector P=GetActorLocation();
    // Anchor at the front of the mouth. Running towards the throat must never
    // carry the camera into the arena and leave the near edge behind it.
    const FVector Eye(-FMath::Clamp(OverviewDistance,1500.f,1780.f),CenterY,FMath::Clamp(OverviewHeight,500.f,700.f));
    const FVector Focus(-300,CenterY+(P.Y-CenterY)*.2,-40);
    Camera->FieldOfView=FMath::Clamp(OverviewFOV,75.f,95.f);
    Camera->AspectRatio=16.f/9.f;
    Camera->bOverrideAspectRatioAxisConstraint=true;
    Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);
    CameraBoom->bEnableCameraLag=false;
    FRotator Rotation=(Focus-Eye).Rotation();
    if(bMouthCameraInitialized) Rotation=FMath::RInterpTo(CameraBoom->GetComponentRotation(),Rotation,Dt,4);

    // Keep the whole avatar inside the viewport, including the near corners,
    // jumps and narrow windows. Only pan/tilt; the front anchor stays fixed.
    int32 Width=0,Height=0;
    if(auto* PC=Cast<APlayerController>(GetController())) PC->GetViewportSize(Width,Height);
    const float Aspect=Height>0?float(Width)/Height:Camera->AspectRatio;
    const float TanY=FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView*.5f))/Camera->AspectRatio;
    const float LimitX=FMath::RadiansToDegrees(FMath::Atan(TanY*Aspect*.86f));
    const float LimitY=FMath::RadiansToDegrees(FMath::Atan(TanY*.82f));
    const FVector Extent(GetCapsuleComponent()->GetScaledCapsuleRadius()+16,GetCapsuleComponent()->GetScaledCapsuleRadius()+16,
        GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+24);
    for(int32 Pass=0;Pass<4;++Pass) {
        float YawCorrection=0,PitchCorrection=0;
        for(int32 I=0;I<8;++I) {
            const FVector Corner=P+FVector(I&1?Extent.X:-Extent.X,I&2?Extent.Y:-Extent.Y,I&4?Extent.Z:-Extent.Z);
            const FVector Local=Rotation.UnrotateVector(Corner-Eye);
            const float Yaw=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
            const float Pitch=FMath::RadiansToDegrees(FMath::Atan2(Local.Z,Local.X));
            const float X=Yaw-FMath::Clamp(Yaw,-LimitX,LimitX),Y=Pitch-FMath::Clamp(Pitch,-LimitY,LimitY);
            if(FMath::Abs(X)>FMath::Abs(YawCorrection)) YawCorrection=X;
            if(FMath::Abs(Y)>FMath::Abs(PitchCorrection)) PitchCorrection=Y;
        }
        Rotation.Yaw+=YawCorrection; Rotation.Pitch+=PitchCorrection;
    }
    // The boom's sweep origin must stay in the air. Sweeping from the visual
    // focus inside the tongue made the arm collapse to zero length.
    const float Distance=700;
    CameraBoom->TargetOffset=Eye+Rotation.Vector()*Distance-P;
    CameraBoom->SetWorldRotation(Rotation);
    CameraBoom->TargetArmLength=Distance;
    bMouthCameraInitialized=true;
}
