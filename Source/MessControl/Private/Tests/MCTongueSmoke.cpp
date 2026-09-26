#include "MCValidationSubsystem.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void UMCValidationSubsystem::TickTongue(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); auto* PC=GetWorld()->GetFirstPlayerController();
    AMCTongue* Tongue=nullptr; for (TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    if (!GS || !PC || !Tongue) { if (Age>60) FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCTongueCapture"));
    int32 Expected=4; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),Expected);
    bool Ready=true;
#if WITH_EDITOR
    Ready=!Capture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    TArray<AMCToothCharacter*> Heroes;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if (Host && DevStage==0)
    {
        GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
        bool Possessed=Heroes.Num()==Expected;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            Possessed&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if (Possessed && Ready) { if (CoffeeReadyAt<0) CoffeeReadyAt=Age; } else CoffeeReadyAt=-1;
        if (CoffeeReadyAt>=0 && Age>CoffeeReadyAt+2 && Age>8)
        {
            GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0;
            GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->ForceNetUpdate();
            const FVector Points[]={FVector(-100,-100,0),FVector(-420,130,0),FVector(300,-170,0),FVector(-600,-340,0)};
            for (int32 I=0;I<Heroes.Num();++I)
            {
                FHitResult Hit; Tongue->SurfacePoint(Points[I],Hit);
                Heroes[I]->GetCharacterMovement()->StopMovementImmediately();
                Heroes[I]->SetActorLocation(Hit.ImpactPoint+FVector(0,0,Heroes[I]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
                Heroes[I]->ForceNetUpdate();
            }
            FHitResult Hit; Tongue->SurfacePoint(FVector(200,230,0),Hit);
            GetWorld()->SpawnActor<AMCFoodActor>(Hit.ImpactPoint+FVector(0,0,35),FRotator::ZeroRotator);
            if (Capture)
            {
                CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(FVector(-1540,-1130,1200),FRotator::ZeroRotator);
                CoffeeCamera->SetActorRotation((FVector(50,-100,-30)-CoffeeCamera->GetActorLocation()).Rotation());
                CoffeeCamera->GetCameraComponent()->SetFieldOfView(70); PC->SetViewTarget(CoffeeCamera);
                IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("TongueFrames")),true);
            }
            DevStage=1;
        }
    }
    if (GS->bDevManualEvents && DevStartedAt<0) DevStartedAt=GS->DayStartedAt;
    const float T=DevStartedAt<0?-1:GS->GetServerWorldTimeSeconds()-DevStartedAt;
    if (Host && DevStage==1 && T>2)
    {
        FHitResult Hit; Tongue->SurfacePoint(FVector(-100,-100,0),Hit);
        auto* Patch=GetWorld()->SpawnActor<AMCMouthSurface>(Hit.ImpactPoint+Hit.ImpactNormal*5,FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator());
        Patch->bUlcer=true; Patch->HealSeconds=5; ++DevStage;
    }
    if (T>=0)
    {
        const auto& V=Tongue->CurrentVertices(); const auto& Indices=Tongue->TriangleIndices();
        // Compare actual render triangle centroids to collision; sampling the formula alone would miss a stale cook.
        int32 Probes=0;
        for (int32 I=0;I+2<Indices.Num();I+=93)
        {
            const FVector P=Tongue->GetActorTransform().TransformPosition((V[Indices[I]]+V[Indices[I+1]]+V[Indices[I+2]])/3);
            if (P.X<-800 || P.X>1000 || FMath::Abs(P.Y)>450) continue;
            FHitResult Hit;
            if (Tongue->SurfacePoint(P,Hit)) { TongueError=FMath::Max(TongueError,float(FMath::Abs(Hit.ImpactPoint.Z-P.Z))); ++Probes; }
            else bTongueInvalid=true;
        }
        if (Probes>8) DevSeen|=1;
        FHitResult Center; Tongue->SurfacePoint(FVector(-100,-100,0),Center);
        TongueMinZ=FMath::Min(TongueMinZ,float(Center.ImpactPoint.Z)); TongueMaxZ=FMath::Max(TongueMaxZ,float(Center.ImpactPoint.Z));
        if (TongueMaxZ-TongueMinZ>8) DevSeen|=2;
        if (Tongue->Pulse.Serial>0)
        {
            DevSeen|=4;
            if (Host && DevStage==2) { bTongueInvalid|=Tongue->TriggerPain(Center.ImpactPoint); ++DevStage; }
        }
        if (const auto* Section=Tongue->Surface->GetProcMeshSection(0))
            for (const auto& Vertex:Section->ProcVertexBuffer) if (Vertex.Color.R>100) { DevSeen|=8; break; }
        for (auto* Hero:Heroes)
        {
            if (Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) DevSeen|=16;
            bTongueInvalid|=Hero->GetActorLocation().ContainsNaN() || Hero->GetActorLocation().Z<-250;
        }
        int32 Ulcers=0;
        for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (It->bUlcer)
        {
            ++Ulcers; DevSeen|=32;
            FHitResult Hit; Tongue->SurfacePoint(It->GetActorLocation(),Hit);
            TonguePatchError=FMath::Max(TonguePatchError,float(FMath::Abs(It->GetActorLocation().Z-Hit.ImpactPoint.Z-5)));
            if (It->Healing>.3) DevSeen|=64;
        }
        if (T>9 && Ulcers==0 && (DevSeen&32)) DevSeen|=128;
        if (Host && T>6 && DevStage==3)
        {
            bTongueInvalid|=Tongue->Pulse.Serial!=1 || Tongue->PlayerPushes!=Expected || Tongue->FoodPushes<1;
            UE_LOG(LogTemp,Display,TEXT("MC_TONGUE_PUSHES players=%d food=%d serial=%d"),Tongue->PlayerPushes,Tongue->FoodPushes,Tongue->Pulse.Serial);
            ++DevStage;
        }
        if (Capture && T>=CoffeeNextFrame && T<11)
        {
            const FString Name=FString::Printf(TEXT("Tongue_%04d.png"),CoffeeFrame++);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("TongueFrames")/Name,false,false);
            CoffeeTiming+=FString::Printf(TEXT("%s,%.6f\n"),*Name,T); CoffeeNextFrame=T+.1f;
        }
    }
    if (Age>=NextLog) { NextLog+=5; UE_LOG(LogTemp,Display,TEXT("MC_TONGUE net=%d t=%.2f seen=%d error=%.3f patch=%.3f delta=%.2f invalid=%d"),int32(GetWorld()->GetNetMode()),T,DevSeen,TongueError,TonguePatchError,TongueMaxZ-TongueMinZ,bTongueInvalid); }
    if (T>(Host?19:15) || Age>100)
    {
        const bool Pass=DevSeen==255 && TongueError<.5f && TonguePatchError<2 && !bTongueInvalid;
        if (Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(FPaths::ProjectSavedDir()/TEXT("TongueFrames/times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s TONGUE net=%d seen=%d collisionError=%.3f patchError=%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,TongueError,TonguePatchError);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTonguePulseTest,"MessControl.Tongue.WaveTimingAndSafety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTonguePulseTest::RunTest(const FString&)
{
    FMCTongueSettings S; S.WaveSpeed=0; S.WaveWidth=-1; S.Cooldown=0; S.WaveHeight=10000; S.Sanitize();
    TestTrue(TEXT("Invalid designer input stays bounded"),S.WaveSpeed>=100 && S.WaveWidth>=100 && S.WaveHeight<=50 && S.Cooldown>S.Duration());
    S=FMCTongueSettings(); S.Sanitize();
    TestEqual(TEXT("No effect before anticipation finishes"),S.Band(0,-.01f),0.f);
    TestEqual(TEXT("Pulse does not loop after reaching edge"),S.Band(500,S.Duration()+1),0.f);
    TestTrue(TEXT("Crest travels from origin"),S.Band(850,1)>.99f && S.Band(0,1)==0);
    TestTrue(TEXT("Swept front catches a low frame rate crossing"),S.Crossed(600,.1f,1.f));
    TestFalse(TEXT("An object behind a passed wave is not hit later"),S.Crossed(50,1.f,1.1f));
    TestFalse(TEXT("Nothing beyond radius is hit"),S.Crossed(S.WaveRadius+1,0,10));
    return true;
}
#endif
