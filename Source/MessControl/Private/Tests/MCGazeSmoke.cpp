#include "MCValidationSubsystem.h"
#include "MCGazeComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "MCDayDirector.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void UMCValidationSubsystem::TickGaze(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); auto* PC=GetWorld()->GetFirstPlayerController();
    AMCTongue* Tongue=nullptr; for (TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    if (!GS || !PC || !Tongue) { if (Age>60) FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCGazeCapture"));
    bool Ready=true;
#if WITH_EDITOR
    Ready=!Capture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    TArray<AMCToothCharacter*> Heroes;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if (Heroes.Num()<4)
    {
        // Clients close first. Their pawns are destroyed before the host's exit deadline.
        if (Host && DevStartedAt>=0 && GS->GetServerWorldTimeSeconds()-DevStartedAt>16)
        {
            const bool Pass=DevSeen==511 && !bTongueInvalid;
            if (Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(FPaths::ProjectSavedDir()/TEXT("GazeFrames/times.csv")));
            UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s GAZE net=%d seen=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
        else if (Age>65) FPlatformMisc::RequestExitWithStatus(false,1);
        return;
    }
    auto* Hero=Heroes[0]; auto* Friend=Heroes[1];
    auto Place=[&](AMCToothCharacter* T,FVector XY,float Yaw)
    {
        FHitResult Hit; Tongue->SurfacePoint(XY,Hit);
        T->GetCharacterMovement()->StopMovementImmediately();
        T->SetActorLocationAndRotation(Hit.ImpactPoint+FVector(0,0,T->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),FRotator(0,Yaw,0),false,nullptr,ETeleportType::TeleportPhysics);
        T->ForceNetUpdate();
    };
    if (Host && DevStage==0)
    {
        GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
        bool Possessed=Ready;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            Possessed&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if (Possessed && Age>8)
        {
            GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0;
            GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->ForceNetUpdate(); Tongue->ResetPain();
            auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
            if (!Mode->DayDirector) Mode->DayDirector=GetWorld()->SpawnActor<AMCDayDirector>();
            for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Destroy();
            for (auto* H:Heroes) { auto Clean=H->Status->State; Clean.CoffeeLeft=0; H->Status->Restore(Clean); }
            Place(Hero,FVector(0,-100,0),0); Place(Friend,FVector(250,40,0),180);
            Place(Heroes[2],FVector(-650,-500,0),180); Place(Heroes[3],FVector(-650,450,0),180);
            if (Capture)
            {
                CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(FVector(390,-250,125),FRotator::ZeroRotator);
                CoffeeCamera->SetActorRotation((Hero->GetActorLocation()+FVector(0,0,5)-CoffeeCamera->GetActorLocation()).Rotation());
                CoffeeCamera->GetCameraComponent()->SetFieldOfView(38); PC->SetViewTarget(CoffeeCamera);
                IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("GazeFrames")),true);
            }
            ++DevStage;
        }
    }
    if (GS->bDevManualEvents && DevStartedAt<0) DevStartedAt=GS->DayStartedAt;
    const float T=DevStartedAt<0?-1:GS->GetServerWorldTimeSeconds()-DevStartedAt;
    if (Host)
    {
        if (DevStage==1 && T>2) { Place(Friend,FVector(250,-280,0),150); Hero->Gaze->BlinkStartedAt=GS->GetServerWorldTimeSeconds()+.2; Hero->ForceNetUpdate(); ++DevStage; }
        if (DevStage==2 && T>4)
        {
            GetWorld()->SpawnActor<AMCFoodActor>(Hero->GetActorLocation()+FVector(200,0,480),FRotator::ZeroRotator); ++DevStage;
        }
        if (DevStage==3 && T>6)
        {
            for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Dispose();
            Hero->Gaze->NoticePoint(Hero->GetActorLocation()+FVector(300,230,180),1.4f); ++DevStage;
        }
        if (DevStage==4 && T>8) { Hero->ToothPhysics->ApplyHit(FVector(-80,70,420),Hero->GetActorLocation()); ++DevStage; }
    }
    if (T>=0)
    {
        if (!Host && !bDevClientGuard)
        {
            const int32 Before=Hero->Gaze->Target.Serial;
            bTongueInvalid|=Hero->Gaze->NoticePoint(Hero->GetActorLocation()+FVector(100,0,0),2);
            bTongueInvalid|=Before!=Hero->Gaze->Target.Serial; bDevClientGuard=true;
        }
        const auto* G=Hero->Gaze.Get();
        if (G->Target.Interest==EMCGazeInterest::Danger && G->PupilScale>1.3f && Hero->GetMesh()->GetMorphTarget(TEXT("Pupil_Dilate"))>.35f) DevSeen|=256;
        if (G->Target.Actor==Friend && G->Target.Interest==EMCGazeInterest::Player) DevSeen|=1;
        auto* Mesh=Hero->GetMesh(); const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
        const int32 Eye=Ref.FindBoneIndex(Hero->RigBone(TEXT("eye_l")));
        if (Eye!=INDEX_NONE)
        {
            const FQuat Actual=Mesh->GetSocketQuaternion(Ref.GetBoneName(Ref.GetParentIndex(Eye))).Inverse()*Mesh->GetSocketQuaternion(Ref.GetBoneName(Eye));
            if (FMath::RadiansToDegrees(Actual.AngularDistance(Ref.GetRefBonePose()[Eye].GetRotation()))>5 && G->LeftAngles.Size()>5) DevSeen|=2;
            DevSeen|=128;
        }
        else bTongueInvalid=true;
        if (Cast<AMCFoodActor>(G->Target.Actor) && G->Target.Interest==EMCGazeInterest::Danger) DevSeen|=4;
        if (T>6 && !G->Target.Actor && G->Target.Interest==EMCGazeInterest::Danger) DevSeen|=8;
        const int32 Lid=Ref.FindBoneIndex(Hero->RigBone(TEXT("lid_top_l")));
        if (Lid!=INDEX_NONE)
        {
            const FQuat Actual=Mesh->GetSocketQuaternion(Ref.GetBoneName(Ref.GetParentIndex(Lid))).Inverse()*Mesh->GetSocketQuaternion(Ref.GetBoneName(Lid));
            if (G->Blink>.1 && FMath::RadiansToDegrees(Actual.AngularDistance(Ref.GetRefBonePose()[Lid].GetRotation()))>25) DevSeen|=16;
        }
        else bTongueInvalid=true;
        if (Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) DevSeen|=32;
        if (T>10 && (DevSeen&32) && Hero->ToothPhysics->GetBodyState()==EMCBodyState::Standing) DevSeen|=64;
        for (const auto* H:Heroes)
        {
            const auto S=H->Gaze->VisualSettings();
            bTongueInvalid|=!FMath::IsFinite(H->Gaze->PupilScale) || H->Gaze->PupilScale<.6f || H->Gaze->PupilScale>1.8f;
            for (const FVector2D A:{H->Gaze->LeftAngles,H->Gaze->RightAngles})
                bTongueInvalid|=A.ContainsNaN() || FMath::Abs(A.X)>S.YawLimit+.01 || FMath::Abs(A.Y)>S.PitchLimit+.01;
        }
        if (Capture && T>=CoffeeNextFrame && T<15)
        {
            const FString Name=FString::Printf(TEXT("Gaze_%04d.png"),CoffeeFrame++);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("GazeFrames")/Name,false,false);
            CoffeeTiming+=FString::Printf(TEXT("%s,%.6f\n"),*Name,T); CoffeeNextFrame=T+.08f;
        }
    }
    if (Age>=NextLog) { NextLog+=3; UE_LOG(LogTemp,Display,TEXT("MC_GAZE t=%.2f seen=%d interest=%d actor=%s angles=%s blink=%.2f invalid=%d"),T,DevSeen,int32(Hero->Gaze->Target.Interest),*GetNameSafe(Hero->Gaze->Target.Actor),*Hero->Gaze->LeftAngles.ToString(),Hero->Gaze->Blink,bTongueInvalid); }
    if (T>(Host?21:17) || Age>100)
    {
        const bool Pass=DevSeen==511 && !bTongueInvalid;
        if (Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(FPaths::ProjectSavedDir()/TEXT("GazeFrames/times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s GAZE net=%d seen=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}
