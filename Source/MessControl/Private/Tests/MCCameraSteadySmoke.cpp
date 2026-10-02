#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"

void MCTickCameraSteadyValidation(UWorld* World)
{
    struct FRun { TWeakObjectPtr<UWorld> World; float Age=0,At=0,MaxAngle=0; int32 Samples=0; bool Setup=false; };
    static FRun R; if(R.World.Get()!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* PC=World->GetFirstPlayerController(); auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* GS=World->GetGameState<AMCGameState>(); if(!H || !GS || R.Age<3) return;
    GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
    if(!R.Setup) {
        R.Setup=true; R.At=R.Age;
        H->GetCharacterMovement()->DisableMovement(); H->SetActorLocation(FVector(0,0,2000));
        H->bManualCameraOrbit=true; H->bMouthCameraInitialized=false;
        H->CameraOrbitYaw=0; H->CameraOrbitPitch=-30; H->CameraOrbitDistance=900; H->UpdateMouthCamera(1);
        auto* Wall=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(30,160,300)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
        Wall->SetActorLocation(FVector(-400,260,2250));
    }
    const float T=R.Age-R.At;
    if(T>.2f) {
        const FRotator View=H->Camera->GetComponentRotation();
        R.MaxAngle=FMath::Max(R.MaxAngle,FMath::Max(FMath::Abs(FMath::FindDeltaAngleDegrees(View.Yaw,0)),FMath::Abs(FMath::FindDeltaAngleDegrees(View.Pitch,-30))));
        ++R.Samples;
    }
    // Small alternating floor corrections plus a lateral route across an obstacle.
    // The mouse angle remains fixed throughout; sweep rotation must not feed it.
    H->SetActorLocation(FVector(0,T<2?T*220:T<4?(4-T)*220:0,2000+(R.Samples%2?2:-2)));
    if(T>5) {
        const bool Pass=R.Samples>50 && R.MaxAngle<.05f;
        UE_LOG(LogTemp,Display,TEXT("MC_CAMERA_STEADY_%s samples=%d maxAngle=%.4f"),Pass?TEXT("PASS"):TEXT("FAIL"),R.Samples,R.MaxAngle);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
}
#endif
