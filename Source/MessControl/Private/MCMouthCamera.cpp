#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "MCThroat.h"
#include "MCRewardChest.h"
#include "MCInventoryComponent.h"
#include "MCOrbitSpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

namespace {
    const FVector OrbitPivotOffset(0,0,30);constexpr float OrbitPitchLimit=85.f;
    // Looking above the horizon lifts the view without swinging the eye into
    // the floor and retracting the arm against the avatar.
    FVector OrbitEyeAxis(const FRotator& View) {return FRotator(FMath::Min(View.Pitch,0.),View.Yaw,0).Vector();}
}

FVector AMCToothCharacter::OrbitCameraPivot() const
{ return FVector(0,0,FMath::Clamp(CameraOrbitHeight,30.f,250.f)); }

FVector AMCToothCharacter::GetCameraFocusLocation() const
{
    if(IsValid(MimicCaptor)) return MimicCaptor->GetMimicCaptureLocation()+FVector(0,0,20);
    return bMouthCameraHeld?FVector(ThroatCaptureStart)+OrbitPivotOffset:GetActorLocation()+OrbitCameraPivot();
}

void AMCToothCharacter::InitializeCameraOrbit()
{
    if (bManualCameraOrbit) return;
    const FVector Eye=Camera->GetComponentLocation();
    const FRotator View=Camera->GetComponentRotation();
    const FVector Pivot=OrbitCameraPivot()+SprayCameraOffset;
    CameraOrbitYaw=float(View.Yaw); CameraOrbitPitch=FMath::Clamp(float(View.Pitch),-OrbitPitchLimit,OrbitPitchLimit);
    CameraOrbitDistance=FMath::Clamp(float(FVector::Dist(Eye,GetActorLocation()+Pivot)),250.f,1600.f);
    CameraOrbitViewDistance=CameraOrbitDistance;
    CameraOrbitViewRotation=FRotator(CameraOrbitPitch,CameraOrbitYaw,0);
    // Start at the current view, then ease the pivot onto the avatar.
    MouthCameraFocus=Eye+OrbitEyeAxis(CameraOrbitViewRotation)*CameraOrbitDistance-Pivot;
    bManualCameraOrbit=true; bMouthCameraInitialized=true;
}

void AMCToothCharacter::ApplyCameraOrbitInput(FVector2D Delta)
{
    const auto* PC=Cast<APlayerController>(GetController());
    if (!IsLocallyControlled() || bMouthCameraHeld || Delta.ContainsNaN() || Delta.IsNearlyZero() || (PC && PC->IsLookInputIgnored())) return;
    InitializeCameraOrbit();
    const float Sensitivity=FMath::Clamp(CameraOrbitSensitivity,.02f,1.f);
    CameraOrbitYaw=FRotator::NormalizeAxis(CameraOrbitYaw+Delta.X*Sensitivity);
    CameraOrbitPitch=FMath::Clamp(CameraOrbitPitch+Delta.Y*Sensitivity,-OrbitPitchLimit,OrbitPitchLimit);
}

void AMCToothCharacter::ZoomCamera(float ScrollDelta)
{
    const auto* PC=Cast<APlayerController>(GetController());
    if (!IsLocallyControlled() || bMouthCameraHeld || !FMath::IsFinite(ScrollDelta) || FMath::IsNearlyZero(ScrollDelta) || (PC && PC->IsLookInputIgnored())) return;
    InitializeCameraOrbit();
    CameraOrbitDistance=FMath::Clamp(CameraOrbitDistance-ScrollDelta*FMath::Clamp(CameraZoomStep,10.f,300.f),250.f,1600.f);
}

FVector AMCToothCharacter::CameraMoveDirection(bool Right) const
{
    const FRotator Yaw(0,bManualCameraOrbit?Camera->GetComponentRotation().Yaw:0,0);
    return Yaw.RotateVector(Right?FVector::RightVector:FVector::ForwardVector);
}

void AMCToothCharacter::UpdateMouthCamera(float Dt)
{
    APlayerController* Viewer=nullptr;
    for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* PC=It->Get();PC && PC->IsLocalController()) {
            const AActor* Destination=PC->PlayerCameraManager && PC->PlayerCameraManager->PendingViewTarget.Target
                ?PC->PlayerCameraManager->PendingViewTarget.Target.Get():PC->GetViewTarget();
            if(Destination==this) {Viewer=PC;break;}
        }
    if (auto* Arm=Cast<UMCOrbitSpringArmComponent>(CameraBoom)) {
        Arm->SetWallProbeActive(Viewer!=nullptr);
        Arm->SetIgnoredViewActor(Viewer?MimicCaptor.Get():nullptr);
    }
    if(!Viewer) { ClearCameraWallReveal(); return; }
    const bool Aiming=Inventory && Inventory->Selected==EMCToolSlot::Spray && CanWork() && !bInCoffee && Inventory->ShouldPresentTool();
    // Keep the centre ray beside the avatar, with the ordinary collision sweep
    // still beginning at the avatar instead of beyond the shoulder offset.
    const FVector Shoulder=Camera->GetRightVector()*140+FVector::UpVector*75;
    SprayCameraOffset=FMath::VInterpTo(SprayCameraOffset,Aiming?Shoulder:FVector::ZeroVector,Dt,8.f);
    const FVector P=GetActorLocation();
    float Suction=0;
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It)
        Suction=FMath::Max(Suction,It->GetAmbientSuctionStrengthAt(P));
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    // A gentle lens pulse makes the whole room's intake readable while keeping
    // the player's chosen aim and the collision sweep steady.
    Camera->FieldOfView=FMath::Clamp(FollowFOV,45.f,95.f)+Suction*(2.4f+.35f*FMath::Sin(float(Now*10)));
    if(IsValid(MimicCaptor)) {
        const FVector Focus=GetCameraFocusLocation();
        const float Blend=1-FMath::Exp(-FMath::Max(1.f,FollowSpeed)*FMath::Max(0.f,Dt));
        const FRotator Wanted=bManualCameraOrbit?FRotator(CameraOrbitPitch,CameraOrbitYaw,0):MimicCameraRotation;
        const FRotator View=FMath::RInterpTo(Camera->GetComponentRotation(),Wanted,Dt,FollowSpeed);
        const float Distance=bManualCameraOrbit?CameraOrbitDistance:MimicCameraDistance;
        MouthCameraEye=Focus-View.Vector()*FMath::Clamp(Distance,400.f,1600.f);
        MouthCameraFocus=Focus-OrbitPivotOffset;
        CameraOrbitViewRotation=View; CameraOrbitViewDistance=FMath::Lerp(CameraOrbitViewDistance,Distance,Blend);
        CameraBoom->bEnableCameraLag=false; CameraBoom->bDoCollisionTest=true; CameraBoom->ProbeChannel=ECC_Camera;
        // The pawn travels into the box; its camera sweep remains based at the
        // chest and ignores only that captor, retaining other world obstacles.
        CameraBoom->TargetOffset=Focus-P;
        CameraBoom->SetWorldRotation(View); CameraBoom->TargetArmLength=FVector::Dist(Focus,MouthCameraEye);
        Camera->SetWorldRotation(View);
        Camera->AspectRatio=16.f/9.f; Camera->bOverrideAspectRatioAxisConstraint=true;
        Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);
        bMouthCameraInitialized=true;
        UpdateCameraWallReveal(Dt,MouthCameraEye,Focus); return;
    }
    if(bMouthCameraHeld && bMouthCameraInitialized) {
        if (bManualCameraOrbit) {
            const FVector PreviousRoot=CameraBoom->GetUnfixedCameraPosition()+CameraBoom->GetComponentRotation().Vector()*CameraBoom->TargetArmLength-CameraBoom->TargetOffset;
            MouthCameraEye=Camera->GetComponentLocation()-(P-PreviousRoot);
            CameraBoom->TargetArmLength=1;
        }
        // The arm is attached to the pawn. Compensate its root translation so
        // both the camera and its short collision sweep stay in the mouth.
        CameraBoom->TargetOffset=MouthCameraEye+CameraBoom->GetComponentRotation().Vector()*CameraBoom->TargetArmLength-P;
        UpdateCameraWallReveal(Dt,MouthCameraEye,ThroatCaptureStart+OrbitPivotOffset);
        return;
    }
    if (bManualCameraOrbit)
    {
        const bool Reset=!bMouthCameraInitialized || FVector::DistSquared(P,MouthCameraFocus)>FMath::Square(1400.f);
        const float Blend=1-FMath::Exp(-FMath::Max(1.f,FollowSpeed)*FMath::Max(0.f,Dt));
        MouthCameraFocus=Reset?P:FMath::Lerp(MouthCameraFocus,P,Blend);
        const FRotator Wanted(CameraOrbitPitch,CameraOrbitYaw,0);
        const FRotator View=Reset?Wanted:FMath::RInterpTo(CameraOrbitViewRotation,Wanted,Dt,FollowSpeed);
        CameraOrbitViewRotation=View;
        // The arm aims its sweep from the actual pawn to the filtered eye.
        // Its rotation contains raw floor motion and must not steer the view.
        Camera->SetWorldRotation(View);
        CameraBoom->bEnableCameraLag=false;
        CameraBoom->bDoCollisionTest=true; CameraBoom->ProbeChannel=ECC_Camera;
        CameraOrbitViewDistance=Reset?CameraOrbitDistance:FMath::Lerp(CameraOrbitViewDistance,CameraOrbitDistance,Blend);
        MouthCameraEye=MouthCameraFocus+OrbitCameraPivot()+SprayCameraOffset-OrbitEyeAxis(View)*CameraOrbitViewDistance;
        // Trace from the actual pawn, so follow lag cannot put the sweep origin inside a wall at a corner.
        const FVector Arm=P+OrbitPivotOffset-MouthCameraEye;
        CameraBoom->TargetOffset=OrbitPivotOffset;
        CameraBoom->SetWorldRotation(Arm.Rotation());
        CameraBoom->TargetArmLength=Arm.Size();
        Camera->AspectRatio=16.f/9.f; Camera->bOverrideAspectRatioAxisConstraint=true;
        Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);
        bMouthCameraInitialized=true;
        UpdateCameraWallReveal(Dt,MouthCameraEye,P+OrbitPivotOffset);
        return;
    }
    const FVector TrackedP=bMouthCameraHeld?ThroatCaptureStart:P;
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
    const bool Reset=!bMouthCameraInitialized || FVector::DistSquared(TrackedP,MouthCameraFocus)>FMath::Square(1400.f);
    if(Reset) MouthCameraFocus=TrackedP;
    else for(int32 Axis=0;Axis<3;++Axis) {
        const double Delta=TrackedP[Axis]-MouthCameraFocus[Axis],Zone=FMath::Max(0.,CameraDeadZone[Axis]);
        MouthCameraFocus[Axis]+=Delta-FMath::Clamp(Delta,-Zone,Zone);
    }
    const float ViewLift=OrbitCameraPivot().Z-OrbitPivotOffset.Z;
    const FVector Focus=MouthCameraFocus+CameraFocusOffset+SprayCameraOffset+FVector(0,0,ViewLift);
    const FVector Offset(-FMath::Clamp(FollowDistance,400.f,1600.f),(CenterY-MouthCameraFocus.Y)*.70,0);
    FVector WantedEye=ClampEye(MouthCameraFocus+Offset+FVector(0,0,FMath::Clamp(FollowHeight,150.f,700.f)+ViewLift));
    // At the front rim preserve the view height; shrinking height with distance
    // caused the camera to collapse into the player when translation was clamped.
    WantedEye.Z=MouthCameraFocus.Z+FMath::Clamp(FollowHeight,150.f,700.f)+ViewLift;
    WantedEye=ClampEye(WantedEye);
    const float Blend=1-FMath::Exp(-FMath::Max(1.f,FollowSpeed)*FMath::Max(0.f,Dt));
    MouthCameraEye=ClampEye(Reset?WantedEye:FMath::Lerp(MouthCameraEye,WantedEye,Blend));
    const FVector Eye=MouthCameraEye;
    Camera->AspectRatio=16.f/9.f;
    Camera->bOverrideAspectRatioAxisConstraint=true;
    Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);
    CameraBoom->bEnableCameraLag=false;
    FRotator Rotation=(Focus-Eye).Rotation();
    if(!Reset) Rotation=FMath::RInterpTo(Camera->GetComponentRotation(),Rotation,Dt,FollowSpeed);

    // At the arena boundary translation stops. Pan/tilt still keeps the whole
    // avatar in the viewport, including jumps and narrower aspect ratios.
    int32 Width=0,Height=0;
    Viewer->GetViewportSize(Width,Height);
    const float Aspect=Height>0?float(Width)/Height:Camera->AspectRatio;
    const float TanY=FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView*.5f))/Camera->AspectRatio;
    const float LimitX=FMath::RadiansToDegrees(FMath::Atan(TanY*Aspect*.86f));
    const float LimitY=FMath::RadiansToDegrees(FMath::Atan(TanY*.82f));
    const FVector Extent(GetCapsuleComponent()->GetScaledCapsuleRadius()+16,GetCapsuleComponent()->GetScaledCapsuleRadius()+16,
        GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+24);
    // Movement and the cached player view can update after this actor tick.
    // Fit the next movement step too, so low frame rates cannot carry the
    // avatar past the screen edge before the next camera update.
    const FVector NextP=TrackedP+(bMouthCameraHeld?FVector::ZeroVector:GetVelocity())*FMath::Clamp(Dt,0.f,.15f);
    for(int32 Pass=0;Pass<4;++Pass) {
        float YawCorrection=0,PitchCorrection=0;
        for(int32 I=0;I<16;++I) {
            const FVector Corner=(I&8?NextP:TrackedP)+FVector(I&1?Extent.X:-Extent.X,I&2?Extent.Y:-Extent.Y,I&4?Extent.Z:-Extent.Z);
            const FVector Local=Rotation.UnrotateVector(Corner-Eye);
            const float Yaw=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
            const float Pitch=FMath::RadiansToDegrees(FMath::Atan2(Local.Z,Local.X));
            const float X=Yaw-FMath::Clamp(Yaw,-LimitX,LimitX),Y=Pitch-FMath::Clamp(Pitch,-LimitY,LimitY);
            if(FMath::Abs(X)>FMath::Abs(YawCorrection)) YawCorrection=X;
            if(FMath::Abs(Y)>FMath::Abs(PitchCorrection)) PitchCorrection=Y;
        }
        Rotation.Yaw+=YawCorrection; Rotation.Pitch+=PitchCorrection;
    }
    // Validate the whole path from the avatar. A short sweep at the filtered
    // eye can start behind food or inside an arena tooth after follow lag.
    const FVector Arm=P+OrbitPivotOffset-Eye;
    CameraBoom->bDoCollisionTest=true; CameraBoom->ProbeChannel=ECC_Camera;
    CameraBoom->TargetOffset=OrbitPivotOffset;
    CameraBoom->SetWorldRotation(Arm.Rotation());
    CameraBoom->TargetArmLength=Arm.Size();
    Camera->SetWorldRotation(Rotation);
    bMouthCameraInitialized=true;
    UpdateCameraWallReveal(Dt,MouthCameraEye,P+OrbitPivotOffset);
}
