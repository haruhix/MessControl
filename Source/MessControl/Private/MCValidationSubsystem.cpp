#include "MCValidationSubsystem.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCTaskActor.h"
#include "MCToothPhysicsComponent.h"
#include "MCPrototypeWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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
    const bool bRagdoll = FParse::Param(FCommandLine::Get(),TEXT("MCRagdoll"));
    const bool bRagdollCapture=FParse::Param(FCommandLine::Get(),TEXT("MCRagdollCapture"));
    if (!bSmoke && !bCapture && !bRagdoll && !bRagdollCapture) return;
    Age += DeltaSeconds;
    AMCGameState* State = GetWorld()->GetGameState<AMCGameState>();
    AMCPlayerController* PC = Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
    AMCToothCharacter* Tooth = PC ? Cast<AMCToothCharacter>(PC->GetPawn()) : nullptr;
    if ((bRagdoll || bRagdollCapture) && Tooth)
    {
        if (Tooth->HasAuthority() && !bRagdollSetup && Age>2 && (GetWorld()->GetNetMode()==NM_Standalone || (State && State->PlayerArray.Num()==4)))
        {
            int32 I=0;
            for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
            {
                It->SetActorLocation(FVector(-400+(I%2)*125,(I/2)*300-150,95),false,nullptr,ETeleportType::TeleportPhysics);
                It->SetActorRotation(FRotator(0,(I%2)*180,0)); ++I;
            }
            if (I==1) { Tooth->SetActorRotation(FRotator(0,135,0)); Tooth->SpawnPracticeTooth(); }
            bRagdollSetup=true;
        }
        int32 Falls=0,Recoveries=0;
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            Falls+=It->ToothPhysics->KnockdownCount; Recoveries+=It->ToothPhysics->RecoveryCount;
            const FVector Center=It->GetMesh()->GetBoneLocation(TEXT("body"));
            for (const FName Bone:{FName("hand_l"),FName("hand_r"),FName("foot_l"),FName("foot_r")})
            {
                const FVector Point=It->GetMesh()->GetBoneLocation(Bone);
                bInvalidPhysics |= Point.ContainsNaN() || FVector::Dist(Point,Center)>250.f;
            }
        }
        ObservedFalls=FMath::Max(ObservedFalls,Falls); ObservedRecoveries=FMath::Max(ObservedRecoveries,Recoveries);
        if (Age>=NextSwing && Age<30 && Tooth->IsLocallyControlled() && Tooth->ToothPhysics->CanAct())
        {
            Tooth->SwingBrush(); NextSwing=Age+(bRagdollCapture?20.f:1.3f);
        }
        if (bRagdoll && Age>8 && Age<30 && Tooth->ToothPhysics->CanAct())
        {
            AMCToothCharacter* Target=nullptr; float Distance=MAX_flt;
            for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (*It!=Tooth && It->ToothPhysics->CanAct())
            {
                const float D=FVector::DistSquared2D(Tooth->GetActorLocation(),It->GetActorLocation());
                if (D<Distance) { Target=*It; Distance=D; }
            }
            if (Target && Distance>FMath::Square(100.f)) Tooth->AddMovementInput((Target->GetActorLocation()-Tooth->GetActorLocation()).GetSafeNormal2D());
        }
        if (bRagdollCapture)
        {
            if (CaptureStage==2 && Age>4.2f && IsValid(Tooth->PracticeTooth) && PC->GetViewTarget()!=Tooth->PracticeTooth)
                PC->SetViewTargetWithBlend(Tooth->PracticeTooth,0.3f);
            const float Times[]={2.7f,3.45f,5.6f,8.8f};
            const TCHAR* Names[]={TEXT("Rig"),TEXT("Ragdoll"),TEXT("GetUp"),TEXT("Recovered")};
            if (CaptureStage<4 && Age>Times[CaptureStage])
            {
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/FString::Printf(TEXT("Artifacts/Unreal_%s.png"),Names[CaptureStage]),true,false);
                ++CaptureStage;
            }
        }
    }
    if (State)
    {
        int32 ExpectedPlayers = 1; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),ExpectedPlayers);
        bObservedPlayers |= State->PlayerArray.Num() >= ExpectedPlayers;
        bObservedWork |= State->TasksTotal > State->TasksLeft && State->Day > 0;
        for (TActorIterator<AMCTaskActor> It(GetWorld()); It; ++It) bObservedWork |= It->Progress>0.f;
        if (Age >= NextLog)
        {
            NextLog += 5;
            if (bRagdoll || bRagdollCapture)
                UE_LOG(LogTemp,Display,TEXT("MC_PHYSICS net=%d falls=%d recoveries=%d invalid=%d ownstate=%d body=%s"),static_cast<int32>(GetWorld()->GetNetMode()),ObservedFalls,ObservedRecoveries,bInvalidPhysics,Tooth?static_cast<int32>(Tooth->ToothPhysics->GetBodyState()):-1,Tooth?*Tooth->GetMesh()->GetBoneLocation(TEXT("body")).ToString():TEXT("none"));
            UE_LOG(LogTemp,Display,TEXT("MC_SMOKE net=%d players=%d day=%d phase=%d left=%d health=%.0f work=%d pawn=%s"),
                static_cast<int32>(GetWorld()->GetNetMode()),State->PlayerArray.Num(),State->Day,static_cast<int32>(State->Phase),State->TasksLeft,State->MouthHealth,bObservedWork,Tooth?*Tooth->GetActorLocation().ToString():TEXT("none"));
        }
    }
    if (bSmoke && Tooth && Tooth->IsLocallyControlled() && (!bRagdoll || Age>33))
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
    if (bCapture && PC && PC->PrototypeWidget && Age>22 && Age<23)
    {
        PC->PrototypeWidget->ScrollToPhysics(); Age=23;
    }
    if (bCapture && Age>24 && Age<24.5f)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Artifacts/Unreal_PhysicsLab.png"),true,false); Age=24.5f;
    }
    const float SmokeDuration = GetWorld()->GetNetMode()==NM_ListenServer ? 85.f : 45.f;
    if (Age>(bSmoke?SmokeDuration:bRagdoll?40.f:bRagdollCapture?12.f:25.f))
    {
        bool bSuccess=bSmoke ? bObservedWork && bObservedPlayers && Tooth != nullptr : (bRagdoll || bRagdollCapture)?Tooth!=nullptr:bCaptured && Tooth != nullptr;
        if (bRagdoll || bRagdollCapture) bSuccess &= ObservedFalls>=1 && ObservedRecoveries>=1 && !bInvalidPhysics;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s"),bSuccess?TEXT("PASS"):TEXT("FAIL"));
        FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
    }
#endif
}
