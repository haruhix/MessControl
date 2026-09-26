#include "MCDayOneScenario.h"
#include "MCDayDirector.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCCoffeeFlood.h"
#include "MCArenaTooth.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
AMCDayOneScenario::AMCDayOneScenario() { bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true; }
void AMCDayOneScenario::Move(int32 Slot,FVector P,FRotator R)
{
    if (!Heroes.IsValidIndex(Slot) || !Heroes[Slot]) return;
    auto* H=Heroes[Slot].Get(); H->GetCharacterMovement()->StopMovementImmediately(); H->SetActorLocationAndRotation(P,R,false,nullptr,ETeleportType::TeleportPhysics); H->ForceNetUpdate();
}
void AMCDayOneScenario::Next()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); ++Stage; StageAt=GS->GetServerWorldTimeSeconds();
    GS->StepIndex=Stage<=2?0:Stage==3?1:Stage==6?4:3;
    GS->PhaseEndsAt=Stage==6?StageAt+10:Stage<=3?0:StageAt+20;
    if (Stage==1)
    {
        GS->Day=1; GS->bPhysicalBrushes=true; GS->DayPlan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
        GS->DayStartedAt=StageAt;
        GS->Phase=EMCShiftPhase::Working;
        // Suspend the legacy random-day scheduler while the bounded scenario drives real actions.
        GetWorld()->GetAuthGameMode<AMCGameMode>()->DayDirector=GetWorld()->SpawnActor<AMCDayDirector>();
        bool HasBin=false;
        for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It) HasBin|=It->bBrushBin;
        if (!HasBin) { auto* Bin=GetWorld()->SpawnActor<AMCFoodDisposal>(FVector(-1110,0,140),FRotator::ZeroRotator); Bin->bBrushBin=true; }
        for (int32 I=0;I<4;++I)
        {
            Move(I,FVector(-600+I*220,100,95)); const FTransform T(FVector(-520+I*220,100,280));
            auto* Brush=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T); Brush->ConfigureBrush(); UGameplayStatics::FinishSpawningActor(Brush,T);
        }
    }
    if (Stage==2)
    {
        for (int32 I=0;I<4;++I)
        {
            FVector P(-600+I*230,-200,350); FHitResult Hit;
            if (GetWorld()->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,600),ECC_WorldStatic)) P=Hit.ImpactPoint+FVector(0,0,5);
            auto* Patch=GetWorld()->SpawnActor<AMCMouthSurface>(P,FRotator::ZeroRotator); Patch->Status->ApplyCoffee(); Patches.Add(Patch); Move(I,P+FVector(-65,0,90));
        }
    }
    if (Stage==3) for (int32 I=0;I<4;++I) Move(I,FVector(-975,-300+I*200,95),FRotator(0,180,0));
    if (Stage==4)
    {
        for (int32 I=0;I<4;++I) Move(I,FVector(-100+I*180,400,95));
        Move(0,FVector(-530,-200,95));
        auto* Table=GS->DayPlan->Menu.LoadSynchronous(); const auto* Row=Table->FindRow<FMCFoodRow>(TEXT("Broccoli"),TEXT("Scenario"));
        const FTransform T(FVector(-450,-200,60)); Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
        FRandomStream Random(22); Food->ConfigureItem(TEXT("Broccoli"),*Row,Random); Food->SpoilAt=GS->GetServerWorldTimeSeconds()+10; Food->Batch=44; UGameplayStatics::FinishSpawningActor(Food,T);
    }
    if (Stage==5)
    {
        Move(0,FVector(-700,400,95));
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (It->bFragment) It->SpoilAt=GS->GetServerWorldTimeSeconds()+.5;
    }
    if (Stage==6)
    {
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->IsDisposed()) It->Dispose();
        if (GS->ArenaTeeth.IsEmpty() || !IsValid(GS->ArenaTeeth[0])) { bFailed=true; return; }
        auto* Tooth=GS->ArenaTeeth[0].Get(); FVector P=Tooth->GetActorLocation(); const float Side=FMath::Sign(P.Y);
        P.Y-=Side*(Tooth->Body->Bounds.BoxExtent.Y+65); P.Z=95;
        Move(0,P,FRotator(0,Side*90,0)); Move(1,FVector(-300,0,95)); Move(2,FVector(0,300,95)); Move(3,FVector(300,-200,95));
        Flood=GetWorld()->SpawnActor<AMCCoffeeFlood>(); Flood->Start(GS->DayPlan);
    }
    UE_LOG(LogTemp,Display,TEXT("MC_DAY1_NET_STAGE %d"),Stage); ForceNetUpdate();
}
void AMCDayOneScenario::Tick(float Dt)
{
#if !UE_BUILD_SHIPPING
    Super::Tick(Dt); Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if (!GS) return;
    const double Now=GS->GetServerWorldTimeSeconds(),Elapsed=Now-StageAt;
    bool Ready=true;
    const bool Capture=HasAuthority() && FParse::Param(FCommandLine::Get(),TEXT("MCDayOneCapture"));
#if WITH_EDITOR
    Ready=!Capture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    if (HasAuthority())
    {
        if (Stage==0)
        {
            GS->PhaseEndsAt=Now+999;
            if (GS->PlayerArray.Num()==4 && Age>8 && Ready)
            { for (auto PS:GS->PlayerArray) if (auto* H=Cast<AMCToothCharacter>(PS->GetPawn())) Heroes.Add(H); if (Heroes.Num()==4) Next(); }
        }
        if (Stage==1 && Elapsed>3) { bool All=true; for (auto H:Heroes) All &= H->EquippedBrush!=nullptr; if (All) Next(); }
        else if (Stage==2 && Elapsed>4) { bool All=true; for (auto P:Patches) All &= P->IsClean(); if (All) Next(); }
        else if (Stage==3 && Elapsed>4)
        { int32 Left=0; for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (It->bBrushTool && !It->IsDisposed()) ++Left; if (!Left) Next(); }
        else if (Stage==4)
        {
            if (!Food->IsDisposed() && Heroes[0]->CanWork()) Move(0,Food->GetActorLocation()+FVector(-80,0,40));
            if (Food->IsDisposed() && Elapsed>4) Next();
        }
        else if (Stage==5)
        {
            bool Ulcer=false; for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) Ulcer|=It->bUlcer;
            if (Ulcer && !bObservedUlcer)
            {
                bObservedUlcer=true;
                for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->IsDisposed()) It->Dispose();
            }
            // Spoiled food now launches a real pain wave. Finish that physical reaction
            // before testing the separate coffee knockdown; recovery immunity is intentional.
            bool ReadyForCoffee=bObservedUlcer;
            for (auto Hero:Heroes) ReadyForCoffee &= Hero && Hero->ToothPhysics->CanAct();
            for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
                ReadyForCoffee &= !It->IsMotionActive();
            if (ReadyForCoffee) { if (RecoveryReadyAt<0) RecoveryReadyAt=Now; }
            else RecoveryReadyAt=-1;
            if (RecoveryReadyAt>=0 && Now-RecoveryReadyAt>1 && Elapsed>4) Next();
        }
        else if (Stage==6 && Elapsed>11 && Flood && !Flood->IsActive()) Next();
        GS->TasksLeft=0;
        if (Stage==1) for (auto Hero:Heroes) GS->TasksLeft+=Hero && !Hero->EquippedBrush;
        if (Stage==2) for (auto Patch:Patches) GS->TasksLeft+=Patch && !Patch->IsClean();
        if (Stage==3 || Stage==4) for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
            if (!It->IsDisposed() && (Stage==3?It->bBrushTool:!It->bBrushTool && It->Batch==44)) ++GS->TasksLeft;
        if (Stage==6 && Flood) GS->TasksLeft=FMath::Max(0,Flood->Waves-Flood->Wave);
        if (Stage>0 && Stage<7 && Elapsed>25) { bFailed=true; Stage=7; StageAt=Now; ForceNetUpdate(); }
    }
    auto* PC=GetWorld()->GetFirstPlayerController(); auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    if (H && H->IsLocallyControlled())
    {
        const int32 Slot=Heroes.IndexOfByKey(H);
        if (LocalStage!=Stage)
        {
            LocalStage=Stage; H->StopBrush(); H->StopHandle();
            if (Stage==1) H->StartHandle();
            if (Stage==2) H->StartBrush();
            if (Stage==3) H->ThrowItem();
            if (Stage==6 && Slot==0) H->StartHandle();
        }
        if (Stage==4 && Slot==0 && Age>NextSwing) { H->SwingBrush(); NextSwing=Age+1; }
        if (Stage==6 && Slot==1 && H->bInCoffee) { H->LocalPaddle=FVector2D(-1,0); H->ServerPaddle(H->LocalPaddle); }
    }
    if (Stage==1) for (auto Hero:Heroes) if (Hero && Hero->EquippedBrush) Seen|=1;
    if (Stage==2) for (auto Patch:Patches) if (Patch && Patch->IsClean()) Seen|=2;
    if (Stage>=4) { bool Any=false; for (auto Hero:Heroes) if (Hero && Hero->EquippedBrush) Any=true; if (!Any) Seen|=4; }
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (It->bFragment && It->ItemMesh) Seen|=8;
    for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (It->bUlcer && GS->MouthHealth<100) Seen|=16;
    if (Stage==6 && Heroes.Num()==4)
    {
        if (Heroes[0] && Heroes[0]->ClingTooth) Seen|=32;
        if (Heroes[1] && Heroes[1]->bInCoffee && Heroes[1]->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) Seen|=64;
        if (Flood && Flood->GetPhase()==EMCCoffeePhase::Draining) Seen|=128;
    }
    if (Capture && PC)
    {
        if (!Camera) { Camera=GetWorld()->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(55); PC->SetViewTarget(Camera); IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("DayOneFrames")),true); }
        FVector Aim(0,0,80),P(-2400,0,1000);
        if (Stage==4 || Stage==5) { Aim=FVector(-400,-150,60); P=Aim+FVector(-800,560,500); }
        Camera->SetActorLocationAndRotation(P,(Aim-P).Rotation());
        if (Stage>0 && Stage<7 && Now>=NextCapture)
        { NextCapture=Now+.12; if (LastCapture>=0) CaptureTiming+=FString::Printf(TEXT("duration %.6f\n"),Now-LastCapture); CaptureTiming+=FString::Printf(TEXT("file 'Frame%05d.png'\n"),CaptureFrame); FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("DayOneFrames/Frame%05d.png"),CaptureFrame++),true,false); LastCapture=Now; }
    }
    if (Age>LogAt) { LogAt=Age+3; UE_LOG(LogTemp,Display,TEXT("MC_DAY1_NET net=%d stage=%d seen=%u failed=%d brush=%d work=%d contact=%.2f target=%s"),int32(GetNetMode()),Stage,Seen,bFailed,H&&H->HasBrush(),H&&H->bBrushing,H?H->ContactProgress:0,H?*GetNameSafe(H->CareTarget):TEXT("none")); }
    if ((Stage==7 && Now-StageAt>(HasAuthority()?5:2)) || Age>140)
    {
        if (Capture && CaptureFrame>0) FFileHelper::SaveStringToFile(CaptureTiming+FString::Printf(TEXT("duration 0.12\nfile 'Frame%05d.png'\n"),CaptureFrame-1),*(FPaths::ProjectSavedDir()/TEXT("DayOneFrames/Timing.txt")));
        const bool Pass=Stage==7 && !bFailed && Seen==255;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s dayone seen=%u failed=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Seen,bFailed);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}
void AMCDayOneScenario::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCDayOneScenario,Stage); DOREPLIFETIME(AMCDayOneScenario,StageAt);
    DOREPLIFETIME(AMCDayOneScenario,Heroes); DOREPLIFETIME(AMCDayOneScenario,Patches); DOREPLIFETIME(AMCDayOneScenario,Food); DOREPLIFETIME(AMCDayOneScenario,Flood); DOREPLIFETIME(AMCDayOneScenario,bFailed);
}
