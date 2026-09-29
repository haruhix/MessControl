#include "MCValidationSubsystem.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGameState.h"
#include "MCLocomotionSurface.h"
#include "MCMotionRecorder.h"
#include "MCToothStatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

void UMCValidationSubsystem::TickLocomotion(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); auto* PC=GetWorld()->GetFirstPlayerController();
    if (!GS || !PC) { if (Age>60) FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCLocomotionCapture"));
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("LocomotionFrames");
    TArray<AMCToothCharacter*> Heroes;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    auto Finish=[&]()
    {
        bool Pass=!bTongueInvalid;
        for (int32 I=0;I<4;++I)
        {
            Pass&=StrideWalk[I]>260 && StrideRun[I]>470 && StrideSticky[I]>180 && StrideSticky[I]<390 && StrideDrift[I]>100 && StrideSurfaces[I]==7;
            UE_LOG(LogTemp,Display,TEXT("MC_STRIDE actor=%d walk=%.1f run=%.1f sticky=%.1f coast=%.1f surfaces=%d"),I,StrideWalk[I],StrideRun[I],StrideSticky[I],StrideDrift[I],StrideSurfaces[I]);
        }
        if (Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(Folder/TEXT("times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s LOCOMOTION net=%d invalid=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),bTongueInvalid);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if (Heroes.Num()<4) { if (Age>70 || (DevStartedAt>0 && GS->GetServerWorldTimeSeconds()-DevStartedAt>22)) Finish(); return; }
    if (Host && DevStage==0 && Age>8)
    {
        bool Ready=true;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            Ready&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if (!Ready) return;
        GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0; GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->ForceNetUpdate();
        const FTransform FloorTransform(FRotator::ZeroRotator,FVector(0,0,10000),FVector(150,24,.2));
        auto* Floor=GetWorld()->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(),FloorTransform);
        Floor->SetMobility(EComponentMobility::Movable); Floor->SetReplicates(true); Floor->SetReplicateMovement(true); Floor->GetStaticMeshComponent()->SetIsReplicated(true);
        Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Floor->FinishSpawning(FloorTransform);
        auto* Patch=GetWorld()->SpawnActor<AMCLocomotionSurface>(FVector(0,0,10010),FRotator::ZeroRotator);
        Patch->Tags.Add(TEXT("StrideNetwork")); Patch->HalfExtent=FVector(7500,1200,40); Patch->Surface=EMCGroundSurface::Normal; Patch->RefreshBounds(); Patch->ForceNetUpdate();
        for (int32 I=0;I<4;++I)
        {
            Heroes[I]->Status->Initialize(100); Heroes[I]->GetCharacterMovement()->StopMovementImmediately();
            Heroes[I]->SetActorLocationAndRotation(FVector(-3000,-600+I*400,10071),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics); Heroes[I]->ForceNetUpdate();
        }
        if (Capture)
        {
            CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(); CoffeeCamera->GetCameraComponent()->SetFieldOfView(50); PC->SetViewTarget(CoffeeCamera);
            IFileManager::Get().MakeDirectory(*Folder,true);
            auto* Recorder=NewObject<UMCMotionRecorder>(Heroes[0]); Recorder->RegisterComponent(); Recorder->Start(19,TEXT("locomotion_network"));
        }
        ++DevStage;
    }
    if (GS->bDevManualEvents && DevStartedAt<0) DevStartedAt=GS->DayStartedAt;
    if (DevStartedAt<0) return;
    const float T=GS->GetServerWorldTimeSeconds()-DevStartedAt;
    if (Host)
        for (TActorIterator<AMCLocomotionSurface> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("StrideNetwork")))
        {
            const auto Kind=T<7?EMCGroundSurface::Normal:T<11?EMCGroundSurface::Sticky:EMCGroundSurface::Slippery;
            if (It->Surface!=Kind) { It->Surface=Kind; It->ForceNetUpdate(); }
        }
    for (int32 I=0;I<4;++I)
    {
        auto* Hero=Heroes[I]; auto* Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
        if (Hero->IsLocallyControlled())
        {
            Move->SetSprinting(T>=4 && T<15);
            if (T>=2 && T<15) Hero->AddMovementInput(FVector::ForwardVector);
        }
        const float Speed=Hero->GetVelocity().Size2D();
        if (T>3 && T<4) StrideWalk[I]=FMath::Max(StrideWalk[I],Speed);
        if (T>5.5 && T<6.5) StrideRun[I]=FMath::Max(StrideRun[I],Speed);
        if (T>9 && T<10.5) StrideSticky[I]=FMath::Max(StrideSticky[I],Speed);
        if (T>15.5 && T<16.5) StrideDrift[I]=FMath::Max(StrideDrift[I],Speed);
        if (T>2) StrideSurfaces[I]|=1<<uint8(Move->GroundSurface);
        if (T>2) bTongueInvalid|=Hero->GetActorLocation().ContainsNaN() || Hero->GetActorLocation().Z<10000;
        for (FName Role:{FName("foot_l"),FName("foot_r"),FName("hand_l"),FName("hand_r")})
            bTongueInvalid|=Hero->GetMesh()->GetSocketTransform(Hero->RigBone(Role)).ContainsNaN();
    }
    if (Capture && T<19 && T>1)
    {
        const FVector Focus=Heroes[0]->GetActorLocation();
        const FVector Eye=Focus+FVector(210,-340,140); CoffeeCamera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
        if (T>=CoffeeNextFrame)
        {
            const FString Name=FString::Printf(TEXT("Stride_%04d.bmp"),CoffeeFrame++);
            FScreenshotRequest::RequestScreenshot(Folder/Name,false,false); CoffeeTiming+=FString::Printf(TEXT("%s,%.6f\n"),*Name,T);
            CoffeeNextFrame=T+.0667f;
        }
    }
    if (T>(Host?23:20)) Finish();
#endif
}
