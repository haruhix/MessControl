#include "MCValidationSubsystem.h"
#include "MCGripComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "MCDayDirector.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void UMCValidationSubsystem::TickGrip(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); auto* PC=GetWorld()->GetFirstPlayerController();
    AMCTongue* Tongue=nullptr; for (TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    if (!GS || !PC || !Tongue) { if (Age>60) FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCGripCapture"));
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("GripFrames");
    auto Finish=[&]()
    {
        const bool Pass=DevSeen==131071 && !bTongueInvalid;
        if (Capture) FFileHelper::SaveStringToFile(CoffeeTiming,*(Folder/TEXT("times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s GRIP net=%d seen=%d error=%.2f ready=%.1f/%.1f/%.1f/%.1f invalid=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,GripWorstError,GripReadySeconds[0],GripReadySeconds[1],GripReadySeconds[2],GripReadySeconds[3],bTongueInvalid);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    TArray<AMCToothCharacter*> Heroes;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if (Heroes.Num()<4)
    {
        if (DevStartedAt>=0 && GS->GetServerWorldTimeSeconds()-DevStartedAt>(Host?27:22)) Finish();
        else if (Age>70) Finish();
        return;
    }
    auto Place=[&](AMCToothCharacter* Hero,FVector Point,float Yaw)
    {
        FHitResult Hit; if (!Tongue->SurfacePoint(Point,Hit)) { bTongueInvalid=true; return; }
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Hit.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
        Hero->SetActorRotation(FRotator(0,Yaw,0)); Hero->ForceNetUpdate();
    };
    bool Ready=true;
#if WITH_EDITOR
    Ready=!Capture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    if (Host && DevStage==0)
    {
        GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            Ready&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if (Ready && Age>8)
        {
            GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0;
            GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->ForceNetUpdate();
            auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
            if (!Mode->DayDirector) Mode->DayDirector=GetWorld()->SpawnActor<AMCDayDirector>();
            Tongue->ResetPain(); Tongue->ResetPressure(); Tongue->Settings.IdleHeight=0; Tongue->ForceNetUpdate();
            for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Destroy();
            for (TActorIterator<AActor> It(GetWorld());It;++It)
                if (auto* Status=It->FindComponentByClass<UMCToothStatusComponent>()) Status->Initialize(Status->State.MaxHealth);
            for (int32 I=0;I<4;++I)
            {
                const FVector Point(I<2?-250:250,I%2==0?-160:160,0); FHitResult Hit; Tongue->SurfacePoint(Point,Hit);
                const FTransform T(Hit.ImpactPoint+FVector(0,0,I==2?26:51));
                auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
                FMCFoodRow Row; Row.Label=FText::FromString(TEXT("GRIP")); Row.Mass=4; Row.HalfExtent=FVector(50); Row.SpoilSeconds=300;
                Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
                Row.FragmentMeshes=Row.WholeMeshes;
                FRandomStream Random(1); Food->ConfigureItem(FName(*FString::Printf(TEXT("Grip%d"),I)),Row,Random,I==2); Food->Batch=I;
                Food->FinishSpawning(T);
                Place(Heroes[I],Point+FVector(-220,0,0),0);
            }
            if (Capture)
            {
                CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(); CoffeeCamera->GetCameraComponent()->SetFieldOfView(52); PC->SetViewTarget(CoffeeCamera);
                IFileManager::Get().MakeDirectory(*Folder,true);
                for (TActorIterator<AActor> It(GetWorld());It;++It)
                { TArray<UTextRenderComponent*> Labels; It->GetComponents(Labels); for (auto* Label:Labels) Label->SetVisibility(false); }
            }
            ++DevStage;
        }
    }
    if (GS->bDevManualEvents && DevStartedAt<0) DevStartedAt=GS->DayStartedAt;
    const float T=DevStartedAt<0?-1:GS->GetServerWorldTimeSeconds()-DevStartedAt;
    AMCFoodActor* Food[4]={nullptr,nullptr,nullptr,nullptr};
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        for (int32 I=0;I<4;++I) if (It->ItemName==FName(*FString::Printf(TEXT("Grip%d"),I))) Food[I]=*It;
    if (!Food[0] || !Food[1] || !Food[2] || !Food[3]) { if (T>21) Finish(); return; }
    if (Host && DevStage==1 && T>1)
    {
        const FVector Offset[]={FVector(-86,0,0),FVector(-86,0,0),FVector(-68,0,0),FVector(86,0,0)};
        for (int32 I=0;I<4;++I)
        {
            Place(Heroes[I],Food[I]->GetActorLocation()+Offset[I],0); GripStarts[I]=Food[I]->GetActorLocation();
        }
        ++DevStage;
    }
    // Allow the test teleport to reach the owning clients before starting a grip.
    // Otherwise their in-flight movement corrections can undo the placement during pickup.
    if (Host && DevStage==2 && T>2)
    {
        for (int32 I=0;I<4;++I)
        {
            Heroes[I]->bHandling=true;
            if (!Food[I]->TryGrab(Heroes[I]))
            { bTongueInvalid=true; UE_LOG(LogTemp,Display,TEXT("MC_GRIP_BEGIN_FAIL %d %s"),I,*Heroes[I]->Grip->DebugFailure); }
            Heroes[I]->ForceNetUpdate();
        }
        ++DevStage;
    }
    if (T>2 && GripStarts[0].IsNearlyZero()) for (int32 I=0;I<4;++I) GripStarts[I]=Food[I]->GetActorLocation();
    // Test teleports rotate authority; owning character rotation is normally driven by local movement.
    if (!Host && T>1 && T<3) for (auto* Hero:Heroes) if (Hero->IsLocallyControlled()) Hero->SetActorRotation(FRotator::ZeroRotator);
    // Allow the lift and its replicated pose to settle even during slow captures.
    // Stop by distance so the test remains inside the arena at every frame rate.
    if (T>5 && T<9)
        for (int32 I=0;I<4;++I) if (Heroes[I]->IsLocallyControlled() && FVector::Dist2D(GripStarts[I],Food[I]->GetActorLocation())<110)
            Heroes[I]->AddMovementInput(FVector(I==0 || I==2?-1:1,0,0),.35f);
    if (Host && DevStage==3 && T>12)
    {
        for (int32 I=0;I<4;++I) { Food[I]->Release(Heroes[I]); Heroes[I]->bHandling=false; Heroes[I]->ForceNetUpdate(); }
        ++DevStage;
    }
    if (Host && DevStage==4 && T>15)
    {
        // Reset the rolled test cube before the independent regrip/ragdoll case.
        FHitResult Floor; Tongue->SurfacePoint(Food[1]->GetActorLocation(),Floor);
        Food[1]->Body->SetPhysicsLinearVelocity(FVector::ZeroVector); Food[1]->Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        Food[1]->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,51),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics); Food[1]->ForceNetUpdate();
        Place(Heroes[1],Food[1]->GetActorLocation()+FVector(-86,0,0),0);
        ++DevStage;
    }
    // As for the initial grip, let the teleport reach the owner before regripping.
    if (Host && DevStage==5 && T>16)
    {
        Heroes[1]->bHandling=true;
        if (!Food[1]->TryGrab(Heroes[1])) { bTongueInvalid=true; UE_LOG(LogTemp,Display,TEXT("MC_GRIP_REGRAB_FAIL %s"),*Heroes[1]->Grip->DebugFailure); }
        ++DevStage;
    }
    if (T>16 && T<18 && Heroes[1]->Grip->IsReady() && Heroes[1]->HeldFood==Food[1]) DevSeen|=32768;
    if (Host && DevStage==6 && T>18)
    { Heroes[1]->ToothPhysics->ApplyHit(FVector(0,0,450),Heroes[1]->GetActorLocation()); ++DevStage; }
    const EMCGripPose Expected[]={EMCGripPose::FrontPull,EMCGripPose::Push,EMCGripPose::Carry,EMCGripPose::RearPull};
    if(T>4 && T<12 && Heroes[0]->HeldFood==Food[0] && Food[0]->Phase==EMCFoodPhase::Free && FMath::Abs(Food[0]->GetActorRotation().Yaw)>10)DevSeen|=65536;
    if (T>2 && T<12) for (int32 I=0;I<4;++I)
    {
        auto* Grip=Heroes[I]->Grip.Get();
        if (Grip->Frame.Food==Food[I] && Grip->IsReady())
        {
            GripReadySeconds[I]+=Dt;
            if (GripReadySeconds[I]>.75f) DevSeen|=1<<I;
            if (Grip->Frame.Pose==Expected[I]) DevSeen|=1<<(I+4);
            if (FVector::Dist2D(GripStarts[I],Food[I]->GetActorLocation())>60) DevSeen|=1<<(I+8);
        }
        if (Grip->IsReady() && Grip->Blend()>.99f && Grip->Frame.Food && T>4.3f && T<5)
        {
            GripWorstError=FMath::Max(GripWorstError,Grip->ContactError());
            // A delayed transform can briefly precede its matching pose on a client.
            // Fail a sustained detached hand, while retaining the raw peak in the log.
            GripBadContactSeconds[I]=Grip->ContactError()>12?GripBadContactSeconds[I]+Dt:0;
            bTongueInvalid|=GripBadContactSeconds[I]>.25f;
        }
    }
    if (T>13 && T<15)
    {
        bool Released=true; for (auto* Hero:Heroes) Released&=!Hero->HeldFood && Hero->Grip->Blend()<.01f && Hero->GetCharacterMovement()->bOrientRotationToMovement;
        if (Released) DevSeen|=4096;
    }
    if (T>17 && Heroes[1]->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll && !Heroes[1]->HeldFood && !Heroes[1]->Grip->Frame.Food) DevSeen|=8192;
    if (T>20 && (DevSeen&8192) && Heroes[1]->ToothPhysics->CanAct()) DevSeen|=16384;
    for (auto* Hero:Heroes) bTongueInvalid|=Hero->GetActorLocation().ContainsNaN() || Hero->GetActorLocation().Z<-250;
    if (Capture && T>=0 && T<22)
    {
        const int32 View=T<14?FMath::Clamp(int32(T/3),0,3):1;
        const FVector Focus=(Heroes[View]->GetActorLocation()+Food[View]->GetActorLocation())*.5+FVector(0,0,5);
        const FVector ToHero=(Heroes[View]->GetActorLocation()-Food[View]->GetActorLocation()).GetSafeNormal2D();
        const FVector Camera=Focus+ToHero*260+FVector::CrossProduct(FVector::UpVector,ToHero)*220+FVector(0,0,160);
        CoffeeCamera->SetActorLocationAndRotation(Camera,(Focus-Camera).Rotation());
        if (T>=CoffeeNextFrame)
        {
            const FString Name=FString::Printf(TEXT("Grip_%04d.png"),CoffeeFrame++);
            FScreenshotRequest::RequestScreenshot(Folder/Name,false,false); CoffeeTiming+=FString::Printf(TEXT("%s,%.6f\n"),*Name,T); CoffeeNextFrame=T+.1f;
        }
    }
    if (T>=0 && Age>=NextLog)
    {
        NextLog=Age+3;
        UE_LOG(LogTemp,Display,TEXT("MC_GRIP net=%d t=%.1f seen=%d err=%.1f/%.1f/%.1f/%.1f held=%d/%d/%d/%d invalid=%d"),int32(GetWorld()->GetNetMode()),T,DevSeen,Heroes[0]->Grip->ContactError(),Heroes[1]->Grip->ContactError(),Heroes[2]->Grip->ContactError(),Heroes[3]->Grip->ContactError(),Food[0]->Holders.Num(),Food[1]->Holders.Num(),Food[2]->Holders.Num(),Food[3]->Holders.Num(),bTongueInvalid);
    }
    if (T>(Host?27:24) || (DevStartedAt<0 && Age>180)) Finish();
#endif
}
