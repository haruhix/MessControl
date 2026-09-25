#include "MCArenaDemo.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCPrototypeWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/PointLight.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

AMCArenaDemo::AMCArenaDemo() { PrimaryActorTick.bCanEverTick=true; }
void AMCArenaDemo::Tick(float Dt)
{
#if !UE_BUILD_SHIPPING
    Super::Tick(Dt);
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    auto* PC=Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
    if (!GS || !PC || GS->ArenaTeeth.Num()<3) return;
    // The demonstration pauses day scheduling; it uses the same actor APIs as gameplay.
    GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
    if (!Camera)
    {
        Subject=GS->ArenaTeeth[2]; Anchor=Subject->GetActorLocation();
        Camera=GetWorld()->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(52);
        Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
        Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0;
        PC->SetViewTarget(Camera); PC->SetIgnoreMoveInput(true); PC->SetIgnoreLookInput(true);
        PC->ClientSetHUD(AMCArenaDemoHUD::StaticClass());
        if (PC->PrototypeWidget) PC->PrototypeWidget->SetVisibility(ESlateVisibility::Collapsed);
        if (PC->GetPawn()) PC->GetPawn()->SetActorHiddenInGame(true);
        for (AMCArenaTooth* Tooth:GS->ArenaTeeth) Tooth->Label->SetVisibility(false);
        auto* Light=GetWorld()->SpawnActor<APointLight>(Anchor+FVector(-200,260,320),FRotator::ZeroRotator);
        Light->PointLightComponent->SetIntensity(4500); Light->PointLightComponent->SetAttenuationRadius(1100);
        Light->PointLightComponent->SetSourceRadius(100); Light->PointLightComponent->SetCastShadows(false);
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("ArenaDemoFrames")),true);
    }
#if WITH_EDITOR
    if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    Age+=Dt;
    if (Age>=5 && Stage==0) { Subject->SetCoffee(1); ++Stage; }
    if (Age>=9 && Stage==1) { Subject->ReceiveArenaHit(25,FVector(0,1,0)); ++Stage; }
    if (Age>=11 && Stage==2) { Subject->ReceiveArenaHit(25,FVector(0,1,0)); ++Stage; }
    if (Age>=13 && Stage==3) { Subject->ReceiveArenaHit(25,FVector(0,1,0)); ++Stage; }
    if (Age>=17 && Stage==4) { Subject->ReceiveArenaHit(25,FVector(0,1,0)); ++Stage; }
    const float Zoom=FMath::SmoothStep(1.5f,4.f,Age);
    const float PullBack=FMath::SmoothStep(16.f,18.5f,Age);
    const FVector Close=Anchor+FVector(-530,610,250);
    const FVector FallView=Anchor+FVector(-700,730,420);
    const FVector Position=FMath::Lerp(FVector(-2750,0,1050),FMath::Lerp(Close,FallView,PullBack),Zoom);
    const FVector Aim=FMath::Lerp(FVector(0,0,110),Anchor+FVector(0,110*PullBack,25),Zoom);
    Camera->SetActorLocationAndRotation(Position,(Aim-Position).Rotation());
    if (Age>=0 && Age<23)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("ArenaDemoFrames/Frame%05d.png"),Frame++),true,false);
    }
    if (Age>=23.1f)
    {
        const bool bOK=Subject->State.bLost && GS->AvailableArenaTeeth()==GS->RunSettings.InitialArenaTeeth-1;
        UE_LOG(LogTemp,Display,TEXT("MC_ARENA_DEMO_%s frames=%d"),bOK?TEXT("PASS"):TEXT("FAIL"),Frame);
        FPlatformMisc::RequestExitWithStatus(false,bOK?0:1);
    }
#endif
}
void AMCArenaDemoHUD::DrawHUD()
{
    Super::DrawHUD(); if (!Canvas) return;
    AMCArenaDemo* Demo=nullptr; for (TActorIterator<AMCArenaDemo> It(GetWorld());It;++It) { Demo=*It; break; }
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if (!Demo || !Demo->Subject || !GS) return;
    const float S=Canvas->SizeX/1280.f;
    const FLinearColor Mint(.24f,1,.79f), Cream(1,.96f,.88f), Back(.018f,.035f,.038f,.9f), Orange(1,.5f,.15f);
    auto Text=[&](const FString& Value,float X,float Y,float Scale,FLinearColor Color)
    { DrawText(Value,Color,X*S,Y*S,GEngine->GetMediumFont(),Scale*S); };
    DrawRect(Back,24*S,24*S,670*S,99*S);
    Text(TEXT("MESS / CONTROL    |    STEP 02"),42,36,1.25f,Mint);
    const float T=Demo->Age;
    const TCHAR* Title=T<5?TEXT("01   The mouth's teeth: four per side"):T<9?TEXT("02   Coffee coats a large molar"):T<13?TEXT("03   Damage / squash / recoil"):T<17?TEXT("04   Low health: loose molar"):TEXT("05   A gap in the row: 7 teeth remain");
    Text(Title,42,70,1.6f,Cream);
    DrawRect(Back,976*S,24*S,280*S,137*S);
    Text(FString::Printf(TEXT("ARENA TEETH   %d / %d"),GS->AvailableArenaTeeth(),GS->RunSettings.InitialArenaTeeth),994,40,1.2f,Mint);
    Text(FString::Printf(TEXT("TOOTH 03      HP %.0f"),Demo->Subject->State.Health),994,78,1.2f,Cream);
    DrawRect(FLinearColor(.1f,.13f,.13f),994*S,120*S,240*S,10*S);
    DrawRect(T>=13?Orange:Mint,994*S,120*S,240*S*Demo->Subject->State.Health/Demo->Subject->Settings.MaxHealth,10*S);
    const TCHAR* Caption=T<5?TEXT("The large teeth along the gums are the teeth you care for. No extra inner row."):
        T<9?TEXT("Surface status: brown patches over glossy enamel. Health is still 100."):
        T<13?TEXT("25 damage per hit. The mesh reacts; the collision body stays stable."):
        T<17?TEXT("At 25 HP: persistent wobble, cracks, and follow-through."):
        TEXT("Brief anticipation, then a physical fall. The lost tooth stays out of the reserve.");
    DrawRect(Back,24*S,626*S,1232*S,70*S);
    Text(Caption,42,640,1.16f,Cream);
    Text(TEXT("IN-ENGINE PROTOTYPE  /  Scripted demonstration  /  Respawn consumption comes in a later step"),42,674,.8f,Mint);
}
