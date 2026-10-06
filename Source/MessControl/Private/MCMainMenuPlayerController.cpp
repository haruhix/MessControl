#include "MCMainMenuPlayerController.h"
#include "MCMainMenuWidget.h"

void AMCMainMenuPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    MenuWidget=CreateWidget<UMCMainMenuWidget>(this,UMCMainMenuWidget::StaticClass());
    if (!MenuWidget) return;
    MenuWidget->AddToViewport(50);
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(MenuWidget->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    SetShowMouseCursor(true);
    bEnableClickEvents=true;
    MenuWidget->FocusMenu();
}

void AMCMainMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (MenuWidget) { MenuWidget->RemoveFromParent(); MenuWidget=nullptr; }
    Super::EndPlay(EndPlayReason);
}
