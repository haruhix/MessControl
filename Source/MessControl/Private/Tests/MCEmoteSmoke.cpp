#include "MCValidationSubsystem.h"
#include "MCExpressionComponent.h"
#include "MCGazeComponent.h"
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
#include "Engine/SkeletalMesh.h"
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
    const bool MouthTest=FParse::Param(FCommandLine::Get(),TEXT("MCMouthTest"));
    const bool PupilTest=FParse::Param(FCommandLine::Get(),TEXT("MCPupilTest"));
    const bool FaceTest=MouthTest || PupilTest;
    const bool Capture=Host && (FaceTest || FParse::Param(FCommandLine::Get(),TEXT("MCEmoteCapture")));
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
            const FVector Focus=Heroes[0]->GetActorLocation()+FVector(0,0,FaceTest?12:0);
            CoffeeCamera=GetWorld()->SpawnActor<ACameraActor>(Focus+(FaceTest?FVector(230,-35,25):FVector(680,-190,150)),FRotator::ZeroRotator);
            CoffeeCamera->SetActorRotation((Focus+FVector(0,0,FaceTest?0:40)-CoffeeCamera->GetActorLocation()).Rotation());
            CoffeeCamera->GetCameraComponent()->SetFieldOfView(FaceTest?33:43); PC->SetViewTarget(CoffeeCamera);
            if (FaceTest) CoffeeCamera->GetCameraComponent()->SetAspectRatio(1.f);
            for (TActorIterator<AActor> It(GetWorld());It;++It)
            { TArray<UTextRenderComponent*> Texts; It->GetComponents(Texts); for (auto* Text:Texts) Text->SetVisibility(false); }
            IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("EmoteFrames")),true);
            if (MouthTest) IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("MouthFrames")),true);
            if (PupilTest) IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("PupilFrames")),true);
        }
        ++DevStage;
    }
    if (GS->bDevManualEvents && DevStartedAt<0) DevStartedAt=GS->DayStartedAt;
    const float T=DevStartedAt<0?-1:GS->GetServerWorldTimeSeconds()-DevStartedAt;
    if (PupilTest && T>=0)
    {
        auto* H=Heroes[0];auto* G=H->Gaze.Get();
        if (DevStage==1 && T>1) { G->NoticePoint(H->GetActorLocation()+FVector(300,0,25),1.4f);++DevStage; }
        if (DevStage==2 && T>6) { H->Expression->ServerPlayEmote(TEXT("angry"));++DevStage; }
        if (DevStage==3 && T>9.5f) { H->Expression->ServerPlayEmote(TEXT("happy"));++DevStage; }
        // Keep the authored closed pose long enough to capture its composition
        // with the smile; ordinary gameplay still uses the timed blink pulse.
        if (DevStage==4 && T>10.4f && T<10.65f) G->BlinkStartedAt=GS->GetServerWorldTimeSeconds()-G->Settings.BlinkSeconds*.5;
        if (DevStage==4 && T>10.7f) { H->Status->Damage(10);++DevStage; }
        if (DevStage==5 && T>13.5f) { H->Expression->ServerPlayEmote(TEXT("artist_shock"));++DevStage; }
        const float Times[]={.65f,1.8f,5.8f,6.8f,10.3f,10.55f,11.0f,14.3f};
        const TCHAR* Names[]={TEXT("Calm"),TEXT("Danger"),TEXT("Recovered"),TEXT("Focus"),TEXT("Positive"),TEXT("PositiveBlink"),TEXT("Pain"),TEXT("Shock")};
        if (CaptureStage<8 && T>Times[CaptureStage])
        {
            const float Scale=G->PupilScale;
            const bool Expected=CaptureStage==1 || CaptureStage==7?Scale<.8f:CaptureStage==3?Scale<.87f:CaptureStage==4 || CaptureStage==5?Scale>1.5f:CaptureStage==6?Scale<.95f:FMath::Abs(Scale-1)<.025f;
            bInvalidPhysics|=!FMath::IsFinite(Scale) || !Expected;
            if (CaptureStage==5) bInvalidPhysics|=H->GetMesh()->GetMorphTarget(TEXT("Eyes_Blink"))<.9f;
            UE_LOG(LogTemp,Display,TEXT("MC_PUPIL_STAGE %s scale=%.4f"),Names[CaptureStage],Scale);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("PupilFrames/%s.png"),Names[CaptureStage]),false,false);++CaptureStage;
        }
        if (T>15.3f)
        {
            const bool Pass=CaptureStage==8 && !bInvalidPhysics;
            UE_LOG(LogTemp,Display,TEXT("MC_PUPIL_%s stages=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),CaptureStage);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
        return;
    }
    if (MouthTest && T>=0)
    {
        // Run all twelve speech targets through the real expression component and render path.
        const int32 Index=FMath::FloorToInt(T/1.25f);
        auto* H=Heroes[0]; auto* E=H->Expression.Get();
        if (Index<12)
        {
            const auto Viseme=MCViseme(Index+1); const FName Shape=UMCExpressionComponent::VisemeShape(Viseme);
            E->SetSpeechInput(Viseme==MCViseme::Closed?0.f:1.f,Viseme);
            if (T-Index*1.25f>.65f && CaptureStage==Index)
            {
                float Weight=H->GetMesh()->GetMorphTarget(Shape);
                if (Viseme==MCViseme::Closed && !H->GetMesh()->GetSkeletalMeshAsset()->FindMorphTarget(Shape))
                {
                    float Sum=0; for (FName Name:UMCExpressionComponent::MouthShapes()) Sum+=H->GetMesh()->GetMorphTarget(Name);
                    Weight=1-Sum;
                }
                bInvalidPhysics|=!FMath::IsFinite(Weight) || Weight<.97f;
                UE_LOG(LogTemp,Display,TEXT("MC_MOUTH_SHAPE %s %.4f"),*Shape.ToString(),Weight);
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("MouthFrames/%s.png"),*Shape.ToString()),false,false);
                ++CaptureStage;
            }
        }
        else if (T>16)
        {
            float Sum=0; for (FName Shape:UMCExpressionComponent::MouthShapes()) Sum+=H->GetMesh()->GetMorphTarget(Shape);
            const bool Pass=CaptureStage==12 && !bInvalidPhysics && Sum<.01f;
            UE_LOG(LogTemp,Display,TEXT("MC_MOUTH_%s shapes=%d restWeight=%.5f"),Pass?TEXT("PASS"):TEXT("FAIL"),CaptureStage,Sum);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
        return;
    }
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
            const bool Invalid=P.ContainsNaN() || FVector::Dist(P,H->GetActorLocation())>450;
            if (Invalid && !bInvalidPhysics)
                UE_LOG(LogTemp,Error,TEXT("MC_EMOTE_INVALID t=%.3f hero=%s role=%s bone=%s actor=%s mesh=%s"),T,*H->GetName(),*Role.ToString(),*P.ToString(),*H->GetActorLocation().ToString(),*H->GetMesh()->GetComponentLocation().ToString());
            bInvalidPhysics|=Invalid;
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
