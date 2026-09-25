#include "MCPlayerController.h"
#include "MCPrototypeWidget.h"
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
void AMCPlayerController::ToggleTuning() { if (PrototypeWidget) { PrototypeWidget->ToggleTuning(); UpdateInputMode(); } }
void AMCPlayerController::ToggleConnection() { if (PrototypeWidget) { PrototypeWidget->ToggleConnection(); UpdateInputMode(); } }
void AMCPlayerController::UpdateInputMode()
{
    const bool bPanel = PrototypeWidget && PrototypeWidget->IsPanelOpen();
    bShowMouseCursor = bPanel;
    if (auto* Tooth = Cast<AMCToothCharacter>(GetPawn())) Tooth->bPreviewAnimation = PrototypeWidget && PrototypeWidget->IsTuningOpen();
    ResetIgnoreMoveInput(); SetIgnoreMoveInput(bPanel);
    if (bPanel) { FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(PrototypeWidget->TakeWidget()); Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); }
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
