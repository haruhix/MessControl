#include "MCMainMenuPlayerController.h"
#include "MCMainMenuWidget.h"
#include "MCDataAssets.h"
#include "MCSceneAudioComponent.h"

void AMCMainMenuPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    if(!BackgroundSoundPalette) BackgroundSoundPalette=LoadObject<UMCSoundPalette>(nullptr,TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds"));
    SceneAudio=NewObject<UMCSceneAudioComponent>(this,TEXT("BackgroundSceneAudio"));
    AddInstanceComponent(SceneAudio); SceneAudio->RegisterComponent();
    SceneAudio->SetScene(BackgroundSoundPalette,TEXT("MainMenu"));
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
