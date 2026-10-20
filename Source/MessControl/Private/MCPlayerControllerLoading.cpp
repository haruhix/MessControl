#include "MCPlayerController.h"

#include "MCGameState.h"
#include "MCHubGameState.h"
#include "MCHubWidget.h"
#include "MCHubWidget.h"
#include "MCLoadingWidget.h"
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
    if(!IsLocalController() || !World || World->GetNetMode()==NM_DedicatedServer
        || (World->WorldType!=EWorldType::Game && World->WorldType!=EWorldType::PIE)) return false;
    const FString Map=UGameplayStatics::GetCurrentLevelName(this,true);
    return Map==TEXT("L_Mouth") || Map==TEXT("L_Tutorial") || IsHubWorld();
}

bool AMCPlayerController::IsWorldPresentationReady() const
{
    if(!NeedsWorldPreparation()) return true;
    const auto* Preparation=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
    return Preparation && Preparation->IsReady();
}

bool AMCPlayerController::IsWaitingForWorldStart() const
{
    if(const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>())
        return !Hub->AreAllLoaded() || Hub->Phase==EMCHubPhase::Travelling;
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!State || !GetPlayerState<AMCPlayerState>() || !GetPawn()
        || GetPawn()->GetWorld()!=GetWorld() || !bGameplayLoadAckSent) return true;
    if(State->bDevManualEvents) return false;
    if(State->bTutorialActive) {
        const auto* Tutorial=AMCTutorialDirector::Find(GetWorld());
        return !Tutorial || Tutorial->Stage==EMCTutorialStage::Loading;
    }
    // A durable personal offer is also proof that the server passed the team
    // barrier. It may replicate before the director actor's changed stage.
    if(State->bSingleDayLoop && HasAuthoritativePerkChoice()) return false;
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
    if(!IsLocalController()) return;
    if(!NeedsWorldPreparation()) {ResetLoadingPresentation();return;}
    if(LoadingPresentationWorld.Get()!=GetWorld()) {
        ResetLoadingPresentation();LoadingPresentationWorld=GetWorld();
    }
    auto* Preparation=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
    // The selector creates its material instances while constructing and
    // advancing its 3D presentation. Discover those only after that setup;
    // IsSceneReady does not depend on our ACK or the team's loaded count.
    bool PresentationConstructed=!IsHubWorld();
    if(IsHubWorld() && HubWidget && HubWidget->IsSceneReady()) {
        const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>();
        PresentationConstructed=Hub && (Hub->Phase!=EMCHubPhase::Gathering
            || (GetPawn() && GetPawn()->GetWorld()==GetWorld()));
    }
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
        LoadingWidget=CreateWidget<UMCLoadingWidget>(this,UMCLoadingWidget::StaticClass());
        if(LoadingWidget) {LoadingWidget->AddToViewport(10000);NewWidget=true;}
    }
    if(LoadingWidget) {
        LoadingWidget->SetPreparation(Preparation);
        LoadingWidget->SetWaitingForPlayers(Waiting);
        const bool Russian=FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName()==TEXT("ru");
        FText Detail=Russian?LOCTEXT("WaitingStartRu","Локация откроется, когда команда будет готова.")
            :LOCTEXT("WaitingStart","The location will open when the team is ready.");
        const FText CountPattern=Russian?LOCTEXT("WaitingCountRu","Готовы: {0} / {1}")
            :LOCTEXT("WaitingCount","Players ready: {0} / {1}");
        if(const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>()) {
            const int32 Loaded=Hub->Players.FilterByPredicate([](const FMCHubPlayerReadiness& Entry) {
                return Entry.bLoaded && IsValid(Entry.Player) && !Entry.Player->IsOnlyASpectator()
                    && !Entry.Player->IsInactive() && !Entry.Player->IsABot();
            }).Num();
            Detail=FText::Format(CountPattern,Loaded,Hub->GetParticipantCount());
        }
        else if(const auto* State=GetWorld()->GetGameState<AMCGameState>();State && State->bLobbyWaiting)
            Detail=FText::Format(CountPattern,State->LobbyLoadedPlayers,
                FMath::Max(State->LobbyLoadedPlayers,State->PlayerArray.Num()));
        LoadingWidget->SetWaitingDetail(Detail);
        LoadingWidget->RefreshPresentation();
    }
    const bool NewPawn=GetPawn() && LoadingCancelledPawn.Get()!=GetPawn();
    const bool Failed=Preparation && Preparation->HasFailed();
    if(!WasBlocked || NewWidget || NewPawn || Failed!=bLoadingFailureFocused) ApplyLoadingInputMode();
    bLoadingFailureFocused=Failed;
}

void AMCPlayerController::ApplyLoadingInputMode()
{
    if(!IsLocalController() || !bLoadingInputBlocked) return;
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
    if(WasBlocked && IsLocalController()) {
        ResetIgnoreMoveInput();ResetIgnoreLookInput();
        SetInputMode(FInputModeGameOnly());bShowMouseCursor=false;
    }
}

#undef LOCTEXT_NAMESPACE
