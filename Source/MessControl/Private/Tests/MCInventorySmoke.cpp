#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "MCFoodActor.h"
#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace {
void TickInventoryNetwork(UWorld* World)
{
    struct FRun { TWeakObjectPtr<UWorld> World; float Age=0; int32 Stage=-1,Checked=-1,Seen=0; bool Setup=false,Sprayed=false,Failed=false,Sink=false,Absorbed=false,Burned=false; };
    static FRun R; if(R.World!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>(); auto* PC=World->GetFirstPlayerController();
    auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    if(!GS || !H) return;
    const bool Host=World->GetNetMode()!=NM_Client;
    if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
    const double Now=GS->GetServerWorldTimeSeconds();
    if(Host && !R.Setup && R.Age>3 && GS->PlayerArray.Num()==2) {
        int32 I=0;
        for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(It->GetPlayerState()) {
            FVector P(-300,-320+I++*640,100);
            for(TActorIterator<AMCTongue> Tongue(World);Tongue;++Tongue) { FHitResult Hit; if(Tongue->SurfacePoint(P,Hit)) P.Z=Hit.ImpactPoint.Z+60; break; }
            It->SetActorLocationAndRotation(P,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            It->GetCharacterMovement()->StopMovementImmediately(); It->GetCharacterMovement()->DisableMovement();
            FVector U=P+FVector(125,0,-60);
            for(TActorIterator<AMCTongue> Tongue(World);Tongue;++Tongue) { FHitResult Hit; if(Tongue->SurfacePoint(U,Hit)) U=Hit.ImpactPoint+Hit.ImpactNormal*5; break; }
            auto* Patch=World->SpawnActor<AMCMouthSurface>(U,FRotator::ZeroRotator); Patch->bUlcer=true; Patch->HealSeconds=7;
            Patch->PulseInterval=60; // Keep the inventory routing fixture free of knockdowns from the separate wave mechanic.
        }
        GS->bPhysicalBrushes=false; GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working;
        auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        if(const auto* Row=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Absorption network")):nullptr) {
            FVector P(400,0,70);
            for(TActorIterator<AMCTongue> It(World);It;++It) { FHitResult Hit; if(It->SurfacePoint(P,Hit)) P=Hit.ImpactPoint+FVector(0,0,70); break; }
            const FTransform Transform(P); auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
            FMCFoodRow Config=*Row; Config.SpoilSeconds=3; FRandomStream Random(41);
            Food->ConfigureItem(TEXT("Egg"),Config,Random); Food->Batch=876; Food->FinishSpawning(Transform);
        }
        GS->TasksTotal=4321; GS->StepIndex=0; GS->StepStartedAt=Now; GS->ForceNetUpdate(); R.Setup=true;
    }
    if(GS->TasksTotal!=4321) {
        if(R.Age>45) { UE_LOG(LogTemp,Error,TEXT("MC_INVENTORY_NET_FAIL setup timeout")); FPlatformMisc::RequestExitWithStatus(false,1); }
        return;
    }
    for(TActorIterator<AMCFoodActor> It(World);It;++It) if(It->Batch==876) {
        R.Sink|=It->bSpoiled && !It->bAbsorbed && !It->IsDisposed();
        if(Host && It->bSpoiled && !R.Burned) {AMCFirePatch::Ignite(H,It->GetActorLocation()-FVector(0,0,It->Body->Bounds.BoxExtent.Z),60,2,876);R.Burned=true;}
    }
    for(TActorIterator<AMCMouthSurface> It(World);It;++It) R.Absorbed|=It->bUlcer && It->Batch==876;
    if(Host && Now-GS->StepStartedAt>3.2) { ++GS->StepIndex; GS->StepStartedAt=Now; GS->ForceNetUpdate(); }
    if(GS->StepIndex>=4) {
        // Give the final state time to reach the client before closing the listen server.
        if(!Host || Now-GS->StepStartedAt>2) {
            const bool Pass=!R.Failed && R.Seen==15 && R.Sink && R.Absorbed;
            UE_LOG(LogTemp,Display,TEXT("MC_INVENTORY_NET_%s net=%d stages=%d spoiled_without_absorption=%d damage_ulcer=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),R.Seen,R.Sink,R.Absorbed);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
        return;
    }
    const EMCToolSlot Slots[]={EMCToolSlot::Pickaxe,EMCToolSlot::Knife,EMCToolSlot::Spray,EMCToolSlot::Brush};
    const int32 Stage=GS->StepIndex;
    if(R.Stage!=Stage) { R.Stage=Stage; R.Sprayed=false; H->Inventory->ServerSelect(Slots[Stage]); }
    if(Stage==2 && !R.Sprayed && H->Inventory->Selected==EMCToolSlot::Spray) { H->ServerSetPrimary(true); R.Sprayed=true; }
    if(R.Checked!=Stage && Now-GS->StepStartedAt>2) {
        int32 Players=0,Numb=0; bool OK=true;
        for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(It->GetPlayerState()) {
            ++Players; OK&=It->Inventory->Selected==Slots[Stage];
            if(Stage==2) OK&=It->Inventory->HealingTarget && It->Inventory->HealingTarget->Healing>0;
        }
        if(Stage==2) { for(TActorIterator<AMCMouthSurface> It(World);It;++It) if(It->IsNumb()) ++Numb; OK&=Numb==2; }
        OK&=Players==2; R.Failed|=!OK; if(OK) R.Seen|=1<<Stage; R.Checked=Stage;
        UE_LOG(LogTemp,Display,TEXT("MC_INVENTORY_NET_CHECK %s stage=%d players=%d numb=%d"),OK?TEXT("PASS"):TEXT("FAIL"),Stage,Players,Numb);
    }
}
}

void MCTickInventoryValidation(UWorld* World)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("MCInventoryNetworkTest"))) { TickInventoryNetwork(World); return; }
    struct FRun { TWeakObjectPtr<UWorld> World; float Age=0,StageAt=0; int32 Stage=-1; bool Shot=false,Failed=false; TWeakObjectPtr<AMCFoodActor> Food; TWeakObjectPtr<AMCMouthSurface> Ulcer; FVector HandStart; float HandTravel=0; };
    static FRun R; if(R.World!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* PC=World->GetFirstPlayerController(); auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr; auto* GS=World->GetGameState<AMCGameState>();
    if(!H || !GS || R.Age<3) return;
    if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    auto Check=[&](bool OK,const TCHAR* What) { UE_LOG(LogTemp,Display,TEXT("MC_INVENTORY_CHECK %s %s"),OK?TEXT("PASS"):TEXT("FAIL"),What); R.Failed|=!OK; };
    GS->bDevManualEvents=false; GS->bPhysicalBrushes=false; GS->Phase=EMCShiftPhase::Working;
    const float Age=R.Age-R.StageAt;
    if(R.Stage<0 || Age>2.6f) {
        if(R.Stage==1 || R.Stage==2) Check(R.Food.IsValid() && R.Food->Health<100,TEXT("tool damages matching food"));
        if(R.Stage==1) { Check(R.HandTravel>60,TEXT("pickaxe wrist travels through a broad arc")); UE_LOG(LogTemp,Display,TEXT("MC_INVENTORY_PICK_ARC %.1f cm"),R.HandTravel); }
        if(R.Stage==3) Check(R.Ulcer.IsValid() && R.Ulcer->IsNumb() && R.Ulcer->Healing>0,TEXT("held spray protects and treats ulcer"));
        ++R.Stage; R.StageAt=R.Age; R.Shot=false;
        if(R.Stage>=7) { UE_LOG(LogTemp,Display,TEXT("MC_INVENTORY_%s"),R.Failed?TEXT("FAIL"):TEXT("PASS")); FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0); return; }
        H->ServerSetPrimary(false); H->GetCharacterMovement()->DisableMovement();
        if(R.Food.IsValid()) R.Food->Destroy();
        if(R.Stage==0) {
            GS->DayPlan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01")); GS->StepIndex=0;
            GS->TasksTotal=16; GS->TasksLeft=9; GS->MouthHealth=76; GS->StepStartedAt=GS->GetServerWorldTimeSeconds(); GS->PhaseEndsAt=0;
            FVector P(-430,0,160); for(TActorIterator<AMCTongue> It(World);It;++It) { FHitResult Hit; if(It->SurfacePoint(P,Hit)) P.Z=Hit.ImpactPoint.Z+60; break; }
            H->SetActorLocationAndRotation(P,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            for(int32 I=0;I<3;++I) {
                auto* C=World->SpawnActor<APlayerController>();
                auto* Pawn=World->SpawnActor<AMCToothCharacter>(P+FVector(200+I*120,-210+I*170,0),FRotator::ZeroRotator);
                if(C && Pawn) { C->Possess(Pawn); if(C->PlayerState) C->PlayerState->SetPlayerId(50+I); }
            }
        }
        if(R.Stage==1 || R.Stage==2) {
            GS->StepIndex=3; GS->StepStartedAt=GS->GetServerWorldTimeSeconds()-8; GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+22;
            H->Inventory->ServerSelect(R.Stage==1?EMCToolSlot::Pickaxe:EMCToolSlot::Knife);
            const FVector Position=H->GetActorLocation()+FVector(105,0,-10);
            auto* Food=World->SpawnActor<AMCFoodActor>(Position,FRotator::ZeroRotator); R.Food=Food;
            Food->Body->SetSimulatePhysics(false); Food->Health=100; Food->Phase=EMCFoodPhase::Free;
            Food->FoodData.Resistance=R.Stage==1?EMCFoodResistance::Hard:EMCFoodResistance::Soft;
            R.HandStart=H->GetMesh()->GetSocketLocation(H->RigBone(TEXT("hand_r"))); R.HandTravel=0;
            H->SwingBrush();
        }
        if(R.Stage==3) {
            H->Inventory->ServerSelect(EMCToolSlot::Spray);
            FVector P=H->GetActorLocation()+FVector(140,0,-60);
            for(TActorIterator<AMCTongue> It(World);It;++It) { FHitResult Hit; if(It->SurfacePoint(P,Hit)) P=Hit.ImpactPoint+Hit.ImpactNormal*5; break; }
            auto* Ulcer=World->SpawnActor<AMCMouthSurface>(P,FRotator::ZeroRotator); Ulcer->bUlcer=true; R.Ulcer=Ulcer;
            H->ServerSetPrimary(true);
        }
        if(R.Stage==4) H->NotifyTaskFeedback(true);
        if(R.Stage==5) H->NotifyTaskFeedback(false);
        if(R.Stage==6) { H->Status->Damage(10000); H->RespawnAt=GS->GetServerWorldTimeSeconds()+20; }
    }
    const float Since=R.Age-R.StageAt;
    if(R.Stage==1 && Since<1.05f) R.HandTravel=FMath::Max(R.HandTravel,float(FVector::Distance(R.HandStart,H->GetMesh()->GetSocketLocation(H->RigBone(TEXT("hand_r"))))));
    const float CaptureAt=R.Stage==1?.30f:R.Stage==2?.22f:1.1f;
    if(!R.Shot && Since>=CaptureAt) {
        const FString Folder=FPaths::ProjectDir()/TEXT("Artifacts/Inventory"); IFileManager::Get().MakeDirectory(*Folder,true);
        FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("Stage%02d.png"),R.Stage),true,false);
        UE_LOG(LogTemp,Display,TEXT("MC_INVENTORY_VIEW stage=%d hero=%s camera=%s aim=%s ulcer=%s numb=%d cooldown=%.2f"),R.Stage,*H->GetActorLocation().ToString(),*H->Camera->GetComponentLocation().ToString(),*H->Camera->GetComponentRotation().ToString(),R.Ulcer.IsValid()?*R.Ulcer->GetActorLocation().ToString():TEXT("none"),R.Ulcer.IsValid() && R.Ulcer->IsNumb(),H->Inventory->SpraySecondsLeft());
        R.Shot=true;
    }
}
#endif
