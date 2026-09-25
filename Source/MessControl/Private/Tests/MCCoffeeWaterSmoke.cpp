#include "MCValidationSubsystem.h"
#include "MCPlayerController.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCCoffeeFlood.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void UMCValidationSubsystem::TickCoffeeWater(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    auto* PC=Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
    if (!GS || !PC) return;
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCCoffeeWaterCapture"));
    bool Ready=true;
#if WITH_EDITOR
    Ready=!Capture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    if (Host && DevStage==0)
    {
        GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
        if (Capture && !bCoffeePreloaded)
        {
            // Load the translucent material before waiting for its shaders. The clean
            // event restart removes this inactive actor before the actual test.
            GetWorld()->SpawnActor<AMCCoffeeFlood>();
            bCoffeePreloaded=true;
            return;
        }
        if (GS->PlayerArray.Num()==4 && Age>8 && Ready)
        {
            auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
            const auto* Plan=Mode->FirstDayPlan.LoadSynchronous();
            const int32 Step=Plan->Steps.IndexOfByPredicate([](const FMCDayStepSettings& S){return S.Step==EMCDayStep::CoffeeWaves;});
            PC->RequestDevAction(EMCDevAction::StartStep,Step);
            // Twenty seconds make the visual demo readable; the normal DA event still uses ten.
            Mode->DayDirector->Flood->Start(Mode->DayDirector->Settings,20);
            DevStage=1;
        }
    }
    AMCCoffeeFlood* Flood=nullptr; for (TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) { Flood=*It; break; }
    if (DevStartedAt<0 && GS->bDevManualEvents && Flood) DevStartedAt=GS->DayStartedAt;
    const float T=DevStartedAt<0?-1:GS->GetServerWorldTimeSeconds()-DevStartedAt;
    TArray<AMCToothCharacter*> Heroes;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if (Host && DevStage==1 && Heroes.Num()==4 && !GS->ArenaTeeth.IsEmpty())
    {
        const auto* Anchor=GS->ArenaTeeth[0].Get(); FVector P=Anchor->GetActorLocation(); const float Side=FMath::Sign(P.Y);
        P.Y-=Side*(Anchor->Body->Bounds.BoxExtent.Y+65); P.Z=95;
        for (int32 I=0;I<4;++I)
        {
            auto* H=Heroes[I]; H->GetCharacterMovement()->StopMovementImmediately();
            H->SetActorLocationAndRotation(I==0?P:FVector(-480+(I-1)*300,(I%2?1:-1)*140,95),FRotator(0,I==0?Side*90:0,0),false,nullptr,ETeleportType::TeleportPhysics);
            H->ForceNetUpdate();
        }
        DevStage=2;
    }
    if (T>=0 && Heroes.Num()==4 && Flood)
    {
        if (auto* H=Cast<AMCToothCharacter>(PC->GetPawn()))
        {
            const int32 Slot=Heroes.IndexOfByKey(H);
            if (Slot==0 && T<20 && !H->bWantsCling) H->ServerSetWorking(false,true);
            if (Slot==0 && T>=20 && H->bWantsCling) H->ServerSetWorking(false,false);
            if (Slot>0 && H->bInCoffee)
            {
                H->LocalPaddle=FVector2D(-.8f,Slot==1?.6f:Slot==2?-.6f:0);
                H->ServerPaddle(H->LocalPaddle);
            }
        }
        if (Flood->Profile && Flood->IsActive()) DevSeen|=1;
        if (Heroes[0]->ClingTooth) DevSeen|=2;
        const FVector P=Heroes[1]->ToothPhysics->PhysicalLocation();
        if (Heroes[1]->bInCoffee && Heroes[1]->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll)
        {
            DevSeen|=4;
            if (Flood->Level>Flood->Height*.85f && FMath::Abs(P.Z-Flood->SurfaceHeightAt(P))<70) DevSeen|=8;
            if (!bCoffeeSampleStarted) { CoffeeSwimStart=P; bCoffeeSampleStarted=true; }
            if (P.Y-CoffeeSwimStart.Y>45) DevSeen|=16;
        }
        if (Flood->Wave==4) DevSeen|=32;
        if (T>21 && !Flood->IsActive() && !Heroes[1]->bInCoffee && Heroes[1]->ToothPhysics->GetBodyState()==EMCBodyState::Standing) DevSeen|=64;
        if (P.ContainsNaN()) bInvalidPhysics=true;
    }
    if (Capture && T>=0)
    {
        if (!CoffeeCamera)
        {
            CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(); CoffeeCamera->GetCameraComponent()->SetFieldOfView(57);
            const FVector Aim(-120,0,105),P(-1950,30,850);
            CoffeeCamera->SetActorLocationAndRotation(P,(Aim-P).Rotation()); PC->SetViewTarget(CoffeeCamera);
            IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("CoffeeWaterFrames")),true);
        }
        if (T<25 && T>=CoffeeNextFrame)
        {
            CoffeeNextFrame=T+.1f;
            if (CoffeeLastFrame>=0) CoffeeTiming+=FString::Printf(TEXT("duration %.6f\n"),T-CoffeeLastFrame);
            CoffeeTiming+=FString::Printf(TEXT("file 'Frame%05d.png'\n"),CoffeeFrame);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("CoffeeWaterFrames/Frame%05d.png"),CoffeeFrame++),true,false);
            CoffeeLastFrame=T;
        }
    }
    if (Age>NextLog) { NextLog+=5; UE_LOG(LogTemp,Display,TEXT("MC_COFFEE_WATER net=%d t=%.1f seen=%d invalid=%d"),int32(GetWorld()->GetNetMode()),T,DevSeen,bInvalidPhysics); }
    if (T>(Host?32:28) || Age>150)
    {
        if (Capture && CoffeeFrame>0)
            FFileHelper::SaveStringToFile(CoffeeTiming+FString::Printf(TEXT("duration 0.1\nfile 'Frame%05d.png'\n"),CoffeeFrame-1),*(FPaths::ProjectSavedDir()/TEXT("CoffeeWaterFrames/Timing.txt")));
        const bool Pass=DevSeen==127 && !bInvalidPhysics;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s coffee-water seen=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),DevSeen);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}
