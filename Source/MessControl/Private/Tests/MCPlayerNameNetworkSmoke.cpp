#if !UE_BUILD_SHIPPING
#include "MCPlayerNameComponent.h"
#include "MCToothCharacter.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCGameMode.h"
#include "MCToothStatusComponent.h"
#include "MCScoreboardWidget.h"
#include "MCEmoteWidget.h"
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
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

// Real listen-server/client rendering, replicated Unicode names, a rename and
// a replacement pawn. Inspect the actual text widget, including the old corpse.
void MCTickPlayerNameValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCToothCharacter> OldPawn;
        TWeakObjectPtr<ACameraActor> Camera;
        float Age=0;
        bool Setup=false,Renamed=false,Respawned=false,Captured=false,Died=false,Spectated=false,Returned=false,SignalSeen=false,ScoreboardSeen=false;
        uint8 InitialSeen=0,RenameSeen=0,RespawnSeen=0;
        uint8 ScoreSeen=0;
        bool CapturedBoard=false,CapturedWheel=false,OpenedWheel=false,TabPressed=false,TabReleased=false;
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
        const bool CorpseHidden=!Host || !R.OldPawn.IsValid() || !R.OldPawn->PlayerNameLabel->IsVisible();
        const bool Pass=R.InitialSeen==3 && R.RenameSeen==3 && R.RespawnSeen==3 && CorpseHidden && R.Samples>100
            && R.ScoreSeen==3 && R.SignalSeen && R.ScoreboardSeen && (Host || (R.Spectated && R.Returned));
        UE_LOG(LogTemp,Display,TEXT("MC_NAMES_%s net=%d initial=%d renamed=%d respawn=%d corpse_hidden=%d samples=%d score=%d alarm=%d scoreboard=%d spectator=%d returned=%d"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),R.InitialSeen,R.RenameSeen,R.RespawnSeen,CorpseHidden,R.Samples,R.ScoreSeen,R.SignalSeen,R.ScoreboardSeen,R.Spectated,R.Returned);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(T>(Host?18:17) || R.Age>60) {Finish();return;}
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
            if(auto* State=Heroes[I]->GetPlayerState<AMCPlayerState>()) {
                State->AddPoints(I==0?10:25);
                Heroes[I]->ApplyPlayerColor(I==0?FLinearColor(.2f,.6f,1):FLinearColor(1,.3f,.2f));
            }
        }
        GS->bDevManualEvents=true;GS->Phase=EMCShiftPhase::Intermission;GS->PhaseEndsAt=0;
        GS->TasksTotal=8719;GS->DayStartedAt=Now+1;GS->ForceNetUpdate();R.Setup=true;return;
    }
    if(T<0) return;
    if(Host && T>3 && !R.Renamed) {
        Heroes[1]->GetPlayerState()->SetPlayerName(TEXT("Друг_НовоеИмя"));
        if(auto* Owner=Cast<AMCPlayerController>(Heroes[1]->GetController())) Owner->ServerSendAlarm(EMCPlayerAlarm::Help);
        R.Renamed=true;
    }
    if(Host && T>6 && !R.Died) {
        R.OldPawn=Heroes[1];Heroes[1]->Status->Damage(10000);R.Died=true;
    }
    if(Host && R.Died && !R.Respawned) {
        if(auto* Mode=World->GetAuthGameMode<AMCGameMode>()) Mode->ProcessRespawns();
        if(Heroes[1]!=R.OldPawn.Get()) {
            Heroes[1]->SetActorLocation(FVector(-300,150,10072),false,nullptr,ETeleportType::TeleportPhysics);R.Respawned=true;
        }
    }
    if(!R.Camera.IsValid()) {
        R.Camera=World->SpawnActor<ACameraActor>();R.Camera->GetCameraComponent()->SetFieldOfView(45);
        const FVector Aim(-300,0,10100),Offset(430,-500,280);
        R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());PC->SetViewTarget(R.Camera.Get());
    }
    // Possession after respawn restores the pawn's camera; keep the test view.
    auto* LocalPC=Cast<AMCPlayerController>(PC);
    if(!Host && LocalPC && T>11 && !LocalPC->IsSpectating() && Heroes[1]->Status->IsAlive() && PC->GetViewTarget()==Heroes[1]) R.Returned=true;
    if(T<6 && (!LocalPC || !LocalPC->IsSpectating())) PC->SetViewTarget(R.Camera.Get());
    if(!Host && LocalPC && T>7 && T<10 && LocalPC->IsSpectating()
        && LocalPC->GetSpectatorTarget()==Heroes[0] && PC->GetViewTarget()==Heroes[0]
        && LocalPC->GetRespawnSecondsRemaining()>0) R.Spectated=true;
    if(LocalPC && T>1 && !R.TabPressed) {
        LocalPC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab,IE_Pressed,1,1,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice()));R.TabPressed=true;
    }
    if(LocalPC && T>2.8 && !R.TabReleased) {
        LocalPC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab,IE_Released,0,1,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice()));R.TabReleased=true;
    }
    if(LocalPC && T>1 && T<2.8) {
        LocalPC->ShowScoreboard();
        if(auto* Board=LocalPC->ScoreboardWidget.Get()) {
            Board->Refresh();
            R.ScoreboardSeen|=Board->PlayerEntries.Num()==2 && Board->PlayerEntries[0].Points==25 && Board->PlayerEntries[1].Points==10;
        }
        if(T>1.8 && !R.CapturedBoard && FParse::Param(FCommandLine::Get(),TEXT("MCPlayerNameCapture"))) {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("PlayerNameValidation")/(Host?TEXT("ScoreboardHost.png"):TEXT("ScoreboardClient.png")),true,false);R.CapturedBoard=true;
        }
    } else if(LocalPC) LocalPC->HideScoreboard();
    if(LocalPC && FParse::Param(FCommandLine::Get(),TEXT("MCPlayerNameCapture"))) {
        if(T>4.2 && T<5.2 && !R.OpenedWheel) { LocalPC->ToggleEmotes();R.OpenedWheel=true; }
        if(T>4.6 && T<5.2 && R.OpenedWheel && !R.CapturedWheel) {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("PlayerNameValidation")/(Host?TEXT("RadialHost.png"):TEXT("RadialClient.png")),true,false);R.CapturedWheel=true;
        }
        if(T>5.2 && R.OpenedWheel && LocalPC->EmoteWidget && LocalPC->EmoteWidget->IsVisible()) LocalPC->ToggleEmotes();
    }
    ++R.Samples;
    for(int32 I=0;I<2;++I) {
        const auto* Hero=Heroes[I];const auto* Name=Hero->PlayerNameLabel.Get();
        const auto* Widget=Name?Cast<UMCPlayerNameWidget>(Name->GetUserWidgetObject()):nullptr;
        const FString Expected=I==0?TEXT("Host_Alpha"):T<3?TEXT("Друг_42"):TEXT("Друг_НовоеИмя");
        const bool Ready=Name && Widget && Name->IsVisible() && Name->GetWidgetSpace()==EWidgetSpace::Screen
            && Name->GetCollisionEnabled()==ECollisionEnabled::NoCollision
            && Name->GetDisplayedName()==Expected && Widget->GetPlayerName()==Expected
            && Name->GetComponentLocation().Z>Hero->GetMesh()->GetSocketLocation(Hero->RigBone(TEXT("gaze_head"))).Z+30;
        if(const auto* State=Hero->GetPlayerState<AMCPlayerState>()) {
            const FLinearColor ExpectedColor=I==0?FLinearColor(.2f,.6f,1):FLinearColor(1,.3f,.2f);
            if(State->Points==(I==0?10:25) && State->bSessionHost==(I==0)
                && State->PlayerColor.Equals(ExpectedColor,.01f) && Hero->GetPlayerColor().Equals(ExpectedColor,.01f)) R.ScoreSeen|=1u<<I;
            if(T>4 && T<5.8 && I==1 && State->Alarm==EMCPlayerAlarm::Help && State->AlarmUntil>Now) R.SignalSeen=true;
        }
        if(!Ready) continue;
        const uint8 Bit=1u<<I;
        if(T>1 && T<2.8) R.InitialSeen|=Bit;
        if(T>4 && T<5.8) R.RenameSeen|=Bit;
        if(T>12 && T<16) R.RespawnSeen|=Bit;
    }
    if(T>14 && !R.Captured && FParse::Param(FCommandLine::Get(),TEXT("MCPlayerNameCapture"))) {
        for(const auto* Hero:Heroes) UE_LOG(LogTemp,Display,TEXT("MC_NAMES_ANCHOR name=%s label_z=%.2f mesh_top_z=%.2f head_z=%.2f"),
            *Hero->GetPlayerState()->GetPlayerName(),Hero->PlayerNameLabel->GetComponentLocation().Z,
            Hero->GetMesh()->Bounds.GetBox().Max.Z,Hero->GetMesh()->GetSocketLocation(Hero->RigBone(TEXT("gaze_head"))).Z);
        const FString Image=FPaths::ProjectSavedDir()/TEXT("PlayerNameValidation")/(Host?TEXT("Host.png"):TEXT("Client.png"));
        FScreenshotRequest::RequestScreenshot(Image,true,false);R.Captured=true;
    }
}
#endif
