#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

void AMCToothCharacter::UpdateMouthCamera(float Dt)
{
    if(!IsLocallyControlled()) return;
    const FVector P=GetActorLocation();
    float CenterY=0;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {CenterY=It->GetActorLocation().Y;break;}
    if(!MouthCameraBounds.IsValid())
        for(TActorIterator<AActor> It(GetWorld());It;++It)
            if(It->ActorHasTag(TEXT("MCCameraBounds")) && It->FindComponentByClass<UBoxComponent>()) {MouthCameraBounds=*It;break;}
    const auto* Box=MouthCameraBounds.IsValid()?MouthCameraBounds->FindComponentByClass<UBoxComponent>():nullptr;
    FVector ArenaCenter(0,CenterY,350),ArenaExtent(1250,600,300);
    if(const auto* GS=GetWorld()->GetGameState<AMCGameState>();GS && GS->DayPlan) {
        ArenaCenter.X=GS->DayPlan->ArenaCenter.X;ArenaCenter.Y=GS->DayPlan->ArenaCenter.Y;
        ArenaExtent.X=GS->DayPlan->ArenaHalfSize.X;ArenaExtent.Y=GS->DayPlan->ArenaHalfSize.Y*.7;
    }
    auto ClampEye=[&](FVector Value) {
        if(Box) {
            const FTransform T=Box->GetComponentTransform();
            const FVector Extent=(Box->GetUnscaledBoxExtent()-FVector(25)).ComponentMax(FVector(1));
            return T.TransformPosition(T.InverseTransformPosition(Value).BoundToBox(-Extent,Extent));
        }
        return Value.BoundToBox(ArenaCenter-ArenaExtent,ArenaCenter+ArenaExtent);
    };
    const bool Reset=!bMouthCameraInitialized || FVector::DistSquared(P,MouthCameraFocus)>FMath::Square(1400.f);
    if(Reset) MouthCameraFocus=P;
    else for(int32 Axis=0;Axis<3;++Axis) {
        const double Delta=P[Axis]-MouthCameraFocus[Axis],Zone=FMath::Max(0.,CameraDeadZone[Axis]);
        MouthCameraFocus[Axis]+=Delta-FMath::Clamp(Delta,-Zone,Zone);
    }
    const FVector Focus=MouthCameraFocus+FVector(80,0,75);
    const FVector Offset(-FMath::Clamp(FollowDistance,400.f,1600.f),(CenterY-MouthCameraFocus.Y)*.70,0);
    FVector WantedEye=ClampEye(MouthCameraFocus+Offset+FVector(0,0,FMath::Clamp(FollowHeight,150.f,700.f)));
    // At the front rim preserve the view height; shrinking height with distance
    // caused the camera to collapse into the player when translation was clamped.
    WantedEye.Z=MouthCameraFocus.Z+FMath::Clamp(FollowHeight,150.f,700.f);
    WantedEye=ClampEye(WantedEye);
    const float Blend=1-FMath::Exp(-FMath::Max(1.f,FollowSpeed)*FMath::Max(0.f,Dt));
    MouthCameraEye=ClampEye(Reset?WantedEye:FMath::Lerp(MouthCameraEye,WantedEye,Blend));
    const FVector Eye=MouthCameraEye;
    Camera->FieldOfView=FMath::Clamp(FollowFOV,45.f,95.f);
    Camera->AspectRatio=16.f/9.f;
    Camera->bOverrideAspectRatioAxisConstraint=true;
    Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);
    CameraBoom->bEnableCameraLag=false;
    FRotator Rotation=(Focus-Eye).Rotation();
    if(!Reset) Rotation=FMath::RInterpTo(CameraBoom->GetComponentRotation(),Rotation,Dt,FollowSpeed);

    // At the arena boundary translation stops. Pan/tilt still keeps the whole
    // avatar in the viewport, including jumps and narrower aspect ratios.
    int32 Width=0,Height=0;
    if(auto* PC=Cast<APlayerController>(GetController())) PC->GetViewportSize(Width,Height);
    const float Aspect=Height>0?float(Width)/Height:Camera->AspectRatio;
    const float TanY=FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView*.5f))/Camera->AspectRatio;
    const float LimitX=FMath::RadiansToDegrees(FMath::Atan(TanY*Aspect*.86f));
    const float LimitY=FMath::RadiansToDegrees(FMath::Atan(TanY*.82f));
    const FVector Extent(GetCapsuleComponent()->GetScaledCapsuleRadius()+16,GetCapsuleComponent()->GetScaledCapsuleRadius()+16,
        GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+24);
    // Movement and the cached player view can update after this actor tick.
    // Fit the next movement step too, so low frame rates cannot carry the
    // avatar past the screen edge before the next camera update.
    const FVector NextP=P+GetVelocity()*FMath::Clamp(Dt,0.f,.15f);
    for(int32 Pass=0;Pass<4;++Pass) {
        float YawCorrection=0,PitchCorrection=0;
        for(int32 I=0;I<16;++I) {
            const FVector Corner=(I&8?NextP:P)+FVector(I&1?Extent.X:-Extent.X,I&2?Extent.Y:-Extent.Y,I&4?Extent.Z:-Extent.Z);
            const FVector Local=Rotation.UnrotateVector(Corner-Eye);
            const float Yaw=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
            const float Pitch=FMath::RadiansToDegrees(FMath::Atan2(Local.Z,Local.X));
            const float X=Yaw-FMath::Clamp(Yaw,-LimitX,LimitX),Y=Pitch-FMath::Clamp(Pitch,-LimitY,LimitY);
            if(FMath::Abs(X)>FMath::Abs(YawCorrection)) YawCorrection=X;
            if(FMath::Abs(Y)>FMath::Abs(PitchCorrection)) PitchCorrection=Y;
        }
        Rotation.Yaw+=YawCorrection; Rotation.Pitch+=PitchCorrection;
    }
    // A short sweep stays above the tongue. Keep both endpoints inside the
    // camera volume, including the endpoint used when collision retracts it.
    float Distance=180;
    while(Distance>1 && !ClampEye(Eye+Rotation.Vector()*Distance).Equals(Eye+Rotation.Vector()*Distance,.1)) Distance*=.5f;
    CameraBoom->TargetOffset=Eye+Rotation.Vector()*Distance-P;
    CameraBoom->SetWorldRotation(Rotation);
    CameraBoom->TargetArmLength=Distance;
    bMouthCameraInitialized=true;
}
