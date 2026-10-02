#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void MCTickCameraRevealValidation(UWorld* World)
{
    struct FRun { TWeakObjectPtr<UWorld> World; TWeakObjectPtr<UStaticMeshComponent> Wall;
        float Age=0,At=0,BeforeDistance=0; int32 Stage=0; bool Invalid=false; };
    static FRun R; if(R.World.Get()!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* PC=World->GetFirstPlayerController(); auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* GS=World->GetGameState<AMCGameState>(); if(!H || !GS || R.Age<3) return;
    GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
    auto Advance=[&](int32 Stage) { R.Stage=Stage; R.At=R.Age; };
    auto Shot=[&](const TCHAR* Name) {
        if(FParse::Param(FCommandLine::Get(),TEXT("MCOrbitCameraCapture")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("CameraReveal")/Name,true,false);
    };
    auto Finish=[&](bool Pass) {
        UE_LOG(LogTemp,Display,TEXT("MC_REVEAL_%s radial=%d distance=%.1f"),Pass?TEXT("PASS"):TEXT("FAIL"),!R.Invalid,H->CameraBoom->TargetArmLength);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(R.Stage==0) {
        H->GetCharacterMovement()->DisableMovement(); H->bManualCameraOrbit=true; H->bMouthCameraInitialized=false;
        H->CameraOrbitYaw=0; H->CameraOrbitPitch=-20; H->CameraOrbitDistance=900; H->bCameraWallReveal=false;
        H->UpdateMouthCamera(1);
        const FVector Pivot=H->GetActorLocation()+FVector(0,0,30),Direction=-H->CameraBoom->GetForwardVector();
        auto* Wall=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Wall); Wall->SetRootComponent(Mesh);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/Arena/MI_Roof_0.MI_Roof_0")));
        Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore); Mesh->RegisterComponent();
        Wall->SetActorLocationAndRotation(Pivot+Direction*450,Direction.Rotation()); Wall->SetActorScale3D(FVector(.7,7,7));
        R.Wall=Mesh; Advance(1);
    } else if(R.Stage==1 && R.Age-R.At>1) {
        // Existing teeth or other opaque geometry can still retract the camera.
        R.BeforeDistance=float(FVector::Dist(H->GetActorLocation()+FVector(0,0,30),H->Camera->GetComponentLocation()));
        Shot(TEXT("Before.png")); Advance(2);
    } else if(R.Stage==2 && R.Age-R.At>.25f) {
        R.Wall->SetCollisionResponseToChannel(ECC_Camera,ECR_Block); H->bCameraWallReveal=true; Advance(3);
    } else if(R.Stage==3 && R.Age-R.At>1.2f) {
        const auto& Data=R.Wall->GetCustomPrimitiveData().Data;
        UE_LOG(LogTemp,Display,TEXT("MC_REVEAL_ACTIVE data=%d alpha=%.4f pawn=%d distance=%.2f"),Data.Num(),Data.Num()>24?Data[24]:-1.f,R.Wall->GetCollisionResponseToChannel(ECC_Pawn),FVector::Dist(H->GetActorLocation()+FVector(0,0,30),H->Camera->GetComponentLocation()));
        R.Invalid|=Data.Num()<28 || Data[24]<.98f || R.Wall->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block;
        R.Invalid|=FMath::Abs(float(FVector::Dist(H->GetActorLocation()+FVector(0,0,30),H->Camera->GetComponentLocation()))-R.BeforeDistance)>2;
        Shot(TEXT("After.png")); Advance(4);
    } else if(R.Stage==4 && R.Age-R.At>.25f) {
        R.Wall->GetOwner()->AddActorWorldOffset(FVector(0,1200,0)); Advance(5);
    } else if(R.Stage==5 && R.Age-R.At>1.2f) {
        UE_LOG(LogTemp,Display,TEXT("MC_REVEAL_CLEAR alpha=%.4f"),R.Wall->GetCustomPrimitiveData().Data.Num()>24?R.Wall->GetCustomPrimitiveData().Data[24]:-1.f);
        R.Invalid|=R.Wall->GetCustomPrimitiveData().Data.Num()<28 || R.Wall->GetCustomPrimitiveData().Data[24]>.01f;
        H->ClearCameraWallReveal();
        UE_LOG(LogTemp,Display,TEXT("MC_REVEAL_RESTORE camera=%d"),R.Wall->GetCollisionResponseToChannel(ECC_Camera));
        R.Invalid|=R.Wall->GetCollisionResponseToChannel(ECC_Camera)!=ECR_Block;
        Finish(!R.Invalid); return;
    }
    if(R.Age>20) Finish(false);
}
#endif
