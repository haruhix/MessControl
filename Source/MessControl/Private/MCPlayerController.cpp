#include "MCPlayerController.h"
#include "MCPrototypeWidget.h"
#include "MCDevPanelWidget.h"
#include "MCEmoteWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "MCSteamSessionSubsystem.h"
#include "MCScoreboardWidget.h"
#include "MCPlayerState.h"
#include "MCToothStatusComponent.h"
#include "EngineUtils.h"
#include "MCRewardChest.h"
#include "MCPerkChoiceWidget.h"
#include "TimerManager.h"
#include "MCBossCharacter.h"
#include "MCBossIntro.h"
#include "MCBossHealthWidget.h"
#include "MCMainMenuWidget.h"
#include "MCTutorialWidget.h"
#include "MCTutorialDirector.h"

void AMCPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        if (auto* Steam=GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>()) Steam->RefreshAvailability();
        PrototypeWidget = CreateWidget<UMCPrototypeWidget>(this,UMCPrototypeWidget::StaticClass());
        PrototypeWidget->AddToViewport(); UpdateInputMode();
        BossHealthWidget=CreateWidget<UMCBossHealthWidget>(this,UMCBossHealthWidget::StaticClass());
        if (BossHealthWidget) BossHealthWidget->AddToViewport(40);
        GetWorldTimerManager().SetTimer(BossHUDTimer,this,&AMCPlayerController::RefreshBossHUD,.1f,true);
    }
}
void AMCPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::T,IE_Pressed,this,&AMCPlayerController::ToggleEmotes);
    InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&AMCPlayerController::TogglePauseMenu);
    InputComponent->BindKey(EKeys::Tab,IE_Pressed,this,&AMCPlayerController::ShowScoreboard);
    InputComponent->BindKey(EKeys::Tab,IE_Released,this,&AMCPlayerController::HideScoreboard);
    InputComponent->BindKey(EKeys::Right,IE_Pressed,this,&AMCPlayerController::NextSpectator);
    InputComponent->BindKey(EKeys::Left,IE_Pressed,this,&AMCPlayerController::PreviousSpectator);
#if !UE_BUILD_SHIPPING
    InputComponent->BindKey(EKeys::F3,IE_Pressed,this,&AMCPlayerController::ToggleDevPanel);
#endif
}
void AMCPlayerController::ShowScoreboard()
{
    if (!IsLocalController() || IsRewardInteractionActive() || bBossIntroPlaying) return;
    if (!ScoreboardWidget)
    {
        ScoreboardWidget=CreateWidget<UMCScoreboardWidget>(this,UMCScoreboardWidget::StaticClass());
        if (!ScoreboardWidget) return;
        ScoreboardWidget->AddToViewport(12);
    }
    ScoreboardWidget->SetVisibility(ESlateVisibility::HitTestInvisible); ScoreboardWidget->Refresh();
}
void AMCPlayerController::HideScoreboard()
{ if (ScoreboardWidget) ScoreboardWidget->SetVisibility(ESlateVisibility::Collapsed); }
void AMCPlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsLocalController()) return;
    if (GetWorld()->GetTimeSeconds()>=NextFrontEndCheck) { NextFrontEndCheck=GetWorld()->GetTimeSeconds()+.1; RefreshFrontEnd(); }
    if (bBossIntroPlaying) return;
    // UI input mode can swallow the release event when a menu opens while Tab is held.
    if (ScoreboardWidget && ScoreboardWidget->IsVisible() && !IsInputKeyDown(EKeys::Tab)) HideScoreboard();
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now>=NextSpectatorCheck) { NextSpectatorCheck=Now+.1; UpdateDeathSpectating(); }
}
void AMCPlayerController::UpdateDeathSpectating()
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    const bool Dead=Hero && Hero->Status && !Hero->Status->IsAlive();
    if (!Dead)
    {
        if (bDeathSpectating || PreviousPawn!=GetPawn())
        {
            if (auto* Previous=Cast<AMCToothCharacter>(GetViewTarget())) Previous->ClearCameraWallReveal();
            bDeathSpectating=false; SpectatorTarget.Reset(); bAutoManageActiveCameraTarget=true;
            if (GetPawn()) SetViewTargetWithBlend(GetPawn(),.2f);
        }
        PreviousPawn=GetPawn(); return;
    }
    PreviousPawn=Hero;
    if (!bDeathSpectating)
    {
        bDeathSpectating=true; bAutoManageActiveCameraTarget=false;
        if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
        UpdateInputMode();
    }
    auto* Target=SpectatorTarget.Get();
    if (!IsValid(Target) || !Target->Status->IsAlive() || Target==Hero) CycleSpectator(1);
}
void AMCPlayerController::CycleSpectator(int32 Direction)
{
    if (!IsLocalController() || !bDeathSpectating) return;
    TArray<AMCToothCharacter*> Living;
    for (TActorIterator<AMCToothCharacter> It(GetWorld()); It; ++It)
        if (It->GetPlayerState() && It->Status && It->Status->IsAlive() && *It!=GetPawn()) Living.Add(*It);
    Living.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B)
    { return (A.GetPlayerState()?A.GetPlayerState()->GetPlayerId():0)<(B.GetPlayerState()?B.GetPlayerState()->GetPlayerId():0); });
    if (Living.IsEmpty())
    {
        SpectatorTarget.Reset(); if (GetPawn() && GetViewTarget()!=GetPawn()) SetViewTargetWithBlend(GetPawn(),.2f);
        return;
    }
    const int32 Current=Living.IndexOfByKey(SpectatorTarget.Get());
    const int32 Next=Current==INDEX_NONE?0:(Current+Direction+Living.Num())%Living.Num();
    if (auto* Previous=Cast<AMCToothCharacter>(GetViewTarget())) Previous->ClearCameraWallReveal();
    SpectatorTarget=Living[Next]; SetViewTargetWithBlend(Living[Next],.2f);
}
void AMCPlayerController::NextSpectator() { CycleSpectator(1); }
void AMCPlayerController::PreviousSpectator() { CycleSpectator(-1); }
float AMCPlayerController::GetRespawnSecondsRemaining() const
{
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn()); const auto* State=GetWorld()->GetGameState();
    return Hero && State && !Hero->Status->IsAlive()?FMath::Max(0.,Hero->RespawnAt-State->GetServerWorldTimeSeconds()):0.f;
}
void AMCPlayerController::ServerSendAlarm_Implementation(EMCPlayerAlarm Alarm)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn()); auto* State=GetPlayerState<AMCPlayerState>();
    auto* Game=GetWorld()->GetGameState();
    if (!Hero || !Hero->Status->IsAlive() || !State || !Game || Alarm==EMCPlayerAlarm::None || uint8(Alarm)>uint8(EMCPlayerAlarm::Help)) return;
    const double Now=Game->GetServerWorldTimeSeconds(); if (Now<NextAlarmAt) return;
    NextAlarmAt=Now+2.; State->Alarm=Alarm; State->AlarmUntil=Now+4.; State->ForceNetUpdate();
}
void AMCPlayerController::ServerSetPlayerColor_Implementation(FLinearColor Color)
{
    if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B) || !FMath::IsFinite(Color.A)) return;
    const double Now=GetWorld()->GetTimeSeconds(); if (Now<NextColorAt) return; NextColorAt=Now+.2;
    Color=Color.GetClamped(0,1); Color.A=1;
    if (auto* State=GetPlayerState<AMCPlayerState>()) { State->PlayerColor=Color; State->ForceNetUpdate(); }
    if (auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->ApplyPlayerColor(Color);
}
void AMCPlayerController::ToggleDevPanel()
{
#if !UE_BUILD_SHIPPING
    if (!IsLocalController() || IsRewardInteractionActive() || bBossIntroPlaying) return;
    if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (!DevPanel)
    {
        DevPanel=CreateWidget<UMCDevPanelWidget>(this,UMCDevPanelWidget::StaticClass());
        DevPanel->AddToViewport(20); DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    }
    const bool bOpen=!DevPanel->IsVisible();
    if (PrototypeWidget) PrototypeWidget->ClosePanels();
    DevPanel->SetVisibility(bOpen?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if (bOpen) DevPanel->RefreshActions();
    UpdateInputMode();
#endif
}
bool AMCPlayerController::CanUseDevPanel() const
{
    const auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    return Mode && Mode->CanUseDevPanel(this);
}
void AMCPlayerController::RequestDevAction(EMCDevAction Action,int32 StepIndex)
{
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>())
    {
        const FText Result=Mode->ExecuteDevAction(this,Action,StepIndex);
        if (DevPanel) DevPanel->SetFeedback(Result);
    }
    else if (DevPanel) DevPanel->SetFeedback(FText::FromString(TEXT("События запускает хост. Его изменения появятся у всех игроков.")));
    UpdateInputMode();
}
void AMCPlayerController::ToggleEmotes()
{
    if (!IsLocalController() || IsRewardInteractionActive() || bBossIntroPlaying) return;
    if (!EmoteWidget)
    {
        EmoteWidget=CreateWidget<UMCEmoteWidget>(this,UMCEmoteWidget::StaticClass());
        EmoteWidget->AddToViewport(15); EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    const bool Open=!EmoteWidget->IsVisible();
    if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (PrototypeWidget) PrototypeWidget->ClosePanels();
    EmoteWidget->SetVisibility(Open?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if (Open) EmoteWidget->Refresh(); UpdateInputMode();
}
void AMCPlayerController::ToggleTuning() { if (IsRewardInteractionActive() || bBossIntroPlaying) return; if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed); if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed); if (PrototypeWidget) { PrototypeWidget->ToggleTuning(); UpdateInputMode(); } }
void AMCPlayerController::ToggleConnection() { if (IsRewardInteractionActive() || bBossIntroPlaying) return; if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed); if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed); if (PrototypeWidget) { PrototypeWidget->ToggleConnection(); UpdateInputMode(); } }
void AMCPlayerController::UpdateInputMode()
{
    if (bLobbyUIOpen || bPauseMenuOpen || bTutorialMenuInput)
    {
        bShowMouseCursor=true;
        ResetIgnoreMoveInput(); SetIgnoreMoveInput(true);
        ResetIgnoreLookInput(); SetIgnoreLookInput(true);
        if (auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
        FInputModeUIOnly Mode;
        if ((bLobbyUIOpen || bPauseMenuOpen) && FrontEndWidget) Mode.SetWidgetToFocus(FrontEndWidget->TakeWidget());
        else if (TutorialWidget) Mode.SetWidgetToFocus(TutorialWidget->TakeWidget());
        SetInputMode(Mode);
        if ((bLobbyUIOpen || bPauseMenuOpen) && FrontEndWidget) FrontEndWidget->FocusMenu();
        return;
    }
    if (bBossIntroPlaying)
    {
        bShowMouseCursor=false;
        ResetIgnoreMoveInput(); SetIgnoreMoveInput(true);
        ResetIgnoreLookInput(); SetIgnoreLookInput(true);
        SetInputMode(FInputModeGameOnly());
        return;
    }
    const bool bReward=IsRewardMenuOpen();
    const bool bOpening=IsRewardInteractionActive() && !bReward;
    const bool bDev=DevPanel && DevPanel->IsVisible();
    const bool bEmote=EmoteWidget && EmoteWidget->IsVisible();
    const bool bPanel = bReward || bEmote || bDev || (PrototypeWidget && PrototypeWidget->IsPanelOpen());
    bShowMouseCursor = bPanel;
    if (auto* Tooth = Cast<AMCToothCharacter>(GetPawn())) Tooth->bPreviewAnimation = PrototypeWidget && PrototypeWidget->IsTuningOpen();
    ResetIgnoreMoveInput(); SetIgnoreMoveInput(bPanel || bOpening);
    ResetIgnoreLookInput(); SetIgnoreLookInput(bPanel);
    if (bReward || bDev || bEmote)
    {
        if (auto* Tooth=Cast<AMCToothCharacter>(GetPawn())) Tooth->CancelGameplayInput();
        FInputModeUIOnly Mode; Mode.SetWidgetToFocus(bReward?PerkChoiceWidget->TakeWidget():(bEmote?EmoteWidget->TakeWidget():DevPanel->TakeWidget())); SetInputMode(Mode);
    }
    else if (bPanel) {
        if(auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
        FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(PrototypeWidget->TakeWidget()); Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode);
    }
    else SetInputMode(FInputModeGameOnly());
}

bool AMCPlayerController::IsRewardMenuOpen() const
{
    return PerkChoiceWidget && PerkChoiceWidget->IsInViewport() && PerkChoiceWidget->IsVisible();
}

bool AMCPlayerController::IsRewardInteractionActive() const
{
    return ActiveRewardChest.IsValid() && RewardPawn.Get()==GetPawn();
}

bool AMCPlayerController::PrepareRewardInteraction(AMCRewardChest* Chest)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if (!IsLocalController() || !IsValid(Chest) || !Hero || !Hero->Status || !Hero->Status->IsAlive()) return false;
    ActiveRewardChest=Chest;
    RewardPawn=Hero;
    RewardShownAt=GetWorld()->GetTimeSeconds();
    bRewardChoicePending=false;
    if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (PrototypeWidget) PrototypeWidget->ClosePanels();
    HideScoreboard();
    Hero->CancelGameplayInput();
    GetWorldTimerManager().SetTimer(RewardUITimer,this,&AMCPlayerController::CheckRewardUI,.1f,true);
    return true;
}

bool AMCPlayerController::PrepareRewardUI(AMCRewardChest* Chest)
{
    if (!PrepareRewardInteraction(Chest)) return false;
    if (!PerkChoiceWidget)
        PerkChoiceWidget=CreateWidget<UMCPerkChoiceWidget>(this,UMCPerkChoiceWidget::StaticClass());
    if (!PerkChoiceWidget) { CloseRewardUI(); return false; }
    if (!PerkChoiceWidget->IsInViewport()) PerkChoiceWidget->AddToViewport(30);
    PerkChoiceWidget->SetVisibility(ESlateVisibility::Visible);
    return true;
}

void AMCPlayerController::ClientShowRewardOpening_Implementation(AMCRewardChest* Chest,double ServerEndsAt)
{
    if (!FMath::IsFinite(ServerEndsAt) || !PrepareRewardInteraction(Chest)) return;
    // Lockpicking is presented by the character animation in the world. The modal
    // is created only after the server has opened the lid and sends three cards.
    if (PerkChoiceWidget) PerkChoiceWidget->RemoveFromParent();
    UpdateInputMode();
    CheckRewardUI();
}

void AMCPlayerController::ClientShowPerkChoices_Implementation(AMCRewardChest* Chest,const TArray<FName>& IDs,EMCPerkPolarity Polarity)
{
    if (IDs.Num()!=3 || IDs.Contains(NAME_None) || IDs[0]==IDs[1] || IDs[1]==IDs[2] || IDs[0]==IDs[2]
        || uint8(Polarity)>uint8(EMCPerkPolarity::Negative) || !PrepareRewardUI(Chest)) return;
    // Render the reliable payload directly; actor replication may still be one frame behind this RPC.
    PerkChoiceWidget->ShowChoices(IDs,Polarity);
    UpdateInputMode();
    PerkChoiceWidget->SetUserFocus(this);
}

void AMCPlayerController::ChooseRewardPerk(int32 ChoiceIndex)
{
    if (!IsLocalController() || !IsRewardMenuOpen() || !PerkChoiceWidget->IsShowingChoices()
        || bRewardChoicePending || ChoiceIndex<0 || ChoiceIndex>=3) return;
    auto* Chest=ActiveRewardChest.Get();
    if (!IsValid(Chest)) { CloseRewardUI(); return; }
    bRewardChoicePending=true;
    PerkChoiceWidget->SetSelectionPending(true);
    ServerChooseRewardPerk(Chest,ChoiceIndex);
}

void AMCPlayerController::ServerChooseRewardPerk_Implementation(AMCRewardChest* Chest,int32 ChoiceIndex)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if (!IsValid(Chest) || ChoiceIndex<0 || ChoiceIndex>=3 || !Hero || !Chest->TryChooseCard(Hero,ChoiceIndex))
    {
        if (IsValid(Chest) && Hero && Chest->Stage==EMCRewardChestStage::Open && Chest->OpeningPlayer==Hero)
            ClientShowPerkChoices(Chest,Chest->LootIDs,Chest->Polarity);
        else ClientClosePerkChoices(Chest);
    }
}

void AMCPlayerController::ClientClosePerkChoices_Implementation(AMCRewardChest* Chest)
{
    if (!Chest || ActiveRewardChest.Get()==Chest) CloseRewardUI();
}

void AMCPlayerController::CheckRewardUI()
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    auto* Chest=ActiveRewardChest.Get();
    if (!IsRewardInteractionActive() || !IsValid(Chest) || !Hero || Hero!=RewardPawn.Get() || !Hero->Status || !Hero->Status->IsAlive()
        || Chest->Stage==EMCRewardChestStage::Exhausted)
    { CloseRewardUI(); return; }
    // The explicit close RPC handles restart; this also covers loss of actor relevance or a cancelled interaction.
    if (GetWorld()->GetTimeSeconds()-RewardShownAt>2. && Chest->Stage!=EMCRewardChestStage::Lockpicking
        && Chest->Stage!=EMCRewardChestStage::Opening && Chest->Stage!=EMCRewardChestStage::Open)
    { CloseRewardUI(); return; }
}

void AMCPlayerController::CloseRewardUI(bool bRestoreInput)
{
    GetWorldTimerManager().ClearTimer(RewardUITimer);
    ActiveRewardChest.Reset();
    RewardPawn.Reset();
    bRewardChoicePending=false;
    if (PerkChoiceWidget) PerkChoiceWidget->RemoveFromParent();
    if (bRestoreInput && IsLocalController()) UpdateInputMode();
}

void AMCPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (FrontEndWidget) FrontEndWidget->RemoveFromParent();
    if (TutorialWidget) TutorialWidget->RemoveFromParent();
    GetWorldTimerManager().ClearTimer(BossHUDTimer);
    if (IsValid(BossIntro)) BossIntro->CancelIntro();
    BossIntro=nullptr;
    if (BossHealthWidget) BossHealthWidget->RemoveFromParent();
    CloseRewardUI(false);
    Super::EndPlay(EndPlayReason);
}
void AMCPlayerController::ServerGameplayLoaded_Implementation()
{
    if (GetPawn() && GetPlayerState<AMCPlayerState>()) bGameplayLoaded=true;
}
void AMCPlayerController::ServerTutorialLoaded_Implementation()
{
    if (!GetPawn()) return;
    if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->SetLoaded(GetPlayerState<AMCPlayerState>());
}
void AMCPlayerController::ServerSetTutorialReady_Implementation(bool bReady)
{
    if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->SetReady(GetPlayerState<AMCPlayerState>(),bReady);
}
void AMCPlayerController::ServerStartLobby_Implementation()
{
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->StartLobby(this);
}
void AMCPlayerController::ReturnToMainMenu()
{
    if (!IsLocalController()) return;
    if (GetNetMode()==NM_Standalone) UGameplayStatics::SetGamePaused(this,false);
    if (auto* Steam=GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>()) Steam->LeaveRoom();
    else UGameplayStatics::OpenLevel(this,TEXT("/Game/Maps/L_MainMenu"));
}
void AMCPlayerController::TogglePauseMenu()
{
    if (!IsLocalController() || bLobbyUIOpen || IsRewardInteractionActive() || bBossIntroPlaying) return;
    bPauseMenuOpen=!bPauseMenuOpen;
    if (bPauseMenuOpen)
    {
        if (!FrontEndWidget) FrontEndWidget=CreateWidget<UMCMainMenuWidget>(this,UMCMainMenuWidget::StaticClass());
        if (FrontEndWidget) { FrontEndWidget->ShowPause(); if (!FrontEndWidget->IsInViewport()) FrontEndWidget->AddToViewport(60); }
        if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
        if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
        if (PrototypeWidget) PrototypeWidget->ClosePanels();
        HideScoreboard();
    }
    else if (FrontEndWidget) FrontEndWidget->RemoveFromParent();
    UpdateInputMode();
}
void AMCPlayerController::RefreshFrontEnd()
{
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if (!State || !GetPawn() || !GetPlayerState<AMCPlayerState>()) return;
    if (!bGameplayLoadAckSent) { ServerGameplayLoaded(); bGameplayLoadAckSent=true; }
    if (State->bLobbyWaiting && !bLobbyUIOpen)
    {
        bLobbyUIOpen=true;
        if (!FrontEndWidget) FrontEndWidget=CreateWidget<UMCMainMenuWidget>(this,UMCMainMenuWidget::StaticClass());
        if (FrontEndWidget) { FrontEndWidget->ShowLobby(); FrontEndWidget->AddToViewport(60); }
        UpdateInputMode();
    }
    else if (!State->bLobbyWaiting && bLobbyUIOpen)
    {
        bLobbyUIOpen=false;
        if (FrontEndWidget) FrontEndWidget->RemoveFromParent();
        UpdateInputMode();
    }
    const bool HideGameplay=State->bLobbyWaiting || State->bTutorialActive;
    if (PrototypeWidget && HideGameplay) { PrototypeWidget->SetVisibility(ESlateVisibility::Collapsed); bFrontEndHidPrototype=true; }
    else if (PrototypeWidget && bFrontEndHidPrototype) { PrototypeWidget->SetVisibility(ESlateVisibility::Visible); bFrontEndHidPrototype=false; }
    if (State->bTutorialActive)
    {
        if (!TutorialWidget) { TutorialWidget=CreateWidget<UMCTutorialWidget>(this,UMCTutorialWidget::StaticClass()); if (TutorialWidget) TutorialWidget->AddToViewport(20); }
        if (TutorialWidget) TutorialWidget->RefreshState();
        auto* Director=AMCTutorialDirector::Find(GetWorld());
        if (Director && AcknowledgedTutorial.Get()!=Director) { ServerTutorialLoaded(); AcknowledgedTutorial=Director; }
        const bool NeedsInput=TutorialWidget && TutorialWidget->RequiresMenuInput();
        if (NeedsInput!=bTutorialMenuInput) { bTutorialMenuInput=NeedsInput; UpdateInputMode(); if (NeedsInput && TutorialWidget->GetReadyFocusTarget()) TutorialWidget->GetReadyFocusTarget()->SetUserFocus(this); }
    }
    else
    {
        if (TutorialWidget) { TutorialWidget->RemoveFromParent(); TutorialWidget=nullptr; }
        AcknowledgedTutorial.Reset();
        if (bTutorialMenuInput) { bTutorialMenuInput=false; UpdateInputMode(); }
    }
}

void AMCPlayerController::ClientPlayBossIntro_Implementation(AMCBossCharacter* Boss)
{
    if (!IsLocalController() || IsRewardInteractionActive() || !IsValid(Boss)) return;
    if (IsValid(BossIntro)) BossIntro->CancelIntro();
    FActorSpawnParameters Spawn; Spawn.Owner=this;
    BossIntro=GetWorld()->SpawnActor<AMCBossIntro>(AMCBossIntro::StaticClass(),FTransform::Identity,Spawn);
    if (BossIntro && !BossIntro->PlayIntro(this,Boss)) { BossIntro->Destroy(); BossIntro=nullptr; }
}

void AMCPlayerController::SetBossIntroGuard(AMCBossCharacter* Boss)
{
    if (!HasAuthority()) return;
    GuardedIntroBoss=Boss;
    GuardedIntroSerial=IsValid(Boss)?Boss->Runtime.PreviewSerial:0;
}

bool AMCPlayerController::IsBossIntroPlaying() const
{
    const auto* Boss=GuardedIntroBoss.Get();
    return bBossIntroPlaying || (HasAuthority() && IsValid(Boss) && Boss->IsBossAlive()
        && Boss->Runtime.AnimationPreview==EMCBossAnimationPreview::Roar
        && Boss->Runtime.PreviewSerial==GuardedIntroSerial);
}

void AMCPlayerController::BeginBossIntroPresentation()
{
    if (bBossIntroPlaying) return;
    bBossIntroPlaying=true;
    if (PrototypeWidget)
    {
        PreviousPrototypeVisibility=PrototypeWidget->GetVisibility();
        PrototypeWidget->ClosePanels(); PrototypeWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    HideScoreboard();
    if (auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
    if (BossHealthWidget) BossHealthWidget->SetLetterbox(true);
    UpdateInputMode();
}

void AMCPlayerController::FinishBossIntroPresentation()
{
    if (!bBossIntroPlaying) return;
    if (auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->CancelGameplayInput();
    bBossIntroPlaying=false;
    if (PrototypeWidget) PrototypeWidget->SetVisibility(PreviousPrototypeVisibility);
    if (BossHealthWidget) BossHealthWidget->SetLetterbox(false);
    UpdateInputMode();
    RefreshBossHUD();
}

void AMCPlayerController::RefreshBossHUD()
{
    if (!BossHealthWidget || !IsLocalController()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    auto* Boss=HealthBoss.Get();
    const auto Relevant=[](const AMCBossCharacter* Candidate)
    {
        return IsValid(Candidate) && !Candidate->IsActorBeingDestroyed()
            && Candidate->Runtime.AnimationPreview==EMCBossAnimationPreview::None
            && Candidate->Runtime.State!=EMCBossState::Dormant;
    };
    if (!Relevant(Boss))
    {
        HealthBoss.Reset(); Boss=nullptr; BossDiedAt=-1;
        if (Now>=NextBossScan)
        {
            NextBossScan=Now+.5;
            double BestDistance=FMath::Square(6500.f);
            const FVector Eye=GetPawn()?GetPawn()->GetActorLocation():GetFocalLocation();
            for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
            {
                if (!Relevant(*It) || It->Runtime.State==EMCBossState::Dead) continue;
                const double Distance=FVector::DistSquared(Eye,It->GetActorLocation());
                if (Distance<BestDistance) { BestDistance=Distance; Boss=*It; }
            }
            HealthBoss=Boss;
        }
    }
    if (Boss && Boss->Runtime.State==EMCBossState::Dead)
    {
        if (BossDiedAt<0) BossDiedAt=Now;
        if (Now-BossDiedAt>2.) { BossHealthWidget->HideHealth(); return; }
    }
    else BossDiedAt=-1;
    if (Boss) BossHealthWidget->ShowHealth(Boss->Runtime.Health,Boss->Runtime.MaxHealth,.1f);
    else BossHealthWidget->HideHealth();
}
void AMCPlayerController::HostGame()
{
    if (auto* Steam=GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>(); Steam && Steam->CanUseSteam())
        Steam->HostRoom();
    else UGameplayStatics::OpenLevel(this,TEXT("L_Mouth"),true,TEXT("listen"));
}
void AMCPlayerController::JoinGame(const FString& Address)
{
    FString Clean = Address.TrimStartAndEnd();
    // Only hostname/IP and optional port; no URL options, commands or file paths.
    if (Clean.IsEmpty() || Clean.Len()>128) return;
    for (TCHAR C : Clean) if (!FChar::IsAlnum(C) && C != '.' && C != ':' && C != '-') return;
    ClientTravel(Clean,TRAVEL_Absolute);
}
void AMCPlayerController::RequestRestart() { ServerRestartShift(); }
void AMCPlayerController::ServerRestartShift_Implementation()
{
    // Only the listen host may restart, and only after a finished run.
    AMCGameState* State = GetWorld()->GetGameState<AMCGameState>();
    if (!IsLocalController() || !State || (!State->bDayOneComplete && State->Phase != EMCShiftPhase::Won && State->Phase != EMCShiftPhase::Lost)) return;
    if (AMCGameMode* Mode = GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->RestartShift();
}
