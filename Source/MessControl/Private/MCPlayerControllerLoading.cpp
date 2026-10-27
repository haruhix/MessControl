#include "MCPlayerController.h"

#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCHubWidget.h"
#include "MCLoadingWidget.h"
#include "MCPerkChoiceWidget.h"
#include "MCGameplayMenuWidget.h"
#include "MCFishingRewardWidget.h"
#include "MCTaskCoinPresentationComponent.h"
#include "MCGameplayUIFactory.h"
#include "MCPlayerState.h"
#include "MCSingleDayDirector.h"
#include "MCToothCharacter.h"
#include "MCTutorialDirector.h"
#include "MCWorldPreloadSubsystem.h"
#include "Engine/World.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "MCLoadingController"

bool AMCPlayerController::NeedsWorldPreparation() const
{
    const UWorld* World=GetWorld();
    if(!HasLocalPresentationOwner() || !World || World->GetNetMode()==NM_DedicatedServer
        || (World->WorldType!=EWorldType::Game && World->WorldType!=EWorldType::PIE)) return false;
    const FString Map=UGameplayStatics::GetCurrentLevelName(this,true);
    return Map==TEXT("L_Mouth") || Map==TEXT("L_Tutorial") || IsHubWorld();
}

bool AMCPlayerController::IsWorldPresentationReady() const
{
    // Seamless travel can retain a locally designated controller before its
    // ULocalPlayer is attached to this destination world. That is not ready UI.
    if(IsLocalController() && !HasLocalPresentationOwner()) return false;
    if(!NeedsWorldPreparation()) return true;
    const auto* Preparation=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
    return Preparation && Preparation->IsReady();
}

bool AMCPlayerController::IsWaitingForWorldStart() const
{
    if(const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>())
        // Scene-generation ACKs also run on open/return. Those updates must
        // never bring the initial world-loading cover back over the selector.
        // A partial replicated roster may initially contain only the host.
        // Require this local player in the acknowledged snapshot as well.
        return !bLoadingPresentationReleased && (!Hub->HasLoadedScene(GetPlayerState<AMCPlayerState>())
            || !Hub->AreAllLoaded() || Hub->Phase==EMCHubPhase::Travelling);
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!State || !GetPlayerState<AMCPlayerState>() || !GetPawn()
        || GetPawn()->GetWorld()!=GetWorld() || !HasAcknowledgedGameplay()) return true;
    if(State->bDevManualEvents) return false;
    if(State->bTutorialActive) {
        const auto* Tutorial=AMCTutorialDirector::Find(GetWorld());
        return !Tutorial || Tutorial->Stage==EMCTutorialStage::Loading;
    }
    if(State->bLobbyWaiting) {
        // The old manual lobby needs its Start button. Hub runs auto-start
        // after the same ACK barrier, so only those keep the waiting cover.
        return FCString::Atoi(GetWorld()->URL.GetOption(TEXT("MCHubRun="),TEXT("0")))!=0;
    }
    if(State->bSingleDayLoop)
        return !IsValid(State->SingleDayDirector) || State->SingleDayDirector->Stage==EMCSingleDayStage::Training;
    return false;
}

void AMCPlayerController::RefreshLoadingPresentation()
{
    if(!HasLocalPresentationOwner()) {ResetLoadingPresentation();return;}
    if(!NeedsWorldPreparation()) {ResetLoadingPresentation();return;}
    if(LoadingPresentationWorld.Get()!=GetWorld()) {
        ResetLoadingPresentation();LoadingPresentationWorld=GetWorld();
    }
    auto* Preparation=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
    if(IsHubWorld()) PrepareFishingRewardPresentation();
    else PrepareTaskCoinPresentation();
    if(!PauseWidget) {
        if(auto* Menu=EnsureGameplayMenu(false)) {
            Menu->SetVisibility(ESlateVisibility::Collapsed);Menu->AddToViewport(50);
            Menu->TakeWidget();Menu->ForceLayoutPrepass();
        }
    }
    // The selector creates its material instances while constructing and
    // advancing its 3D presentation. Discover those only after that setup;
    // IsSceneReady does not depend on our ACK or the team's loaded count.
    if(!IsHubWorld() && !PerkChoiceWidget) {
        // Native widget trees are not executed by the saved-asset manifest.
        // Retain this modal and its dynamic brushes behind the loading cover,
        // including the stage countdown, before the first reward interaction.
        PerkChoiceWidget=CreateWidget<UMCPerkChoiceWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("PerkChoice"),UMCPerkChoiceWidget::StaticClass()));
        if(PerkChoiceWidget) {
            PerkChoiceWidget->SetVisibility(ESlateVisibility::Collapsed);
            PerkChoiceWidget->AddToViewport(30);
            PerkChoiceWidget->TakeWidget();PerkChoiceWidget->ForceLayoutPrepass();
        }
    }
    // A collapsed/detached modal need not retain a Slate wrapper. Its UObject
    // tree and material instances remain owned by the controller across closes.
    bool PresentationConstructed=!IsHubWorld() && PerkChoiceWidget && PerkChoiceWidget->GetRootWidget();
    if(IsHubWorld() && HubWidget && HubWidget->IsSceneReady()) {
        const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>();
        PresentationConstructed=Hub && (Hub->Phase!=EMCHubPhase::Gathering
            || (GetPawn() && GetPawn()->GetWorld()==GetWorld()));
    }
    PresentationConstructed &= !IsHubWorld() || HasPreparedWardrobePresentation();
    PresentationConstructed &= !IsHubWorld() || (FishingRewardWidget && FishingRewardWidget->HasPreparedPresentation());
    PresentationConstructed &= IsHubWorld() || (TaskCoinPresentation && TaskCoinPresentation->HasPreparedPresentation());
    PresentationConstructed &= PauseWidget && PauseWidget->GetRootWidget();
    if(Preparation) {
        Preparation->SetPresentationReady(PresentationConstructed);
        Preparation->BeginPreparation(this);
    }
    if(bLoadingPresentationReleased) return;
    const bool Ready=Preparation && Preparation->IsReady();
    const bool Waiting=Ready && IsWaitingForWorldStart();
    if(Ready && !Waiting) {
        bLoadingPresentationReleased=true;
        if(LoadingWidget) LoadingWidget->RemoveFromParent();LoadingWidget=nullptr;
        bLoadingInputBlocked=false;LoadingCancelledPawn.Reset();
        UpdateInputMode();return;
    }
    const bool WasBlocked=bLoadingInputBlocked;
    bLoadingInputBlocked=true;
    bool NewWidget=false;
    if(!LoadingWidget) {
        LoadingWidget=CreateWidget<UMCLoadingWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("Loading"),UMCLoadingWidget::StaticClass()));
        if(LoadingWidget) {LoadingWidget->AddToViewport(10000);NewWidget=true;}
    }
    if(LoadingWidget) {
        LoadingWidget->SetPreparation(Preparation);
        LoadingWidget->SetWaitingForPlayers(Waiting);
        const bool Russian=FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName()==TEXT("ru");
        FText Detail=Russian?LOCTEXT("WaitingStartRu","Локация откроется, когда команда будет готова.")
            :LOCTEXT("WaitingStart","The location will open when the team is ready.");
        const FText CountPattern=Russian?LOCTEXT("WaitingLoadedCountRu","Загрузились: {0} / {1}")
            :LOCTEXT("WaitingLoadedCount","Players loaded: {0} / {1}");
        if(const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>()) {
            Detail=FText::Format(CountPattern,Hub->GetLoadedCount(),Hub->GetParticipantCount());
        }
        else if(const auto* State=GetWorld()->GetGameState<AMCGameState>();State && State->bLobbyWaiting)
            Detail=FText::Format(CountPattern,State->LobbyLoadedPlayers,
                State->LobbyParticipantPlayers);
        LoadingWidget->SetWaitingDetail(Detail);
        LoadingWidget->RefreshPresentation();
        if(NewWidget) LoadingWidget->ForceLayoutPrepass();
    }
    const bool NewPawn=GetPawn() && LoadingCancelledPawn.Get()!=GetPawn();
    const bool Failed=Preparation && Preparation->HasFailed();
    if(!WasBlocked || NewWidget || NewPawn || Failed!=bLoadingFailureFocused) ApplyLoadingInputMode();
    bLoadingFailureFocused=Failed;
}

void AMCPlayerController::ApplyLoadingInputMode()
{
    if(!HasLocalPresentationOwner() || !bLoadingInputBlocked) return;
    bShowMouseCursor=true;
    ResetIgnoreMoveInput();SetIgnoreMoveInput(true);
    ResetIgnoreLookInput();SetIgnoreLookInput(true);
    if(GetPawn() && LoadingCancelledPawn.Get()!=GetPawn()) {
        LoadingCancelledPawn=GetPawn();
        if(auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
    }
    FInputModeUIOnly Mode;
    if(LoadingWidget) {
        if(UWidget* Focus=LoadingWidget->GetFocusTarget()) Mode.SetWidgetToFocus(Focus->TakeWidget());
    }
    SetInputMode(Mode);
    if(LoadingWidget) if(UWidget* Focus=LoadingWidget->GetFocusTarget()) Focus->SetUserFocus(this);
}

void AMCPlayerController::ResetLoadingPresentation()
{
    const bool WasBlocked=bLoadingInputBlocked;
    if(LoadingWidget) LoadingWidget->RemoveFromParent();LoadingWidget=nullptr;
    LoadingPresentationWorld.Reset();LoadingCancelledPawn.Reset();
    bLoadingInputBlocked=false;bLoadingPresentationReleased=false;bLoadingFailureFocused=false;
    if(WasBlocked && HasLocalPresentationOwner()) {
        ResetIgnoreMoveInput();ResetIgnoreLookInput();
        SetInputMode(FInputModeGameOnly());bShowMouseCursor=false;
    }
}

#undef LOCTEXT_NAMESPACE
