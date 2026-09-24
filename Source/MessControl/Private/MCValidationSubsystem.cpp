#include "MCValidationSubsystem.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCTaskActor.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void UMCValidationSubsystem::Tick(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
    const bool bSmoke = FParse::Param(FCommandLine::Get(),TEXT("MCSmoke"));
    const bool bCapture = FParse::Param(FCommandLine::Get(),TEXT("MCCapture"));
    if (!bSmoke && !bCapture) return;
    Age += DeltaSeconds;
    AMCGameState* State = GetWorld()->GetGameState<AMCGameState>();
    AMCPlayerController* PC = Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
    AMCToothCharacter* Tooth = PC ? Cast<AMCToothCharacter>(PC->GetPawn()) : nullptr;
    if (State)
    {
        int32 ExpectedPlayers = 1; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),ExpectedPlayers);
        bObservedPlayers |= State->PlayerArray.Num() >= ExpectedPlayers;
        bObservedWork |= State->TasksTotal > State->TasksLeft && State->Day > 0;
        for (TActorIterator<AMCTaskActor> It(GetWorld()); It; ++It) bObservedWork |= It->Progress>0.f;
        if (Age >= NextLog)
        {
            NextLog += 5;
            UE_LOG(LogTemp,Display,TEXT("MC_SMOKE net=%d players=%d day=%d phase=%d left=%d health=%.0f work=%d pawn=%s"),
                static_cast<int32>(GetWorld()->GetNetMode()),State->PlayerArray.Num(),State->Day,static_cast<int32>(State->Phase),State->TasksLeft,State->MouthHealth,bObservedWork,Tooth?*Tooth->GetActorLocation().ToString():TEXT("none"));
        }
    }
    if (bSmoke && Tooth && Tooth->IsLocallyControlled())
    {
        AMCTaskActor* Best=nullptr; float BestDistance=MAX_flt;
        for (TActorIterator<AMCTaskActor> It(GetWorld()); It; ++It)
        {
            const float Distance=FVector::DistSquared2D(Tooth->GetActorLocation(),It->GetActorLocation());
            if (It->Progress<1.f && Distance<BestDistance) { Best=*It; BestDistance=Distance; }
        }
        if (Best)
        {
            const FVector Direction=(Best->GetActorLocation()-Tooth->GetActorLocation()).GetSafeNormal2D();
            if (BestDistance>FMath::Square(85.f)) Tooth->AddMovementInput(Direction);
            if (!Tooth->bBrushing) Tooth->StartBrush();
            if (!Tooth->bHandling) Tooth->StartHandle();
        }
        else
        {
            if (Tooth->bBrushing) Tooth->StopBrush();
            if (Tooth->bHandling) Tooth->StopHandle();
        }
    }
    if (bCapture && PC && Tooth && !bCaptured && Age>12)
    {
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectDir()/TEXT("Artifacts")),true);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Artifacts/Unreal_Gameplay.png"),true,false);
        bCaptured=true;
    }
    if (bCapture && PC && bCaptured && !bCapturedLab && Age>17)
    {
        PC->ToggleTuning();
        bCapturedLab=true;
    }
    if (bCapture && bCapturedLab && Age>20 && Age<21)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Artifacts/Unreal_AnimationLab.png"),true,false);
        Age=21;
    }
    const float SmokeDuration = GetWorld()->GetNetMode()==NM_ListenServer ? 85.f : 45.f;
    if (Age>(bSmoke?SmokeDuration:25.f))
    {
        const bool bSuccess=bSmoke ? bObservedWork && bObservedPlayers && Tooth != nullptr : bCaptured && Tooth != nullptr;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s"),bSuccess?TEXT("PASS"):TEXT("FAIL"));
        FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
    }
#endif
}
