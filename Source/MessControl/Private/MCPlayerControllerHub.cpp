#include "MCPlayerController.h"
#include "MCHubGameMode.h"
#include "MCHubGameState.h"
#include "MCHubWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCPrototypeWidget.h"
#include "MCBossHealthWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/WorldSettings.h"

bool AMCPlayerController::IsHubWorld() const
{
    if(!GetWorld()) return false;
    // Map identity also works before the newly replicated GameState arrives.
    const FString Map=UGameplayStatics::GetCurrentLevelName(this,true);
    if(Map==TEXT("Hub") || Map==TEXT("L_HubSelection")) return true;
    const auto* Settings=GetWorld()->GetWorldSettings();
    return Settings && Settings->DefaultGameMode && Settings->DefaultGameMode->IsChildOf(AMCHubGameMode::StaticClass());
}

void AMCPlayerController::ResetHubPresentation()
{
    if(HubWidget) HubWidget->RemoveFromParent();
    HubWidget=nullptr;
    if(HubSceneCamera.IsValid()) {
        bAutoManageActiveCameraTarget=true;
        if(GetPawn()) SetViewTarget(GetPawn());
    }
    HubSceneCamera.Reset(); bHubMenuInput=false; bHubStagePrerequisitesAdded=false; NextHubLoadAckAt=0;
}

void AMCPlayerController::RefreshHubPresentation(float DeltaSeconds)
{
    if(!IsLocalController()) return;
    if(!IsHubWorld()) {
        if(HubWidget) {ResetHubPresentation(); UpdateInputMode();}
        return;
    }
    auto* State=GetWorld()->GetGameState<AMCHubGameState>();
    if(!State) return;
    if(!HubWidget) {
        HubWidget=CreateWidget<UMCHubWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("Hub"),UMCHubWidget::StaticClass()));
        if(HubWidget) HubWidget->AddToViewport(25);
    }
    if(!HubWidget) return;
    HubWidget->TickPresentation(DeltaSeconds);
    if(State->Phase!=EMCHubPhase::Gathering && !HubSceneCamera.IsValid()) {
        for(TActorIterator<AActor> It(GetWorld());It;++It)
            if(It->ActorHasTag(TEXT("Menu3D_Camera")) && It->FindComponentByClass<UCameraComponent>()) {
                HubSceneCamera=*It; bAutoManageActiveCameraTarget=false; SetViewTarget(*It); break;
            }
    }
    if(HubSceneCamera.IsValid() && !bHubStagePrerequisitesAdded) {
        for(TActorIterator<AActor> It(GetWorld());It;++It) {
            const bool StageActor=It->Tags.ContainsByPredicate([](FName Tag) {return Tag.ToString().StartsWith(TEXT("Menu3D_"));});
            if(!StageActor) continue;
            TInlineComponentArray<USkeletalMeshComponent*> Meshes(*It);
            for(auto* Mesh:Meshes) Mesh->AddTickPrerequisiteActor(this);
        }
        bHubStagePrerequisitesAdded=true;
    }
    const bool NeedsMenu=HubWidget->RequiresMenuInput();
    if(NeedsMenu!=bHubMenuInput) {bHubMenuInput=NeedsMenu; UpdateInputMode();}
    const auto* LocalPlayerState=GetPlayerState<AMCPlayerState>();
    const bool Loaded=State->Players.ContainsByPredicate([LocalPlayerState](const FMCHubPlayerReadiness& Entry) {
        return Entry.Player==LocalPlayerState && Entry.bLoaded;
    });
    const bool HasScene=State->Phase==EMCHubPhase::Gathering
        ? GetPawn() && GetPawn()->GetWorld()==GetWorld() : HubSceneCamera.IsValid();
    if(LocalPlayerState && HasScene && IsWorldPresentationReady() && HubWidget->IsSceneReady() && !Loaded
        && GetWorld()->GetRealTimeSeconds()>=NextHubLoadAckAt) {
        NextHubLoadAckAt=GetWorld()->GetRealTimeSeconds()+1.; ServerHubSceneLoaded();
    }
}

void AMCPlayerController::HandleHubStartInput()
{
    if(const auto* State=GetWorld()->GetGameState<AMCHubGameState>();
        State && State->Phase==EMCHubPhase::Gathering && State->AreAllReady()) RequestHubOpenSelection();
}

void AMCPlayerController::RequestHubOpenSelection() {if(IsLocalController() && IsHubWorld()) ServerHubOpenSelection();}
void AMCPlayerController::RequestHubSelectDay(int32 Day) {if(IsLocalController() && IsHubWorld()) ServerHubSelectDay(Day);}
void AMCPlayerController::RequestHubLaunch() {if(IsLocalController() && IsHubWorld()) ServerHubLaunch();}
void AMCPlayerController::RequestHubReturn() {if(IsLocalController() && IsHubWorld()) ServerHubReturn();}
void AMCPlayerController::ServerHubOpenSelection_Implementation()
{if(auto* Mode=GetWorld()->GetAuthGameMode<AMCHubGameMode>()) Mode->RequestOpenSelection(this);}
void AMCPlayerController::ServerHubSelectDay_Implementation(int32 Day)
{if(auto* Mode=GetWorld()->GetAuthGameMode<AMCHubGameMode>()) Mode->RequestSelectDay(this,Day);}
void AMCPlayerController::ServerHubLaunch_Implementation()
{if(auto* Mode=GetWorld()->GetAuthGameMode<AMCHubGameMode>()) Mode->RequestLaunch(this);}
void AMCPlayerController::ServerHubReturn_Implementation()
{if(auto* Mode=GetWorld()->GetAuthGameMode<AMCHubGameMode>()) Mode->RequestReturnToHub(this);}
void AMCPlayerController::ServerHubSceneLoaded_Implementation()
{if(auto* Mode=GetWorld()->GetAuthGameMode<AMCHubGameMode>()) Mode->AcknowledgeSceneLoaded(this);}
