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

void AMCPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        if (auto* Steam=GetGameInstance()->GetSubsystem<UMCSteamSessionSubsystem>()) Steam->RefreshAvailability();
        PrototypeWidget = CreateWidget<UMCPrototypeWidget>(this,UMCPrototypeWidget::StaticClass());
        PrototypeWidget->AddToViewport(); UpdateInputMode();
    }
}
void AMCPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::T,IE_Pressed,this,&AMCPlayerController::ToggleEmotes);
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
    if (!IsLocalController() || IsRewardMenuOpen()) return;
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
    if (!IsLocalController() || IsRewardMenuOpen()) return;
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
    if (!IsLocalController() || IsRewardMenuOpen()) return;
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
void AMCPlayerController::ToggleTuning() { if (IsRewardMenuOpen()) return; if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed); if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed); if (PrototypeWidget) { PrototypeWidget->ToggleTuning(); UpdateInputMode(); } }
void AMCPlayerController::ToggleConnection() { if (IsRewardMenuOpen()) return; if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed); if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed); if (PrototypeWidget) { PrototypeWidget->ToggleConnection(); UpdateInputMode(); } }
void AMCPlayerController::UpdateInputMode()
{
    const bool bReward=IsRewardMenuOpen();
    const bool bDev=DevPanel && DevPanel->IsVisible();
    const bool bEmote=EmoteWidget && EmoteWidget->IsVisible();
    const bool bPanel = bReward || bEmote || bDev || (PrototypeWidget && PrototypeWidget->IsPanelOpen());
    bShowMouseCursor = bPanel;
    if (auto* Tooth = Cast<AMCToothCharacter>(GetPawn())) Tooth->bPreviewAnimation = PrototypeWidget && PrototypeWidget->IsTuningOpen();
    ResetIgnoreMoveInput(); SetIgnoreMoveInput(bPanel);
    ResetIgnoreLookInput(); SetIgnoreLookInput(bPanel);
    if (bReward || bDev || bEmote)
    {
        if (auto* Tooth=Cast<AMCToothCharacter>(GetPawn())) Tooth->CancelGameplayInput();
        FInputModeUIOnly Mode; Mode.SetWidgetToFocus(bReward?PerkChoiceWidget->TakeWidget():(bEmote?EmoteWidget->TakeWidget():DevPanel->TakeWidget())); SetInputMode(Mode);
    }
    else if (bPanel) { FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(PrototypeWidget->TakeWidget()); Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); }
    else SetInputMode(FInputModeGameOnly());
}

bool AMCPlayerController::IsRewardMenuOpen() const
{
    return PerkChoiceWidget && PerkChoiceWidget->IsInViewport() && PerkChoiceWidget->IsVisible();
}

bool AMCPlayerController::PrepareRewardUI(AMCRewardChest* Chest)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if (!IsLocalController() || !IsValid(Chest) || !Hero || !Hero->Status || !Hero->Status->IsAlive()) return false;
    if (!PerkChoiceWidget)
        PerkChoiceWidget=CreateWidget<UMCPerkChoiceWidget>(this,UMCPerkChoiceWidget::StaticClass());
    if (!PerkChoiceWidget) return false;
    ActiveRewardChest=Chest;
    RewardPawn=Hero;
    RewardShownAt=GetWorld()->GetTimeSeconds();
    bRewardChoicePending=false;
    if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed);
    if (PrototypeWidget) PrototypeWidget->ClosePanels();
    HideScoreboard();
    if (!PerkChoiceWidget->IsInViewport()) PerkChoiceWidget->AddToViewport(30);
    PerkChoiceWidget->SetVisibility(ESlateVisibility::Visible);
    GetWorldTimerManager().SetTimer(RewardUITimer,this,&AMCPlayerController::CheckRewardUI,.1f,true);
    return true;
}

void AMCPlayerController::ClientShowRewardOpening_Implementation(AMCRewardChest* Chest,double ServerEndsAt)
{
    if (!FMath::IsFinite(ServerEndsAt) || !PrepareRewardUI(Chest)) return;
    PerkChoiceWidget->ShowOpening(ServerEndsAt);
    UpdateInputMode();
    PerkChoiceWidget->SetUserFocus(this);
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
    if (!IsRewardMenuOpen() || !IsValid(Chest) || !Hero || Hero!=RewardPawn.Get() || !Hero->Status || !Hero->Status->IsAlive()
        || Chest->Stage==EMCRewardChestStage::Exhausted)
    { CloseRewardUI(); return; }
    // The explicit close RPC handles restart; this also covers loss of actor relevance or a cancelled interaction.
    if (GetWorld()->GetTimeSeconds()-RewardShownAt>2. && Chest->Stage!=EMCRewardChestStage::Lockpicking
        && Chest->Stage!=EMCRewardChestStage::Opening && Chest->Stage!=EMCRewardChestStage::Open)
    { CloseRewardUI(); return; }
    const auto* State=GetWorld()->GetGameState();
    PerkChoiceWidget->UpdateOpeningProgress(State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds());
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
    CloseRewardUI(false);
    Super::EndPlay(EndPlayReason);
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
