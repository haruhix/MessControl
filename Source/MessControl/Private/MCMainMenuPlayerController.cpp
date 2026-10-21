#include "MCMainMenuPlayerController.h"
#include "MCMainMenuWidget.h"
#include "MCDataAssets.h"
#include "MCSceneAudioComponent.h"
#include "MCLoadingWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCWorldPreloadSubsystem.h"
#include "Engine/World.h"

void AMCMainMenuPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    if(!BackgroundSoundPalette) BackgroundSoundPalette=LoadObject<UMCSoundPalette>(nullptr,TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds"));
    SceneAudio=NewObject<UMCSceneAudioComponent>(this,TEXT("BackgroundSceneAudio"));
    AddInstanceComponent(SceneAudio); SceneAudio->RegisterComponent();
    SceneAudio->SetScene(BackgroundSoundPalette,TEXT("MainMenu"));
    MenuWidget=CreateWidget<UMCMainMenuWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("MainMenu"),UMCMainMenuWidget::StaticClass()));
    if (!MenuWidget) return;
    MenuWidget->AddToViewport(50);
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(MenuWidget->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    SetShowMouseCursor(true);
    bEnableClickEvents=true;
    MenuWidget->FocusMenu();
    RefreshPreparation();
}

void AMCMainMenuPlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(IsLocalController() && !bPreparationReleased) RefreshPreparation();
}

void AMCMainMenuPlayerController::RefreshPreparation()
{
    auto* Preparation=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
    if(!Preparation) return;
    Preparation->BeginPreparation(this);
    if(Preparation->IsReady()) {
        bPreparationReleased=true;
        if(LoadingWidget) LoadingWidget->RemoveFromParent();
        LoadingWidget=nullptr;
        if(MenuWidget) {
            FInputModeUIOnly Mode;Mode.SetWidgetToFocus(MenuWidget->TakeWidget());
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);
            MenuWidget->FocusMenu();
        }
        return;
    }
    const bool NewWidget=!LoadingWidget;
    if(NewWidget) {
        LoadingWidget=CreateWidget<UMCLoadingWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("Loading"),UMCLoadingWidget::StaticClass()));
        if(LoadingWidget) LoadingWidget->AddToViewport(10000);
    }
    if(LoadingWidget) {
        LoadingWidget->SetPreparation(Preparation);
        if(NewWidget || (Preparation->HasFailed() && !bPreparationFailureFocused)) {
            FInputModeUIOnly Mode;
            if(UWidget* Focus=LoadingWidget->GetFocusTarget()) Mode.SetWidgetToFocus(Focus->TakeWidget());
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);
        }
        bPreparationFailureFocused=Preparation->HasFailed();
    }
}

void AMCMainMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if(LoadingWidget) {LoadingWidget->RemoveFromParent();LoadingWidget=nullptr;}
    if (MenuWidget) { MenuWidget->RemoveFromParent(); MenuWidget=nullptr; }
    Super::EndPlay(EndPlayReason);
}
