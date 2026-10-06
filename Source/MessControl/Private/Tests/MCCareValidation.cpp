#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCDayDirector.h"
#include "MCGameState.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Opt-in fixture. Exercises real artist teeth, authoritative brushing, two workers,
// a late-joining fourth process, and the final persistent mask on every client.
void MCTickCareValidation(UWorld* World)
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCArenaTooth> Tooth;
        TWeakObjectPtr<AMCMouthSurface> Ulcer;
        TWeakObjectPtr<ACameraActor> Camera;
        TWeakObjectPtr<AMCToothCharacter> Workers[2];
        int32 Shots=0,Checks=0; bool Setup=false,Initial=false,Partial=false,Complete=false;
        float Age=0;
    };
    static FRun Run;
    if (Run.World.Get()!=World) { Run=FRun(); Run.World=World; }
    Run.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>(); auto* PC=World->GetFirstPlayerController();
    if (!GS || !PC || GS->ArenaTeeth.Num()<3) return;
    const bool Host=World->GetNetMode()!=NM_Client;
    const bool Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCCareCapture"));
    const FString Folder=FPaths::ProjectDir()/TEXT("Artifacts/Care");
    if (Run.Age>180) { UE_LOG(LogTemp,Error,TEXT("MC_CARE_FAIL timeout")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    if (Host && !Run.Setup)
    {
        // Let the real day start first. Forcing Working before its director exists
        // makes GameMode finish the empty day and later replace our test state.
        AMCDayDirector* Director=nullptr;
        for (TActorIterator<AMCDayDirector> It(World);It;++It) { Director=*It; break; }
        if (!Director) return;
        GS->bDevManualEvents=true; GS->bPhysicalBrushes=false; GS->PhaseEndsAt=0; GS->Phase=EMCShiftPhase::Working;
        GS->DayStartedAt=GS->GetServerWorldTimeSeconds()+100000; // Clients wait through asset/shader warmup.
        Director->SetActorTickEnabled(false);
        TArray<AMCToothCharacter*> Players;
        for (TActorIterator<AMCToothCharacter> It(World);It;++It) if (It->GetPlayerState()) Players.Add(*It);
        if (Players.Num()<3 || Run.Age<8) return;
#if WITH_EDITOR
        if (Capture && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
        Players.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
        for (AMCArenaTooth* T:GS->ArenaTeeth) if (T) T->SetCoffee(0);
        for (TActorIterator<AMCFoodActor> It(World);It;++It) It->Destroy();
        for (TActorIterator<AMCMouthSurface> It(World);It;++It) It->Destroy();
        AMCTongue* Tongue=nullptr; for (TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It; break; }
        Run.Tooth=GS->ArenaTeeth[2]; Run.Tooth->SetCoffee(1);
        const FVector P=Run.Tooth->GetActorLocation(); const auto Bounds=Run.Tooth->Visual->Bounds;
        for (int32 I=0;I<2;++I)
        {
            auto* Hero=Players[I]; Run.Workers[I]=Hero; Hero->Status->Initialize(100);
            FVector At(P.X+(I==0?-38:38),P.Y+Bounds.BoxExtent.Y+42,50);
            FHitResult Floor; if (Tongue && Tongue->SurfacePoint(At,Floor)) At.Z=Floor.ImpactPoint.Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3;
            Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->DisableMovement();
            Hero->SetActorLocationAndRotation(At,FRotator(0,-90,0),false,nullptr,ETeleportType::TeleportPhysics);
            Hero->Inventory->ServerSelect(EMCToolSlot::Brush); Hero->ForceNetUpdate();
        }
        FVector UlcerPoint=P+FVector(130,390,0); FHitResult Floor;
        if (Tongue && Tongue->SurfacePoint(UlcerPoint,Floor)) UlcerPoint=Floor.ImpactPoint+Floor.ImpactNormal*5;
        const FTransform Transform(UlcerPoint);
        auto* Ulcer=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Transform);
        Ulcer->bUlcer=true; Ulcer->HealSeconds=1000; Ulcer->FinishSpawning(Transform); Run.Ulcer=Ulcer;
        if (Capture)
        {
            IFileManager::Get().MakeDirectory(*Folder,true);
            auto* Camera=World->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(55);
            Camera->SetActorLocationAndRotation(P+FVector(-340,380,580),(P+FVector(0,40,30)-(P+FVector(-340,380,580))).Rotation());
            PC->SetViewTarget(Camera); Run.Camera=Camera;
        }
        GS->DayStartedAt=GS->GetServerWorldTimeSeconds(); GS->ForceNetUpdate(); Run.Setup=true;
    }
    if (!GS->bDevManualEvents) return;
    auto* Tooth=GS->ArenaTeeth[2].Get(); if (!Tooth) return;
    const float T=GS->GetServerWorldTimeSeconds()-GS->DayStartedAt;
    int32 Changed=0; for (uint8 V:Tooth->GrimeMask) if (V<255) ++Changed;
    Run.Initial|=Tooth->Status->State.CoffeeLeft==4;
    Run.Partial|=Changed>0 && Tooth->Status->State.CoffeeLeft>0;
    Run.Complete|=Changed>0 && Tooth->Status->State.CoffeeLeft==0;
    if (Host && Run.Setup)
    {
        for (auto Worker:Run.Workers) if (auto* H=Worker.Get())
        {
            const bool Brush=(T>3 && T<3.65f) || (T>7 && T<9.5f);
            if (H->bBrushing!=Brush) { H->bBrushing=Brush; H->ForceNetUpdate(); }
        }
        if (T>4 && !(Run.Checks&1))
        {
            for (auto Worker:Run.Workers) if (auto* H=Worker.Get())
                UE_LOG(LogTemp,Display,TEXT("MC_CARE_CONTACT worker=%s tool=%d contact=%d work=%d left=%d cells=%d"),*H->GetName(),H->HasBrush(),H->CanContact(Tooth),H->CanWork(),Tooth->Status->State.CoffeeLeft,Changed);
            Run.Checks|=1;
        }
        auto Shot=[&](int32 Id,float At,const TCHAR* Name)
        {
            if (Capture && T>At && !(Run.Shots&(1<<Id)))
            { FScreenshotRequest::RequestScreenshot(Folder/Name,false,false); Run.Shots|=1<<Id; }
        };
        Shot(0,2,TEXT("01_Dirty.png")); Shot(1,3.55f,TEXT("02_Brushing.png")); Shot(2,5,TEXT("03_LocalTrail.png")); Shot(3,10.5f,TEXT("04_Clean.png"));
        if (T>12 && Run.Camera.IsValid() && Run.Ulcer.IsValid())
        {
            const FVector P=Run.Ulcer->GetActorLocation(); const FVector Camera=P+FVector(-120,225,260);
            Run.Camera->SetActorLocationAndRotation(Camera,(P-Camera).Rotation());
        }
        if (Run.Ulcer.IsValid()) Run.Ulcer->Healing=T>15?.72f:.05f;
        Shot(4,13.5f,TEXT("05_Ulcer.png")); Shot(5,17,TEXT("06_Healing.png"));
    }
    if (T>(Host?29:26) && (Run.Setup || !Host))
    {
        const bool Pass=Run.Complete && Changed>0 && (!Host || (Run.Initial && Run.Partial));
        const uint32 Hash=FCrc::MemCrc32(Tooth->GrimeMask.GetData(),Tooth->GrimeMask.Num());
        UE_LOG(LogTemp,Display,TEXT("MC_CARE_%s net=%d initial=%d partial=%d complete=%d cells=%d hash=%u"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),Run.Initial,Run.Partial,Run.Complete,Changed,Hash);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
}
#endif
