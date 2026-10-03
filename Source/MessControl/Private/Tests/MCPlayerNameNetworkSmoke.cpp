#if !UE_BUILD_SHIPPING
#include "MCPlayerNameComponent.h"
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Real listen-server/client rendering, replicated Unicode names, a rename and
// a replacement pawn. Inspect the actual text widget, including the old corpse.
void MCTickPlayerNameValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCToothCharacter> OldPawn;
        TWeakObjectPtr<ACameraActor> Camera;
        float Age=0;
        bool Setup=false,Renamed=false,Respawned=false,Captured=false;
        uint8 InitialSeen=0,RenameSeen=0,RespawnSeen=0;
        int32 Samples=0;
    };
    static FRun R;if(R.World.Get()!=World) {R=FRun();R.World=World;}
    R.Age+=World->GetDeltaSeconds();
    const bool Host=World->GetNetMode()!=NM_Client;
    auto* GS=World->GetGameState<AMCGameState>();
    auto* PC=World->GetFirstPlayerController();
    if(!GS || !PC) {if(R.Age>60) FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const double Now=GS->GetServerWorldTimeSeconds();
    const double T=GS->TasksTotal==8719?Now-GS->DayStartedAt:-1;
    auto Finish=[&]() {
        const bool CorpseHidden=!Host || (R.OldPawn.IsValid() && !R.OldPawn->PlayerNameLabel->IsVisible());
        const bool Pass=R.InitialSeen==3 && R.RenameSeen==3 && R.RespawnSeen==3 && CorpseHidden && R.Samples>100;
        UE_LOG(LogTemp,Display,TEXT("MC_NAMES_%s net=%d initial=%d renamed=%d respawn=%d corpse_hidden=%d samples=%d"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),R.InitialSeen,R.RenameSeen,R.RespawnSeen,CorpseHidden,R.Samples);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(T>(Host?13:12) || R.Age>60) {Finish();return;}
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It)
        if(It->GetPlayerState() && It->GetPlayerState()->GetPawn()==*It) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    if(Heroes.Num()!=2) return;
    if(Host && !R.Setup && R.Age>3) {
        for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            if(!It->Get()->GetPawn() || (!It->Get()->IsLocalController() && It->Get()->AcknowledgedPawn!=It->Get()->GetPawn())) return;
        auto* Floor=World->SpawnActor<AStaticMeshActor>();
        Floor->SetMobility(EComponentMobility::Movable);
        Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Floor->GetStaticMeshComponent()->SetIsReplicated(true);
        Floor->SetReplicateMovement(true);
        Floor->SetActorLocation(FVector(-300,0,10000));Floor->SetActorScale3D(FVector(10,12,.2f));Floor->SetReplicates(true);
        if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        for(int32 I=0;I<2;++I) {
            Heroes[I]->CancelGameplayInput();Heroes[I]->GetCharacterMovement()->StopMovementImmediately();
            Heroes[I]->SetActorLocation(FVector(-300,-150+I*300,10072),false,nullptr,ETeleportType::TeleportPhysics);
            Heroes[I]->GetPlayerState()->SetPlayerName(I==0?TEXT("Host_Alpha"):TEXT("Друг_42"));Heroes[I]->ForceNetUpdate();
        }
        GS->bDevManualEvents=true;GS->Phase=EMCShiftPhase::Intermission;GS->PhaseEndsAt=0;
        GS->TasksTotal=8719;GS->DayStartedAt=Now+1;GS->ForceNetUpdate();R.Setup=true;return;
    }
    if(T<0) return;
    if(Host && T>3 && !R.Renamed) {Heroes[1]->GetPlayerState()->SetPlayerName(TEXT("Друг_НовоеИмя"));R.Renamed=true;}
    if(Host && T>6 && !R.Respawned) {
        auto* Owner=Cast<APlayerController>(Heroes[1]->GetController());
        if(!Owner) return;
        R.OldPawn=Heroes[1];Owner->UnPossess();
        World->GetAuthGameMode()->RestartPlayerAtTransform(Owner,FTransform(FVector(-300,150,10072)));
        R.Respawned=true;return;
    }
    if(!R.Camera.IsValid()) {
        R.Camera=World->SpawnActor<ACameraActor>();R.Camera->GetCameraComponent()->SetFieldOfView(45);
        const FVector Aim(-300,0,10100),Offset(430,-500,280);
        R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());PC->SetViewTarget(R.Camera.Get());
    }
    // Possession after respawn restores the pawn's camera; keep the test view.
    PC->SetViewTarget(R.Camera.Get());
    ++R.Samples;
    for(int32 I=0;I<2;++I) {
        const auto* Hero=Heroes[I];const auto* Name=Hero->PlayerNameLabel.Get();
        const auto* Widget=Name?Cast<UMCPlayerNameWidget>(Name->GetUserWidgetObject()):nullptr;
        const FString Expected=I==0?TEXT("Host_Alpha"):T<3?TEXT("Друг_42"):TEXT("Друг_НовоеИмя");
        const bool Ready=Name && Widget && Name->IsVisible() && Name->GetWidgetSpace()==EWidgetSpace::Screen
            && Name->GetCollisionEnabled()==ECollisionEnabled::NoCollision
            && Name->GetDisplayedName()==Expected && Widget->GetPlayerName()==Expected
            && Name->GetComponentLocation().Z>Hero->GetMesh()->GetSocketLocation(Hero->RigBone(TEXT("gaze_head"))).Z+30;
        if(!Ready) continue;
        const uint8 Bit=1u<<I;
        if(T>1 && T<2.8) R.InitialSeen|=Bit;
        if(T>4 && T<5.8) R.RenameSeen|=Bit;
        if(T>8 && T<11) R.RespawnSeen|=Bit;
    }
    if(T>9 && !R.Captured && FParse::Param(FCommandLine::Get(),TEXT("MCPlayerNameCapture"))) {
        for(const auto* Hero:Heroes) UE_LOG(LogTemp,Display,TEXT("MC_NAMES_ANCHOR name=%s label_z=%.2f mesh_top_z=%.2f head_z=%.2f"),
            *Hero->GetPlayerState()->GetPlayerName(),Hero->PlayerNameLabel->GetComponentLocation().Z,
            Hero->GetMesh()->Bounds.GetBox().Max.Z,Hero->GetMesh()->GetSocketLocation(Hero->RigBone(TEXT("gaze_head"))).Z);
        const FString Image=FPaths::ProjectSavedDir()/TEXT("PlayerNameValidation")/(Host?TEXT("Host.png"):TEXT("Client.png"));
        FScreenshotRequest::RequestScreenshot(Image,true,false);R.Captured=true;
    }
}
#endif
