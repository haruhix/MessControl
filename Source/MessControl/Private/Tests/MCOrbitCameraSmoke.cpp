#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

// Exercise free MouseX/MouseY look, wheel zoom and the separate RMB attack in a running game.
void MCTickOrbitCameraValidation(UWorld* World)
{
    struct FRun { TWeakObjectPtr<UWorld> World; float Age=0,At=0,LastAxis=0,Travel=0,PreviousYaw=0,StoppedYaw=0,InitialPitch=0,ZoomBefore=0,MenuDistance=0;
        int32 Stage=0,Samples=0,Retracted=0; bool Invalid=false; FRotator Body;
        TWeakObjectPtr<AMCArenaTooth> Tooth; TWeakObjectPtr<UStaticMeshComponent> EndpointWall; };
    static FRun R; if(R.World.Get()!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* PC=Cast<AMCPlayerController>(World->GetFirstPlayerController());
    auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* GS=World->GetGameState<AMCGameState>();
    if (!H || !GS || R.Age<3) return;
    GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
    auto Key=[&](FKey K,EInputEvent Event,float Value) {
        auto Args=FInputKeyEventArgs::CreateSimulated(K,Event,Value,1,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice());
        Args.DeltaTime=World->GetDeltaSeconds(); PC->InputKey(Args);
    };
    auto Advance=[&](int32 Stage) { R.Stage=Stage; R.At=R.Age; };
    auto Finish=[&](bool Pass) {
        UE_LOG(LogTemp,Display,TEXT("MC_ORBIT_%s inputYawTravel=%.1f collisionSamples=%d retracted=%d attacks=%d meshGuard=%d wallVolume=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),R.Travel,R.Samples,R.Retracted,H->ValidatedSwingCount,R.Tooth.IsValid(),R.EndpointWall.IsValid());
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if (R.Stage==0) {
        H->GetCharacterMovement()->DisableMovement(); R.Body=H->GetActorRotation();
        // A hidden wall forces the running input test to exercise arm retraction on any map revision.
        auto* Wall=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Wall);
        Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(30,250,500));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Ignore);
        Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Block); Box->RegisterComponent();
        Wall->SetActorLocation(H->GetActorLocation()+FVector(450,0,250));
        Key(EKeys::MouseX,IE_Axis,80); Key(EKeys::MouseY,IE_Axis,35); Advance(1);
    } else if (R.Stage==1 && R.Age-R.At>.15f) {
        if(!H->bManualCameraOrbit) { Finish(false); return; }
        R.ZoomBefore=H->CameraOrbitDistance; Key(EKeys::MouseWheelAxis,IE_Axis,1); Advance(2);
    } else if (R.Stage==2 && R.Age-R.At>.25f) {
        R.Invalid|=!FMath::IsNearlyEqual(H->CameraOrbitDistance,FMath::Max(250.f,R.ZoomBefore-H->CameraZoomStep),.1f);
        R.ZoomBefore=H->CameraOrbitDistance; Key(EKeys::MouseWheelAxis,IE_Axis,-1);
        R.PreviousYaw=H->CameraOrbitYaw; R.InitialPitch=H->CameraOrbitPitch;
        Key(EKeys::MouseY,IE_Axis,-40); Advance(3);
    } else if (R.Stage==3) {
        R.Travel+=FMath::Abs(FMath::FindDeltaAngleDegrees(R.PreviousYaw,H->CameraOrbitYaw)); R.PreviousYaw=H->CameraOrbitYaw;
        if(R.Age-R.At<4.8f && R.Age-R.LastAxis>.05f) { Key(EKeys::MouseX,IE_Axis,35); R.LastAxis=R.Age; }
        const FVector Eye=H->Camera->GetComponentLocation();
        FCollisionQueryParams Q(SCENE_QUERY_STAT(MCOrbitOverlap),false,H);
        R.Invalid|=Eye.ContainsNaN() || World->OverlapBlockingTestByChannel(Eye,FQuat::Identity,ECC_Camera,FCollisionShape::MakeSphere(18),Q);
        ++R.Samples; if(H->CameraBoom->IsCollisionFixApplied()) ++R.Retracted;
        if(R.Age-R.At>5) {
            R.Invalid|=FMath::IsNearlyEqual(H->CameraOrbitPitch,R.InitialPitch,.1f) || !H->GetActorRotation().Equals(R.Body,.1);
            R.Invalid|=!FMath::IsNearlyEqual(H->CameraOrbitDistance,FMath::Min(1600.f,R.ZoomBefore+H->CameraZoomStep),.1f);
            R.StoppedYaw=H->CameraOrbitYaw; Advance(4);
            if(FParse::Param(FCommandLine::Get(),TEXT("MCOrbitCameraCapture")))
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("OrbitCamera.png"),true,false);
        }
    } else if (R.Stage==4 && R.Age-R.At>1) {
        R.Invalid|=!FMath::IsNearlyEqual(H->CameraOrbitYaw,R.StoppedYaw,.1f) || H->ValidatedSwingCount!=0;
        PC->ToggleConnection(); R.MenuDistance=H->CameraOrbitDistance;
        Key(EKeys::MouseX,IE_Axis,80); Key(EKeys::MouseWheelAxis,IE_Axis,-1); Advance(5);
    } else if (R.Stage==5 && R.Age-R.At>.2f) {
        R.Invalid|=!FMath::IsNearlyEqual(H->CameraOrbitYaw,R.StoppedYaw,.1f) || !FMath::IsNearlyEqual(H->CameraOrbitDistance,R.MenuDistance,.1f);
        PC->ToggleConnection(); Advance(6);
    } else if (R.Stage==6 && R.Age-R.At>.2f) {
        Key(EKeys::RightMouseButton,IE_Pressed,1); Advance(7);
    } else if (R.Stage==7 && R.Age-R.At>.05f) {
        Key(EKeys::RightMouseButton,IE_Released,0); Advance(8);
    } else if (R.Stage==8 && R.Age-R.At>.2f) {
        R.Invalid|=R.Travel<=330 || R.Samples<=50 || R.Retracted==0 || H->ValidatedSwingCount!=1;
        H->SetActorLocation(FVector(0,0,2000)); H->bMouthCameraInitialized=false;
        H->CameraOrbitYaw=0; H->CameraOrbitPitch=-30; H->CameraOrbitDistance=900;
        const FVector Pivot=H->GetActorLocation()+FVector(0,0,30),Ray=-FRotator(-30,0,0).Vector();
        auto* Tooth=World->SpawnActor<AMCArenaTooth>(Pivot+Ray*450,Ray.Rotation());
        // The camera must see actual enamel even where the physics proxy has no shape.
        Tooth->Body->SetCollisionEnabled(ECollisionEnabled::NoCollision); Tooth->SetActorTickEnabled(false);
        R.Tooth=Tooth; Advance(9);
    } else if (R.Stage==9 && R.Age-R.At>1.f) {
        const FVector Eye=H->Camera->GetComponentLocation(),Pivot=H->GetActorLocation()+FVector(0,0,30);
        FHitResult Touch;
        const bool Penetrated=R.Tooth->BrushSurface->SweepComponent(Touch,Eye,Eye+FVector(0,0,.1),FQuat::Identity,FCollisionShape::MakeSphere(18),true);
        const float Distance=float(FVector::Distance(Pivot,Eye));
        R.Invalid|=Penetrated || Distance>=450 || !H->CameraBoom->IsCollisionFixApplied();
        UE_LOG(LogTemp,Display,TEXT("MC_ORBIT_MESH_GUARD distance=%.1f penetrated=%d"),Distance,Penetrated);
        R.Tooth->SetActorLocation(FVector(0,2000,2000));
        auto* Wall=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Wall); Wall->SetRootComponent(Mesh);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/Arena/MI_Roof_0.MI_Roof_0")));
        Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->RegisterComponent();
        const FVector Ray=-FRotator(-30,0,0).Vector();
        Wall->SetActorLocationAndRotation(Pivot+Ray*900,Ray.Rotation()); Wall->SetActorScale3D(FVector(.7,7,7));
        H->bCameraWallReveal=true; R.EndpointWall=Mesh; Advance(10);
    } else if (R.Stage==10 && R.Age-R.At>1.2f) {
        const FVector Eye=H->Camera->GetComponentLocation(); FHitResult Touch;
        const bool Penetrated=R.EndpointWall->SweepComponent(Touch,Eye,Eye+FVector(0,0,.1),FQuat::Identity,FCollisionShape::MakeSphere(18),true);
        R.Invalid|=Penetrated || R.EndpointWall->GetCollisionResponseToChannel(ECC_Camera)!=ECR_Ignore;
        UE_LOG(LogTemp,Display,TEXT("MC_ORBIT_WALL_VOLUME penetrated=%d cameraResponse=%d"),Penetrated,R.EndpointWall->GetCollisionResponseToChannel(ECC_Camera));
        if(FParse::Param(FCommandLine::Get(),TEXT("MCOrbitCameraCapture")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("OrbitCameraMeshGuard.png"),true,false);
        Advance(11);
    } else if (R.Stage==11 && R.Age-R.At>.2f) {
        Finish(!R.Invalid); return;
    }
    if(R.Age>25) Finish(false);
}
#endif
