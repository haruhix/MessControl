#include "MCPlayerController.h"
#include "MCPrototypeWidget.h"
#include "MCDevPanelWidget.h"
#include "MCEmoteWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void AMCPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        PrototypeWidget = CreateWidget<UMCPrototypeWidget>(this,UMCPrototypeWidget::StaticClass());
        PrototypeWidget->AddToViewport(); UpdateInputMode();
    }
}
void AMCPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::T,IE_Pressed,this,&AMCPlayerController::ToggleEmotes);
#if !UE_BUILD_SHIPPING
    InputComponent->BindKey(EKeys::F3,IE_Pressed,this,&AMCPlayerController::ToggleDevPanel);
#endif
}
void AMCPlayerController::ToggleDevPanel()
{
#if !UE_BUILD_SHIPPING
    if (!IsLocalController()) return;
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
    if (!IsLocalController()) return;
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
void AMCPlayerController::ToggleTuning() { if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed); if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed); if (PrototypeWidget) { PrototypeWidget->ToggleTuning(); UpdateInputMode(); } }
void AMCPlayerController::ToggleConnection() { if (EmoteWidget) EmoteWidget->SetVisibility(ESlateVisibility::Collapsed); if (DevPanel) DevPanel->SetVisibility(ESlateVisibility::Collapsed); if (PrototypeWidget) { PrototypeWidget->ToggleConnection(); UpdateInputMode(); } }
void AMCPlayerController::UpdateInputMode()
{
    const bool bDev=DevPanel && DevPanel->IsVisible();
    const bool bEmote=EmoteWidget && EmoteWidget->IsVisible();
    const bool bPanel = bEmote || bDev || (PrototypeWidget && PrototypeWidget->IsPanelOpen());
    bShowMouseCursor = bPanel;
    if (auto* Tooth = Cast<AMCToothCharacter>(GetPawn())) Tooth->bPreviewAnimation = PrototypeWidget && PrototypeWidget->IsTuningOpen();
    ResetIgnoreMoveInput(); SetIgnoreMoveInput(bPanel);
    if (bDev || bEmote)
    {
        if (auto* Tooth=Cast<AMCToothCharacter>(GetPawn())) Tooth->CancelGameplayInput();
        FInputModeUIOnly Mode; Mode.SetWidgetToFocus(bEmote?EmoteWidget->TakeWidget():DevPanel->TakeWidget()); SetInputMode(Mode);
    }
    else if (bPanel) { FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(PrototypeWidget->TakeWidget()); Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); }
    else SetInputMode(FInputModeGameOnly());
}
void AMCPlayerController::HostGame() { UGameplayStatics::OpenLevel(this,TEXT("L_Mouth"),true,TEXT("listen")); }
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
