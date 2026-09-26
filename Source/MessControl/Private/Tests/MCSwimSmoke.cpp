#include "MCValidationSubsystem.h"
#include "MCCoffeeFlood.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void UMCValidationSubsystem::TickSwim(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;auto* GS=GetWorld()->GetGameState<AMCGameState>();auto* PC=GetWorld()->GetFirstPlayerController();if(!GS || !PC)return;
    const bool Host=GetWorld()->GetNetMode()!=NM_Client,Single=GetWorld()->GetNetMode()==NM_Standalone;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCSwimCapture"));const int32 Count=Single?1:4;
    auto Finish=[&](){const bool Pass=DevSeen==127 && !bInvalidPhysics;UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s SWIM net=%d seen=%d invalid=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,bInvalidPhysics);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);};
    TArray<AMCToothCharacter*> Heroes;for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)if(It->GetPlayerState())Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if(Heroes.Num()<Count){if((DevStartedAt>=0 && GS->GetServerWorldTimeSeconds()-DevStartedAt>(Host?11:9)) || Age>90)Finish();return;}
    AMCCoffeeFlood* Water=nullptr;for(TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It){Water=*It;break;}
    if(Host && DevStage==0 && Age>8)
    {
        if(!Water){GetWorld()->SpawnActor<AMCCoffeeFlood>();return;}
        bool Ready=true;
#if WITH_EDITOR
        if(Capture && GShaderCompilingManager && GShaderCompilingManager->IsCompiling())Ready=false;
#endif
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)Ready&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if(!Ready)return;
        GS->bDevManualEvents=true;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;GS->DayStartedAt=GS->GetServerWorldTimeSeconds();GS->ForceNetUpdate();GetWorld()->GetAuthGameMode<AMCGameMode>()->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(GetWorld());It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It)It->Destroy();
        float FloorZ=0;for(TActorIterator<AMCTongue> It(GetWorld());It;++It){It->ResetPain();It->ResetPressure();FHitResult Floor;if(It->SurfacePoint(FVector(-400,0,0),Floor))FloorZ=Floor.ImpactPoint.Z;}
        Water->Height=FloorZ+320;Water->HalfSize=FVector(1900,1400,500);Water->Seconds=11;
        Water->WaterSettings.FillSeconds=1;Water->WaterSettings.DrainSeconds=10;Water->WaterSettings.DryHeight=FloorZ-20;Water->WaterSettings.DrainPoint=FVector(1100,0,FloorZ);Water->WaterSettings.ImpactImpulse=0;Water->WaterSettings.SwimStrokeMultiplier=3;
        Water->StartedAt=GetWorld()->GetTimeSeconds()-1.3;Water->bActive=true;Water->ForceNetUpdate();
        for(int32 I=0;I<Count;++I){auto* H=Heroes[I];H->CancelGameplayInput();H->Status->Initialize(100);H->SetActorLocationAndRotation(FVector(-400,(I-(Count-1)*.5f)*180,Water->Height-25),FRotator(0,180,0),false,nullptr,ETeleportType::TeleportPhysics);H->GetCharacterMovement()->StopMovementImmediately();H->ForceNetUpdate();}
        if(Capture)
        {
            const FVector Focus(-500,0,Water->Height-10);CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(Focus+FVector(-650,-700,380),FRotator::ZeroRotator);CoffeeCamera->SetActorRotation((Focus-CoffeeCamera->GetActorLocation()).Rotation());CoffeeCamera->GetCameraComponent()->SetFieldOfView(48);PC->SetViewTarget(CoffeeCamera);
            IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("SwimFrames")),true);
            for(TActorIterator<AActor> It(GetWorld());It;++It){TArray<UTextRenderComponent*> Labels;It->GetComponents(Labels);for(auto* L:Labels)L->SetVisibility(false);}
        }
        ++DevStage;
    }
    if(GS->bDevManualEvents && Water && DevStartedAt<0)DevStartedAt=GS->DayStartedAt;
    const float T=DevStartedAt<0?-1:GS->GetServerWorldTimeSeconds()-DevStartedAt;
    if(T>=0 && Water)
    {
        if(Host && DevStage==1 && T>.8f){if(!Single)Heroes.Last()->ToothPhysics->ApplyHit(FVector(0,0,450),Heroes.Last()->GetActorLocation());++DevStage;}
        if(!bCoffeeSampleStarted && T>.9f){CoffeeSwimStart=Heroes[0]->GetActorLocation();bCoffeeSampleStarted=true;}
        for(auto* H:Heroes)if(H->IsLocallyControlled() && T>1 && T<4.2f){H->AddMovementInput(FVector(-1,0,0));H->LocalPaddle=FVector2D(-1,0);}
        if(T>4.2f)for(auto* H:Heroes)if(H->IsLocallyControlled())H->LocalPaddle=FVector2D::ZeroVector;
        bool AllSwim=true,AllPose=true,AllGround=true;
        for(auto* H:Heroes){AllSwim&=H->GetCharacterMovement()->IsSwimming();AllPose&=H->AnimationSwim>.8f;AllGround&=H->GetCharacterMovement()->IsMovingOnGround();bInvalidPhysics|=H->GetActorLocation().ContainsNaN() || H->GetVelocity().ContainsNaN();}
        if(AllSwim)DevSeen|=1;if(AllPose)DevSeen|=2;
        if(bCoffeeSampleStarted && T<4.5f && CoffeeSwimStart.X-Heroes[0]->GetActorLocation().X>80)DevSeen|=4;
        if(Heroes[0]->GetCharacterMovement()->IsSwimming() && T>2 && FMath::Abs(Heroes[0]->GetActorLocation().Z-Water->SurfaceHeightAt(Heroes[0]->GetActorLocation())+Water->WaterSettings.SwimFloatDepth)<45)DevSeen|=8;
        if(Single || (Heroes.Last()->ToothPhysics->RecoveryCount>0 && Heroes.Last()->GetCharacterMovement()->IsSwimming()))DevSeen|=64;
        if(Host && DevStage==2 && T>4.8f){Water->Stop();++DevStage;}
        if(T>6 && !Water->bActive && AllGround)DevSeen|=16;
        if(T>6 && Heroes[0]->AnimationSwim<.02f)DevSeen|=32;
        if(Capture && CoffeeCamera)
        {
            const FVector Focus=Heroes[0]->GetActorLocation()+FVector(0,0,12);
            CoffeeCamera->SetActorLocation(Focus+FVector(-340,-240,175));CoffeeCamera->SetActorRotation((Focus-CoffeeCamera->GetActorLocation()).Rotation());CoffeeCamera->GetCameraComponent()->SetFieldOfView(42);
            if(T<5 && T>1.4f && T>CoffeeNextFrame){CoffeeNextFrame=T+.15f;FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("SwimFrames/Frame%03d.png"),CoffeeFrame++),false,false);}
        }
    }
    if(Age>NextLog){NextLog=Age+2;UE_LOG(LogTemp,Display,TEXT("MC_SWIM t=%.2f seen=%d pos=%s mode=%d pose=%.2f effort=%.2f"),T,DevSeen,*Heroes[0]->GetActorLocation().ToCompactString(),int32(Heroes[0]->GetCharacterMovement()->MovementMode),Heroes[0]->AnimationSwim,Heroes[0]->AnimationSwimEffort);}
    if(T>(Host?11:9) || Age>100)Finish();
#endif
}
