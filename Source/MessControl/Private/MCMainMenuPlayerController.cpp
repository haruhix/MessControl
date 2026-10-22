#include "MCMainMenuPlayerController.h"
#include "MCMainMenuWidget.h"
#include "MCDataAssets.h"
#include "MCSceneAudioComponent.h"
#include "MCLoadingWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCStudioSplashWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void AMCMainMenuPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    if(!BackgroundSoundPalette) BackgroundSoundPalette=LoadObject<UMCSoundPalette>(nullptr,TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds"));
    SceneAudio=NewObject<UMCSceneAudioComponent>(this,TEXT("BackgroundSceneAudio"));
    AddInstanceComponent(SceneAudio); SceneAudio->RegisterComponent();
    MenuWidget=CreateWidget<UMCMainMenuWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("MainMenu"),UMCMainMenuWidget::StaticClass()));
    if (!MenuWidget) return;
    MenuWidget->AddToViewport(50);
    auto* Startup=GetGameInstance()?GetGameInstance()->GetSubsystem<UMCStudioSplashSubsystem>():nullptr;
    if(Startup && Startup->ClaimStartupPresentation())
    {
        StudioSplash=CreateWidget<UMCStudioSplashWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("StudioSplash"),UMCStudioSplashWidget::StaticClass()));
        if(StudioSplash)
        {
            StudioSplash->OnFinished.AddUniqueDynamic(this,&ThisClass::FinishStudioSplash);
            StudioSplash->AddToViewport(20000);
            FInputModeUIOnly Mode;Mode.SetWidgetToFocus(StudioSplash->TakeWidget());
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);
            SetShowMouseCursor(false);bEnableClickEvents=false;
        }
    }
    if(!StudioSplash) SceneAudio->SetScene(BackgroundSoundPalette,TEXT("MainMenu"));
    RefreshPreparation();
}

void AMCMainMenuPlayerController::FocusPreparedMenu()
{
    if(StudioSplash || !MenuWidget || !bPreparationReleased) return;
    FInputModeUIOnly Mode;Mode.SetWidgetToFocus(MenuWidget->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);
    SetShowMouseCursor(true);bEnableClickEvents=true;MenuWidget->FocusMenu();
}

void AMCMainMenuPlayerController::FinishStudioSplash()
{
    if(StudioSplash) {StudioSplash->OnFinished.RemoveAll(this);StudioSplash->RemoveFromParent();StudioSplash=nullptr;}
    if(SceneAudio) SceneAudio->SetScene(BackgroundSoundPalette,TEXT("MainMenu"));
    SetShowMouseCursor(true);bEnableClickEvents=true;
    // Preparation ran behind the ident. Its actual readiness/failure still owns
    // the next screen; the three-second timer never bypasses asset loading.
    if(bPreparationReleased) FocusPreparedMenu();
    else {bLoadingFocused=false;bPreparationFailureFocused=false;RefreshPreparation();}
}

void AMCMainMenuPlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(IsLocalController() && !bPreparationReleased) RefreshPreparation();
}

void AMCMainMenuPlayerController::RefreshPreparation()
{
    auto* Preparation=GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>();
    if(!Preparation) {bPreparationReleased=true;FocusPreparedMenu();return;}
    Preparation->BeginPreparation(this);
    if(Preparation->IsReady()) {
        bPreparationReleased=true;
        if(LoadingWidget) LoadingWidget->RemoveFromParent();
        LoadingWidget=nullptr;
        FocusPreparedMenu();
        return;
    }
    const bool NewWidget=!LoadingWidget;
    if(NewWidget) {
        LoadingWidget=CreateWidget<UMCLoadingWidget>(this,UMCGameplayUIFactory::ResolveClass(TEXT("Loading"),UMCLoadingWidget::StaticClass()));
        if(LoadingWidget) LoadingWidget->AddToViewport(10000);
    }
    if(LoadingWidget) {
        LoadingWidget->SetPreparation(Preparation);
        if(!StudioSplash && (NewWidget || !bLoadingFocused || (Preparation->HasFailed() && !bPreparationFailureFocused))) {
            FInputModeUIOnly Mode;
            if(UWidget* Focus=LoadingWidget->GetFocusTarget()) Mode.SetWidgetToFocus(Focus->TakeWidget());
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);
            bLoadingFocused=true;
        }
        if(!StudioSplash) bPreparationFailureFocused=Preparation->HasFailed();
    }
}

void AMCMainMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if(StudioSplash) {StudioSplash->OnFinished.RemoveAll(this);StudioSplash->RemoveFromParent();StudioSplash=nullptr;}
    if(LoadingWidget) {LoadingWidget->RemoveFromParent();LoadingWidget=nullptr;}
    if (MenuWidget) { MenuWidget->RemoveFromParent(); MenuWidget=nullptr; }
    Super::EndPlay(EndPlayReason);
}
