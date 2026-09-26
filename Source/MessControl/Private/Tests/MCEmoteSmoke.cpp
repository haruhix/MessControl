#include "MCValidationSubsystem.h"
#include "MCExpressionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "MCDayDirector.h"
#include "MCTaskActor.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void UMCValidationSubsystem::TickEmotes(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt; auto* GS=GetWorld()->GetGameState<AMCGameState>(); auto* PC=Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
    if (!GS || !PC) return;
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCEmoteCapture"));
    auto Finish=[&]()
    {
        const bool Pass=DevSeen==31 && !bInvalidPhysics;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s EMOTES net=%d seen=%d invalid=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,bInvalidPhysics);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    TArray<AMCToothCharacter*> Heroes;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    const int32 PlayerCount=GetWorld()->GetNetMode()==NM_Standalone?1:4;
    if (Heroes.Num()<PlayerCount)
    {
        if (DevStartedAt>=0 && GS->GetServerWorldTimeSeconds()-DevStartedAt>17) Finish();
        else if (Age>100) Finish();
        return;
    }
    if (Host && DevStage==0 && Age>8)
    {
#if WITH_EDITOR
        if (Capture && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
        bool Ready=true;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            Ready&=It->Get()->GetPawn() && (It->Get()->IsLocalController() || It->Get()->AcknowledgedPawn==It->Get()->GetPawn());
        if (!Ready) return;
        GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0; GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->ForceNetUpdate();
        GetWorld()->GetAuthGameMode<AMCGameMode>()->SetActorTickEnabled(false);
        for (TActorIterator<AMCDayDirector> It(GetWorld());It;++It) It->SetActorTickEnabled(false);
        for (TActorIterator<AMCTaskActor> It(GetWorld());It;++It) It->Destroy();
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->Destroy();
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            It->ResetPain();
            for (int32 I=0;I<Heroes.Num();++I)
            {
                auto* H=Heroes[I]; FHitResult Hit; It->SurfacePoint(FVector(-120,I*230-350,0),Hit);
                H->CancelGameplayInput(); H->Status->Initialize(100);
                H->SetActorLocationAndRotation(Hit.ImpactPoint+FVector(0,0,61),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics); H->ForceNetUpdate();
            }
            break;
        }
        if (Capture)
        {
            const FVector Focus=Heroes[0]->GetActorLocation();
            CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(Focus+FVector(680,-190,150),FRotator::ZeroRotator);
            CoffeeCamera->SetActorRotation((Focus+FVector(0,0,40)-CoffeeCamera->GetActorLocation()).Rotation());
            CoffeeCamera->GetCameraComponent()->SetFieldOfView(43); PC->SetViewTarget(CoffeeCamera);
            for (TActorIterator<AActor> It(GetWorld());It;++It)
            { TArray<UTextRenderComponent*> Texts; It->GetComponents(Texts); for (auto* Text:Texts) Text->SetVisibility(false); }
            IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("EmoteFrames")),true);
        }
        ++DevStage;
    }
    if (GS->bDevManualEvents && DevStartedAt<0) DevStartedAt=GS->DayStartedAt;
    const float T=DevStartedAt<0?-1:GS->GetServerWorldTimeSeconds()-DevStartedAt;
    if (T>1 && !bDevClientGuard)
    {
        if (auto* Own=Cast<AMCToothCharacter>(PC->GetPawn())) Own->Expression->ServerPlayEmote(TEXT("hello"));
        bDevClientGuard=true;
    }
    if (Host)
    {
        if (DevStage==1 && T>4) { for (auto* H:Heroes) H->Expression->ServerPlayEmote(TEXT("highfive")); ++DevStage; }
        if (DevStage==2 && T>8) { for (auto* H:Heroes) H->Expression->ServerPlayEmote(TEXT("happy")); ++DevStage; }
        if (DevStage==3 && T>10) { for (auto* H:Heroes) H->Status->Damage(10); ++DevStage; }
    }
    bool Hello=true,Highfive=true,Happy=true,Pain=true,Speech=true;
    for (auto* H:Heroes)
    {
        auto* E=H->Expression.Get();
        Hello&=E->State.Id==TEXT("hello") && E->BodyAlpha()>.5f;
        Highfive&=E->State.Id==TEXT("highfive") && E->BodyAlpha()>.5f;
        Happy&=E->CurrentEmotion==EMCEmotion::Happy && E->State.Id==TEXT("happy");
        Pain&=E->CurrentEmotion==EMCEmotion::Pain;
        if (T>12 && T<14) E->SetSpeechInput(.3f+.5f*FMath::Abs(FMath::Sin(T*10)),MCViseme::Round);
        Speech&=E->SpeechAmount()>.1f;
        for (const FName Role:{FName(TEXT("body")),FName(TEXT("hand_l")),FName(TEXT("hand_r"))})
        {
            const FVector P=H->GetMesh()->GetSocketLocation(H->RigBone(Role));
            bInvalidPhysics|=P.ContainsNaN() || FVector::Dist(P,H->GetActorLocation())>450;
        }
    }
    if (Hello) DevSeen|=1; if (Highfive) DevSeen|=2; if (Happy) DevSeen|=4; if (Pain) DevSeen|=8; if (Speech) DevSeen|=16;
    if (Capture)
    {
        const float Times[]={1.8f,4.7f,8.6f,10.15f,12.8f,15.2f};
        const TCHAR* Names[]={TEXT("Hello"),TEXT("Highfive"),TEXT("Happy"),TEXT("Pain"),TEXT("Speech"),TEXT("Menu")};
        if (T>15 && CaptureStage==5 && !bCapturedLab) { PC->ToggleEmotes(); bCapturedLab=true; }
        if (CaptureStage<6 && T>Times[CaptureStage])
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("EmoteFrames/%s.png"),Names[CaptureStage]),CaptureStage==5,false); ++CaptureStage;
        }
    }
    if (T>(Host?19:17) || Age>140) Finish();
#endif
}
