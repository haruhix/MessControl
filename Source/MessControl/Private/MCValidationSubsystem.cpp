#include "MCValidationSubsystem.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "Components/StaticMeshComponent.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCTaskActor.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "Components/BoxComponent.h"
#include "MCPrototypeWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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
    if (FParse::Param(FCommandLine::Get(),TEXT("MCEmoteTest")) || FParse::Param(FCommandLine::Get(),TEXT("MCMouthTest")) || FParse::Param(FCommandLine::Get(),TEXT("MCPupilTest"))) { TickEmotes(DeltaSeconds); return; }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCGripTest"))) { TickGrip(DeltaSeconds); return; }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCTonguePressureTest"))) { TickTonguePressure(DeltaSeconds); return; }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCGazeTest"))) { TickGaze(DeltaSeconds); return; }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCTongueTest"))) { TickTongue(DeltaSeconds); return; }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCCoffeeWaterTest"))) { TickCoffeeWater(DeltaSeconds); return; }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCDevPanelSmoke"))) { TickDevPanel(DeltaSeconds); return; }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCCore")) || FParse::Param(FCommandLine::Get(),TEXT("MCDayOne"))) return;
    if (FParse::Param(FCommandLine::Get(),TEXT("MCLimbs"))) { TickLimbStability(DeltaSeconds); return; }
    const bool bSmoke = FParse::Param(FCommandLine::Get(),TEXT("MCSmoke"));
    const bool bArena = FParse::Param(FCommandLine::Get(),TEXT("MCArenaNet"));
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
            const FVector Center=It->ToothPhysics->PhysicalLocation();
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
        if (bArena && State->ArenaTeeth.Num()==8)
        {
            AMCArenaTooth* Arena=State->ArenaTeeth[2];
            if (IsValid(Arena))
            {
                bArenaInitial |= State->AvailableArenaTeeth()==8 && Arena->State.Health==100 &&
                    Arena->Visual->Bounds.BoxExtent.Z>100 && FMath::Abs(Arena->GetActorLocation().Y)>750;
                if (Arena->HasAuthority() && State->PlayerArray.Num()==4 && ArenaReadyAt<0) ArenaReadyAt=Age;
                // PlayerState exists on the host before the new client has received its initial actor bunches.
                if (Arena->HasAuthority() && ArenaReadyAt>=0 && Age>ArenaReadyAt+6 && ArenaStage==0)
                { Arena->SetCoffee(1); Arena->ReceiveArenaHit(75,FVector(0,1,0)); ++ArenaStage; }
                if (Arena->HasAuthority() && Age>ArenaReadyAt+18 && ArenaStage==1)
                { Arena->ReceiveArenaHit(25,FVector(0,1,0)); ++ArenaStage; }
                bArenaDamaged |= Arena->State.Coffee==1 && Arena->State.Health==25 && Arena->IsLoose();
                bArenaLost |= Arena->State.bLost && State->AvailableArenaTeeth()==7 && !Arena->GetActorLocation().ContainsNaN();
            }
        }
        int32 ExpectedPlayers = 1; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),ExpectedPlayers);
        bObservedPlayers |= State->PlayerArray.Num() >= ExpectedPlayers;
        bObservedWork |= State->TasksTotal > State->TasksLeft && State->Day > 0;
        for (TActorIterator<AMCTaskActor> It(GetWorld()); It; ++It) bObservedWork |= It->Progress>0.f;
        if (Age >= NextLog)
        {
            NextLog += 5;
            if (bRagdoll || bRagdollCapture)
                UE_LOG(LogTemp,Display,TEXT("MC_PHYSICS net=%d falls=%d recoveries=%d invalid=%d ownstate=%d body=%s"),static_cast<int32>(GetWorld()->GetNetMode()),ObservedFalls,ObservedRecoveries,bInvalidPhysics,Tooth?static_cast<int32>(Tooth->ToothPhysics->GetBodyState()):-1,Tooth?*Tooth->ToothPhysics->PhysicalLocation().ToString():TEXT("none"));
            UE_LOG(LogTemp,Display,TEXT("MC_SMOKE net=%d players=%d day=%d phase=%d left=%d health=%.0f work=%d pawn=%s"),
                static_cast<int32>(GetWorld()->GetNetMode()),State->PlayerArray.Num(),State->Day,static_cast<int32>(State->Phase),State->TasksLeft,State->MouthHealth,bObservedWork,Tooth?*Tooth->GetActorLocation().ToString():TEXT("none"));
        }
    }
    if (bSmoke && Tooth && Tooth->IsLocallyControlled() && (!bRagdoll || Age>33))
    {
        // Exercise the production inputs against actual teeth/food, not the retired task proxies.
        if (Tooth->HeldFood)
        {
            const FVector Direction=Tooth->HeldFood->Phase==EMCFoodPhase::Stuck?Tooth->HeldFood->PullDirection:(FVector(965,0,95)-Tooth->GetActorLocation()).GetSafeNormal2D();
            Tooth->AddMovementInput(Direction);
        }
        else if (Tooth->Status->NeedsCare(true) || Tooth->Status->NeedsCare(false))
        {
            if (!Tooth->bSelfCare) Tooth->ServerToggleSelfCare();
            if (Tooth->Status->NeedsCare(true)) { if (!Tooth->bBrushing) Tooth->StartBrush(); }
            else if (!Tooth->bHandling) Tooth->StartHandle();
        }
        else
        {
            if (Tooth->bSelfCare) Tooth->ServerToggleSelfCare();
            AActor* Best=nullptr; float BestDistance=MAX_flt; bool bBrush=false; FVector Goal;
            for (TActorIterator<AActor> It(GetWorld());It;++It)
            {
                auto* Status=It->FindComponentByClass<UMCToothStatusComponent>(); auto* Food=Cast<AMCFoodActor>(*It);
                auto* Arena=Cast<AMCArenaTooth>(*It);
                if (*It==Tooth || (Arena && !Arena->IsAvailable())) continue;
                const bool bNeedsCare=Status && (Status->NeedsCare(true) || Status->NeedsCare(false));
                if (!bNeedsCare && (!Food || Food->IsDisposed())) continue;
                FVector Point=It->GetActorLocation();
                if (Arena) Point.Y-=FMath::Sign(Point.Y)*(Arena->Body->Bounds.BoxExtent.Y+75);
                else Point+=(Tooth->GetActorLocation()-Point).GetSafeNormal2D()*90;
                const float D=FVector::DistSquared2D(Point,Tooth->GetActorLocation());
                if (D<BestDistance) { Best=*It; BestDistance=D; Goal=Point; bBrush=bNeedsCare && Status->NeedsCare(true); }
            }
            if (Best)
            {
                Tooth->AddMovementInput((BestDistance>FMath::Square(20.f)?Goal-Tooth->GetActorLocation():Best->GetActorLocation()-Tooth->GetActorLocation()).GetSafeNormal2D(),BestDistance>FMath::Square(20.f)?1.f:.03f);
                if (bBrush) { if (!Tooth->bBrushing) Tooth->StartBrush(); }
                else if (!Tooth->bHandling) Tooth->StartHandle();
            }
            else { if (Tooth->bBrushing) Tooth->StopBrush(); if (Tooth->bHandling) Tooth->StopHandle(); }
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
        if (bArena)
        {
            bSuccess &= bArenaInitial && bArenaDamaged && bArenaLost;
            UE_LOG(LogTemp,Display,TEXT("MC_ARENA_NET initial=%d damaged=%d lost=%d"),bArenaInitial,bArenaDamaged,bArenaLost);
        }
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s"),bSuccess?TEXT("PASS"):TEXT("FAIL"));
        FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
    }
#endif
}

void UMCValidationSubsystem::TickLimbStability(float Dt)
{
#if !UE_BUILD_SHIPPING
    if (Age==0) NextLog=0;
    Age+=Dt;
    if (auto* GS=GetWorld()->GetGameState<AMCGameState>(); GS && GS->HasAuthority()) GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+999;
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Tooth=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    if (!Tooth)
    {
        if (Age>5) { UE_LOG(LogTemp,Error,TEXT("MC_LIMBS_FAIL no pawn")); FPlatformMisc::RequestExitWithStatus(false,1); }
        return;
    }
    if (Age>3 && Age<7) Tooth->AddMovementInput(FVector(FMath::Cos(Age*3),FMath::Sin(Age*3),0));
    if (Age>7 && Age<10) { if (!Tooth->bBrushing) Tooth->StartBrush(); }
    else if (Tooth->bBrushing) Tooth->StopBrush();
    if (Age>11 && !bLimbKnockedDown)
    {
        Tooth->ToothPhysics->ApplyHit(FVector(350,0,250),Tooth->GetActorLocation()); bLimbKnockedDown=true;
    }
    Tooth->bPreviewAnimation=Age>20 && Age<28;
    const bool bIdle=(Age>1.5f && Age<3) || (Age>17 && Age<20) || Age>30;
    if (Tooth->ToothPhysics->CanAct() && Age>1)
    {
        auto* Mesh=Tooth->GetMesh(); const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
        bool bBad=false;
        for (const FName Name:{FName("arm_l"),FName("hand_l"),FName("arm_r"),FName("hand_r")})
        {
            const int32 I=Ref.FindBoneIndex(Tooth->RigBone(Name)), Parent=Ref.GetParentIndex(I);
            const FQuat Local=Mesh->GetBoneQuaternion(Ref.GetBoneName(Parent)).Inverse()*Mesh->GetBoneQuaternion(Tooth->RigBone(Name));
            const float Angle=FMath::RadiansToDegrees(Local.AngularDistance(Ref.GetRefBonePose()[I].GetRotation()));
            MaxLimbAngle=FMath::Max(MaxLimbAngle,Angle); bBad |= !FMath::IsFinite(Angle) || Angle>135;
            if (const FQuat* Last=LastLimbRotations.Find(Name))
                if (bIdle && Dt>SMALL_NUMBER) MaxIdleSpin=FMath::Max(MaxIdleSpin,static_cast<float>(FMath::RadiansToDegrees(Local.AngularDistance(*Last))/Dt));
            LastLimbRotations.Add(Name,Local);
            if (bIdle && !bLimbKnockedDown) NeutralLimbRotations.Add(Name,Local);
            else if (bIdle)
                if (const FQuat* Neutral=NeutralLimbRotations.Find(Name))
                    MaxIdleDeviation=FMath::Max(MaxIdleDeviation,static_cast<float>(FMath::RadiansToDegrees(Local.AngularDistance(*Neutral))));
        }
        if (bBad) BadLimbSeconds+=Dt;
    }
    else LastLimbRotations.Reset();
    if (Age>NextLog)
    {
        NextLog+=2;
        UE_LOG(LogTemp,Display,TEXT("MC_LIMBS time=%.2f angle=%.2f idle_spin=%.2f idle_deviation=%.2f bad_seconds=%.3f falls=%d recoveries=%d"),Age,MaxLimbAngle,MaxIdleSpin,MaxIdleDeviation,BadLimbSeconds,Tooth->ToothPhysics->KnockdownCount,Tooth->ToothPhysics->RecoveryCount);
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCLimbsCapture")) && Age>LimbSnapshotAt)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("LimbFrames/Frame%02d.png"),FMath::FloorToInt(LimbSnapshotAt*2)),true,false);
        LimbSnapshotAt+=0.5f;
    }
    if (Age>33)
    {
        const bool bSuccess=BadLimbSeconds<0.15f && MaxIdleSpin<120 && MaxIdleDeviation<35 && Tooth->ToothPhysics->RecoveryCount>0 && Tooth->ToothPhysics->CanAct();
        UE_LOG(LogTemp,Display,TEXT("MC_LIMBS_%s angle=%.2f idle_spin=%.2f idle_deviation=%.2f bad_seconds=%.3f"),bSuccess?TEXT("PASS"):TEXT("FAIL"),MaxLimbAngle,MaxIdleSpin,MaxIdleDeviation,BadLimbSeconds);
        FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
    }
#endif
}
